#include <malloc.h>
#include <memory.h>
#include <string.h>

#include "ext_util.h"
#include "extents.h"

ext_stats_t estats = {0,};
bool g_do_stats = false;

/*
 * used by extent splitting.
 */
#define EXT_MAY_ZEROOUT  \
    0x1                  /* safe to zeroout if split fails \
                     due to ENOSPC */
#define EXT_MARK_UNWRIT1 0x2 /* mark first half unwritten */
#define EXT_MARK_UNWRIT2 0x4 /* mark second half unwritten */

#define EXT_DATA_VALID1 0x8  /* first half contains valid data */
#define EXT_DATA_VALID2 0x10 /* second half contains valid data */

#define BUG_ON(x) 0

/*
 * Return the right sibling of a tree node(either leaf or indexes node)
 */

#define EXT_MAX_BLOCKS 0xffffffff

static inline int ext_space_block(idx_struct_t *ext_idx){
    int size;

    static device_info_t devinfo = {0,};
    if (unlikely(devinfo.di_block_size == 0)) {
        int err = CB(ext_idx, cb_get_dev_info, &devinfo);
        if (err) return err;
    }
      
    size = (devinfo.di_block_size - sizeof(extent_header_t)) / sizeof(extent_leaf_t);

    return size;
}

static inline int ext_space_block_idx(idx_struct_t *ext_idx){
    int size;

    static device_info_t devinfo = {0,};
    if (unlikely(devinfo.di_block_size == 0)) {
        int err = CB(ext_idx, cb_get_dev_info, &devinfo);
        if (err) return err;
    }

    size = (devinfo.di_block_size - sizeof(extent_header_t)) / sizeof(extent_branch_t);

    return size;
}

static inline ssize_t ext_space_root(const idx_struct_t *ext_idx) {
    EXTMETA(ext_idx, ext_meta);
    return (ext_meta->et_direct_range.pr_nbytes - sizeof(extent_header_t))
            / sizeof(extent_leaf_t);
}

static inline int ext_space_root_idx(const idx_struct_t *ext_idx) {
    EXTMETA(ext_idx, ext_meta);
    return (ext_meta->et_direct_range.pr_nbytes - sizeof(extent_header_t))
            / sizeof(extent_branch_t);
}

static int ext_max_entries(idx_struct_t *ext_idx, int depth) {
    int max;

    if (depth == ext_tree_depth(ext_idx)) {
        if (depth == 0) {
            max = ext_space_root(ext_idx);
        } else {
            max = ext_space_root_idx(ext_idx);
        }
    } else {
        if (depth == 0) {
            max = ext_space_block(ext_idx);
        } else {
            max = ext_space_block_idx(ext_idx);
        }
    }

    return max;
}

static int ext_check(idx_struct_t *ext_idx, extent_header_t *eh,
                          int depth, paddr_t pblk);

int extent_tree_init(const idx_spec_t *idx_spec,
                     const paddr_range_t *direct_ents, 
                     idx_struct_t *ext_idx)
{
    if (NULL == ext_idx) return -EINVAL;
    if (NULL == idx_spec) return -EINVAL;
    if (NULL == direct_ents) return -EINVAL;

    EXTMETA(ext_idx, ext_meta);

    if (NULL != ext_meta) return -EEXIST;

    // if null, then read from device.
    ext_idx->idx_mem_man   = idx_spec->idx_mem_man;
    ext_idx->idx_callbacks = idx_spec->idx_callbacks;
    ext_idx->idx_fns       = &extent_tree_fns;
    ext_meta               = ZALLOC(idx_spec, sizeof(*ext_meta));

    if (NULL == ext_meta) return -ENOMEM;

    ext_idx->idx_metadata = (void*)ext_meta;

    ext_meta->reread_meta = true; // Always read on init.
    ext_meta->et_direct_range = *direct_ents;

    size_t ents_root = ext_space_root(ext_idx);

    ext_meta->et_stats = ZALLOC(idx_spec, sizeof(*(ext_meta->et_stats)));
    if (NULL == ext_meta->et_stats) return -ENOMEM;
    ext_meta->et_enable_stats = false;

    ext_meta->path = ZALLOC(idx_spec, sizeof(*(ext_meta->path)) * MAX_DEPTH);
    ext_meta->prev_path = ZALLOC(idx_spec, sizeof(*(ext_meta->path)) * MAX_DEPTH);
    if (!ext_meta->path || !ext_meta->prev_path) return -ENOMEM;

    #if 0
    ext_meta->et_buffers = ZALLOC(idx_spec, MAX_DEPTH * sizeof(char*));
    if (NULL == ext_meta->et_buffers) return -ENOMEM;

    size_t blksz = device_block_size(ext_idx);

    for (int i = 0; i < MAX_DEPTH; ++i) {
        ext_meta->et_buffers[i] = ZALLOC(idx_spec, blksz);
        if (NULL == ext_meta->et_buffers[i]) return -ENOMEM;
    }
    #endif

    ext_meta->et_direct_data_cache = ZALLOC(idx_spec,
            (ents_root * sizeof(extent_leaf_t)) + sizeof(extent_header_t));
    if (NULL == ext_meta->et_direct_data_cache) return -ENOMEM; 

    int read_ret = read_ext_direct_data(ext_idx);
    if (read_ret) return read_ret;

    EXTHDR(ext_meta, eh);

    if (eh->eh_magic != cpu_to_le16(EXT_MAGIC)) {
        memset(eh, 0, sizeof(*eh));
        eh->eh_magic = cpu_to_le16(EXT_MAGIC);
        eh->eh_max = cpu_to_le16(ents_root);

        if (write_ext_direct_data(ext_idx)) return -EIO;
    }

    // Get the device root address
    int aerr = CB(ext_idx, cb_get_addr, 0, 0, &(ext_meta->devaddr));
    if_then_panic(aerr, "Could not get device address!\n");

    device_info_t devinfo = {0,};
    int derr = CB(ext_idx, cb_get_dev_info, &devinfo);
    if (derr) return derr;
    ext_meta->blksz = devinfo.di_block_size;

    // Caching:
    ext_meta->et_cached = false;
    ext_meta->et_direct_data_cache_state = 0;
    ext_meta->et_direct_cache = ZALLOC(idx_spec, (ents_root * sizeof(extent_leaf_t)));
    if (NULL == ext_meta->et_direct_cache) return -ENOMEM;
    

    return 0;
}

/*
 * read_extent_tree_block:
 * Get a buffer_head by fs_bread, and read fresh data from the storage.
 */
static int read_extent_tree_block(idx_struct_t *ext_idx, char **buf,
                                    paddr_t pblk, int depth)
{
    EXTMETA(ext_idx, ext_meta);
    DECLARE_TIMING();
    if (ext_meta->et_enable_stats) {
        START_TIMING();
    }
    if (depth >= MAX_DEPTH) {
        fprintf(stderr, "depth (%d) >= MAX_DEPTH (%d)!\n", depth, MAX_DEPTH);
        panic("not enough buffers!");
    }

    uint64_t second_tsc = _asm_rdtscp();
    #if 0
    device_info_t devinfo;
    err = CB(ext_idx, cb_get_dev_info, &devinfo);
    if (err) return err;
    ssize_t nbytes = CB(ext_idx, cb_read,
                        pblk, 0, devinfo.di_block_size, buf);
    #elif 0
    err = CB(ext_idx, cb_get_addr, pblk, 0, buf);
    #else
    *buf = getaddr(ext_meta, pblk);
    #endif
    if (ext_meta->et_enable_stats) {
        UPDATE_STAT(&estats, read_from_device, second_tsc);
    }
    #if 0
    if (nbytes != devinfo.di_block_size) {
        return -EIO;
    }
    #elif 0
    if (err) return -EIO;
    #endif

    if (depth >= 0) {
        int err = ext_check(ext_idx, ext_header_from_block(*buf), depth, pblk);
        if (err) return err;
    }

    if (ext_meta->et_enable_stats) {
        UPDATE_TIMING(&estats, read_metadata_blocks);
    }

    return 0;
}

int ext_check_inode(idx_struct_t *ext_idx)
{
    return ext_check(ext_idx, ext_header(ext_idx), ext_tree_depth(ext_idx), 0);
}

static uint32_t ext_block_csum(idx_struct_t *ext_idx, extent_header_t *eh)
{
    /*return crc32c(inode->i_csum, eh, EXTENT_TAIL_OFFSET(eh));*/
    return ET_CHECKSUM_MAGIC;
}

static void extent_block_csum_set(idx_struct_t *ext_idx, extent_header_t *eh)
{
    extent_tail_t *tail;

    tail = find_ext_tail(eh);
    tail->et_checksum = ext_block_csum(ext_idx, eh);
    nvm_persist_struct(tail->et_checksum);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                          sizeof(tail->et_checksum));
    }

}

static int split_extent_at(idx_struct_t *ext_idx,
                           extent_path_t **ppath,
                           laddr_t split,
                           int split_flag,
                           int flags);

static inline int force_split_extent_at(idx_struct_t *ext_idx,
                                        extent_path_t **ppath,
                                        laddr_t lblk,
                                        int nofail)
{
    extent_path_t *path = *ppath;
    int unwritten = ext_is_unwritten(path[path->p_depth].p_ext);

    return split_extent_at(ext_idx, ppath, lblk,
            unwritten ? EXT_MARK_UNWRIT1 | EXT_MARK_UNWRIT2 : 0,
            EX_NOCACHE | GET_BLOCKS_PRE_IO |
            (nofail ? GET_BLOCKS_METADATA_NOFAIL : 0));
}

static paddr_t ext_find_goal(idx_struct_t *ext_idx,
                                  extent_path_t *path,
                                  laddr_t block)
{
    if (path) {
        int depth = path->p_depth;
        extent_leaf_t *ex;

        /*
         * Try to predict block placement assuming that we are
         * filling in a file which will eventually be
         * non-sparse --- i.e., in the case of libbfd writing
         * an ELF object sections out-of-order but in a way
         * the eventually results in a contiguous object or
         * executable file, or some database extending a table
         * space file.  However, this is actually somewhat
         * non-ideal if we are writing a sparse file such as
         * qemu or KVM writing a raw image file that is going
         * to stay fairly sparse, since it will end up
         * fragmenting the file system's free space.  Maybe we
         * should have some hueristics or some way to allow
         * userspace to pass a hint to file system,
         * especially if the latter case turns out to be
         * common.
         */
        ex = path[depth].p_ext;
        if (ex) {
            paddr_t ext_pblk = ext_pblock(ex);
            laddr_t ext_block = le32_to_cpu(ex->ee_block);

            if (block > ext_block)
                return ext_pblk + (block - ext_block);
            else
                return ext_pblk - (ext_block - block);
        }

        /* it looks like index is empty;
         * try to find starting block from index itself
         *
        if (path[depth].p_bh)
            return path[depth].p_bh->b_blocknr;
         */
        return path[depth].p_block;
    }

    return 0;
}

