#ifndef __NVM_IDX_EXT_UTIL_H__
#define __NVM_IDX_EXT_UTIL_H__

#ifdef __cplusplus
#define _Static_assert static_assert
extern "C" {
#endif

#include "common/common.h"

#include "ext_types.h"

uint32_t crc32c(uint32_t crc, const void *buf, size_t size);

/* ERRORs
 */
#define EFSCORRUPTED    117
#define EFSBADCRC        74

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
    uint32_t eh_generation; /* generation of the tree */
} extent_header_t;

/*
 * API Data Structures
 */


#define EXTMETA(i, n) ext_meta_t *(n) = (ext_meta_t*)(i)->idx_metadata
#define EXTHDR(m, n)  extent_header_t *(n) = (extent_header_t*)((m)->et_direct_data)

#define EXT_MAGIC (0xf30a)

#define EXTENT_TAIL_OFFSET(hdr)                 \
    (sizeof(extent_header_t) +             \
     (sizeof(extent_leaf_t) * (hdr)->eh_max))

static inline extent_tail_t *find_ext_tail(extent_header_t *eh)
{
#ifdef __cplusplus
    return (extent_tail_t*)((eh) + EXTENT_TAIL_OFFSET(eh));
#else
    return (extent_tail_t*)(((void *)eh) + EXTENT_TAIL_OFFSET(eh));
#endif
}

/*
 * Array of ext_path contains path to some extent.
 * It works as a cursor for a given key (logical block).
 * Creation/lookup routines use it for traversal/splitting/etc.
 * Truncate uses it to simulate recursive walking.
 */
typedef struct ext_path {
    paddr_t p_block;
    uint16_t p_depth;
    uint16_t p_maxdepth;
    extent_leaf_t *p_ext;
    extent_branch_t *p_idx;
    extent_header_t *p_hdr;
    char *p_raw;
    paddr_t p_pblk;
} extent_path_t;

/*
 * Flags used by ext4_map_blocks()
 */
    /* Allocate any needed blocks and/or convert an unwritten
       extent to be an initialized ext4 */
#define GET_BLOCKS_CREATE 0x0001 //alias of CREATE_DATA
#define GET_BLOCKS_CREATE_DATA            0x0001
#define GET_BLOCKS_CREATE_DATA_LOG 0x0002
#define GET_BLOCKS_CREATE_META            0x0004
    /* Request the creation of an unwritten extent */
#define GET_BLOCKS_UNWRIT_EXT        0x0008
#define GET_BLOCKS_CREATE_UNWRIT_EXT    (GET_BLOCKS_UNWRIT_EXT|\
                         GET_BLOCKS_CREATE)
    /* caller is from the direct IO path, request to creation of an
    unwritten extents if not allocated, split the unwritten
    extent if blocks has been preallocated already*/
#define GET_BLOCKS_PRE_IO            0x0010
    /* Eventual metadata allocation (due to growing extent tree)
     * should not fail, so try to use reserved blocks for that.*/
#define GET_BLOCKS_METADATA_NOFAIL        0x0020
    /* Convert written extents to unwritten */
#define GET_BLOCKS_CONVERT_UNWRITTEN    0x0040
    /* Write zeros to newly created written extents */
#define GET_BLOCKS_ZERO            0x0080
#define GET_BLOCKS_CREATE_ZERO        (GET_BLOCKS_CREATE |\
                    GET_BLOCKS_ZERO)

/*
 * Flags used in mballoc's allocation_context flags field.
 */
#define MB_USE_RESERVED        0x2000

/*
 * The bit position of these flags must not overlap with any of the
 * GET_BLOCKS_*.  They are used by find_extent(),
 * read_extent_tree_block(), split_extent_at(),
 * ext_insert_extent(), and ext_create_new_leaf().
 * EX_NOCACHE is used to indicate that the we shouldn't be
 * caching the extents when reading from the extent tree while a
 *  truncate or punch hole operation is in progress.
 */
#define EX_NOCACHE             0x40000000
#define EX_FORCE_CACHE         0x20000000

/*
 * Flags used by free_blocks
 */
#define FREE_BLOCKS_METADATA   0x0001
#define FREE_BLOCKS_FORGET     0x0002
#define FREE_BLOCKS_VALIDATED  0x0004
#define FREE_BLOCKS_NO_QUOT_UPDATE 0x0008
#define FREE_BLOCKS_NOFREE_FIRST_CLUSTER   0x0010
#define FREE_BLOCKS_NOFREE_LAST_CLUSTER    0x0020

