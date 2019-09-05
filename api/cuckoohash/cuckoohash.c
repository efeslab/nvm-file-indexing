#include <stdbool.h>
#include "cuckoohash.h"

int cuckoohash_initialize(const idx_spec_t *idx_spec,
                         idx_struct_t *idx_struct,
                         paddr_t *location) 
{

    if_then_panic(!idx_spec, "idx_spec cannot be null!");
    if_then_panic(!idx_struct, "idx_struct cannot be null!");
    if_then_panic(!location, "location ptr cannot be null!");

    nvm_cuckoo_idx_t *ht = (nvm_cuckoo_idx_t*)idx_struct->idx_metadata;

    if (ht) return -EEXIST;

    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_fns       = &cuckoohash_fns;

    device_info_t devinfo;
    int ret = CB(idx_struct, cb_get_dev_info, &devinfo);

    // Allocate space on device.

    if (!*location) {
        ssize_t nalloc = CB(idx_struct, cb_alloc_metadata, 1, location);

        if_then_panic(nalloc < 1, "no room for metadata!");
    }

    ret = cuckoo_hash_init(&ht, *location, devinfo.di_size_blocks, idx_spec);
    if_then_panic(ret, "could not allocate hash table");

    idx_struct->idx_metadata = (void*)ht;

    return 0;
}

// if not exists, then the value was not already in the table, therefore
// success.
// returns 1 on success, 0 if key already existed
ssize_t cuckoohash_create(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t size, paddr_t *paddr) {
    CUCKOOHASH(idx_struct, ht);

    ssize_t nalloc = CB(idx_struct, cb_alloc_data, size, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    }

    for (size_t blkno = 0; blkno < nalloc; ++blkno) {
        hash_key_t k = MAKECUCKOOKEY(inum, laddr + blkno);
        // Range: how many more logical blocks are contiguous after this one?
        uint32_t range = (uint32_t)(nalloc - blkno);
        // Index: how many more logical blocks are contiguous before this one?
        uint32_t index = (uint32_t)blkno;
        int err = cuckoo_hash_insert(ht, k, (*paddr) + blkno, index, range);

        if (!err) {
            ssize_t dealloc = CB(idx_struct, cb_dealloc_data, nalloc, *paddr);
            if_then_panic(nalloc != dealloc, "could not free data blocks!\n");
            *paddr = 0;
            return -EEXIST;
        }
    }

    if (ht->do_stats) {
        INCR_STAT(&cstats, nwrites);
        ADD_STAT(&cstats, nblocks_inserted, nalloc);
    }

    return nalloc;
}

/*
 * Returns 0 if found, or -errno otherwise.
 */
ssize_t cuckoohash_lookup(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t max, paddr_t* paddr) {
    CUCKOOHASH(idx_struct, ht);

    hash_key_t k = MAKECUCKOOKEY(inum, laddr);
    uint32_t size;
    int err = cuckoo_hash_lookup(ht, k, paddr, &size);
    if (err) return err;

    if (*paddr != 0) return (ssize_t)size;

    return -ENOENT;
}

/*
 * Returns FALSE if the requested logical block was not present in any of the
 * two hash tables.
 */
ssize_t cuckoohash_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr,
                         size_t size) {
    CUCKOOHASH(idx_struct, ht);

    if (size == 0) return -EINVAL;

    ssize_t ret = 0;
    uint32_t smallest_idx = UINT32_MAX;
    laddr_t smallest_lblk;
    size_t new_size;
    laddr_t range_start;
    for (laddr_t lblk = laddr; lblk < laddr + size; ++lblk) {
        hash_key_t k = MAKECUCKOOKEY(inum, lblk);
        paddr_t removed;
        uint32_t index, range;
        int err = cuckoo_hash_remove(ht, k, &removed, &index, &range);
        if (err) return err;
        if_then_panic(!range, "size was 0 on delete!");

        if (lblk == laddr) {
            // total size of the contiguous region
            new_size = (index + range) - size;
            range_start = laddr - index;
        }

        // Maintain the smallest index. After this loop, go back and amend
        // previous entries to reduce range size.
        if (index < smallest_idx) {
            smallest_idx = index;
            smallest_lblk = lblk;
        }

        ssize_t ndeleted = CB(idx_struct, cb_dealloc_data, 1, removed);
        if_then_panic(ndeleted < 0, "error in dealloc!");
        ++ret;
    }

    if (new_size > 0) {
        for (laddr_t lblk = range_start; lblk < smallest_lblk; ++lblk) {
            hash_key_t k = MAKECUCKOOKEY(inum, lblk);
            size_t new_range = new_size - lblk;
            if_then_panic(new_range == 0, "Cannot insert with size 0!");
            int uerr = cuckoo_hash_update(ht, k, new_range);
            if_then_panic(uerr, "could not update range size!\n");
        }
    }

    if (ret <= 0) return -ENOENT;

    if (ret != size) printf("Only freed %lu of %lu blocks!\n", ret, size);

    return ret;
}

int cuckoohash_set_caching(idx_struct_t *idx_struct, bool enable) {
    return 0;
}

int cuckoohash_set_locking(idx_struct_t *idx_struct, bool enable) {
    return 0;
}

int cuckoohash_persist_updates(idx_struct_t *idx_struct) {
    return 0; 
}

int cuckoohash_invalidate_caches(idx_struct_t *idx_struct) {
    return 0;
}

void cuckoohash_set_stats(idx_struct_t *idx_struct, bool enable) {
    CUCKOOHASH(idx_struct, ht);
    ht->do_stats = enable;
}

void cuckoohash_print_stats(idx_struct_t *idx_struct) {
    cuckoohash_print_global_stats();
}

void cuckoohash_print_global_stats(void) {
    printf("CUCKOO HASH TABLE:\n");
    printf("\tInserts: %.1f blocks per op (%lu / %lu)\n",
        (float)cstats.nblocks_inserted / (float)cstats.nwrites,
        cstats.nblocks_inserted, cstats.nwrites);
    printf("\tInserts: %.1f cachelines per op (%lu / %lu)\n",
        (float)cstats.ncachelines_written / (float)cstats.nwrites, 
        cstats.ncachelines_written, cstats.nwrites);
    PFIELD(&cstats, compute_hash);
}

void cuckoohash_clean_global_stats(void) {
    memset(&cstats, 0, sizeof(cstats));
}

idx_fns_t cuckoohash_fns = {
    .im_init               = cuckoohash_initialize,
    .im_init_prealloc      = NULL,
    .im_lookup             = cuckoohash_lookup,
    .im_create             = cuckoohash_create,
    .im_remove             = cuckoohash_remove,

    .im_set_caching        = cuckoohash_set_caching,
    .im_set_locking        = cuckoohash_set_locking,
    .im_persist            = cuckoohash_persist_updates,
    .im_invalidate         = cuckoohash_invalidate_caches,

    .im_set_stats          = cuckoohash_set_stats,
    .im_print_stats        = cuckoohash_print_stats,
    .im_print_global_stats = cuckoohash_print_global_stats,
    .im_clean_global_stats = cuckoohash_clean_global_stats
};
