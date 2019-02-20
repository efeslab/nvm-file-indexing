/* GLIB - Library of useful routines for C programming
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

#ifndef __NVM_IDX_G_HASH_MOD_H__
#define __NVM_IDX_G_HASH_MOD_H__ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// from common
#include "common/common.h"

// local includes
#include "hash_functions.h"

// For the big hash table, mapping (inode, lblk) -> single block
typedef struct hash_index_entry {
  paddr_t      key;
  uint16_t     size;
  uint16_t     value_hi16;
  uint32_t     value_low32;
} hash_ent_t;

#define HASH_ENT_VAL(x) (((paddr_t)x.value_hi16 << 32) | ((paddr_t)x.value_low32))
#define HASH_ENT_IS_TOMBSTONE(x) (x.value_hi16 == ~0 && x.value_low32 == ~0)
#define HASH_ENT_IS_EMPTY(x) (x.value_hi16 == 0 && x.value_low32 == 0)
#define HASH_ENT_IS_VALID(x) (!HASH_ENT_IS_EMPTY(x) && !HASH_ENT_IS_TOMBSTONE(x))

#define HASH_ENT_SET_TOMBSTONE(x) do {x.value_hi16 = ~0; x.value_low32 = ~0;} while(0)
#define HASH_ENT_SET_EMPTY(x) do {x.value_hi16 = 0; x.value_low32 = 0;} while(0)
#define HASH_ENT_SET_VAL(x,v) do {x.value_hi16 = (uint16_t)(v >> 32); x.value_low32 = (uint32_t)(v);} while(0)

//#define RANGE_SIZE (1 << 5) // 32
#define RANGE_SIZE (1 << 9) // 512 -- 2MB
#define RANGE_BITS (RANGE_SIZE - 1)
#define RANGE_MASK (~RANGE_BITS)
#define RANGE_KEY(i, l) ( (((uint64_t)(i)) << 32) | ((l) & RANGE_MASK))

#define HASH_IS_REAL(h_) ((h_) >= 2)

#define BLK_IDX(ht, x) (x % (ht->blksz / sizeof(hash_ent_t)))
#define BLK_NUM(ht, x) (x / (ht->blksz / sizeof(hash_ent_t)))

//#define HASHCACHE

// This is the hash table meta-data that is persisted to NVRAM, that we may read
// it and know everything we need to know in order to reconstruct it in memory.
typedef struct device_hashtable_metadata {
  // Metadata for the in-memory state.
  uint32_t size;
  uint32_t mod;
  uint32_t mask;
  uint32_t nnodes;
  uint32_t noccupied;
  // Metadata about the on-disk state.
  size_t  blksz;
  paddr_t nvram_size;
  paddr_t range_size;
  paddr_t data_start;
} dev_hash_metadata_t;


typedef struct nvm_hashtable_index {
    int             size;
    int             mod;
    unsigned        mask;
    int             nnodes;
    int             noccupied;  /* nnodes + tombstones */

    paddr_t         metadata;
    paddr_t         data;
    paddr_t         nvram_size;
    size_t          blksz;
    size_t          range_size;
    size_t          nblocks;

    hash_func_t      hash_func;
    int              ref_count;

    // callbacks
    callback_fns_t *callbacks;

    // device infomation
    device_info_t  *devinfo;

    // concurrency
    pthread_rwlock_t *locks;
    pthread_mutex_t *metalock;

    // caching
#if 0 && defined(HASHCACHE)
    pthread_rwlock_t *cache_lock;
    unsigned long* cache_bitmap;
    // array of blocks
    hash_ent_t **cache;
#endif
} nvm_hash_idx_t;


nvm_hash_idx_t *
nvm_hash_table_new (hash_func_t       hash_func,
                  size_t            max_entries,
                  size_t            block_size,
                  size_t            range_size,
                  paddr_t           metadata_location,
                  const idx_spec_t *idx_spec);

void nvm_hash_table_destroy(nvm_hash_idx_t     *hash_table);

int nvm_hash_table_insert(nvm_hash_idx_t *hash_table,
                          paddr_t     key,
                          paddr_t     value,
                          paddr_t     range);

int nvm_hash_table_replace(nvm_hash_idx_t *hash_table,
                           paddr_t key,
                           paddr_t value);

int nvm_hash_table_remove(nvm_hash_idx_t *hash_table,
                          paddr_t key,
                          paddr_t *value,
                          size_t  *nblocks);

void nvm_hash_table_lookup(nvm_hash_idx_t *hash_table, paddr_t key,
    paddr_t *val, paddr_t *size, bool force);

int nvm_hash_table_contains(nvm_hash_idx_t *hash_table,
                          paddr_t key);

