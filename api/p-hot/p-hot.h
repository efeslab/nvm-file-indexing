#ifndef __NVM_IDX_P_HOT__
#define __NVM_IDX_P_HOT__ 1

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern idx_fns_t p_hot_fns;

int p_hot_init(const idx_spec_t *idx_spec,
               const paddr_range_t *metadata_location,
               idx_struct_t *idx_struct);

ssize_t p_hot_create(idx_struct_t *idx_struct, inum_t inum,
                          laddr_t laddr, size_t size, paddr_t *new_paddr);

ssize_t p_hot_lookup(idx_struct_t *idx_struct, inum_t inum,
                          laddr_t laddr, size_t max, paddr_t* paddr);
ssize_t p_hot_lookup_parallel(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t max, paddr_t* paddr);

ssize_t p_hot_remove(idx_struct_t *idx_struct,
                          inum_t inum, laddr_t laddr, size_t size);

int p_hot_set_caching(idx_struct_t *idx_struct, bool enable);
int p_hot_set_locking(idx_struct_t *idx_struct, bool enable);
int p_hot_persist_updates(idx_struct_t *idx_struct);
int p_hot_invalidate_caches(idx_struct_t *idx_struct);

#ifdef __cplusplus
}
#endif

#endif  // __NVM_IDX_P_HOT__
