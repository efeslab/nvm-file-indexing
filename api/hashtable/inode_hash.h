#ifndef __NVM_IDX_INODE_HASH__
#define __NVM_IDX_INODE_HASH__ 1

#include <malloc.h>
#include <memory.h>
#include <string.h>

#include "ghash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAKEKEY(inum, key) (((uint64_t)inum << 32) | key)

typedef paddr_t hash_key_t;

extern idx_fns_t hash_fns;

/*
 * Generic hash table functions.
 */

#define NVMHASH(i, n) nvm_hash_idx_t *(n) = (nvm_hash_idx_t*)(i)->idx_metadata

int hashtable_initialize(const idx_spec_t *idx_spec,
                         idx_struct_t *idx_struct, paddr_t *location);

ssize_t hashtable_create(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t hashtable_lookup(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t max, paddr_t* paddr);

ssize_t hashtable_remove(idx_struct_t *idx_struct,
                         inum_t inum, laddr_t laddr, size_t size);

int hashtable_set_caching(idx_struct_t *idx_struct, bool enable);
int hashtable_set_locking(idx_struct_t *idx_struct, bool enable);
int hashtable_persist_updates(idx_struct_t *idx_struct);
int hashtable_invalidate_caches(idx_struct_t *idx_struct);

void hashtable_set_stats(idx_struct_t* idx_struct, bool enable);

void hashtable_print_global_stats();

#ifdef __cplusplus
}
#endif

#endif  // __NVM_IDX_INODE_HASH__