int __ext_dirty(const char *where, unsigned int line,
                idx_struct_t *ext_idx,
                extent_path_t *path)
{
    int err = 0;

    if (path->p_raw) {
        //printf("write block %lu\n", path->p_pblk);
        /* path points to block */
        extent_block_csum_set(ext_idx, ext_header_from_block(path->p_raw));
        //fs_mark_buffer_dirty(path->p_bh);
    #if 0 
        ssize_t nwrite = CB(ext_idx, cb_write,
                            path->p_pblk,
                            0,
                            device_block_size(ext_idx),
                            path->p_raw);
        if (nwrite != device_block_size(ext_idx)) err = -EIO;
    #endif  
    } else {
        //printf("write direct\n");
        /* path points to leaf/index in inode body */
        //err = mark_inode_dirty(ext_idx);
        err = write_ext_direct_data(ext_idx);
    }

    return err;
}

/**
 * Clean up path references by setting all the raw data buffers to NULL.
 * Does not call FREE on anything.
 */
#if 0
void ext_drop_refs(idx_struct_t *ext_idx, extent_path_t *path)
{
    int depth, i;

    if (!path) return;

    depth = path->p_depth;
    for (i = 0; i <= depth; i++, path++) {
        if (path->p_raw) {
            //FREE(ext_idx, path->p_raw);
            path->p_raw = NULL;
        }
    }
}
#else
#define ext_drop_refs(ext_idx, path) 0
#endif

/*
 * Check that whether the basic information inside the extent header
 * is correct or not.
 */
static int ext_check(idx_struct_t *ext_idx, extent_header_t *eh,
                     int depth, paddr_t pblk)
{
    extent_tail_t *tail;
    const char *error_msg;
    int max = 0;

    if (eh->eh_magic != EXT_MAGIC) {
        error_msg = "%s invalid magic";
        goto corrupted;
    }
    if (le16_to_cpu(eh->eh_depth) != depth) {
        error_msg = "%s unexpected eh_depth";
        goto corrupted;
    }
    if (eh->eh_max == 0) {
        error_msg = "%s invalid eh_max";
        goto corrupted;
    }
    if (eh->eh_entries > eh->eh_max) {
        error_msg = "%s invalid eh_entries";
        goto corrupted;
    }

    tail = find_ext_tail(eh);
    if (tail->et_checksum != ext_block_csum(ext_idx, eh)) {
    }

    return 0;

corrupted:
    if_then_panic(true, error_msg, __func__);
    return -EIO;
}

/*
 * ext_binsearch_idx:
 * binary search for the closest index of the given block
 * the header must be checked before calling this
 */
static void ext_binsearch_idx(idx_struct_t *ext_idx, extent_path_t *path,
                              laddr_t block)
{
    //EXTMETA(ext_idx, ext_meta);
    //EXTHDR(ext_meta, eh);
    extent_header_t *eh = path->p_hdr;
    extent_branch_t *r, *l, *m;

    l = EXT_FIRST_INDEX(eh) + 1;
    r = EXT_LAST_INDEX(eh);

    while (l <= r) {
        m = l + (r - l) / 2;
        if (block < idx_lblock(m))
            r = m - 1;
        else
            l = m + 1;
    }

    path->p_idx = l - 1;
}

/*
 * ext_binsearch:
 * binary search for closest extent of the given block
 * the header must be checked before calling this
 * When returning, it sets proper extents to path->p_ext.
 */
static void ext_binsearch(idx_struct_t *ext_idx, extent_path_t *path,
                          laddr_t block)
{
    //EXTMETA(ext_idx, ext_meta);
    //EXTHDR(ext_meta, eh);
    extent_header_t *eh = path->p_hdr;
    extent_leaf_t *r, *l, *m;

    if (eh->eh_entries == 0) {
        /*
         * this leaf is empty:
         * we get such a leaf in split/add case
         */
        return;
    }

    l = EXT_FIRST_EXTENT(eh) + 1;
    r = EXT_LAST_EXTENT(eh);

    while (l <= r) {
        m = l + (r - l) / 2;
        if (block < ext_lblock(m)) {
            r = m - 1;
        } else {
            l = m + 1;
        }
    }

    path->p_ext = l - 1;
}

ssize_t search_extent_leaf(extent_leaf_t *ex, laddr_t laddr, paddr_t *paddr) {
    if (!ex) return 0;

    laddr_t ee_block = le32_to_cpu(ex->ee_block);
    paddr_t ee_start = ext_pblock(ex);
    unsigned short ee_len;

    /*
        * unwritten extents are treated as holes, except that
        * we split out initialized portions during a write.
        */
    ee_len = ext_get_real_len(ex);

    /* find extent covers block. simply return the extent */
    if (in_range(laddr, ee_block, ee_len)) {
        /* number of remain blocks in the extent */
        size_t nblocks = ee_len + ee_block - laddr;

        if (!ext_is_unwritten(ex)) {
            *paddr = laddr - ee_block + ee_start;
            return nblocks;
        }
    }

    return 0;
}

/* path works like cursor of extent tree.
 * path[0] is root of the tree (stored in inode->i_data)
 */
extent_path_t *find_extent(idx_struct_t *ext_idx, laddr_t block,
                           extent_path_t **orig_path, int flags)
{
    //struct buffer_head *bh;
    char *buf;
    extent_path_t *path = orig_path ? *orig_path : NULL;
    short int depth, i, ppos = 0;
    int ret;

    EXTMETA(ext_idx, ext_meta);
    EXTHDR(ext_meta, eh);

    depth = ext_tree_depth(ext_idx);

    if_then_panic(!path, "should send pre-allocated path");

    ext_drop_refs(ext_idx, path);
    assert(depth <= MAX_DEPTH);
  
    path[0].p_hdr = eh;
    //if (ext_meta->et_cached) {
    //    path[0].p_node = ext_meta->et_direct_cache;
    //}
    // buffer_head of root is always NULL.
    //path[0].p_bh = NULL;

    i = depth;
    /* walk through internal nodes (index nodes) of the tree from a root */
    while (i) {

        #if 0 && defined(EXT_MEMOIZATION)
        extent_path_t *prevp = &(ext_meta->prev_path[ppos]);
        extent_path_t *prevp_next = &(ext_meta->prev_path[ppos + 1]);
        if (prevp->p_hdr) {
            extent_branch_t *l, *r;
            l = prevp->p_idx;
            r = EXT_LAST_INDEX(prevp->p_hdr) != l ? l + 1 : l;
            
            if (l && r && block >= idx_lblock(l) && block < idx_lblock(r)) {
                path[ppos].p_block = idx_pblock(l);
                path[ppos].p_depth = i;
                path[ppos].p_idx = prevp->p_idx;
                path[ppos].p_ext = prevp->p_ext;
                i--; ppos++;

                if (unlikely(ppos > depth)) {
                    ret = -EIO;
                    goto err;
                }

                path[ppos].p_raw = prevp_next->p_raw;
                //path[ppos].p_pblk = prevp->p_block;
                path[ppos].p_pblk = idx_pblock(l);
                path[ppos].p_hdr = ext_header_from_block(prevp_next->p_raw);
                continue;
            }
        }
        #endif

        /* set the nearest index node */
        ext_binsearch_idx(ext_idx, path + ppos, block);

        path[ppos].p_block = idx_pblock(path[ppos].p_idx);
        path[ppos].p_depth = i;
        path[ppos].p_ext = NULL;


        i--;

        ret = read_extent_tree_block(ext_idx, &buf, path[ppos].p_block, i);

        if (unlikely(ret)) {
            goto err;
        }

        eh = ext_header_from_block(buf);

        ppos++;
        if (unlikely(ppos > depth)) {
            //fs_brelse(bh);
            //FREE(ext_idx, buf);
            //ERROR_INODE(inode, "ppos %d > depth %d", ppos, depth);
            ret = -EIO;
            goto err;
        }

        //path[ppos].p_bh = bh;
        path[ppos].p_raw = buf;
        path[ppos].p_pblk = path[ppos-1].p_block;
        path[ppos].p_hdr = eh;
    }

    // (iangneal): Don't add new memoization here because it was the first
    // thing we checked before we even called into find_extent.

    path[ppos].p_depth = i;
    path[ppos].p_ext = NULL;
    path[ppos].p_idx = NULL;

    /* Search leaf node (extent) of the tree */
    ext_binsearch(ext_idx, path + ppos, block);

    /* if not an empty leaf */
    if (path[ppos].p_ext) {
        path[ppos].p_block = ext_pblock(path[ppos].p_ext);
    }

    return path;

err:
    ext_drop_refs(ext_idx, path);
    if (path) {
        //FREE(ext_idx, path);
        if (orig_path) {
            *orig_path = NULL;
        }
    }
    return (extent_path_t*)ERR_PTR(ret);
}
/*
 * ext_insert_index:
 * insert new index [@logical;@ptr] into the block at @curp;
 * check where to insert: before @curp or after @curp
 */
static int ext_insert_index(idx_struct_t *ext_idx,
        extent_path_t *curp, int logical, paddr_t ptr)
{
    extent_branch_t *ix;
    int len, err;

    if (unlikely(logical == le32_to_cpu(curp->p_idx->ei_block))) {
        //ERROR_INODE(inode, "logical %d == ei_block %d!", logical,
        //        le32_to_cpu(curp->p_idx->ei_block));
        return -EIO;
    }

    if (unlikely(le16_to_cpu(curp->p_hdr->eh_entries) >=
                le16_to_cpu(curp->p_hdr->eh_max))) {
        //ERROR_INODE(inode, "eh_entries %d >= eh_max %d!",
        //        le16_to_cpu(curp->p_hdr->eh_entries),
        //        le16_to_cpu(curp->p_hdr->eh_max));
        return -EIO;
    }

    if (logical > le32_to_cpu(curp->p_idx->ei_block)) {
        /* insert after */
        ix = curp->p_idx + 1;
    } else {
        /* insert before */
        ix = curp->p_idx;
    }

    len = EXT_LAST_INDEX(curp->p_hdr) - ix + 1;
    if (len > 0) {
        pmem_memmove_persist(ix + 1, ix, len * sizeof(extent_branch_t));
        
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, len * sizeof(*ix));
        }
    }

    if (unlikely(ix > EXT_MAX_INDEX(curp->p_hdr))) {
        //ERROR_INODE(inode, "%s\n", "ix > EXT_MAX_INDEX!");
        return -EIO;
    }

    ix->ei_block = cpu_to_le32(logical);
    idx_store_pblock(ix, ptr);
    le16_add_cpu(&curp->p_hdr->eh_entries, 1);
    nvm_persist_struct(curp->p_hdr->eh_entries);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                          sizeof(curp->p_hdr->eh_entries));
    }

    if (unlikely(ix > EXT_LAST_INDEX(curp->p_hdr))) {
        //ERROR_INODE(inode, "%s\n", "ix > EXT_LAST_INDEX!");
        return -EIO;
    }

    err = ext_dirty(ext_idx, curp);
    // std_error(inode->i_sb, err);

    return err;
}

/*
 * ext_split:
 * inserts new subtree into the path, using free index entry
 * at depth @at:
 * - allocates all needed blocks (new leaf and all intermediate index blocks)
 * - makes decision where to split
 * - moves remaining extents and index entries (right to the split point)
 *   into the newly allocated blocks
 * - initializes subtree
 */
