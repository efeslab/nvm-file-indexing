#ifndef __NVM_IDX_EXTENTS_H__
#define __NVM_IDX_EXTENTS_H__

#include "common/common.h"
#include "ext_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * API Functions
 */

int extent_tree_init(const idx_spec_t *idx_spc,
                     const paddr_range_t *direct_ents,
                     idx_struct_t *ext_idx);

ssize_t extent_tree_create(idx_struct_t *idx_struct, inum_t inum,
                           laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t extent_tree_lookup(idx_struct_t *idx_struct, inum_t inum,
                           laddr_t laddr, size_t max, paddr_t* paddr);

ssize_t extent_tree_remove(idx_struct_t *idx_struct,
                           inum_t inum, laddr_t laddr, size_t size);

int extent_tree_set_caching(idx_struct_t* idx_struct, bool enable);
int extent_tree_persist_updates(idx_struct_t* idx_struct);
int extent_tree_invalidate_caches(idx_struct_t* idx_struct);

void extent_tree_set_stats(idx_struct_t *ext_idx, bool enable);

void extent_tree_print_stats(idx_struct_t *ext_idx);

extern idx_fns_t extent_tree_fns;

#ifdef __cplusplus
}
#endif

#endif /* _NEW_BTREE_H */
