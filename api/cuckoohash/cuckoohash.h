#ifndef __NVM_IDX_CUCKOO_HASH__
#define __NVM_IDX_CUCKOO_HASH__ 1

#include <malloc.h>
#include <memory.h>
#include <string.h>

#include "common/common.h"
#include "cuckoo_hash_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

// iangneal: ensure the key is never 0
#define MAKECUCKOOKEY(inum, key) (((uint64_t)(inum + 1) << 32) | key)

typedef paddr_t hash_key_t;

extern idx_fns_t cuckoohash_fns;

/*
 * Generic hash table functions.
 */

#define CUCKOOHASH(i, n) nvm_cuckoo_idx_t *(n) = (nvm_cuckoo_idx_t*)(i)->idx_metadata

int cuckoohash_initialize(const idx_spec_t *idx_spec,
                         idx_struct_t *idx_struct, paddr_t *location);

ssize_t cuckoohash_create(idx_struct_t *idx_struct, inum_t inum,
                          laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t cuckoohash_lookup(idx_struct_t *idx_struct, inum_t inum,
                          laddr_t laddr, size_t max, paddr_t* paddr);
ssize_t cuckoohash_lookup_parallel(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t max, paddr_t* paddr);

ssize_t cuckoohash_remove(idx_struct_t *idx_struct,
                          inum_t inum, laddr_t laddr, size_t size);

int cuckoohash_set_caching(idx_struct_t *idx_struct, bool enable);
int cuckoohash_set_locking(idx_struct_t *idx_struct, bool enable);
int cuckoohash_persist_updates(idx_struct_t *idx_struct);
int cuckoohash_invalidate_caches(idx_struct_t *idx_struct);

void cuckoohash_print_global_stats(void);

#ifdef __cplusplus
}
#endif

#endif  // __NVM_IDX_CUCKOO_HASH__