static int ext_split(idx_struct_t *ext_idx,
        unsigned int flags, extent_path_t *path,
        extent_leaf_t *newext, int at)
{
    //struct buffer_head *bh = NULL;
    EXTMETA(ext_idx, ext_meta);
    char *buf = NULL;
    int depth = ext_tree_depth(ext_idx);
    extent_header_t *neh;
    extent_branch_t *fidx;
    int i = at, k, m, a, ret;
    paddr_t newblock, oldblock;
    __le32 border;
    paddr_t ablocks[MAX_DEPTH]; /* array of allocated blocks */
    int err = 0;

    /* make decision: where to split? */
    /* FIXME: now decision is simplest: at current extent */

    /* if current leaf will be split, then we should use
     * border from split point */
    if (unlikely(path[depth].p_ext > EXT_MAX_EXTENT(path[depth].p_hdr))) {
        //ERROR_INODE(inode, "%s\n", "p_ext > EXT_MAX_EXTENT!");
        return -EIO;
    }
    if (path[depth].p_ext != EXT_MAX_EXTENT(path[depth].p_hdr)) {
        border = path[depth].p_ext[1].ee_block;
    } else {
        border = newext->ee_block;
    }

    /*
     * If error occurs, then we break processing
     * and mark filesystem read-only. index won't
     * be inserted and tree will be in consistent
     * state. Next mount will repair buffers too.
     */

    /*
     * Get array to track all allocated blocks.
     * We need this to handle errors and free blocks
     * upon them.
     */
    // ablocks = (paddr_t *)ZALLOC(ext_idx, sizeof(paddr_t) * depth);
    // if (!ablocks)
    //     return -ENOMEM;

    /* allocate all needed blocks */
    for (a = 0; a < depth - at; a++) {
        /*
        newblock = ext_new_meta_block(ext_idx, path, newext,
                &err, flags);
        */
        ssize_t nalloc = CB(ext_idx, cb_alloc_metadata,
                            1, &newblock);
        if (newblock == 0 || nalloc < 1) {
            goto cleanup;
        }
        //printf("block %lu is now metadata!\n", newblock);

        ablocks[a] = newblock;
    }

    /* initialize new leaf */
    newblock = ablocks[--a];
    if (unlikely(newblock == 0)) {
        //ERROR_INODE(inode, "%s\n", "newblock == 0!");
        err = -EIO;
        goto cleanup;
    }

    #if 0
    buf = ZALLOC(ext_idx, device_block_size(ext_idx));

    if (NULL == buf) {
        err = -ENOMEM;
        goto cleanup;
    }
    #elif 0
    err = CB(ext_idx, cb_get_addr, newblock, 0, &buf);
    if (err) goto cleanup;
    #else
    buf = getaddr(ext_meta, newblock);
    #endif


    //TODO: call sync dirty buffer
    //bh = write(inode->i_sb, newblock);

    /*
    err = journal_get_create_access(handle, bh);
    if (err)
        goto cleanup;
    */

    neh = ext_header_from_block(buf);
    memset(neh, 0, sizeof(*neh));
    neh->eh_max = cpu_to_le16(ext_space_block(ext_idx));
    neh->eh_magic = cpu_to_le16(EXT_MAGIC);
    
    /* move remainder of path[depth] to the new leaf */
    if (unlikely(path[depth].p_hdr->eh_entries != path[depth].p_hdr->eh_max)) {
        err = -EIO;
        goto cleanup;
    }

    /* start copy from next extent */
    m = EXT_MAX_EXTENT(path[depth].p_hdr) - path[depth].p_ext++;
    //ext_show_move(ext_idx, path, newblock, depth);
    if (m) {
        extent_leaf_t *ex;
        ex = EXT_FIRST_EXTENT(neh);
        pmem_memmove_persist(ex, path[depth].p_ext, sizeof(extent_leaf_t) * m);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, m * sizeof(*ex));
        }

        le16_add_cpu(&neh->eh_entries, m);
    }

    extent_block_csum_set(ext_idx, neh);
    nvm_persist_struct_ptr(neh);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*neh));
    }

    /* correct old leaf */
    if (m) {
        le16_add_cpu(&path[depth].p_hdr->eh_entries, -m);
        err = ext_dirty(ext_idx, path + depth);
        nvm_persist_struct(path[depth].p_hdr->eh_entries);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, 
                              sizeof(path[depth].p_hdr->eh_entries));
        }

        if (err)
            goto cleanup;
    }

    /* create intermediate indexes */
    k = depth - at - 1;
    if (unlikely(k < 0)) {
        //ERROR_INODE(inode, "k %d < 0!", k);
        err = -EIO;
        goto cleanup;
    }

    /* insert new index into current index block */
    /* current depth stored in i var */
    i = depth - 1;

    while (k--) {
        oldblock = newblock;
        newblock = ablocks[--a];

    #if 0
        buf = ZALLOC(ext_idx, device_block_size(ext_idx));

        if (NULL == buf) {
            err = -ENOMEM;
            goto cleanup;
        }
    #elif 0
        err = CB(ext_idx, cb_get_addr, newblock, 0, &buf);
        if (err) goto cleanup;
    #else
        buf = getaddr(ext_meta, newblock);
    #endif

        neh = ext_header_from_block(buf);
        neh->eh_entries = cpu_to_le16(1);
        neh->eh_magic = cpu_to_le16(EXT_MAGIC);
        neh->eh_max = cpu_to_le16(ext_space_block_idx(ext_idx));
        neh->eh_depth = cpu_to_le16(depth - i);
        memset(&neh->eh_lock, 0, sizeof(neh->eh_lock));
        fidx = EXT_FIRST_INDEX(neh);
        fidx->ei_block = border;
        idx_store_pblock(fidx, oldblock);

        /* move remainder of path[i] to the new index block */
        if (unlikely(EXT_MAX_INDEX(path[i].p_hdr) !=
                    EXT_LAST_INDEX(path[i].p_hdr))) {
            /*
            ERROR_INODE(inode,
                    "EXT_MAX_INDEX != EXT_LAST_INDEX ee_block %d!",
                    le32_to_cpu(path[i].p_ext->ee_block));
            */
            err = -EIO;
            goto cleanup;
        }
        /* start copy indexes */
        m = EXT_MAX_INDEX(path[i].p_hdr) - path[i].p_idx++;

        //ext_show_move(ext_idx, path, newblock, i);
        if (m) {
            pmem_memmove_persist(++fidx, path[i].p_idx, sizeof(extent_branch_t) * m);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                  sizeof(extent_branch_t) * m);
            }

            le16_add_cpu(&neh->eh_entries, m);
        }

        extent_block_csum_set(ext_idx, neh);
        nvm_persist_struct_ptr(neh);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*neh));
        }

        /* correct old index */
        if (m) {
            le16_add_cpu(&path[i].p_hdr->eh_entries, -m);
            err = ext_dirty(ext_idx, path + i);
            nvm_persist_struct(path[i].p_hdr->eh_entries);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                sizeof(path[depth].p_hdr->eh_entries));
            }

            if (err) goto cleanup;
        }

        i--;
    }

    /* insert new index */
    err = ext_insert_index(ext_idx, path + at,
                            le32_to_cpu(border), newblock);

cleanup:

    if (err) {
        /* free all allocated blocks in error case */
        for (i = 0; i < depth; i++) {
            if (!ablocks[i]) continue;
            /*
            free_blocks(ext_idx, NULL, ablocks[i], 1,
                    FREE_BLOCKS_METADATA);
            */
            (void)CB(ext_idx, cb_dealloc_metadata, 1, ablocks[i]);
        }
    }

    return err;
}

/*
 * ext_grow_indepth:
 * implements tree growing procedure:
 * - allocates new block
 * - moves top-level data (index block or leaf) into the new block
 * - initializes new top-level, creating index that points to the
 *   just created block
 */
static int ext_grow_indepth(idx_struct_t *ext_idx, unsigned int flags)
{
    EXTMETA(ext_idx, ext_meta);
    extent_header_t *neh;
    //struct buffer_head *bh;
    char *buf;
    paddr_t newblock, goal = 0;
    int err = 0, ret;
    laddr_t count = 1;

    /* Try to prepend new index to old one */
    if (ext_tree_depth(ext_idx)) {
        goal = idx_pblock(EXT_FIRST_INDEX(ext_header(ext_idx)));
    }

    goal = 0;

    /*
    newblock = new_meta_blocks(ext_idx, goal, flags, &count, &err);
    if (newblock == 0)
        return err;
    */
    ssize_t nalloc = CB(ext_idx, cb_alloc_metadata, count, &newblock);
    if (nalloc < count || newblock == 0) {
        return nalloc;
    }
    #if 0
    buf = ZALLOC(ext_idx, device_block_size(ext_idx));
    if (!buf) return -ENOMEM;
    #elif 0
    err = CB(ext_idx, cb_get_addr, newblock, 0, &buf);
    if (err) goto out;
    #else
    buf = getaddr(ext_meta, newblock);
    #endif

    /*
    err = journal_get_create_access(handle, bh);
    if (err)
        goto out;
    */

    /* move top-level index/leaf into new block */
    //memmove(bh->b_data, inode->i_data, sizeof(inode->l1.i_data));
    //memmove(bh->b_data, ext_header(ext_idx), sizeof(inode->l1.i_data));
    pmem_memmove_persist(buf, ext_header(ext_idx), ext_meta->et_direct_range.pr_nbytes);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                          ext_meta->et_direct_range.pr_nbytes);
    }

    /* set size of new block */
    neh = ext_header_from_block(buf);
    /* old root could have indexes or leaves
     * so calculate e_max right way */
    if (ext_tree_depth(ext_idx)) {
        neh->eh_max = cpu_to_le16(ext_space_block_idx(ext_idx));
    } else {
        neh->eh_max = cpu_to_le16(ext_space_block(ext_idx));
    }

    neh->eh_magic = cpu_to_le16(EXT_MAGIC);
    extent_block_csum_set(ext_idx, neh);
    memset(&neh->eh_lock, 0, sizeof(neh->eh_lock));
    
    nvm_persist_struct_ptr(neh);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*neh));
    }
    #if 0
    ssize_t nwrite = CB(ext_idx, cb_write,
                        newblock, 0, device_block_size(ext_idx), buf);
    if_then_panic(nwrite != device_block_size(ext_idx), "wat");
    #endif
    /*
    set_buffer_uptodate(bh);
    unlock_buffer(bh);

    err = handle_dirty_metadata(ext_idx, bh);
    if (err) goto out;
    */

    /* Update top-level index: num,max,pointer */
    neh = ext_header(ext_idx);
    neh->eh_entries = cpu_to_le16(1);
    idx_store_pblock(EXT_FIRST_INDEX(neh), newblock);
    if (neh->eh_depth == 0) {
        /* Root extent block becomes index block */
        neh->eh_max = cpu_to_le16(ext_space_root_idx(ext_idx));
        EXT_FIRST_INDEX(neh)->ei_block = EXT_FIRST_EXTENT(neh)->ee_block;
    }

    le16_add_cpu(&neh->eh_depth, 1); 
    err = write_ext_direct_data(ext_idx); 
