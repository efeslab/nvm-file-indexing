#ifndef __NVM_IDX_RADIXTREE_RADIXTREE_H__
#define __NVM_IDX_RADIXTREE_RADIXTREE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "common/common.h"

#define GET_RADIX(i) radixtree_meta_t *radix = (radixtree_meta_t*)(i)->idx_metadata

typedef struct radix_node {
    // Where this page is located on the device.
    // First block of many.
    paddr_t rn_page;
    // How deep the node is in the tree. Radix tree has fixed depth.
    size_t rn_depth;
    // Number of valid entries in this page. If 0, should deallocate.
    size_t rn_nvalid;
    // The contents of this page on the device.
    paddr_t *rn_dev_contents;
    // Status of cache: 0, uptodate, 1, dirty, -1, invalid
    int8_t *rn_cache_state;
    // The in-memory cached structures.
    struct radix_node *rn_cache_tree;
} radix_node_t;

#define RADIX_MAGIC 0xfeedbeef
typedef struct ondevice_radixtree_metadata {
    uint32_t magic;
    uint16_t nlevels;
    uint32_t nentries;
    paddr_t top_page;
} ondev_radix_meta_t;

typedef struct radixtree_metadata {
    bool cached;
    paddr_range_t metadata_loc;
    paddr_t top_page;
    uint16_t nlevels;
    uint32_t nentries;
    radix_node_t *cached_tree;

    size_t blksz;
    size_t ent_size;
    // We can make the size of radix nodes larger than one block.
    // We want 4 levels, so we have 16 bits of indexing per node.
    size_t ents_per_node;
    size_t node_nbytes;
    size_t nblk_per_node;
    size_t ent_idx_mask;
    size_t ent_shift;

    size_t max_depth;

    callback_fns_t *idx_callbacks;
    mem_man_fns_t  *idx_mem_man;

} radixtree_meta_t;

int radixtree_init(const idx_spec_t *idx_spec,
                   const paddr_range_t *metadata_location,
                   idx_struct_t *idx_struct);

ssize_t radixtree_lookup(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t max, paddr_t *paddr);

ssize_t radixtree_create(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages, paddr_t *paddr);

ssize_t radixtree_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages);

extern idx_fns_t radixtree_fns;

#ifdef __cplusplus
}
#endif

#endif
