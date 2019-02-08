#ifndef __INODE_HASH__
#define __INODE_HASH__ 1

#include <malloc.h>
#include <memory.h>
#include <string.h>

#include "ghash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTINUITY_BITS 4
#define MAX_CONTIGUOUS_BLOCKS (2 << 4)
#define REMAINING_BITS ((CHAR_BIT * sizeof(paddr_t)) - CONTINUITY_BITS - 1)

#define TO_64(hk) (*((uint64_t*)&hk))

typedef paddr_t hash_value_t;

#define SPECIAL(hv) !(!(hv & (1UL << 63UL)))
#define INDEX(hv) ((hv >> REMAINING_BITS) & ((1lu << CONTINUITY_BITS) - 1lu))
#define ADDR(hv) (((1lu << REMAINING_BITS) - 1lu) & hv)

#define SBIT(cond) (cond ? (1UL << 63UL) : 0UL)
#define IBITS(i) ((uint64_t)i << REMAINING_BITS)

#define MAKEVAL(cond, idx, addr) (SBIT(cond) | IBITS(idx) | addr)

#define GVAL2PTR(hv) ((void*)(hv))
#define GPTR2VAL(pt) ((uint64_t)pt)


#define MASK_32 ((1lu << 32) - 1)
#define MAKEKEY(inum, key) (((uint64_t)inum << 32) | key)
#define GET_INUM(hk) (hk >> 32)
#define GET_LBLK(hk) (hk & MASK_32)
#define GKEY2PTR(hk) ((void*)(hk))
#define GPTR2KEY(pt) ((uint64_t)pt)

typedef paddr_t hash_key_t;

/*
 * This is how the global hash table structure is designed and used:
 * TODO
 */


// TODO: just to fix compile issues.
struct mlfs_map_blocks { int whatever; };
typedef paddr_t handle_t;
struct inode { int x; };

/*
 * Generic hash table functions.
 */

#define GHASH(i, n) GHashTable *(n) = (GHashTable*)(i)->idx_metadata

int init_hash(const idx_spec_t *idx_spec, idx_struct_t *idx_struct);

ssize_t insert_hash(idx_struct_t *idx_struct, inum_t inum,
                    laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t lookup_hash(idx_struct_t *idx_struct, inum_t inum,
                laddr_t laddr, paddr_t* paddr);

ssize_t erase_hash(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, size_t size);
/*
 * Emulated mlfs_ext functions.
 */


int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags, bool force);

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		laddr_t start, laddr_t end);

/*
 * Helper functions.
 */

int mlfs_hash_persist();
int mlfs_hash_cache_invalidate();

#ifdef __cplusplus
}
#endif

#endif  // __INODE_HASH__