out:
    return err;
}

/*
 * ext_create_new_leaf:
 * finds empty index and adds new leaf.
 * if no free index is found, then it requests in-depth growing.
 */
static int ext_create_new_leaf(idx_struct_t *ext_idx,
        unsigned int mb_flags, unsigned int gb_flags,
        extent_path_t **ppath, extent_leaf_t *newext)
{
    extent_path_t *path = *ppath;
    extent_path_t *curp;
    int depth, i, err = 0;

repeat:
    i = depth = ext_tree_depth(ext_idx);

    /* walk up to the tree and look for free index entry */
    curp = path + depth;
    while (i > 0 && !EXT_HAS_FREE_INDEX(curp)) {
        i--;
        curp--;
    }

    /* we use already allocated block for index block,
     * so subsequent data blocks should be contiguous */
    if (EXT_HAS_FREE_INDEX(curp)) {
        /* if we found index with free entry, then use that
         * entry: create all needed subtree and add new leaf */
        err = ext_split(ext_idx, mb_flags, path, newext, i);
        if (err)
            goto out;

        /* refill path */
        path = find_extent(ext_idx, ext_lblock(newext),
                ppath, gb_flags);

        if (IS_ERR(path))
            err = PTR_ERR(path);
    } else {
        /* tree is full, time to grow in depth */
        err = ext_grow_indepth(ext_idx, mb_flags);
        if (err)
            goto out;

        /* refill path */
        path = find_extent(ext_idx, ext_lblock(newext),
                           ppath, gb_flags);
        if (IS_ERR(path)) {
            err = PTR_ERR(path);
            goto out;
        }

        /*
         * only first (depth 0 -> 1) produces free space;
         * in all other cases we have to split the grown tree
         */
        depth = ext_tree_depth(ext_idx);
        if (path[depth].p_hdr->eh_entries == path[depth].p_hdr->eh_max) {
            /* now we need to split */
            goto repeat;
        }
    }

out:
    return err;
}

/*
 * search the closest allocated block to the left for *logical
 * and returns it at @logical + it's physical address at @phys
 * if *logical is the smallest allocated block, the function
 * returns 0 at @phys
 * return value contains 0 (success) or error code
 */
static int ext_search_left(idx_struct_t *ext_idx, extent_path_t *path,
        laddr_t *logical, paddr_t *phys)
{
    extent_branch_t *ix;
    extent_leaf_t *ex;
    int depth, ee_len;

    if (unlikely(path == NULL)) {
        //ERROR_INODE(inode, "path == NULL *logical %d!", *logical);
        return -EIO;
    }
    depth = path->p_depth;
    *phys = 0;

    if (depth == 0 && path->p_ext == NULL)
        return 0;

    /* usually extent in the path covers blocks smaller
     * then *logical, but it can be that extent is the
     * first one in the file */

    ex = path[depth].p_ext;
    ee_len = ext_get_real_len(ex);
    if (*logical < le32_to_cpu(ex->ee_block)) {
        if (unlikely(EXT_FIRST_EXTENT(path[depth].p_hdr) != ex)) {
            /*
            ERROR_INODE(inode,
                    "EXT_FIRST_EXTENT != ex *logical %d ee_block %d!",
                    *logical, le32_to_cpu(ex->ee_block));
            */
            return -EIO;
        }
        while (--depth >= 0) {
            ix = path[depth].p_idx;
            if (unlikely(ix != EXT_FIRST_INDEX(path[depth].p_hdr))) {
                /*
                ERROR_INODE(
                        inode, "ix (%d) != EXT_FIRST_INDEX (%d) (depth %d)!",
                        ix != NULL ? le32_to_cpu(ix->ei_block) : 0,
                        EXT_FIRST_INDEX(path[depth].p_hdr) != NULL
                        ? le32_to_cpu(
                            EXT_FIRST_INDEX(path[depth].p_hdr)->ei_block)
                        : 0,
                        depth);
                */
                return -EIO;
            }
        }
        return 0;
    }

    if (unlikely(*logical < (le32_to_cpu(ex->ee_block) + ee_len))) {
        /*
        ERROR_INODE(inode, "logical %d < ee_block %d + ee_len %d!",
                *logical, le32_to_cpu(ex->ee_block), ee_len);
        */
        return -EIO;
    }

    *logical = le32_to_cpu(ex->ee_block) + ee_len - 1;
    *phys = ext_pblock(ex) + ee_len - 1;
    return 0;
}

/*
 * search the closest allocated block to the right for *logical
 * and returns it at @logical + it's physical address at @phys
 * if *logical is the largest allocated block, the function
 * returns 0 at @phys
 * return value contains 0 (success) or error code
 */
static int ext_search_right(idx_struct_t *ext_idx,
        extent_path_t *path, laddr_t *logical, paddr_t *phys,
        extent_leaf_t **ret_ex)
{
    //struct buffer_head *bh = NULL;
    EXTMETA(ext_idx, ext_meta);
    char *buf;
    extent_header_t *eh;
    extent_branch_t *ix;
    extent_leaf_t *ex;
    paddr_t block;
    int depth; /* Note, NOT eh_depth; depth from top of tree */
    int ee_len;

    if (path == NULL) {
        //ERROR_INODE(inode, "path == NULL *logical %d!", *logical);
        return -EIO;
    }
    depth = path->p_depth;
    *phys = 0;

    if (depth == 0 && path->p_ext == NULL)
        return 0;

    /* usually extent in the path covers blocks smaller
     * then *logical, but it can be that extent is the
     * first one in the file */

    ex = path[depth].p_ext;
    ee_len = ext_get_real_len(ex);
    /*if (*logical < le32_to_cpu(ex->ee_block)) {*/
    if (*logical < (ex->ee_block)) {
        if (unlikely(EXT_FIRST_EXTENT(path[depth].p_hdr) != ex)) {
            return -EIO;
        }
        while (--depth >= 0) {
            ix = path[depth].p_idx;
            if (unlikely(ix != EXT_FIRST_INDEX(path[depth].p_hdr))) {
                return -EIO;
            }
        }
        goto found_extent;
    }

    /*if (unlikely(*logical < (le32_to_cpu(ex->ee_block) + ee_len))) {*/
    if (unlikely(*logical < ((ex->ee_block) + ee_len))) {
        return -EIO;
    }

    if (ex != EXT_LAST_EXTENT(path[depth].p_hdr)) {
        /* next allocated block in this leaf */
        ex++;
        goto found_extent;
    }

    /* go up and search for index to the right */
    while (--depth >= 0) {
        ix = path[depth].p_idx;
        if (ix != EXT_LAST_INDEX(path[depth].p_hdr))
            goto got_index;
    }

    /* we've gone up to the root and found no index to the right */
    return 0;

got_index:
    /* we've found index to the right, let's
     * follow it and find the closest allocated
     * block to the right */
    ix++;
    block = idx_pblock(ix);
    while (++depth < path->p_depth) {
        /* subtract from p_depth to get proper eh_depth */
        int rerr = read_extent_tree_block(ext_idx, &buf, block, path->p_depth - depth);
        if (rerr) {
            return rerr;
        }

        eh = ext_header_from_block(buf);
        ix = EXT_FIRST_INDEX(eh);
        block = idx_pblock(ix);
        //fs_brelse(bh);
        //FREE(ext_idx, buf);
    }

    int rerr = read_extent_tree_block(ext_idx, &buf, block, path->p_depth - depth);
    if (rerr) return rerr;

    eh = ext_header_from_block(buf);
    ex = EXT_FIRST_EXTENT(eh);
found_extent:
    /**logical = le32_to_cpu(ex->ee_block);*/
    *logical = (ex->ee_block);
    *phys = ext_pblock(ex);
    *ret_ex = ex;

    return 0;
}

/*
 * ext_next_allocated_block:
 * returns allocated block in subsequent extent or EXT_MAX_BLOCKS.
 * NOTE: it considers block number from index entry as
 * allocated block. Thus, index entries have to be consistent
 * with leaves.
 */
laddr_t ext_next_allocated_block(extent_path_t *path)
{
    int depth;

    depth = path->p_depth;

    if (depth == 0 && path->p_ext == NULL) {
        return EXT_MAX_BLOCKS;
    }

    while (depth >= 0) {
        if (depth == path->p_depth) {
            /* extent (leaf) */
            if (path[depth].p_ext &&
                path[depth].p_ext != EXT_LAST_EXTENT(path[depth].p_hdr)) {
                return ext_lblock(&path[depth].p_ext[1]);
            }
        } else {
            /* index */
            if (path[depth].p_idx != EXT_LAST_INDEX(path[depth].p_hdr)) {
                return idx_lblock(&path[depth].p_idx[1]);
            }
        }
        depth--;
    }

    return EXT_MAX_BLOCKS;
}

/*
 * ext_next_leaf_block:
 * returns first allocated block from next leaf or EXT_MAX_BLOCKS
 */
static laddr_t ext_next_leaf_block(extent_path_t *path)
{
    int depth;

    depth = path->p_depth;

    /* zero-tree has no leaf blocks at all */
    if (depth == 0)
        return EXT_MAX_BLOCKS;

    /* go to upper internal node (index block) */
    depth--;

    while (depth >= 0) {
        if (path[depth].p_idx != EXT_LAST_INDEX(path[depth].p_hdr))
            /* return lblock of a next index node in a index block */
            return idx_lblock(&path[depth].p_idx[1]);

        depth--;
    }

    return EXT_MAX_BLOCKS;
}

/*
 * ext_correct_indexes:
 * if leaf gets modified and modified extent is first in the leaf,
 * then we have to correct all indexes above.
 * TODO: do we need to correct tree in all cases?
 */
static int ext_correct_indexes(idx_struct_t *ext_idx, extent_path_t *path)
{
    extent_header_t *eh;
    int depth = ext_tree_depth(ext_idx);
    extent_leaf_t *ex;
    __le32 border;
    int k, err = 0;

    eh = path[depth].p_hdr;
    ex = path[depth].p_ext;

    if (unlikely(ex == NULL || eh == NULL)) {
        //ERROR_INODE(inode, "ex %p == NULL or eh %p == NULL", ex, eh);
        return -EIO;
    }

    if (depth == 0) {
        /* there is no tree at all */
        return 0;
    }

    if (ex != EXT_FIRST_EXTENT(eh)) {
        /* we correct tree if first leaf got modified only */
        return 0;
    }

    /*
     * TODO: we need correction if border is smaller than current one
     */
    k = depth - 1;
    border = path[depth].p_ext->ee_block;

    path[k].p_idx->ei_block = border;
    err = ext_dirty(ext_idx, path + k);
    nvm_persist_struct(path[k].p_idx->ei_block);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                          sizeof(path[k].p_idx->ei_block));
    }

    if (err)
        return err;

    while (k--) {
        /* change all left-side indexes */
        if (path[k + 1].p_idx != EXT_FIRST_INDEX(path[k + 1].p_hdr))
            break;

        path[k].p_idx->ei_block = border;
        err = ext_dirty(ext_idx, path + k);
        nvm_persist_struct(path[k].p_idx->ei_block);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, 
                              sizeof(path[k].p_idx->ei_block));
        }
        if (err) break;
    }

    return err;
}