#define MAP_NEW        (1 << 0)
#define MAP_LOG_ALLOC  (1 << 1)
#define MAP_GC_ALLOC   (1 << 2)

typedef struct nvm_api_map_blocks {
    paddr_t m_pblk;
    laddr_t m_lblk;
    uint32_t m_len;
    uint32_t m_flags;
} map_blocks_t;

/*
 * EXT_INIT_MAX_LEN is the maximum number of blocks we can have in an
 * initialized extent. This is 2^15 and not (2^16 - 1), since we use the
 * MSB of ee_len field in the extent datastructure to signify if this
 * particular extent is an initialized extent or an uninitialized (i.e.
 * preallocated).
 * EXT_UNINIT_MAX_LEN is the maximum number of blocks we can have in an
 * uninitialized extent.
 * If ee_len is <= 0x8000, it is an initialized extent. Otherwise, it is an
 * uninitialized one. In other words, if MSB of ee_len is set, it is an
 * uninitialized extent with only one special scenario when ee_len = 0x8000.
 * In this case we can not have an uninitialized extent of zero length and
 * thus we make it as a special case of initialized extent with 0x8000 length.
 * This way we get better extent-to-group alignment for initialized extents.
 * Hence, the maximum number of blocks we can have in an *initialized*
 * extent is 2^15 (32768) and in an *uninitialized* extent is 2^15-1 (32767).
 */
#define EXT_INIT_MAX_LEN (1 << 15)
#define EXT_UNWRITTEN_MAX_LEN (EXT_INIT_MAX_LEN - 1)

#define EXT_EXTENT_SIZE sizeof(extent_leaf_t)
#define EXT_INDEX_SIZE sizeof(extent_branch_t)

#define EXT_FIRST_EXTENT(__hdr__)                                 \
    ((extent_leaf_t *)(((char *)(__hdr__)) +                 \
        sizeof(extent_header_t)))
#define EXT_FIRST_INDEX(__hdr__)                                  \
    ((extent_branch_t *)(((char *)(__hdr__)) +             \
        sizeof(extent_header_t)))
#define EXT_HAS_FREE_INDEX(__path__)                              \
    ((__path__)->p_hdr->eh_entries < (__path__)->p_hdr->eh_max)
#define EXT_LAST_EXTENT(__hdr__)                                  \
    (EXT_FIRST_EXTENT((__hdr__)) + (__hdr__)->eh_entries - 1)
#define EXT_LAST_INDEX(__hdr__)                                   \
    (EXT_FIRST_INDEX((__hdr__)) + (__hdr__)->eh_entries - 1)
#define EXT_MAX_EXTENT(__hdr__)                                   \
    (EXT_FIRST_EXTENT((__hdr__)) + (__hdr__)->eh_max - 1)
#define EXT_MAX_INDEX(__hdr__)                                    \
    (EXT_FIRST_INDEX((__hdr__)) + (__hdr__)->eh_max - 1)

static inline extent_header_t *ext_header(const idx_struct_t *ext_idx)
{
    EXTMETA(ext_idx, ext_meta); EXTHDR(ext_meta, eh);
    return eh;
}

static inline int write_ext_direct_data(const idx_struct_t *ext_idx)
{
    EXTMETA(ext_idx, ext_meta); EXTHDR(ext_meta, eh);
    ssize_t nbytes = CB(ext_idx, cb_write,
                        ext_meta->et_direct_range.pr_start,
                        ext_meta->et_direct_range.pr_blk_offset,
                        ext_meta->et_direct_range.pr_nbytes,
                        (char*)ext_meta->et_direct_data);
    if (nbytes != ext_meta->et_direct_range.pr_nbytes) {
        printf("nbytes = %lu, but wrote %lu\n",
                ext_meta->et_direct_range.pr_nbytes,
                nbytes);
        fflush(stdout);
        return -EIO;
    }

    return 0;
}

static inline extent_header_t *ext_header_from_block(char *buf)
{
    return (extent_header_t*)buf;
}

static inline uint16_t ext_tree_depth(const idx_struct_t *ext_idx)
{
    return le16_to_cpu(ext_header(ext_idx)->eh_depth);
}

static inline size_t device_block_size(const idx_struct_t *ext_idx)
{
    device_info_t devinfo;
    int err = CB(ext_idx, cb_get_dev_info, &devinfo);
    if_then_panic(err, "Could not retrieve block size!");
    return devinfo.di_block_size;
}

