#include "levelhash.h"

idx_fns_t levelhash_fns = {
    .im_init          = NULL,
    .im_init_prealloc = levelhash_init,
    .im_lookup        = levelhash_lookup,
    .im_create        = levelhash_create,
    .im_remove        = levelhash_remove,
    .im_set_stats     = levelhash_set_stats,
    .im_print_stats   = levelhash_print_stats
};

int levelhash_init(const idx_spec_t* idx_spec, const paddr_range_t* direct_ents, 
                   idx_struct_t* level_idx) {
    int ret = 0;

    level_hash_t *lh = level_init(idx_spec, direct_ents, 2);

    level_idx->idx_callbacks = idx_spec->idx_callbacks;
    level_idx->idx_mem_man   = idx_spec->idx_mem_man;
    level_idx->idx_fns       = &levelhash_fns;
    level_idx->idx_metadata  = (void*)lh;

    return ret;
}

ssize_t levelhash_lookup(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, paddr_t* paddr) {
    LEVELMETA(level_idx, lh);
    size_t size = level_dynamic_query(lh, laddr, paddr);

    if (0 == size) return -ENOENT;

    return (ssize_t)size;
}

ssize_t levelhash_create(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk, paddr_t* paddr) {
    LEVELMETA(level_idx, lh);

    ssize_t nalloc = CB(level_idx, cb_alloc_data, nblk, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    }

    for (size_t blk = 0; blk < nblk; ++blk) {
        uint8_t ret = level_insert(lh, laddr + blk, (*paddr) + blk, nalloc - blk);
        if (ret) {
            // Trying to resize.
            level_expand(lh);
            ret = level_insert(lh, laddr + blk, (*paddr) + blk, nalloc - blk);
            if (ret) return -EIO;
        }
    }

    int err = write_metadata(lh);
    if (err) return -EIO;

    return (ssize_t) nblk;
}

// TODO also free the data!
ssize_t levelhash_remove(idx_struct_t* level_idx, inum_t inum, 
                         laddr_t laddr, size_t nblk) {
    LEVELMETA(level_idx, lh);

    for (size_t blk = 0; blk < nblk; ++blk) {
        uint8_t ret = level_delete(lh, laddr + blk);
        if (ret) return -EIO;
    }

    level_shrink(lh);

    int err = write_metadata(lh);
    if (err) return -EIO;

    return (ssize_t) nblk;
}


void levelhash_set_stats(idx_struct_t* level_idx, bool enable) {}
void levelhash_print_stats(idx_struct_t* level_idx) {}