int can_extents_be_merged(idx_struct_t *ext_idx, extent_leaf_t *ex1,
        extent_leaf_t *ex2)
{
    unsigned short ext1_ee_len, ext2_ee_len;

    /*
     * Make sure that both extents are initialized. We don't merge
     * unwritten extents so that we can be sure that end_io code has
     * the extent that was written properly split out and conversion to
     * initialized is trivial.
     */
    if (ext_is_unwritten(ex1) != ext_is_unwritten(ex2))
        return 0;

    ext1_ee_len = ext_get_real_len(ex1);
    ext2_ee_len = ext_get_real_len(ex2);

    /* Logical block is contiguous */
    if (ext_lblock(ex1) + ext1_ee_len != ext_lblock(ex2))
        return 0;

    /*
     * To allow future support for preallocated extents to be added
     * as an RO_COMPAT feature, refuse to merge to extents if
     * this can result in the top bit of ee_len being set.
     */
    if (ext1_ee_len + ext2_ee_len > EXT_INIT_MAX_LEN)
        return 0;

    if (ext_is_unwritten(ex1) &&
            (ext1_ee_len + ext2_ee_len > EXT_UNWRITTEN_MAX_LEN))
        return 0;

    /* Physical block is contiguous */
    if (ext_pblock(ex1) + ext1_ee_len == ext_pblock(ex2))
        return 1;

    return 0;
}

/*
 * This function tries to merge the "ex" extent to the next extent in the tree.
 * It always tries to merge towards right. If you want to merge towards
 * left, pass "ex - 1" as argument instead of "ex".
 * Returns 0 if the extents (ex and ex+1) were _not_ merged and returns
 * 1 if they got merged.
 */
static int ext_try_to_merge_right(idx_struct_t *ext_idx,
                                       extent_path_t *path,
                                       extent_leaf_t *ex)
{
    extent_header_t *eh;
    unsigned int depth, len;
    int merge_done = 0, unwritten;

    depth = ext_tree_depth(ext_idx);
    assert(path[depth].p_hdr != NULL);
    eh = path[depth].p_hdr;

    while (ex < EXT_LAST_EXTENT(eh)) {
        if (!can_extents_be_merged(ext_idx, ex, ex + 1))
            break;

        /* merge with next extent! */
        unwritten = ext_is_unwritten(ex);
        ex->ee_len = cpu_to_le16(ext_get_real_len(ex) +
                ext_get_real_len(ex + 1));

        if (unwritten)
            ext_mark_unwritten(ex);

        nvm_persist_struct(ex->ee_len);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(ex->ee_len));
        }

        if (ex + 1 < EXT_LAST_EXTENT(eh)) {
            len = (EXT_LAST_EXTENT(eh) - ex - 1) * sizeof(extent_leaf_t);
            pmem_memmove_persist(ex + 1, ex + 2, len);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, len);
            }
        }

        le16_add_cpu(&eh->eh_entries, -1);
        merge_done = 1;

        nvm_persist_struct(eh->eh_entries);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, 
                              sizeof(eh->eh_entries));
        }
    }

    return merge_done;
}

/*
 * This function does a very simple check to see if we can collapse
 * an extent tree with a single extent tree leaf block into the inode.
 */
static void ext_try_to_merge_up(idx_struct_t *ext_idx, extent_path_t *path)
{
    size_t s;
    unsigned max_root = ext_space_root(ext_idx);
    paddr_t blk;

    if ((path[0].p_depth != 1) ||
            (le16_to_cpu(path[0].p_hdr->eh_entries) != 1) ||
            (le16_to_cpu(path[1].p_hdr->eh_entries) > max_root))
        return;

    /*
     * We need to modify the block allocation bitmap and the block
     * group descriptor to release the extent tree block.  If we
     * can't get the journal credits, give up.
     */
    /*
    if (journal_extend(handle, 2))
        return;
    */
    /*
     * Copy the extent data up to the inode
     */
    blk = idx_pblock(path[0].p_idx);
    s = le16_to_cpu(path[1].p_hdr->eh_entries) * sizeof(extent_branch_t);
    s += sizeof(extent_header_t);

    path[1].p_maxdepth = path[0].p_maxdepth;
    pmem_memcpy_persist(path[0].p_hdr, path[1].p_hdr, s);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, s);
    }

    path[0].p_depth = 0;
    path[0].p_ext = EXT_FIRST_EXTENT(path[0].p_hdr) +
        (path[1].p_ext - EXT_FIRST_EXTENT(path[1].p_hdr));
    path[0].p_hdr->eh_max = cpu_to_le16(max_root);

    nvm_persist_struct(path[0].p_hdr->eh_max);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                            sizeof(path[0].p_hdr->eh_max));
    }

    (void)CB(ext_idx, cb_dealloc_metadata, 1, blk);
}

/*
 * This function tries to merge the @ex extent to neighbours in the tree.
 * return 1 if merge left else 0.
 */
static void ext_try_to_merge(idx_struct_t *ext_idx,
                                  extent_path_t *path,
                                  extent_leaf_t *ex)
{
    extent_header_t *eh;
    unsigned int depth;
    int merge_done = 0;

    depth = ext_tree_depth(ext_idx);
    BUG_ON(path[depth].p_hdr == NULL);
    eh = path[depth].p_hdr;

    if (ex > EXT_FIRST_EXTENT(eh))
        merge_done = ext_try_to_merge_right(ext_idx, path, ex - 1);

    if (!merge_done)
        (void)ext_try_to_merge_right(ext_idx, path, ex);

    ext_try_to_merge_up(ext_idx, path);
}

/*
 * ext_insert_extent:
 * tries to merge requsted extent into the existing extent or
 * inserts requested extent as new one into the tree,
 * creating new leaf in the no-space case.
 */
int ext_insert_extent(idx_struct_t *ext_idx, extent_path_t **ppath,
                      extent_leaf_t *newext, int gb_flags)
{
    extent_path_t *path = *ppath;
    extent_header_t *eh;
    extent_leaf_t *ex, *last_ex;
    extent_leaf_t *nearex; /* nearest extent */
    extent_path_t *npath = NULL;
    int depth, len, err;
    laddr_t next_lb;
    int mb_flags = 0, unwritten;

    if (unlikely(ext_get_real_len(newext) == 0)) {
        return -EIO;
    }

    depth = ext_tree_depth(ext_idx);
    ex = path[depth].p_ext;
    eh = path[depth].p_hdr;

    if (unlikely(path[depth].p_hdr == NULL)) {
        return -EIO;
    }

    /* try to insert block into found extent and return */
    if (ex && !(gb_flags & GET_BLOCKS_PRE_IO)) {
        /*
         * Try to see whether we should rather test the extent on
         * right from ex, or from the left of ex. This is because
         * find_extent() can return either extent on the
         * left, or on the right from the searched position. This
         * will make merging more effective.
         */
        if (ex < EXT_LAST_EXTENT(eh) &&
                (ext_lblock(ex) + ext_get_real_len(ex) <
                 ext_lblock(newext))) {
            ex += 1;
            goto prepend;
        } else if ((ex > EXT_FIRST_EXTENT(eh)) &&
                (ext_lblock(newext) + ext_get_real_len(newext) <
                 ext_lblock(ex)))
            ex -= 1;

        /* Try to append newex to the ex */
        if (can_extents_be_merged(ext_idx, ex, newext)) {
            unwritten = ext_is_unwritten(ex);
            ex->ee_len = cpu_to_le16(ext_get_real_len(ex) +
                    ext_get_real_len(newext));
            
            if (unwritten) ext_mark_unwritten(ex);
            nvm_persist_struct(ex->ee_len);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                  sizeof(ex->ee_len));
            }
            
            eh = path[depth].p_hdr;
            nearex = ex;

            goto merge;
        }

prepend:
        /* Try to prepend newex to the ex */
        if (can_extents_be_merged(ext_idx, newext, ex)) {
            unwritten = ext_is_unwritten(ex);
            ex->ee_block = newext->ee_block;

            ext_store_pblock(ex, ext_pblock(newext));
            ex->ee_len = cpu_to_le16(ext_get_real_len(ex) +
                    ext_get_real_len(newext));

            if (unwritten) ext_mark_unwritten(ex);

            nvm_persist_struct_ptr(ex);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex));
            }

            eh = path[depth].p_hdr;
            nearex = ex;

            goto merge;
        }
    }

    depth = ext_tree_depth(ext_idx);
    eh = path[depth].p_hdr;

    if (le16_to_cpu(eh->eh_entries) < le16_to_cpu(eh->eh_max))
        goto has_space;

    /* probably next leaf has space for us? */
    last_ex = EXT_LAST_EXTENT(eh);
    next_lb = EXT_MAX_BLOCKS;

    if (ext_lblock(newext) > ext_lblock(last_ex)) {
        next_lb = ext_next_leaf_block(path);
    }

    /* There is a possibility to add new extent in a sibling
     * extent block */
    if (next_lb != EXT_MAX_BLOCKS) {
        BUG_ON(npath != NULL);

        npath = find_extent(ext_idx, next_lb, NULL, 0);
        if (IS_ERR(npath))
            return PTR_ERR(npath);

        BUG_ON(npath->p_depth != path->p_depth);

        eh = npath[depth].p_hdr;

        /* checks whether next a extent block is full or not */
        if (le16_to_cpu(eh->eh_entries) < le16_to_cpu(eh->eh_max)) {
            /* update path to new one since the newext will use
             * the next extent block */
            path = npath;
            goto has_space;
        }
    }

    /*
     * There is no free space in the found leaf.
     * We're gonna add a new leaf in the tree.
     */
    if (gb_flags & GET_BLOCKS_METADATA_NOFAIL)
        mb_flags |= MB_USE_RESERVED;

    /* create new leaf extent block and update path */
    err = ext_create_new_leaf(ext_idx, mb_flags, gb_flags,
            ppath, newext);

    if (err) goto cleanup;

    depth = ext_tree_depth(ext_idx);
    eh = path[depth].p_hdr;

has_space:
    /* At this point, it is guaranteed that there exists a room
     * for adding the new extent */

    /* nearex is a pointer in a extent block and will be updated
     * to the proper location */
    nearex = path[depth].p_ext;

    if (!nearex) {
        /* there is no extent in this leaf (e.g., extent block is newly created),
         * create first one */
        nearex = EXT_FIRST_EXTENT(eh);
    } else {
        if (ext_lblock(newext) > ext_lblock(nearex)) {
            nearex++;
        } 

        len = EXT_LAST_EXTENT(eh) - nearex + 1;

        if (len > 0) {
            pmem_memmove_persist(nearex + 1, nearex, len * sizeof(extent_leaf_t));
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                    len * sizeof(extent_leaf_t));
            }
        }
    }

    le16_add_cpu(&eh->eh_entries, 1);
    path[depth].p_ext = nearex;
    nvm_persist_struct(eh->eh_entries);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(eh->eh_entries));
    }

    /* nearex points to the location in a extent block.
     * update nearex with newext information */
    //nearex->ee_block = ext_lblock(newext);
    ext_set_lblock(nearex, ext_lblock(newext));
    ext_store_pblock(nearex, ext_pblock(newext));
    nearex->ee_len = newext->ee_len;
    nvm_persist_struct_ptr(nearex);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*nearex));
    }

