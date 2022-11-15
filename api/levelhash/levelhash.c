#include "levelhash.h"

idx_fns_t levelhash_fns = {
    .im_init               = NULL,
    .im_init_prealloc      = levelhash_init,
    .im_lookup             = levelhash_lookup,
    .im_create             = levelhash_create,
    .im_remove             = levelhash_remove,

    .im_set_caching        = levelhash_set_caching,
    .im_persist            = levelhash_persist_updates,
    .im_invalidate         = levelhash_invalidate_caches,
    .im_clear_metadata     = levelhash_clear_metadata,

    .im_set_stats          = levelhash_set_stats,
    .im_print_global_stats = levelhash_print_global_stats,
    .im_add_global_to_json = levelhash_add_global_to_json,
};

#define USE_CLEVEL 0

#if defined(USE_CLEVEL) && USE_CLEVEL

#include "clevel.hpp"

void levelhash_add_global_to_json(json_object *root) {}

int levelhash_init(const idx_spec_t* idx_spec, const paddr_range_t* direct_ents, 
                   idx_struct_t* level_idx) {
    int ret = 0;

    clevel_init();

    level_idx->idx_callbacks = idx_spec->idx_callbacks;
    level_idx->idx_mem_man   = idx_spec->idx_mem_man;
    level_idx->idx_fns       = &levelhash_fns;
    level_idx->idx_metadata  = (void*)NULL;

    return ret;
}

ssize_t levelhash_lookup(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t max, paddr_t* paddr) {

    ssize_t size = clevel_lookup(inum, laddr, paddr);

    if (0 == size) return -ENOENT;

    return size > max ? max : size;
}

ssize_t levelhash_create(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk, paddr_t* paddr) {
    ssize_t already_exists = clevel_lookup(inum, laddr, paddr);
    if (already_exists > 0) return already_exists;

    ssize_t nalloc = CB(level_idx, cb_alloc_data, 1, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    }

    uint8_t ret = clevel_create(inum, laddr, *paddr);

    return (ssize_t) nalloc;
}

ssize_t levelhash_remove(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk) {
    for (size_t blk = 0; blk < nblk; ++blk) {
        size_t idx;
        size_t full_range;
        paddr_t paddr;

        laddr_t cur_laddr = laddr + blk;

        uint8_t ret = clevel_remove(inum, cur_laddr);

        if (!ret) return -ENOENT;

        ssize_t ndealloc = CB(level_idx, cb_dealloc_data, 1, paddr);

        if (ndealloc < 0) {
            return -EINVAL;
        } else if (ndealloc == 0) {
            return -ENOMEM;
        }
    }

    return (ssize_t) nblk;
}

int levelhash_set_caching(idx_struct_t* level_idx, bool enable) { return 0; }
int levelhash_persist_updates(idx_struct_t* level_idx) { return 0; }
int levelhash_invalidate_caches(idx_struct_t* level_idx) { return 0; }
void levelhash_set_stats(idx_struct_t* level_idx, bool enable) { }
void levelhash_print_global_stats() { }
void levelhash_clear_metadata(idx_struct_t* level_idx) { }
#else

void levelhash_add_global_to_json(json_object *root) {
   js_add_int64(root, "compute_tsc", level_stats.compute_hash_tsc); 
   js_add_int64(root, "compute_nr", level_stats.compute_hash_nr); 
}

int levelhash_init(const idx_spec_t* idx_spec, const paddr_range_t* direct_ents, 
                   idx_struct_t* level_idx) {
    int ret = 0;

    level_hash_t *lh = level_init(idx_spec, direct_ents, 3);

    level_idx->idx_callbacks = idx_spec->idx_callbacks;
    level_idx->idx_mem_man   = idx_spec->idx_mem_man;
    level_idx->idx_fns       = &levelhash_fns;
    level_idx->idx_metadata  = (void*)lh;

    return ret;
}

