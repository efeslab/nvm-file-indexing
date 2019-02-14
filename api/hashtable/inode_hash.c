#include <stdbool.h>
#include "inode_hash.h"


int init_hash(const idx_spec_t *idx_spec, idx_struct_t *idx_struct, paddr_t *location) {
    if_then_panic(!idx_spec, "idx_spec cannot be null!");
    if_then_panic(!idx_struct, "idx_struct cannot be null!");
    if_then_panic(!location, "location ptr cannot be null!");

    GHashTable *ht = (GHashTable*)idx_struct->idx_metadata;

    if (ht) return -EEXIST;

    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_fns       = &hash_fns;

    device_info_t devinfo;
    int ret = CB(idx_struct, cb_get_dev_info, &devinfo);

    // Allocate space on device.
    if_then_panic(devinfo.di_block_size % sizeof(hash_entry_t) != 0,
                  "bad hesh_entry_t size");

    size_t entries_per_block = devinfo.di_block_size / sizeof(hash_entry_t);

    size_t nbytes = devinfo.di_size_blocks * sizeof(hash_entry_t);

    if (!*location) {
        ssize_t nalloc = CB(idx_struct, cb_alloc_metadata, 1, location);

        if_then_panic(nalloc < 1, "no room for metadata!");
    }

    ht = g_hash_table_new(hash6432shift, devinfo.di_size_blocks,
                          devinfo.di_block_size, 1, *location,
                          idx_spec);
    if_then_panic(ht == NULL, "could not allocate hash table");

    idx_struct->idx_metadata = (void*)ht;

    return 0;
}

// if not exists, then the value was not already in the table, therefore
// success.
// returns 1 on success, 0 if key already existed
ssize_t insert_hash(idx_struct_t *idx_struct, inum_t inum,
                    laddr_t laddr, size_t size, paddr_t *paddr) {
    GHASH(idx_struct, ht);

    ssize_t nalloc = CB(idx_struct, cb_alloc_data, size, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc < size) {
        CB(idx_struct, cb_dealloc_data, nalloc, *paddr);
        return -ENOMEM;
    }

    for (size_t blkno = 0; blkno < nalloc; ++blkno) {
        hash_key_t k = MAKEKEY(inum, laddr + blkno);
        int err = g_hash_table_insert(ht, k, (*paddr) + blkno, size - blkno);
        if (!err) return -EEXIST;
    }

    return nalloc;
}

/*
 * Returns 0 if found, or -errno otherwise.
 */
ssize_t lookup_hash(idx_struct_t *idx_struct, inum_t inum,
                laddr_t laddr, paddr_t* paddr) {
    GHASH(idx_struct, ht);

    hash_key_t k = MAKEKEY(inum, laddr);
    size_t size;
    g_hash_table_lookup(ht, k, paddr, &size, false);
    if (*paddr != 0) return (ssize_t)size;
    return -ENOENT;
}

/*
 * Returns FALSE if the requested logical block was not present in any of the
 * two hash tables.
 */