merge:
    /* try to merge extents */
    if (!(gb_flags & GET_BLOCKS_PRE_IO)) {
        ext_try_to_merge(ext_idx, path, nearex);
    }

    /* time to correct all indexes above */
    err = ext_correct_indexes(ext_idx, path);
    if (err) goto cleanup;

    err = ext_dirty(ext_idx, path + path->p_depth);

cleanup:
    if (npath) {
        ext_drop_refs(ext_idx, npath);
        FREE(ext_idx, npath);
    }

    return err;
}

/* FIXME!! we need to try to merge to left or right after zero-out  */
static int ext_zeroout(idx_struct_t *ext_idx, extent_leaf_t *ex)
{
    paddr_t ee_pblock;
    unsigned int ee_len;
    int ret;

    ee_len = ext_get_real_len(ex);
    ee_pblock = ext_pblock(ex);

    ret = 0;

    return ret;
}

static int remove_blocks(idx_struct_t *ext_idx,
        extent_leaf_t *ex, unsigned long from, unsigned long to)
{
    //struct buffer_head *bh;
    int i;

    if (from >= le32_to_cpu(ex->ee_block) &&
            to == le32_to_cpu(ex->ee_block) + ext_get_real_len(ex) - 1) {
        /* tail removal */
        unsigned long num, start;
        num = le32_to_cpu(ex->ee_block) + ext_get_real_len(ex) - from;
        start = ext_pblock(ex) + ext_get_real_len(ex) - num;
        //free_blocks(ext_idx, NULL, start, num, 0);
        (void)CB(ext_idx, cb_dealloc_data, num, start);
    }

    return 0;
}

/*
 * routine removes index from the index block
 * it's used in truncate case only. thus all requests are for
 * last index in the block only
 */
int ext_rm_idx(idx_struct_t *ext_idx, extent_path_t *path, int depth)
{
    int err;
    paddr_t leaf;

    path--;
    path = path + depth;
    leaf = idx_pblock(path->p_idx);

    // index block is corrupted.
    if (path->p_idx != EXT_LAST_INDEX(path->p_hdr)) {
        int len = EXT_LAST_INDEX(path->p_hdr) - path->p_idx;
        len *= sizeof(extent_branch_t);
        pmem_memmove_persist(path->p_idx, path->p_idx + 1, len);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, len);
        }
    }

    le16_add_cpu(&path->p_hdr->eh_entries, -1);
    nvm_persist_struct(path->p_hdr->eh_entries);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, 
                            sizeof(path->p_hdr->eh_entries));
    }

    if ((err = ext_dirty(ext_idx, path)))
        return err;

    //free_blocks(ext_idx, NULL, leaf, 1, 0);
    CB(ext_idx, cb_dealloc_metadata, 1, leaf);

    while (--depth >= 0) {
        if (path->p_idx != EXT_FIRST_INDEX(path->p_hdr))
            break;

        // if current path pointer is root, then escape this loop.
        if (path->p_hdr == ext_header(ext_idx))
            break;

        path--;

        assert(path->p_idx);
        assert((path+1)->p_idx);

        path->p_idx->ei_block = (path+1)->p_idx->ei_block;
        err = ext_dirty(ext_idx, path);
        nvm_persist_struct_ptr(path->p_idx);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*path->p_idx));
        }
        if (err)
            break;
    }

    return err;
}

static int ext_rm_leaf(idx_struct_t *ext_idx, extent_path_t *path,
                       laddr_t start, laddr_t end)
{
    int err = 0, correct_index = 0;
    int depth = ext_tree_depth(ext_idx);
    extent_header_t *eh;
    laddr_t a, b, block;
    unsigned int num, unwritten = 0;
    laddr_t ex_ee_block;
    unsigned short ex_ee_len;
    extent_leaf_t *ex;

    /* the header must be checked already in ext_remove_space() */
    if (!path[depth].p_hdr) {
        path[depth].p_hdr = ext_header_from_block(path[depth].p_raw);
    }

    eh = path[depth].p_hdr;
    BUG_ON(eh == NULL);

    /* Start removing extent from the end. ex is pointer for
     * extents in the given extent block */
    ex = EXT_LAST_EXTENT(eh);

    ex_ee_block = le32_to_cpu(ex->ee_block);
    ex_ee_len = ext_get_real_len(ex);

    while (ex >= EXT_FIRST_EXTENT(eh) &&
            ex_ee_block + ex_ee_len > start) {

        if (ext_is_unwritten(ex))
            unwritten = 1;
        else
            unwritten = 0;

        path[depth].p_ext = ex;

        /* a is starting logical address of current extent and
         * b is ending logical address to remove */
        a = ex_ee_block > start ? ex_ee_block : start;
        b = ex_ee_block+ex_ee_len - 1 < end ?
            ex_ee_block+ex_ee_len - 1 : end;

        /* If this extent is beyond the end of the hole, skip it */
        if (end < ex_ee_block) {
            ex--;
            ex_ee_block = le32_to_cpu(ex->ee_block);
            ex_ee_len = ext_get_real_len(ex);
            continue;
        } else if (b != ex_ee_block + ex_ee_len - 1) {
            /*
            ERROR_INODE(inode,
                     "can not handle truncate %u:%u "
                     "on extent %u:%u",
                     start, end, ex_ee_block,
                     ex_ee_block + ex_ee_len - 1);
             */
            err = -EFSCORRUPTED;
            goto out;
        } else if (a != ex_ee_block) {
            /* remove tail of the extent */
            num = a - ex_ee_block;
        } else {
            /* remove whole extent: excellent! */
            num = 0;
        }

        if (ex == EXT_FIRST_EXTENT(eh))
            correct_index = 1;

        err = remove_blocks(ext_idx, ex, a, b);
        if (err)
            goto out;

        /* this extent is removed entirely mark slot unused */
        if (num == 0)
            ext_store_pblock(ex, 0);

        ex->ee_len = cpu_to_le16(num);

        /*
         * Do not mark unwritten if all the blocks in the
         * extent have been removed.
         */
        if (unwritten && num)
            ext_mark_unwritten(ex);

        nvm_persist_struct_ptr(ex);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex));
        }
        /*
         * If the extent was completely released,
         * we need to remove it from the leaf
         */
        if (num == 0) {
            if (end != EXT_MAX_BLOCKS - 1) {
                /*
                 * For hole punching, we need to scoot all the
                 * extents up when an extent is removed so that
                 * we dont have blank extents in the middle
                 */
                pmem_memmove_persist(ex, ex+1, (EXT_LAST_EXTENT(eh) - ex) *
                    sizeof(extent_leaf_t));
                
                /* Now get rid of the one at the end */
                pmem_memset_persist(EXT_LAST_EXTENT(eh), 0,
                    sizeof(extent_leaf_t));

                if (g_do_stats) {
                    INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                    (EXT_LAST_EXTENT(eh) - ex + 1) *
                    sizeof(extent_leaf_t) );
                }
            }
            le16_add_cpu(&eh->eh_entries, -1);
            nvm_persist_struct(eh->eh_entries);
            if (g_do_stats) {
                INCR_NR_CACHELINE(&estats, ncachelines_written, 
                                  sizeof(eh->eh_entries));
            }
        }

        err = ext_dirty(ext_idx, path + depth);
        if (err)
            goto out;

        ex--;
        ex_ee_block = le32_to_cpu(ex->ee_block);
        ex_ee_len = ext_get_real_len(ex);
    }

    if (correct_index && eh->eh_entries)
        err = ext_correct_indexes(ext_idx, path);

    /* if this leaf is free, then we should
     * remove it from index block above */
    if (err == 0 && eh->eh_entries == 0 && path[depth].p_raw != NULL) {
        err = ext_rm_idx(ext_idx, path, depth);
    }

out:
    return err;
}

/*
 * split_extent_at() splits an extent at given block.
 *
 * @handle: the journal handle
 * @inode: the file inode
 * @path: the path to the extent
 * @split: the logical block where the extent is splitted.
 * @split_flags: indicates if the extent could be zeroout if split fails, and
 *         the states(init or unwritten) of new extents.
 * @flags: flags used to insert new extent to extent tree.
 *
 *
 * Splits extent [a, b] into two extents [a, @split) and [@split, b], states
 * of which are deterimined by split_flag.
 *
 * There are two cases:
 *  a> the extent are splitted into two extent.
 *  b> split is not needed, and just mark the extent.
 *
 * return 0 on success.
 */
static int split_extent_at(idx_struct_t *ext_idx,
                                extent_path_t **ppath, laddr_t split,
                                int split_flag, int flags)
{
    extent_path_t *path = *ppath;
    paddr_t newblock;
    laddr_t ee_block;
    extent_leaf_t *ex, newex, orig_ex, zero_ex;
    extent_leaf_t *ex2 = NULL;
    unsigned int ee_len, depth;
    int err = 0;

    //ext_show_leaf(inode, path);

    depth = ext_tree_depth(ext_idx);
    ex = path[depth].p_ext;
    ee_block = le32_to_cpu(ex->ee_block);
    ee_len = ext_get_real_len(ex);
    newblock = split - ee_block + ext_pblock(ex);

    BUG_ON(split < ee_block || split >= (ee_block + ee_len));

    if (split == ee_block) {
        /*
         * case b: block @split is the block that the extent begins with
         * then we just change the state of the extent, and splitting
         * is not needed.
         */
        if (split_flag & EXT_MARK_UNWRIT2)
            ext_mark_unwritten(ex);
        else
            ext_mark_as_init(ex);

        if (!(flags & GET_BLOCKS_PRE_IO))
            ext_try_to_merge(ext_idx, path, ex);

        err = ext_dirty(ext_idx, path + path->p_depth);
        goto out;
    }

    /* case a */
    pmem_memcpy_persist(&orig_ex, ex, sizeof(orig_ex));
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(orig_ex));
    }
    ex->ee_len = cpu_to_le16(split - ee_block);

    if (split_flag & EXT_MARK_UNWRIT1)
        ext_mark_unwritten(ex);

    nvm_persist_struct_ptr(ex);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex));
    }

    /*
     * path may lead to new leaf, not to original leaf any more
     * after ext_insert_extent() returns,
     */
    err = ext_dirty(ext_idx, path + depth);
    if (err)
        goto fix_extent_len;

    ex2 = &newex;
    ex2->ee_block = cpu_to_le32(split);
    ex2->ee_len = cpu_to_le16(ee_len - (split - ee_block));
    ext_store_pblock(ex2, newblock);
    if (split_flag & EXT_MARK_UNWRIT2)
        ext_mark_unwritten(ex2);

    nvm_persist_struct_ptr(ex2);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex2));
    }

    err = ext_insert_extent(ext_idx, ppath, &newex, flags);

    if (err == -ENOSPC && (EXT_MAY_ZEROOUT & split_flag)) {
        if (split_flag & (EXT_DATA_VALID1 | EXT_DATA_VALID2)) {
            if (split_flag & EXT_DATA_VALID1) {
                err = ext_zeroout(ext_idx, ex2);
                zero_ex.ee_block = ex2->ee_block;
                zero_ex.ee_len = cpu_to_le16(ext_get_real_len(ex2));
                ext_store_pblock(&zero_ex, ext_pblock(ex2));
            } else {
                err = ext_zeroout(ext_idx, ex);
                zero_ex.ee_block = ex->ee_block;
                zero_ex.ee_len = cpu_to_le16(ext_get_real_len(ex));
                ext_store_pblock(&zero_ex, ext_pblock(ex));
            }
        } else {
            err = ext_zeroout(ext_idx, &orig_ex);
            zero_ex.ee_block = orig_ex.ee_block;
            zero_ex.ee_len = cpu_to_le16(ext_get_real_len(&orig_ex));
            ext_store_pblock(&zero_ex, ext_pblock(&orig_ex));
        }

        nvm_persist_struct(zero_ex);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(zero_ex));
        }

        if (err)
            goto fix_extent_len;
        /* update the extent length and mark as initialized */
        ex->ee_len = cpu_to_le16(ee_len);
        ext_try_to_merge(ext_idx, path, ex);
        err = ext_dirty(ext_idx, path + path->p_depth);
        nvm_persist_struct_ptr(ex);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex));
        }
        if (err)
            goto fix_extent_len;

        goto out;
    } else if (err)
        goto fix_extent_len;