static inline uint16_t ext_get_real_len(extent_leaf_t *ext)
{
    return (le16_to_cpu(ext->ee_len) <= EXT_INIT_MAX_LEN
            ? le16_to_cpu(ext->ee_len)
            : (le16_to_cpu(ext->ee_len) - EXT_INIT_MAX_LEN));
}

static inline void ext_mark_as_init(extent_leaf_t *ext)
{
    ext->ee_len = cpu_to_le16(ext_get_real_len(ext));
}

static inline void ext_mark_unwritten(extent_leaf_t *ext)
{
    ext->ee_len |= cpu_to_le16(EXT_INIT_MAX_LEN);
}

static inline int ext_is_unwritten(extent_leaf_t *ext)
{
    /* Extent with ee_len of 0x8000 is treated as an initialized extent */
    return (le16_to_cpu(ext->ee_len) > EXT_INIT_MAX_LEN);
}

static inline laddr_t ext_lblock(extent_leaf_t *ex)
{
    return le32_to_cpu(ex->ee_block);
}

static inline laddr_t idx_lblock(extent_branch_t *ix)
{
    return le32_to_cpu(ix->ei_block);
}

/*
 * ext_pblock:
 * combine low and high parts of physical block number into paddr_t
 */
static inline paddr_t ext_pblock(extent_leaf_t *ex)
{
    paddr_t block;

    block = ex->ee_start_lo;
    block |= ((paddr_t)ex->ee_start_hi << 31) << 1;
    return block;
}

/*
 * idx_pblock:
 * combine low and high parts of a leaf physical block number into paddr_t
 */
static inline paddr_t idx_pblock(extent_branch_t *ix)
{
    paddr_t block;

    block = ix->ei_leaf_lo;
    block |= ((paddr_t)ix->ei_leaf_hi << 31) << 1;
    return block;
}

static inline void ext_set_lblock(extent_leaf_t *ex,
        laddr_t lblk)
{
    ex->ee_block = cpu_to_le32(lblk);
}

static inline void idx_set_lblock(extent_branch_t *ix,
        laddr_t lblk)
{
    ix->ei_block = cpu_to_le32(lblk);
}

/*
 * ext_set_pblock:
 * stores a large physical block number into an extent struct,
 * breaking it into parts
 */
static inline void ext_set_pblock(extent_leaf_t *ex,
        paddr_t pb)
{
    ex->ee_start_lo = (unsigned long)(pb & 0xffffffff);
    ex->ee_start_hi = (unsigned long)((pb >> 31) >> 1) & 0xffff;
}

static inline void ext_store_pblock(extent_leaf_t *ex,
        paddr_t pb)
{
    ex->ee_start_lo = (unsigned long)(pb & 0xffffffff);
    ex->ee_start_hi = (unsigned long)((pb >> 31) >> 1) & 0xffff;
}

/*
 * idx_set_pblock:
 * stores a large physical block number into an index struct,
 * breaking it into parts
 */
static inline void idx_set_pblock(extent_branch_t *ix,
        paddr_t pb)
{
    ix->ei_leaf_lo = (unsigned long)(pb & 0xffffffff);
    ix->ei_leaf_hi = (unsigned long)((pb >> 31) >> 1) & 0xffff;
}

static inline void idx_store_pblock(extent_branch_t *ix,
        paddr_t pb)
{
    ix->ei_leaf_lo = (unsigned long)(pb & 0xffffffff);
    ix->ei_leaf_hi = (unsigned long)((pb >> 31) >> 1) & 0xffff;
}

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#define in_range(b, first, len) \
    ((b) >= (first) && (b) <= (first) + (len) - 1)

#define ext_dirty(ext_idx, path)  \
    __ext_dirty(__func__, __LINE__, (ext_idx), (path))

int ext_get_blocks(idx_struct_t *ext_idx, map_blocks_t *map, int flags);

extent_path_t *find_extent(idx_struct_t *ext_idx, laddr_t block,
                           extent_path_t **orig_path, int flags);

int ext_truncate(idx_struct_t *ext_idx, laddr_t from, laddr_t to);

void extent_tree_set_stats(idx_struct_t *ext_idx, bool enable);

void extent_tree_print_stats(idx_struct_t *ext_idx);

#ifdef __cplusplus
}
#endif

#endif