ssize_t erase_hash(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr,
                   size_t size) {
    GHASH(idx_struct, ht);

    if (size == 0) return -EINVAL;

    ssize_t ret = 0;
    for (laddr_t lblk = laddr; lblk < laddr + size; ++lblk) {
        hash_key_t k = MAKEKEY(inum, lblk);
        ret += g_hash_table_remove(ht, k);
    }

    if (ret == 0) return -ENOENT;

    return ret;
}
#if 0
int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags, bool force) {
#if 1
    return 0;
#else
	int err = 0;
	laddr_t allocated = 0;
	int create;

	mlfs_assert(handle);

	create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = 0;
  map->m_pblk = 0;

  // lookup all blocks.
  uint32_t len = map->m_len;
  bool set = false;

  for (laddr_t i = 0; i < max(map->m_len, 1); ) {
    hash_value_t index;
    hash_value_t value;
    hash_value_t size;
    //printf("LUP %lu\n", map->m_lblk + i);
    int pre = lookup_hash(inode, map->m_lblk + i, &value, &size, &index, force);
    //printf("LDOWN value = %llu size = %llu index = %llu\n", value, size, index);
    if (!pre) {
      goto create;
    }

    if (!set) {
      //printf("Setting to %lu + %lu\n", value, index);
      map->m_pblk = value + index;
      set = true;
    } else if (value + index != map->m_pblk + i) {
      // only return contiguous ranges
      return i;
    }

    len -= size - index;
    i += size - index;
    ret = i;
  }

  ret = min(ret, map->m_len);

  return ret;

create:
  if (create) {
    mlfs_fsblk_t blockp;
    struct super_block *sb = get_inode_sb(handle->dev, inode);
    enum alloc_type a_type;

    if (flags & MLFS_GET_BLOCKS_CREATE_DATA_LOG) {
      a_type = DATA_LOG;
    } else if (flags & MLFS_GET_BLOCKS_CREATE_META) {
      a_type = TREE;
    } else {
      a_type = DATA;
    }

    pthread_mutex_lock(&alloc_tex);
    laddr_t lb = map->m_lblk + (map->m_len - len);
    int r = mlfs_new_blocks(sb, &blockp, len, 0, 0, a_type, 0);
    if (r > 0) {
      bitmap_bits_set_range(sb->s_blk_bitmap, blockp, r);
      sb->used_blocks += r;
    } else if (r == -ENOSPC) {
      panic("Failed to allocate block -- no space!\n");
    } else if (r == -EINVAL) {
      panic("Failed to allocate block -- invalid arguments!\n");
    } else {
      panic("Failed to allocate block -- unknown error!\n");
    }

    ret = r;
    pthread_mutex_unlock(&alloc_tex);

    //printf("Starting insert: %u, %lu, %lu\n", map->m_lblk, map->m_len, len);
    for (int c = 0; c < ret; ) {
      int offset = ((lb + c) & RANGE_BITS);
      int aligned = offset == 0;
      if (unlikely(ret >= (RANGE_SIZE / 2) && aligned)) {
        /*
         * It's possible part of the range has already been allocated.
         * Say if someone requests (RANGE_SIZE + 1) blocks, but the blocks from
         * (RANGE_SIZE, RANGE_SIZE + RANGE_SIZE) have already been allocated,
         * we need to skip the last block.
         */
        hash_value_t index;
        hash_value_t value;
        hash_value_t size;
        // Doesn't make sense to do this on the first pass though, we just
        // looked it up.
        if (c > 0) {
          int pre = lookup_hash(inode, lb + c, &value, &size, &index, force);
          if (pre) {
            c += size;
            if (!set) {
              map->m_pblk = value + index;
              set = true;
            }
            continue;
          }
        }

        uint32_t nblocks = min(ret - c, RANGE_SIZE);
        //printf("Insert to big.\n");
        hash_key_t k = RANGE_KEY(inode->inum, lb + c);
        int already_exists = !insert_hash(gsuper, inode, k, blockp, nblocks);
        if (already_exists) {
          //panic("Could not insert huge range!\n");
          printf("Weird, already exists?\n");
        }

        if (!set) {
          map->m_pblk = blockp;
          set = true;
        }

        c += nblocks;
        blockp += nblocks;

      } else {
        uint32_t nblocks_to_alloc = min(ret - c, RANGE_SIZE - offset);

        if (!set) {
          map->m_pblk = blockp;
          set = true;
        }

        //printf("Insert to small: %u.\n", nblocks_to_alloc);
        for (uint32_t i = 0; i < nblocks_to_alloc; ++i) {
          hash_key_t k = MAKEKEY(inode, lb + i + c);
          int already_exists = !insert_hash(ghash, inode, k, blockp + i,
              nblocks_to_alloc - i);

          if (already_exists) {
            fprintf(stderr, "could not insert: key = %u, val = %0lx, already exists (small)\n",
                lb + i + c, blockp);
            //panic("Could not insert into small table!");
          }

        }

        c += nblocks_to_alloc;
        blockp += nblocks_to_alloc;
      }
    }

    mlfs_hash_persist();
  }

  ret = min(ret, map->m_len);

  return ret;
#endif
}
#endif
#if 0
int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		laddr_t start, laddr_t end) {
#if 0
  hash_value_t size, value, index;
  // TODO: probably inefficient
  for (laddr_t i = start; i <= end;) {
    if (lookup_hash(inode, i, &value, &size, &index, false)) {
      mlfs_fsblk_t pblock = value + index;
      mlfs_free_blocks(handle, inode, NULL, pblock, size, 0);
      erase_hash(inode, i);
      i += size;
    }
  }

  //return mlfs_hash_persist();
#endif
  return 0;
}

double check_load_factor(struct inode *inode) {
#if 0
  double load = 0.0;
  double allocated_size = (double)ghash->size;
  double current_size = (double)ghash->noccupied;
  load = current_size / allocated_size;
  return load;
#else
  return 0.0;
#endif
}
#endif

int mlfs_hash_persist() {
#if 1
  return 0;
#else
  pthread_mutex_lock(ghash->metalock);
  pthread_mutex_lock(gsuper->metalock);

  sync_all_buffers(g_bdev[g_root_dev]);
  nvram_flush(ghash);
  nvram_flush(gsuper);

  pthread_mutex_unlock(gsuper->metalock);
  pthread_mutex_unlock(ghash->metalock);

  return 0;
#endif
}

// TODO: probably could keep track of this with a bitmap or something, but this
// is very easy to implement
int mlfs_hash_cache_invalidate() {
#if 0
  //pthread_rwlock_wrlock(ghash->cache_lock);
  /*
  for (size_t i = 0; i < ghash->nblocks; ++i) {
    if(ghash->cache[i]) {
      free(ghash->cache[i]);
      ghash->cache[i] = NULL;
    }
  }
  */
#ifdef HASHCACHE
  bitmap_set(ghash->cache_bitmap, 0, ghash->nblocks);
  //pthread_rwlock_unlock(ghash->cache_lock);

  //pthread_rwlock_wrlock(gsuper->cache_lock);
  /*
  for (size_t i = 0; i < gsuper->nblocks; ++i) {
    if(gsuper->cache[i]) {
      free(gsuper->cache[i]);
      gsuper->cache[i] = NULL;
    }
  }
  */
  bitmap_set(gsuper->cache_bitmap, 0, gsuper->nblocks);
  //pthread_rwlock_unlock(gsuper->cache_lock);
#endif
#endif
  return 0;
}

idx_fns_t hash_fns = {
    .im_init   = init_hash,
    .im_lookup = lookup_hash,
    .im_create = insert_hash,
    .im_remove = erase_hash
};