out:
    //ext_show_leaf(inode, path);
    return err;

fix_extent_len:
    ex->ee_len = orig_ex.ee_len;
    ext_dirty(ext_idx, path + path->p_depth);
    nvm_persist_struct_ptr(ex);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ex));
    }
    return err;
}

/*
 * returns 1 if current index have to be freed (even partial)
 */
static int inline ext_more_to_rm(extent_path_t *path)
{
    if (path->p_idx < EXT_FIRST_INDEX(path->p_hdr)) {
        return 0;
    }

    /*
     * if truncate on deeper level happened it it wasn't partial
     * so we have to consider current index for truncation
     */
    if (le16_to_cpu(path->p_hdr->eh_entries) == path->p_block) {
        return 0;
    }

    return 1;
}

int ext_remove_space(idx_struct_t *ext_idx, laddr_t start, laddr_t end)
{
    EXTMETA(ext_idx, ext_meta);
    EXTHDR(ext_meta, ext_hdr);
    int depth = ext_tree_depth(ext_idx);
    extent_path_t *path = ext_meta->path;
    int i = 0, err = 0;

    /*
     * Check if we are removing extents inside the extent tree. If that
     * is the case, we are going to punch a hole inside the extent tree.
     * We have to check whether we need to split the extent covering
     * the last block to remove. We can easily remove the part of it
     * in ext_rm_leaf().
     */
    if (end < EXT_MAX_BLOCKS - 1) {
        extent_leaf_t *ex;
        laddr_t ee_block, ex_end, lblk;
        paddr_t pblk;

        /* find extent for or closest extent to this block */
        path = find_extent(ext_idx, end, &path, EX_NOCACHE);
        if (IS_ERR(path)) return PTR_ERR(path);

        //ext_show_path(inode, path);

        depth = ext_tree_depth(ext_idx);
        /* Leaf node may not exist only if inode has no blocks at all */
        ex = path[depth].p_ext;
        if (!ex) {
            if (depth) {
                //ERROR_INODE(inode, "path[%d].p_hdr == NULL", depth);
                err = -EFSCORRUPTED;
            }
            goto out;
        }

        ee_block = le32_to_cpu(ex->ee_block);

        // inode has zero-length.
        if (ee_block + ext_get_real_len(ex) == 0) {
            goto out;
        }

        ex_end = ee_block + ext_get_real_len(ex) - 1;

        /*
         * See if the last block is inside the extent, if so split
         * the extent at 'end' block so we can easily remove the
         * tail of the first part of the split extent in
         * ext_rm_leaf().
         */
        if (end >= ee_block && end < ex_end) {
            /*
             * Split the extent in two so that 'end' is the last
             * block in the first new extent. Also we should not
             * fail removing space due to ENOSPC so try to use
             * reserved block if that happens.
             */
            err = force_split_extent_at(ext_idx, &path, end + 1, 1);
            if (err < 0) goto out;

        } else if (end >= ex_end) {
            /*
             * If there's an extent to the right its first cluster
             * contains the immediate right boundary of the
             * truncated/punched region. The end < ee_block case
             * is handled in ext_rm_leaf().
             */
            lblk = ex_end + 1;
            err = ext_search_right(ext_idx, path, &lblk, &pblk, &ex);
            if (err)
                goto out;
        }
    }

    /*
     * we start scanning from right side freeing all the blocks
     * after i_size and walking into the deep
     */
    depth = ext_tree_depth(ext_idx);
    if (path) {
        int k = i = depth;
        // ?
        while (--k > 0) {
            path[k].p_block = le16_to_cpu(path[k].p_hdr->eh_entries)+1;
        }
            
    } else {
        path = (extent_path_t *)ZALLOC(ext_idx,
                sizeof(extent_path_t) * (depth + 1));

        if (path == NULL)
            return -ENOMEM;

        path[0].p_maxdepth = path[0].p_depth = depth;
        path[0].p_hdr = ext_header(ext_idx);
        i = 0;

        if (ext_check_inode(ext_idx)) {
            err = -EFSCORRUPTED;
            goto out;
        }
    }

    err = 0;

    while (i >= 0 && err == 0) {
        if (i == depth) {
            /* this is leaf block */
            err = ext_rm_leaf(ext_idx, path, start, end);
            /* root level have p_bh == NULL, fs_brelse() can handle this. */
            //fs_brelse(path[i].p_bh);
            //FREE(ext_idx, path[i].p_raw);
            path[i].p_raw = NULL;
            i--;
            continue;
        }

        /* this is index block */
        if (!path[i].p_hdr) {
            path[i].p_hdr = ext_header_from_block(path[i].p_raw);
        }

        if (!path[i].p_idx) {
            /* this level hasn't touched yet */
            path[i].p_idx = EXT_LAST_INDEX(path[i].p_hdr);
            path[i].p_block = le16_to_cpu(path[i].p_hdr->eh_entries) + 1;
            //lsm_debug("init index ptr: hdr 0x%p, num %d\n",
            //      path[i].p_hdr, le16_to_cpu(path[i].p_hdr->eh_entries));
        } else {
            /* we've already was here, see at next index */
            path[i].p_idx--;
        }

        if (ext_more_to_rm(path + i)) {
            char *buf;
            /* go to the next level */
            memset(path + i + 1, 0, sizeof(*path));

            //buf = ext_meta->et_buffers[path[0].p_depth - (i + 1)];
            int rerr = read_extent_tree_block(ext_idx, &buf,
                    idx_pblock(path[i].p_idx), path[0].p_depth - (i + 1));
            if (rerr) {
                /* should we reset i_size? */
                err = -EIO;
                break;
            }
            path[i + 1].p_raw = buf;
            path[i + 1].p_pblk = idx_pblock(path[i].p_idx);


            /* put actual number of indexes to know is this
             * number got changed at the next iteration */
            path[i].p_block = le16_to_cpu(path[i].p_hdr->eh_entries);
            i++;
        } else {
            /* we finish processing this index, go up */
            if (path[i].p_hdr->eh_entries == 0 && i > 0) {
                /* index is empty, remove it
                 * handle must be already prepared by the
                 * truncatei_leaf() */
                err = ext_rm_idx(ext_idx, path, i);
            }
            /* root level have p_bh == NULL, fs_brelse() eats this */
            /*
            fs_brelse(path[i].p_bh);
            path[i].p_bh = NULL;
            */
            //FREE(ext_idx, path[i].p_raw);
            path[i].p_raw = NULL;
            i--;
        }
    }

    /* TODO: flexible tree reduction should be here */
    if (path->p_hdr->eh_entries == 0) {
        /*
         * truncate to zero freed all the tree
         * so, we need to correct eh_depth
         */
        ext_header(ext_idx)->eh_depth = 0;
        ext_header(ext_idx)->eh_max = cpu_to_le16(
                ext_space_root(ext_idx));
        err = ext_dirty(ext_idx, path);
        nvm_persist_struct_ptr(ext_hdr);
        if (g_do_stats) {
            INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(*ext_hdr));
        }
    }
out:
    if (path) {
        ext_drop_refs(ext_idx, path);
        //FREE(ext_idx, path);
    }

    return err;
}

static int ext_convert_to_initialized(idx_struct_t *ext_idx,
                                    extent_path_t **ppath,
                                    laddr_t split,
                                    size_t blocks,
                                    int flags)
{
    int depth = ext_tree_depth(ext_idx), err;
    extent_leaf_t *ex = (*ppath)[depth].p_ext;

    assert(le32_to_cpu(ex->ee_block) <= split);

    if (split + blocks ==
            le32_to_cpu(ex->ee_block) + ext_get_real_len(ex)) {
        /* split and initialize right part */
        err = split_extent_at(ext_idx, ppath, split,
                EXT_MARK_UNWRIT1, flags);

    } else if (le32_to_cpu(ex->ee_block) == split) {
        /* split and initialize left part */
        err = split_extent_at(ext_idx, ppath, split + blocks,
                EXT_MARK_UNWRIT2, flags);

    } else {
        /* split 1 extent to 3 and initialize the 2nd */
        err = split_extent_at(ext_idx, ppath, split + blocks,
                EXT_MARK_UNWRIT1 | EXT_MARK_UNWRIT2, flags);
        if (0 == err) {
            err = split_extent_at(ext_idx, ppath, split,
                    EXT_MARK_UNWRIT1, flags);
        }
    }

    return err;
}

/*
 * ext_determine_hole - determine hole around given block
 * @inode:    inode we lookup in
 * @path:    path in extent tree to @lblk
 * @lblk:    pointer to logical block around which we want to determine hole
 *
 * Determine hole length (and start if easily possible) around given logical
 * block. We don't try too hard to find the beginning of the hole but @path
 * actually points to extent before @lblk, we provide it.
 *
 * The function returns the length of a hole starting at @lblk. We update @lblk
 * to the beginning of the hole if we managed to find it.
 */
static laddr_t ext_determine_hole(idx_struct_t *ext_idx,
                    extent_path_t *path, laddr_t *lblk)
{
    int depth = ext_tree_depth(ext_idx);
    extent_leaf_t *ex;
    laddr_t len;

    ex = path[depth].p_ext;
    if (ex == NULL) {
        /* there is no extent yet, so gap is [0;-] */
        *lblk = 0;
        len = EXT_MAX_BLOCKS;
    } else if (*lblk < le32_to_cpu(ex->ee_block)) {
        len = le32_to_cpu(ex->ee_block) - *lblk;
    } else if (*lblk >= le32_to_cpu(ex->ee_block)
            + ext_get_real_len(ex)) {
        laddr_t next;

        *lblk = le32_to_cpu(ex->ee_block) + ext_get_real_len(ex);
        next = ext_next_allocated_block(path);
        BUG_ON(next == *lblk);
        len = next - *lblk;
    } else {
        BUG_ON(1);
    }
    return len;
}

