#include <stdbool.h>
#include "inode_hash.h"

#if 0
#define trace_me() \
	do { \
		fprintf(stdout, "[%s.%s():%d] trace\n",  \
			    __FILE__, __func__, __LINE__); \
		fflush(stdout); \
	} while (0)
#else
#define trace_me() 0
#endif

int hashtable_initialize(const idx_spec_t *idx_spec,
                         idx_struct_t *idx_struct, paddr_t *location) {
    trace_me();
    if_then_panic(!idx_spec, "idx_spec cannot be null!");
    if_then_panic(!idx_struct, "idx_struct cannot be null!");
    if_then_panic(!location, "location ptr cannot be null!");

    nvm_hash_idx_t *ht = (nvm_hash_idx_t*)idx_struct->idx_metadata;

    if (ht) return -EEXIST;

    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_fns       = &hash_fns;

    device_info_t devinfo;
    int ret = CB(idx_struct, cb_get_dev_info, &devinfo);

    // Allocate space on device.
    if_then_panic(devinfo.di_block_size % sizeof(hash_ent_t) != 0,
                  "bad hesh_entry_t size");

    size_t entries_per_block = devinfo.di_block_size / sizeof(hash_ent_t);

    size_t nbytes = devinfo.di_size_blocks * sizeof(hash_ent_t);

    if (!*location) {
        ssize_t nalloc = CB(idx_struct, cb_alloc_metadata, 1, location);

        if_then_panic(nalloc < 1, "no room for metadata!");
    }

    ht = nvm_hash_table_new(nvm_idx_hash6432shift, devinfo.di_size_blocks,
                            devinfo.di_block_size, 1, *location, idx_spec);
    if_then_panic(ht == NULL, "could not allocate hash table");

    idx_struct->idx_metadata = (void*)ht;

    return 0;
}

// if not exists, then the value was not already in the table, therefore
// success.
// returns 1 on success, 0 if key already existed
ssize_t hashtable_create(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t size, paddr_t *paddr) {
    trace_me();
    NVMHASH(idx_struct, ht);

    ssize_t nalloc = CB(idx_struct, cb_alloc_data, size, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    }

    for (size_t blkno = 0; blkno < nalloc; ++blkno) {
        hash_key_t k = MAKEKEY(inum, laddr + blkno);
        int err = nvm_hash_table_insert(ht, k, (*paddr) + blkno, size - blkno);
        if (!err) return -EEXIST;
    }

    return nalloc;
}

/*
 * Returns 0 if found, or -errno otherwise.
 */
ssize_t hashtable_lookup(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, paddr_t* paddr) {
    trace_me();
    NVMHASH(idx_struct, ht);

    hash_key_t k = MAKEKEY(inum, laddr);
    size_t size;
    nvm_hash_table_lookup(ht, k, paddr, &size, false);
    if (*paddr != 0) return (ssize_t)size;
    return -ENOENT;
}

/*
 * Returns FALSE if the requested logical block was not present in any of the
 * two hash tables.
 * TODO need to deallocate here!
 */
ssize_t hashtable_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr,
                         size_t size) {
    trace_me();
    NVMHASH(idx_struct, ht);

    if (size == 0) return -EINVAL;

    ssize_t ret = 0;
    for (laddr_t lblk = laddr; lblk < laddr + size; ++lblk) {
        hash_key_t k = MAKEKEY(inum, lblk);
        paddr_t removed;
        size_t nblk;
        ssize_t was_removed = nvm_hash_table_remove(ht, k, &removed, &nblk);
        if_then_panic(!nblk, "size was 0 on delete!");

        if (was_removed > 0) {
            ssize_t ndeleted = CB(idx_struct, cb_dealloc_data, was_removed, removed);
            if_then_panic(ndeleted < 0, "error in dealloc!");
            if_then_panic(was_removed != (size_t)ndeleted, "could not deallocate!");
            ret += was_removed;
        } else {
            printf("Weird, but not removed! Returned %lld\n", was_removed);
        }

    }

    if (ret <= 0) return -ENOENT;

    return ret;
}

idx_fns_t hash_fns = {
    .im_init          = hashtable_initialize,
    .im_init_prealloc = NULL,
    .im_lookup        = hashtable_lookup,
    .im_create        = hashtable_create,
    .im_remove        = hashtable_remove
};
