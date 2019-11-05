#ifndef __NVM_IDX_RADIXTREE_RADIXTREE_H__
#define __NVM_IDX_RADIXTREE_RADIXTREE_H__

#ifdef __cplusplus
extern "C" {
#define _Static_assert static_assert
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

// For path memoization.
typedef struct radixtree_path {
    paddr_t page;
    int max_level;
    paddr_t *contents;
    laddr_t last_idx;
    paddr_t last_ent;
} radixpath_t;

#define DO_MEMOIZATION
//#undef DO_MEMOIZATION

#define METADATA_CACHING
//#undef METADATA_CACHING

#define RADIX_NDIRECT 7

typedef struct ondevice_radixtree_metadata {
    volatile uint32_t nentries;  
    volatile uint16_t nlevels;
    volatile uint16_t is_locked;
    pmlock_t rwlock;
} ondev_radix_meta_t;

_Static_assert(sizeof(ondev_radix_meta_t) <= 64, "On-device metadata > 64!");

typedef struct radixtree_stats {
    uint64_t depth_total;
    uint64_t depth_nr;
    uint64_t memo_hits;
    uint64_t references;
    uint64_t nlookups;
} radixtree_stats_t;

extern radixtree_stats_t rstats;
extern bool g_radix_do_stats;

static void print_radixtree_stats(radixtree_stats_t *s) {
    printf("Radixtree stats:\n");
    printf("\tAverage depth: %.1f\n", 
           (double)s->depth_total / (double)s->depth_nr);
    printf("\tAverage references per lookup: %.1f\n",
           (double)s->references / (double)s->nlookups);
    printf("\tAverage memo hit ratio: %.1f\n",
           (double)s->memo_hits / (double)s->references);
}

#define print_global_radixtree_stats() print_radixtree_stats(&rstats)

typedef struct radixtree_metadata {
    bool cached;
    paddr_range_t metadata_loc;

    // -- CACHING
    // ---- Metadata
    bool reread_meta;
    bool use_direct;
    bool rewrite_meta;
    paddr_t *direct_entries;
    ondev_radix_meta_t *ondev;
    // ---- General
    radix_node_t *cached_tree;

    size_t blksz;
    char*  dev_addr; // Avoid frequent callbacks.
    size_t ent_size;
    // We can make the size of radix nodes larger than one block.
    // We want 4 levels, so we have 16 bits of indexing per node.
    size_t ents_per_node;
    size_t node_nbytes;
    size_t nblk_per_node;
    size_t ent_idx_mask;
    size_t ent_shift;

    size_t max_depth;

    // For memoization. One per possible level of the radix tree.
    radixpath_t prev_path[4];

    callback_fns_t *idx_callbacks;
    mem_man_fns_t  *idx_mem_man;

} radixtree_meta_t;

static void inc_stats(radixtree_meta_t *radix, radixtree_stats_t *s) {
    ADD_STAT(s, depth_total, radix->ondev->nlevels + 1);
    INCR_STAT(s, depth_nr);
}

#define inc_global_stats(radix) \
    if (g_radix_do_stats) inc_stats(radix, &rstats)

#define get_contents(r, pblk) (paddr_t*)(r->dev_addr + (r->blksz * pblk))
#define rmeta(r) (&((r)->metadata_loc))
#define get_ondev(r) (ondev_radix_meta_t*)(r->dev_addr + (r->blksz * rmeta(r)->pr_start) \
                                + rmeta(r)->pr_blk_offset)
#define get_direct(r) (paddr_t*)(r->dev_addr + (r->blksz * rmeta(r)->pr_start) \
                                + rmeta(r)->pr_blk_offset + sizeof(ondev_radix_meta_t))
#define top_page(r) (r->direct_entries[0])
#define use_direct(r) (r->ondev->nlevels == 0)

int radixtree_init(const idx_spec_t *idx_spec,
                   const paddr_range_t *metadata_location,
                   idx_struct_t *idx_struct);

ssize_t radixtree_lookup(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t max, paddr_t *paddr);

ssize_t radixtree_create(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages, paddr_t *paddr);

ssize_t radixtree_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages);

void radixtree_clear_metadata_cache(idx_struct_t *idx_struct);

extern idx_fns_t radixtree_fns;

#ifdef __cplusplus
}
#endif

#endif