int ext_truncate(idx_struct_t *ext_idx, laddr_t start, laddr_t end)
{
    return ext_remove_space(ext_idx, start, end);
}

/******************************************************************************
 * API Section
 *****************************************************************************/

ssize_t extent_tree_create(idx_struct_t *ext_idx, inum_t inum,
                           laddr_t laddr, size_t size, paddr_t *new_paddr)
{
    extent_path_t *path = NULL;
    extent_leaf_t newex, *ex;
    int goal, err = 0, depth;
    laddr_t allocated = 0;
    paddr_t next, newblock;
    int create;
    EXTMETA(ext_idx, ext_meta);

    if (NULL == ext_idx) return -EINVAL;
    size = size > UINT16_MAX ? UINT16_MAX : size;

    int read_ret = read_ext_direct_data(ext_idx);
    if (read_ret) return read_ret;

    write_lock(ext_idx);

    /* find extent for this block */
    path = ext_meta->path;
    path = find_extent(ext_idx, laddr, &(path), 0);
    if (IS_ERR(path)) {
        err = PTR_ERR(path);
        path = NULL;
        goto out2;
    }

    depth = ext_tree_depth(ext_idx);

    /*
     * consistent leaf must not be empty
     * this situations is possible, though, _during_ tree modification
     * this is why assert can't be put in ext_find_extent()
     */
    BUG_ON(path[depth].p_ext == NULL && depth != 0);

    ex = path[depth].p_ext;
    if (ex) {
        laddr_t ee_block = le32_to_cpu(ex->ee_block);
        paddr_t ee_start = ext_pblock(ex);
        unsigned short ee_len;

        /*
         * unwritten extents are treated as holes, except that
         * we split out initialized portions during a write.
         */
        ee_len = ext_get_real_len(ex);

        /* find extent covers block. simply return the extent */
        if (in_range(laddr, ee_block, ee_len)) {
            /* number of remain blocks in the extent */
            allocated = ee_len + ee_block - laddr;

            if (ext_is_unwritten(ex)) {
                newblock = laddr - ee_block + ee_start;
                err = ext_convert_to_initialized(ext_idx,
                        &path, laddr, allocated, 0);
                if (err) {
                    goto out2;
                }
                goto out;
            } else {
                newblock = laddr - ee_block + ee_start;
                goto out;
            }
        }
    }

    /* find next allocated block so that we know how many
     * blocks we can allocate without ovelapping next extent */
    next = ext_next_allocated_block(path);
    BUG_ON(next <= laddr);

    allocated = next - laddr;

    /*
    if ((flags & GET_BLOCKS_PRE_IO) && map->m_len > EXT_UNWRITTEN_MAX_LEN) {
        map->m_len = EXT_UNWRITTEN_MAX_LEN;
    }
    */

    if (allocated > size)
        allocated = size;

    ssize_t nalloc = CB(ext_idx, cb_alloc_data, allocated, &newblock);

    if (!newblock || nalloc < 0) {
        goto out2;
    }

    allocated = nalloc;

    /* try to insert new extent into found leaf and return */
    newex.ee_block = cpu_to_le32(laddr);
    ext_store_pblock(&newex, newblock);
    newex.ee_len = cpu_to_le16(allocated);

    /* if it's fallocate, mark ex as unwritten */
    /*
    if (flags & GET_BLOCKS_PRE_IO) {
        ext_mark_unwritten(&newex);
    }
    */

    err = ext_insert_extent(ext_idx, &path, &newex, 0);
                                 //flags & GET_BLOCKS_PRE_IO);
    nvm_persist_struct(newex);
    if (g_do_stats) {
        INCR_NR_CACHELINE(&estats, ncachelines_written, sizeof(newex));
    }

    if (err) {
        /* free data blocks we just allocated */
        ssize_t nfreed = CB(ext_idx, cb_dealloc_data, le16_to_cpu(newex.ee_len),
                            ext_pblock(&newex));
        goto out2;
    }

    /* previous routine could use block we allocated */
    if (ext_is_unwritten(&newex)) {
        newblock = 0;
    } else {
        newblock = ext_pblock(&newex);
    }

out:
    if (allocated > size) {
        allocated = size;
    }

    *new_paddr = newblock;

out2:
    if (path) {
        /* write back tree changes (internal/leaf nodes) */
        ext_drop_refs(ext_idx, path);
        //FREE(ext_idx, path);
    }

    if (g_do_stats) {
        INCR_STAT(&estats, nwrites);
        ADD_STAT(&estats, nblocks_inserted, allocated);
        ADD_STAT(&estats, depth_total, depth);
        INCR_STAT(&estats, depth_nr);
    }

    write_unlock(ext_idx);

    return err ? err : allocated;
}

static pthread_mutex_t lmutex = PTHREAD_MUTEX_INITIALIZER;
ssize_t extent_tree_lookup(idx_struct_t *ext_idx, inum_t inum,
                           laddr_t laddr, size_t max, paddr_t* paddr)
{
    extent_path_t *path = NULL;
    extent_leaf_t newex, *ex;
    int goal, err = 0, depth;
    paddr_t next, newblock;
    ssize_t ret;

    *paddr = 0;
    ret = 0;


    int read_ret = read_ext_direct_data(ext_idx);
    if (read_ret) return read_ret;

    EXTMETA(ext_idx, ext_meta);
    read_lock(ext_idx);

    static __thread extent_path_t path_arr[MAX_DEPTH]; // concurrency things
    #ifdef EXT_MEMOIZATION
    depth = ext_tree_depth(ext_idx);
    ret = search_extent_leaf(ext_meta->prev_path[depth].p_ext, laddr, paddr);
    if (ret > 0) {
        read_unlock(ext_idx);
        return ret;
    }
    #endif

    /* find extent for this block */
    extent_path_t *pathp = &path_arr[0];
    path = find_extent(ext_idx, laddr, &(pathp), 0);   
    if (IS_ERR(path)) {
        err = PTR_ERR(path);
        path = NULL;
        read_unlock(ext_idx);
        return err;
    }

    //if (path != ext_meta->path) {
    //    abort();
    //}

    depth = ext_tree_depth(ext_idx);

    /*
     * consistent leaf must not be empty
     * this situations is possible, though, _during_ tree modification
     * this is why assert can't be put in ext_find_extent()
     */
    BUG_ON(path[depth].p_ext == NULL && depth != 0);
 
    ret = search_extent_leaf(path[depth].p_ext, laddr, paddr);

#if 0
    if (!ret) {
        if (path[depth].p_ext) {
            printf("%x\n", path[depth].p_ext->ee_len);
            printf("%u not in %u--%u\n", laddr, path[depth].p_ext->ee_block, 
                    path[depth].p_ext->ee_block
                    + ((uint32_t)path[depth].p_ext->ee_len));
        }

        //while(1);
    }
#endif

    #if 1 && defined(EXT_MEMOIZATION)
    if (ret > 0) {
        (void)pthread_mutex_lock(&lmutex);
        #if 0
        extent_path_t *tmp = ext_meta->prev_path;
        ext_meta->prev_path = ext_meta->path;
        ext_meta->path = tmp;
        #elif 0
        memcpy(ext_meta->prev_path, ext_meta->path, sizeof(*ext_meta->path) * MAX_DEPTH);
        #elif 0
        extent_path_t *tmp = ext_meta->prev_path;
        ext_meta->prev_path = ext_meta->path;
        ext_meta->path = tmp;
        memset(ext_meta->path, 0, sizeof(*ext_meta->path)*MAX_DEPTH);
        #else
        memcpy(ext_meta->prev_path, path_arr, sizeof(*ext_meta->path) * MAX_DEPTH);
        #endif
        (void)pthread_mutex_unlock(&lmutex);
    }
    #endif

    if (g_do_stats) {
        ADD_STAT(&estats, depth_total, depth);
        INCR_STAT(&estats, depth_nr);
    }

    read_unlock(ext_idx);

    return ret;
}

ssize_t extent_tree_remove(idx_struct_t *ext_idx,
                           inum_t inum, laddr_t laddr, size_t size) {
    int read_ret = read_ext_direct_data(ext_idx);
    if (read_ret) return read_ret;

    EXTMETA(ext_idx, ext_meta);

    write_lock(ext_idx);
    int err = ext_truncate(ext_idx, laddr, laddr + size - 1);
    write_unlock(ext_idx);
    
    if (err) return err;
    
    return size;
}

void extent_tree_clear_metadata_cache(idx_struct_t *ext_idx) {
    EXTMETA(ext_idx, ext_meta);
    ext_meta->reread_meta = true;

    int read_ret = read_ext_direct_data(ext_idx);
    if_then_panic(read_ret, "could not reread metadata!\n"); 
}

int extent_tree_set_caching(idx_struct_t* ext_idx, bool enable) {
    EXTMETA(ext_idx, ext_meta);
    ext_meta->et_cached = enable;
    return 0;
}

int extent_tree_persist_updates(idx_struct_t* idx_struct) {
    // Traverse all nodes and write them back.
    return 0;
}

int extent_tree_invalidate_caches(idx_struct_t* idx_struct) {
    // Traverse all nodes and invalidate them.
    return 0;
}

void extent_tree_set_stats(idx_struct_t *ext_idx, bool enable) {
    EXTMETA(ext_idx, ext_meta);
    ext_meta->et_enable_stats = enable;
    g_do_stats = enable;
}

void extent_tree_print_stats(idx_struct_t *ext_idx) {
    EXTMETA(ext_idx, ext_meta);
    print_ext_stats(ext_meta->et_stats);
}

void extent_tree_print_global_stats(void) {
    print_ext_stats(&estats);
}

void extent_tree_clean_global_stats(void) {
    memset(&estats, 0, sizeof(estats));
}

void extent_tree_add_global_to_json(json_object *root) {
   js_add_int64(root, "compute_tsc", 0); 
   js_add_int64(root, "compute_nr", 0); 
}

idx_fns_t extent_tree_fns = {
    .im_init               = NULL,
    .im_init_prealloc      = extent_tree_init,
    .im_lookup             = extent_tree_lookup,
    .im_create             = extent_tree_create,
    .im_remove             = extent_tree_remove,

    .im_set_caching        = extent_tree_set_caching,
    .im_persist            = extent_tree_persist_updates,
    .im_invalidate         = extent_tree_invalidate_caches,
    .im_clear_metadata     = extent_tree_clear_metadata_cache,

    .im_set_stats          = extent_tree_set_stats,
    .im_print_stats        = extent_tree_print_stats,
    .im_print_global_stats = extent_tree_print_global_stats,
    .im_clean_global_stats = extent_tree_clean_global_stats,
    .im_add_global_to_json = extent_tree_add_global_to_json,
};
