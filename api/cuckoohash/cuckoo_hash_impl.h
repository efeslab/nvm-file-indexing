/*
  Copyright (C) 2010 Tomash Brechko.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __NVM_IDX_CUCKOO_HASH_IMPL__
#define __NVM_IDX_CUCKOO_HASH_IMPL__ 1

#include <stddef.h>
#include <stdbool.h>
#include <immintrin.h>

#include "common/common.h"
#include "common/hash_functions.h"

#ifdef __cplusplus
extern "C" {
#define _Static_assert static_assert
#endif  /* __cplusplus */

#define CUCKOO_HASH_FAILED  ((void *) -1)


typedef struct cuckoo_hash_item {
    // 8
    paddr_t key;
    // 2
    uint8_t index;
    uint8_t range;
    // 6
    uint16_t value_hi;
    uint32_t value_lo;
} cuckoo_item_t;

typedef struct cuckoo_hash_elem {
    //cuckoo_item_t hash_item;
    // 8
    //uint32_t hash1;
    //uint32_t hash2;
    // 8
    paddr_t key;
    // 2
    uint8_t index;
    uint8_t range;
    // 6
    uint16_t value_hi;
    uint32_t value_lo;
} cuckoo_elem_t;

#define sz sizeof(cuckoo_elem_t)

_Static_assert(sz <= 64, "Must be <= cache line size!");
_Static_assert(sz == 1 || !(sz & (sz - 1)) , "Must be a power of 2!");

#undef sz

#define CUCKOO_MAGIC 0xcafebabe
typedef struct cuckoo_meta {
    uint32_t magic;
    paddr_t elem_start_blk;
    size_t max_size;
    pmlock_t rwlock;
} nvm_cuckoo_metadata_t;

typedef struct cuckoo_hash {
    nvm_cuckoo_metadata_t *meta;
    cuckoo_elem_t *table;
    bool do_stats;
    bool compact_idx;
} nvm_cuckoo_idx_t;

typedef struct cuckoo_hash_stats {
    uint64_t ncachelines_written;
    uint64_t nblocks_inserted;
    uint64_t nwrites;
    STAT_FIELD(compute_hash);
    STAT_FIELD(non_conflict_time);
    STAT_FIELD(conflict_time);
    uint64_t nlookups;
    uint64_t nbuckets_checked;
    uint64_t nconflicts;
} nvm_cuckoo_stats_t;

extern nvm_cuckoo_stats_t cstats;

/*
  cuckoo_hash_init(hash, power):

  Initialize the hash.  power controls the initial hash table size,
  which is (bin_size << power), i.e., 4*2^power.  Zero means one.

  Return 0 on success, -errno if initialization failed.
*/
int
cuckoo_hash_init(nvm_cuckoo_idx_t **ht, paddr_t meta_block, size_t max_entries, 
                 const idx_spec_t *idx_spec);


/*
  cuckoo_hash_destroy(hash):

  Destroy the hash, i.e., free memory.
*/
void
cuckoo_hash_destroy(const struct cuckoo_hash *hash);


/*
  cuckoo_hash_count(hash):

  Return number of elements in the hash.
*/
static inline
size_t
cuckoo_hash_count(struct cuckoo_hash *hash) {
    return 0;
}


/*
  cuckoo_hash_insert(hash, key, key_len, value):

  Insert new value into the hash under the given key.

  Return NULL on success, or the pointer to the existing element with
  the same key, or the constant CUCKOO_HASH_FAILED when operation
  failed (memory exhausted).  If you want to replace the existing
  element, assign the new value to result->value.  You likely will
  have to free the old value, and a new key, if they were allocated
  dynamically.
*/
int
cuckoo_hash_insert(struct cuckoo_hash *hash, paddr_t key, paddr_t value,
                   uint32_t index, uint32_t range);


/*
  cuckoo_hash_lookup(hash, key, key_len):

  Lookup given key in the hash.

  Return pointer to struct cuckoo_hash_item, or NULL if the key
  doesn't exist in the hash.
*/
int
cuckoo_hash_lookup(const struct cuckoo_hash *hash,
                   paddr_t key, paddr_t *value, uint32_t *size);

int
cuckoo_hash_lookup_parallel(const struct cuckoo_hash *hash,
                            paddr_t key, paddr_t *values, size_t nr);
/*
  cuckoo_hash_lookup(hash, key, key_len):

  Lookup given key in the hash.

  Return pointer to struct cuckoo_hash_item, or NULL if the key
  doesn't exist in the hash.
*/
int
cuckoo_hash_update(const struct cuckoo_hash *hash, paddr_t key, uint32_t size);

/*
  cuckoo_hash_remove(hash, hash_item):

  Remove the element from the hash that was previously returned by
  cuckoo_hash_lookup() or cuckoo_hash_next().

  It is safe to pass NULL in place of hash_item, so you may write
  something like

    cuckoo_hash_remove(hash, cuckoo_hash_lookup(hash, key, key_len));

  hash_item passed to cuckoo_hash_remove() stays valid until the next
  call to cuckoo_hash_insert() or cuckoo_hash_destroy(), so if you
  allocated the key and/or value dynamically, you may free them either
  before or after the call (they won't be referenced inside):

    struct cuckoo_hash_item *item = cuckoo_hash_lookup(hash, k, l);
    free((void *) item->key);
    cuckoo_hash_remove(hash, item);
    free(item->value);

  (that (void *) cast above is to cast away the const qualifier).
*/
int
cuckoo_hash_remove(struct cuckoo_hash *hash, paddr_t key, paddr_t *value,
                   uint32_t *index, uint32_t *range);

#ifdef __cplusplus
}      /* extern "C" */
#endif  /* __cplusplus */


#endif  /* ! _CUCKOO_HASH_H */
