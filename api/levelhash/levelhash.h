#ifndef __NVM_IDX_LEVELHASH_LEVELHASH_H__
#define __NVM_IDX_LEVELHASH_LEVELHASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common/common.h"
#include "level_hashing.h"

/*
 *  Here's where we interface with the outside world.
 */

extern idx_fns_t levelhash_fns;

int levelhash_init(const idx_spec_t* idx_spec, const paddr_range_t* dirent_ents, 
                   idx_struct_t* level_idx);

ssize_t levelhash_lookup(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, paddr_t* paddr);

ssize_t levelhash_create(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk, paddr_t* paddr);

ssize_t levelhash_remove(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk);


void levelhash_set_stats(idx_struct_t* level_idx, bool enable);
void levelhash_print_stats(idx_struct_t* level_idx);

/*
 * Convenience macros.
 */

#define LEVELMETA(i, l) level_hash *(l) = (level_hash*)(i)->idx_metadata

#ifdef __cplusplus
}
#endif

#endif
