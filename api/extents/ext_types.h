#ifndef __NVM_IDX_EXT_TYPES_H__
#define __NVM_IDX_EXT_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common/common.h"
#include <pthread.h>

/*******************************************************************************
 * Section: Regular extent tree data types.
 ******************************************************************************/

/*
 * inode has i_data array (60 bytes total).
 * The first 12 bytes store extent_header;
 * the remainder stores an array of extent.
 * For non-inode extent blocks, extent_tail
 * follows the array.
 */

/*
 * This is the extent tail on-disk structure.
 * All other extent structures are 12 bytes long.  It turns out that
 * block_size % 12 >= 4 for at least all powers of 2 greater than 512, which
 * covers all valid ext4 block sizes.  Therefore, this tail structure can be
 * crammed into the end of the block without having to rebalance the tree.
 */
typedef struct nvm_api_extent_tail {
    uint32_t et_checksum; /* crc32c(uuid+inum+extent_block) */
} extent_tail_t;

#define ET_CHECKSUM_MAGIC 0xF1ABCD1F

/*
 * This is the extent on-disk structure.
 * It's used at the bottom of the tree.
 */
typedef struct nvm_api_extent {
    uint32_t ee_block;    /* first logical block extent covers */
    uint16_t ee_len;      /* number of blocks covered by extent */
    uint16_t ee_start_hi; /* high 16 bits of physical block */
    uint32_t ee_start_lo; /* low 32 bits of physical block */
} extent_leaf_t;

/*
 * This is the index on-disk structure.
 * It's used at all the levels except for the bottom.
 */
typedef struct nvm_api_extent_idx {
    uint32_t ei_block;   /* index covers logical blocks from 'block' */
    uint32_t ei_leaf_lo; /* pointer to the physical block of the next level.
                            leaf or next index could be there */
    uint16_t ei_leaf_hi; /* high 16 bits of physical block */
    uint16_t ei_unused;
} extent_branch_t;

#if 0
_Static_assert(sizeof(extent_branch_t) == sizeof(extent_leaf_t),
               "Must be the same size!");
#endif

/*
 * Each block (leaves and indexes), even inode-stored has header.
 */
typedef struct nvm_api_extent_header {
    uint16_t eh_magic;      /* probably will support different formats */
    uint16_t eh_entries;    /* number of valid entries */
    uint16_t eh_max;        /* capacity of store in entries */
    uint16_t eh_depth;      /* has tree real underlying blocks? */
    //uint32_t eh_generation; /* generation of the tree */
    pmlock_t eh_lock;
} extent_header_t;

typedef struct nvm_api_extent_tree_cache_node {
    int8_t cn_status;
    // The buffer of the leaf data -- use this for the path objects too!
    size_t cn_raw_size;
    char *cn_raw;
    // The index into the above buffer is the same for the cache nodes.

    struct nvm_api_extent_tree_cache_node *cn_nodes;
} extent_cache_t;

/*
 * Array of ext_path contains path to some extent.
 * It works as a cursor for a given key (logical block).
 * Creation/lookup routines use it for traversal/splitting/etc.
 * Truncate uses it to simulate recursive walking.
 */
typedef struct nvm_api_extent_tree_path {
    paddr_t p_block;
    uint16_t p_depth;
    uint16_t p_maxdepth;
    extent_leaf_t *p_ext;
    extent_branch_t *p_idx;
    extent_header_t *p_hdr;
    paddr_t p_pblk;
    char *p_raw;
    // CACHING
    int8_t p_cache_state;
    extent_cache_t *p_node;
} extent_path_t;

/*******************************************************************************
 * Section: More API-centric things.
 ******************************************************************************/
typedef struct extent_stats {
    STAT_FIELD(read_metadata_blocks);
    STAT_FIELD(read_from_device);
    uint64_t ncachelines_written;
    uint64_t nblocks_inserted;
    uint64_t nwrites;
    uint64_t depth_total;
    uint64_t depth_nr;
} ext_stats_t;

extern ext_stats_t estats;
extern bool g_do_stats;

static void print_ext_stats(ext_stats_t *s) {
    printf("extent tree stats: \n");
    PFIELD(s, read_metadata_blocks);
    PFIELD(s, read_from_device);
    printf("\tInserts: %.1f blocks per op (%lu / %lu)\n",
        (float)estats.nblocks_inserted / (float)estats.nwrites,
        estats.nblocks_inserted, estats.nwrites);
    printf("\tInserts: %.1f cachelines per op (%lu / %lu)\n",
        (float)estats.ncachelines_written / (float)estats.nwrites, 
        estats.ncachelines_written, estats.nwrites);
    printf("\tAverage depth: %.1f\n",
        (float)estats.depth_total / (float)estats.depth_nr);
}

#define MAX_DEPTH 10
#define EXT_MEMOIZATION
//#undef EXT_MEMOIZATION

#define METADATA_CACHING
//#undef METADATA_CACHING

typedef struct nvm_api_extent_tree_metadata {
    paddr_range_t et_direct_range;
    // stats struct
    bool et_enable_stats;
    ext_stats_t *et_stats;
    // buffers for the paths
    char **et_buffers;
    void *et_direct_data;

    // To avoid malloc/free overhead
    extent_path_t *path;
    extent_path_t *prev_path;

    // To avoid cb_get_addr overhead
    char *devaddr;
    size_t blksz;

    // -- Force re-read of metadata
    bool reread_meta;

    // -- CACHING
    bool et_cached;
    int8_t et_direct_data_cache_state;
    extent_cache_t *et_direct_cache;
    // could be extent leaf or branch nodes
    void *et_direct_data_cache;
} ext_meta_t;

#define getaddr(em, pblk) ((em)->devaddr + ((pblk) * (em)->blksz))

#ifdef __cplusplus
}
#endif

#endif