unsigned nvm_hash_table_size(nvm_hash_idx_t *hash_table);


extern uint64_t reads;
extern uint64_t writes;
extern uint64_t blocks;

#if 0 && defined(HASHCACHE)
/*
 * Read a NVRAM block and give the users a reference to our cache (saves a
 * memcpy).
 * Used to read buckets and potentially iterate over them.
 *
 * buf -- reference to cache page. Don't free!
 * offset -- needs to be block aligned!
 * force -- refresh the cache from NVRAM.
 */
static void
nvram_read(nvm_hash_idx_t *ht, paddr_t offset, hash_ent_t **buf, bool force) {
  struct buffer_head *bh;
  int err;

  /*
   * Do some caching!
   */
  if (__test_and_clear_bit(offset, ht->cache_bitmap)) {
    force = true;
  }
  // if NULL, then it got invalidated or never loaded.
  if (likely(!force && ht->cache[offset] != NULL)) {
    *buf = ht->cache[offset];
    return;
    //*buf = ht->cache[offset];
    //return;
  }

  if (unlikely(ht->cache[offset] == NULL)) {
    ht->cache[offset] = (hash_ent_t*)mlfs_zalloc(g_block_size_bytes);
    mlfs_assert(ht->cache[offset]);
  }

  *buf = ht->cache[offset];

  ht->callbacks->cb_read(ht->data + offset, 0, (char*)ht->cache[offset]);
  reads++;

}

#endif

/*
 * Convenience wrapper for when you need to look up the single value within
 * the block and nothing else. Index is offset from start (bytes).
 */
static void
nvm_read_entry(nvm_hash_idx_t *ht, paddr_t idx, hash_ent_t *ret, bool force) {
    paddr_t block  = ht->data + BLK_NUM(ht, idx);
    off_t   offset = BLK_IDX(ht, idx) * sizeof(*ret);

    ssize_t err = ht->callbacks->cb_read(block, offset,
                                         sizeof(*ret), (char*)ret);

    if_then_panic(sizeof(*ret) != err, "Did not read enough bytes!");

    reads++;
}

/*
 * Read the hashtable metadata from disk. If the size is zero, then we need to
 * allocate the table. Otherwise, the structures have already been
 * pre-allocated.
 *
 * Returns 1 on success, 0 on failure.
 */
static int
nvm_read_metadata(nvm_hash_idx_t *ht) {

    dev_hash_metadata_t metadata;
    ssize_t err = ht->callbacks->cb_read(ht->metadata, 0,
                                         sizeof(metadata), (char*)&metadata);

    if_then_panic(err != sizeof(metadata), "Could not read metadata!");

    // now check the actual metadata
    if (metadata.nvram_size == 0) {
        return 0;
    }

    assert(ht->nvram_size == metadata.nvram_size);
    assert(ht->range_size == metadata.range_size);
    // reconsititute the rest of the httable from
    ht->size = metadata.size;
    ht->blksz = metadata.blksz;
    ht->mod = metadata.mod;
    ht->mask = metadata.mask;
    ht->nnodes = metadata.nnodes;
    ht->noccupied = metadata.noccupied;
    ht->data = metadata.data_start;

    return 1;
}

static int
nvm_write_metadata(nvm_hash_idx_t *ht) {
    dev_hash_metadata_t metadata;

    // reconsititute the rest of the httable from
    metadata.nvram_size = ht->nvram_size;
    metadata.size = ht->size;
    metadata.range_size = ht->range_size;
    metadata.mod = ht->mod;
    metadata.mask = ht->mask;
    metadata.nnodes = ht->nnodes;
    metadata.noccupied = ht->noccupied;
    metadata.data_start = ht->data;

    ssize_t err = ht->callbacks->cb_write(ht->metadata, 0,
                                          sizeof(metadata), (char*)&metadata);

    if_then_panic(err != sizeof(metadata), "Could not write metadata!");

    return 1;
}

/*
 * Update a single slot in NVRAM.
 * Used to insert or update a key -- since we'll never need to modify keys
 * en-masse, this will be fine.
 *
 * start: block address of range.
 * index: byte index into range.
 */
static inline void
nvm_update(nvm_hash_idx_t *ht, paddr_t idx, hash_ent_t* val) {
    paddr_t paddr = ht->data + BLK_NUM(ht, idx);
    size_t size   = sizeof(*val);
    off_t offset  = BLK_IDX(ht, idx) * size;

    ssize_t ret = ht->callbacks->cb_write(paddr, offset, size, (char*)val);

    if_then_panic(ret != size, "did not write full entry!");
}

#ifdef __cplusplus
}
#endif

#endif /* __NVM_IDX_G_HASH_MOD_H__ */