ssize_t levelhash_lookup(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t max, paddr_t* paddr) {
    LEVELMETA(level_idx, lh);

    DECLARE_TIMING();
    if (lh->enable_stats) START_TIMING();
#if 1
    size_t size = level_dynamic_query(lh, laddr, paddr);
#else
    size_t size = level_static_query(lh, laddr, paddr);
#endif
    if (lh->enable_stats) UPDATE_TIMING(&level_stats, read_entries);

    if (0 == size) return -ENOENT;

    return (ssize_t)size;
}

ssize_t levelhash_create(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk, paddr_t* paddr) {
    LEVELMETA(level_idx, lh);
    ssize_t already_exists = levelhash_lookup(level_idx, inum, laddr, nblk, paddr);
    if (already_exists > 0) return already_exists;


    nblk = nblk > LH_MAX_SIZE ? LH_MAX_SIZE : nblk;

    ssize_t nalloc = CB(level_idx, cb_alloc_data, nblk, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    }

    for (size_t blk = 0; blk < nalloc; ++blk) {
        uint8_t ret = level_insert(lh, laddr + blk, (*paddr) + blk, 
                                   blk, nalloc - blk);
        if (ret) {
            // Trying to resize.
            level_expand(lh);

            ret = level_insert(lh, laddr + blk, (*paddr) + blk, 
                               blk, nalloc - blk);
            if (ret) return -EIO;
        }
    }

    return (ssize_t) nalloc;
}

ssize_t levelhash_remove(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk) {
    LEVELMETA(level_idx, lh);

    for (size_t blk = 0; blk < nblk; ++blk) {
        size_t idx;
        size_t full_range;
        paddr_t paddr;

        laddr_t cur_laddr = laddr + blk;

        uint8_t ret = level_delete(lh, cur_laddr, &paddr, &idx, &full_range);
        if (ret) {
            printf("remove failed: laddr = %u\n", cur_laddr);
        }

        ssize_t ndealloc = CB(level_idx, cb_dealloc_data, 1, paddr);

        if (ndealloc < 0) {
            return -EINVAL;
        } else if (ndealloc == 0) {
            return -ENOMEM;
        }

        // 1) Change the sizes of earlier entries in the range.
        if (idx > 0) {
            size_t new_size = idx;
            for (laddr_t l = 0; l < (laddr_t)idx; ++l) {
                size_t new_idx = (size_t)l;
                laddr_t lblk = (cur_laddr - idx) + l;
                uint8_t r = level_update(lh, lblk, new_idx, new_size);
                if (r) {
                    printf("index update failed: laddr = %u, %lu/%lu\n", 
                            lblk, blk + 1, nblk);
                    return -EIO;
                }
            }
        }
        // 2) Change the indices and sizes of the later entries.
        if (idx < full_range) {
            // minus 1 because we just deleted one
            size_t new_size = full_range - idx - 1;
            // start after this laddr we just deleted.
            for (laddr_t l = 1; l < new_size + 1; ++l) {
                size_t new_idx = (size_t)l - 1;
                laddr_t lblk = cur_laddr + l;
                uint8_t r = level_update(lh, lblk, new_idx, new_size);
                if (r) {
                    printf("range update failed: laddr = %u, %lu/%lu\n", 
                            lblk, blk + 1, nblk);
                    return -EIO;
                }
            }
        }

        level_shrink(lh);
    }

    return (ssize_t) nblk;
}

int levelhash_set_caching(idx_struct_t* level_idx, bool enable) {
    LEVELMETA(level_idx, lh);
    lh->do_cache = enable;
    return 0;
}

int levelhash_persist_updates(idx_struct_t* level_idx) {
    LEVELMETA(level_idx, lh);
    return level_persist(lh);
}

int levelhash_invalidate_caches(idx_struct_t* level_idx) {
    LEVELMETA(level_idx, lh);
    return level_cache_invalidate(lh);
}

void levelhash_set_stats(idx_struct_t* level_idx, bool enable) {
    LEVELMETA(level_idx, lh);
    lh->enable_stats = enable;
}

void levelhash_print_global_stats() {
    print_level_stats(&level_stats);
}

void levelhash_clear_metadata(idx_struct_t* level_idx) {
    return;
}

#endif
