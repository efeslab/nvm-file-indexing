#ifndef __INODE_HASH__
#define __INODE_HASH__ 1

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

#define GHASH(i, n) GHashTable *(n) = (GHashTable*)(i)->idx_metadata

int init_hash(const idx_spec_t *idx_spec, idx_struct_t *idx_struct, paddr_t *location);

ssize_t insert_hash(idx_struct_t *idx_struct, inum_t inum,
                    laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t lookup_hash(idx_struct_t *idx_struct, inum_t inum,
                    laddr_t laddr, paddr_t* paddr);

ssize_t erase_hash(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, size_t size);
/*
 * Emulated mlfs_ext functions.
 */

#if 0
int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags, bool force);

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		laddr_t start, laddr_t end);
#endif
/*
 * Helper functions.
 */

int mlfs_hash_persist();
int mlfs_hash_cache_invalidate();

#ifdef __cplusplus
}
#endif

#endif  // __INODE_HASH__
