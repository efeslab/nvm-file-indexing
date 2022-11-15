#include <stdbool.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>

#include "p-hot.h"

#include <iostream>
#include <chrono>
#include <random>
#include "tbb/tbb.h"

using namespace std;

#include <hot/rowex/HOTRowex.hpp>
#include <idx/contenthelpers/IdentityKeyExtractor.hpp>
#include <idx/contenthelpers/OptionalValue.hpp>

struct IntKeyVal {
    laddr_t key;
    paddr_t value;
    size_t nblks;
};

template<typename ValueType = IntKeyVal *>
class IntKeyExtractor {
    public:
    typedef laddr_t KeyType;

    inline KeyType operator()(ValueType const &value) const {
        return value->key;
    }
};

typedef hot::rowex::HOTRowex<IntKeyVal *, IntKeyExtractor> p_hot_meta_t;
#define p_hot(i, name) auto *(name) = (p_hot_meta_t*)((i)->idx_metadata);

int p_hot_init(const idx_spec_t *idx_spec,
               const paddr_range_t *metadata_location,
               idx_struct_t *idx_struct) 
{
    if_then_panic(!idx_spec, "idx_spec cannot be null!");
    if_then_panic(!idx_struct, "idx_struct cannot be null!");
    if_then_panic(!metadata_location, "Nowhere to put metadata!\n");

    //auto *mTrie = new hot::rowex::HOTRowex<IntKeyVal *, IntKeyExtractor>();
    auto *mTrie = new p_hot_meta_t();
    // If all of that is successful, finally set up structure.
    idx_struct->idx_metadata  = (void**)mTrie;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_fns       = &p_hot_fns;

    return 0;
}

// if not exists, then the value was not already in the table, therefore
// success.
// returns 1 on success, 0 if key already existed
ssize_t p_hot_create(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t size, paddr_t *paddr) {
    p_hot(idx_struct, trie);
    ssize_t nalloc = CB(idx_struct, cb_alloc_data, size, paddr);

    if (nalloc < 0) {
        return -EINVAL;
    } else if (nalloc == 0) {
        return -ENOMEM;
    } 

    for (size_t blkno = 0; blkno < nalloc; blkno += 1) {
        IntKeyVal *key;
        posix_memalign((void **)&key, 64, sizeof(IntKeyVal));
        key->key = (uint64_t)laddr + (uint64_t)blkno;
        key->value = ((uint64_t)*paddr) + (uint64_t)blkno;
        key->nblks = nalloc - blkno;

        if (!(trie->insert(key))) {
            fprintf(stderr, "[HOT] insert fail\n");
            exit(1);
        }
        int err = 1;

        if (!err) {
            ssize_t dealloc = CB(idx_struct, cb_dealloc_data, nalloc, *paddr);
            if_then_panic(nalloc != dealloc, "could not free data blocks!\n");
            *paddr = 0;
            return -EEXIST;
        }
       
    }

    return nalloc;
}

/*
 * Returns 0 if found, or -errno otherwise.
 */
ssize_t p_hot_lookup(idx_struct_t *idx_struct, inum_t inum,
                         laddr_t laddr, size_t max, paddr_t* paddr) {
    p_hot(idx_struct, trie);

    idx::contenthelpers::OptionalValue<IntKeyVal *> result = trie->lookup(key);
    if (!result.mIsValid) {
        printf("mIsValid = %d\n", result.mIsValid);
        exit(1);
    }

    uint64_t p = result.mValue->value;
    *paddr = p;
    size_t nblk = result.mValue->nblks;

    if (!blkno) return -ENOENT;
    return blkno;
}

/*
 * Returns FALSE if the requested logical block was not present in any of the
 * two hash tables.
 */
ssize_t p_hot_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr,
                         size_t size) {
    p_hot(idx_struct, trie);

    size_t blkno = 0;
    for (; blkno < size; ++blkno) {
        uint64_t key = (uint64_t)laddr + (uint64_t)blkno;
        idx::contenthelpers::OptionalValue<IntKeyVal *> result = trie->lookup(key);
        if (!result.mIsValid) {
            printf("mIsValid = %d\n", result.mIsValid);
            exit(1);
        }

        uint64_t p = result.mValue->value;

        ssize_t ndeleted = CB(idx_struct, cb_dealloc_data, 1, p);
        if_then_panic(ndeleted < 0, "error in dealloc!");
    }

    if (!blkno) return -ENOENT;
    return blkno;
}

int p_hot_set_caching(idx_struct_t *idx_struct, bool enable) {
    return 0;
}

int p_hot_set_locking(idx_struct_t *idx_struct, bool enable) {
    return 0;
}

int p_hot_persist_updates(idx_struct_t *idx_struct) {
    return 0; 
}

int p_hot_invalidate_caches(idx_struct_t *idx_struct) {
    return 0;
}

