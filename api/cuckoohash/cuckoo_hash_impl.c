/*
  Copyright (C) 2010 Tomash Brechko.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "cuckoo_hash_impl.h"

nvm_cuckoo_stats_t cstats = {0,}; 

static inline
void
compute_hash(paddr_t key, uint64_t *h1, uint64_t *h2, bool compact_idx)
{
#if 0
    extern void hashlittle2(const void *key, size_t length,
                            uint32_t *pc, uint32_t *pb);

    /* Initial values are arbitrary.  */
    *h1 = 0x3ac5d673;
    *h2 = 0x6d7839d0;
    // TODO: might be inefficient
    hashlittle2(&key, sizeof(key), h1, h2);
#elif 0
    static __thread paddr_t last_key = 0;
    static __thread uint32_t last_h1, last_h2; 
    if (compact_idx) {
        *h1 = key == last_key ? last_h1 : (uint32_t)(key);
        *h2 = key == last_key ? last_h2 : (uint32_t)(key + 1);
    } else {
        *h1 = key == last_key ? last_h1 : mix(key);
        *h2 = key == last_key ? last_h2 : mix(~key);
    }
    last_h1 = *h1;
    last_h2 = *h2;
    last_key = key;
#elif 1
	//*h1 = mix(key);
	//*h2 = mix(~key);
	*h1 = hash64_64(key);
	*h2 = hash64_64(~key);
#endif
    if (*h1 == *h2) {
        *h2 = ~*h2;
    }
}


int
cuckoo_hash_init(nvm_cuckoo_idx_t **ht, paddr_t meta_block, 
                 size_t max_entries, const idx_spec_t *idx_spec)
{
    *ht = NULL;
    nvm_cuckoo_idx_t *hash = ZALLOC(idx_spec, sizeof(*hash));
    if (!hash) return -ENOMEM;

    // Read metadata to see if it exists or not.
    ssize_t err = CB(idx_spec, cb_get_addr, meta_block, 0, (char**)&(hash->meta));
    if (err) return err;

    // Avoid conflicts
    max_entries *= 2.5;

    if (hash->meta->magic != CUCKOO_MAGIC) {
        device_info_t devinfo;
        int ret = CB(idx_spec, cb_get_dev_info, &devinfo);
        if_then_panic(ret, "Could not get device info!");

        hash->meta->max_size = max_entries;
        hash->meta->magic = CUCKOO_MAGIC;

        // Allocate the element array.
        size_t nbytes = max_entries * sizeof(*hash->table);
        size_t nblk = nbytes / devinfo.di_block_size;
        if (nbytes % devinfo.di_block_size) ++nblk;

        ssize_t nalloc = CB(idx_spec, cb_alloc_metadata, nblk, 
                                      &(hash->meta->elem_start_blk));
        if_then_panic(nalloc < nblk, "no large contiguous region!");

        // Now write back the metadata.
        nvm_persist_struct_ptr(hash->meta);
    }

    pmem_memset_persist(&hash->meta->rwlock, 0, sizeof(hash->meta->rwlock));

    // See if we do compact mode:
    const char* compact_str = getenv("IDX_COMPACT");
    if (compact_str && !strcmp(compact_str, "1")) {
        hash->compact_idx = true;
    } else {
        hash->compact_idx = false;
    }

    // Always have to retrieve the pointer.
    ssize_t perr = CB(idx_spec, cb_get_addr, 
                      hash->meta->elem_start_blk, 0, (char**)&(hash->table));
    if (perr) return perr;

    *ht = hash;
    
    return 0;
}

void
cuckoo_hash_destroy(const struct cuckoo_hash *hash)
{
    if_then_panic(true, "Can't be in here!\n");
    free(hash->table);
}

#if 0
static inline
struct cuckoo_hash_elem *
bin_at(const struct cuckoo_hash *hash, uint32_t index)
{
    return hash->table + index;
}
#else
#define bin_at(hash, index) (hash)->table + (index)
#endif

static inline
struct cuckoo_hash_elem *
lookup(const struct cuckoo_hash *hash, paddr_t key, uint64_t h1, uint64_t h2)
{
    uint64_t mod = (uint64_t)hash->meta->max_size;

    struct cuckoo_hash_elem *elem1 = NULL, *elem2 = NULL;

    elem1 = bin_at(hash, (h1 % mod));

    //if (elem->hash2 == h2 && elem->hash1 == h1
    if (elem1->key == key) {
        cstats.nbuckets_checked += 1;
        return elem1;
    }

    elem2 = bin_at(hash, (h2 % mod));
    //if (elem->hash2 == h1 && elem->hash1 == h2
    if (elem2->key == key) {
        cstats.nbuckets_checked += 2;
        return elem2;
    }

    return NULL;
}

static inline
void
compute_hash_simd64(u512i_64_t *keys1, u512i_64_t *keys2,
                    u512i_64_t *h1s, u512i_64_t *h2s) 
{
#if 0
    uint32_t mod = hash->meta->max_size;
    nvm_mixHash_simd64(mod, keys1, h1s);
    nvm_mixHash_simd64(mod, keys2, h2s);
#else
    hash64_simd64(keys1, h1s);
    hash64_simd64(keys2, h2s);
#endif
}


static inline
void
bin_at_parallel(const struct cuckoo_hash *hash, u256i_32_t *indices, u512i_64_t *elems)
{
    static u512i_64_t sz = {.arr = {
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
    }};
    elems->vec = _mm512_add_epi64(_mm512_set1_epi64((uint64_t)hash->table), 
            _mm512_mullo_epi64(_mm512_cvtepu32_epi64(indices->vec), sz.vec));
}


static inline 
void 
bin_keys_at_parallel(const struct cuckoo_hash *hash, 
                     u256i_32_t *indices, u512i_64_t *keys) 
{
    static u512i_64_t sz = {.arr = {
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
        sizeof(cuckoo_elem_t), sizeof(cuckoo_elem_t),
    }};
    keys->vec = _mm512_i64gather_epi64(
                _mm512_mullo_epi64(_mm512_cvtepu32_epi64(indices->vec),
                                sz.vec), hash->table, 1);
}

static 
void
lookup_parallel(const struct cuckoo_hash *hash, u512i_64_t *keys,
                u512i_64_t *h1, u512i_64_t *h2, u512i_64_t *ret)
{
    uint64_t mod = (uint64_t)hash->meta->max_size;

    u256i_32_t idx1, idx2;

    mod_simd32(mod, h1, &idx1);
    mod_simd32(mod, h2, &idx2);

    u512i_64_t elem1, elem2, keys1, keys2;

    bin_at_parallel(hash, &idx1, &elem1);
    bin_at_parallel(hash, &idx2, &elem2);

    bin_keys_at_parallel(hash, &idx1, &keys1); 
    bin_keys_at_parallel(hash, &idx2, &keys2); 

    __mmask8 m1, m2;

    m1 = _mm512_cmp_epi64_mask(keys->vec, keys1.vec, 0);
    m2 = _mm512_cmp_epi64_mask(keys->vec, keys2.vec, 0);

    ret->vec = _mm512_mask_mov_epi64(ret->vec, m1, elem1.vec);
    ret->vec = _mm512_mask_mov_epi64(ret->vec, m2, elem2.vec);
}

int
cuckoo_hash_lookup_parallel(const struct cuckoo_hash *hash,
                            paddr_t key, paddr_t *values, size_t nr)
{
    DECLARE_TIMING();

    static u512i_64_t offsets = { .arr = {0, 1, 2, 3, 4, 5, 6, 7}};
    u512i_64_t keys_og, keys1, keys2;

    keys1.vec = _mm512_add_epi64(offsets.vec, _mm512_set1_epi64(key));
    keys2.vec = _mm512_xor_epi64(_mm512_set1_epi64(~0), 
                    _mm512_add_epi64(_mm512_set1_epi64(key), offsets.vec));

    keys_og.vec = keys1.vec;

    u512i_64_t h1, h2;
    if (hash->do_stats) START_TIMING();
    compute_hash_simd64(&keys1, &keys2, &h1, &h2);
    if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

    u512i_64_t elems;

    lookup_parallel(hash, &keys_og, &h1, &h2, &elems);

    int nr_found = 0;

    for (int i = 0; i < 8; ++i) {
        cuckoo_elem_t *elem = (cuckoo_elem_t*)elems.arr[i];
        values[i] = ( ((uint64_t)elem->value_hi) << 16 ) 
                 + ((uint64_t)elem->value_lo);
        nr_found += !!values[i];
    }

    return nr_found;
}

int
cuckoo_hash_lookup(const struct cuckoo_hash *hash,
                   paddr_t key, paddr_t *value, uint32_t *size)
{
    DECLARE_TIMING();
    *value = *size = 0;

    static int tsx_success = 0;

    uint64_t h1, h2;
    if (hash->do_stats) START_TIMING();
    compute_hash(key, &h1, &h2, hash->compact_idx);
    //if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

    if (hash->do_stats) cstats.nlookups++;
#if 0
    unsigned status;
    pmlock_rd_lock(&hash->meta->rwlock);
    if ((status = _xbegin ()) == _XBEGIN_STARTED) {
        cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
        if (!elem) {
            _xabort(-1);
            pmlock_rd_unlock(&hash->meta->rwlock);
            return -ENOENT;
        }

        *value = ( ((uint64_t)elem->value_hi) << 16 ) 
                 + ((uint64_t)elem->value_lo);
        *size = elem->range;
        _xend();
        pmlock_rd_unlock(&hash->meta->rwlock);
        if (hash->do_stats) UPDATE_TIMING(&cstats, non_conflict_time);
    } else {
        if (hash->do_stats) cstats.nconflicts++;
        // Effectively serializes
        pmlock_rd_unlock(&hash->meta->rwlock);
        pmlock_wr_lock(&hash->meta->rwlock);

        cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
        if (!elem) {
            pmlock_wr_unlock(&hash->meta->rwlock);
            return -ENOENT;
        }

        *value = ( ((uint64_t)elem->value_hi) << 16 ) 
                 + ((uint64_t)elem->value_lo);
        *size = elem->range;

        pmlock_wr_unlock(&hash->meta->rwlock);
        if (hash->do_stats) UPDATE_TIMING(&cstats, conflict_time);
    }
#else
    // Effectively serializes
    //pmlock_wr_lock(&hash->meta->rwlock);

    cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
    if (!elem) {
        //pmlock_wr_unlock(&hash->meta->rwlock);
        return -ENOENT;
    }

    *value = ( ((uint64_t)elem->value_hi) << 16 ) 
             + ((uint64_t)elem->value_lo);
    *size = elem->range;

    //pmlock_wr_unlock(&hash->meta->rwlock);
    if (hash->do_stats) UPDATE_TIMING(&cstats, conflict_time);
#endif

    return 0;
}

int
cuckoo_hash_update(const struct cuckoo_hash *hash, paddr_t key, uint32_t size)
{
    uint64_t h1, h2;
    DECLARE_TIMING();
    if (hash->do_stats) START_TIMING();
    compute_hash(key, &h1, &h2, hash->compact_idx);
    if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

    unsigned status;
    cuckoo_elem_t *elem;
    if ((status = _xbegin ()) == _XBEGIN_STARTED) {

        elem = lookup(hash, key, h1, h2);
        if (!elem) { 
            _xabort(1);
            return -ENOENT;
        }

        elem->range = (uint8_t)size;
        _xend();
    } else {
        pmlock_wr_lock(&hash->meta->rwlock);
        
        elem = lookup(hash, key, h1, h2);
        if (!elem) { 
            pmlock_wr_unlock(&hash->meta->rwlock);
            return -ENOENT;
        }

        elem->range = (uint8_t)size;
        pmlock_wr_unlock(&hash->meta->rwlock);
    }

    nvm_persist_struct(elem->range);

    if (hash->do_stats) {
        INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(elem->range));
    }

    return 0;
}

int
cuckoo_hash_remove(struct cuckoo_hash *hash, paddr_t key, paddr_t *value,
                   uint32_t *index, uint32_t *range)
{
    uint64_t h1, h2;
    DECLARE_TIMING();
    if (hash->do_stats) START_TIMING();
    compute_hash(key, &h1, &h2, hash->compact_idx);
    if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

    unsigned status;
    static int tsx_success = 0;
    cuckoo_elem_t *elem;
    cuckoo_elem_t zero = {0,};
    if ((status = _xbegin ()) == _XBEGIN_STARTED) {
        elem = lookup(hash, key, h1, h2);
        if (!elem) {
            _xabort(1);
            return -ENOENT;
        }

        *value = ( ((uint64_t)elem->value_hi) << 16 ) 
                 + ((uint64_t)elem->value_lo);
        *index = elem->index;
        *range = elem->range;

        *elem = zero;
        _xend();
        tsx_success++;
    } else {
        tsx_success = 0;
        pmlock_wr_lock(&hash->meta->rwlock);
        elem = lookup(hash, key, h1, h2);
        if (!elem) {
            pmlock_wr_unlock(&hash->meta->rwlock);
            return -ENOENT;
        }

        *value = ( ((uint64_t)elem->value_hi) << 16 ) 
                 + ((uint64_t)elem->value_lo);
        *index = elem->index;
        *range = elem->range;
        memset((void*)elem, 0, sizeof(*elem));
        pmlock_wr_unlock(&hash->meta->rwlock);
    }

    nvm_persist_struct(*elem);

    return 0;
}

static
bool
undo_insert(struct cuckoo_hash *hash, struct cuckoo_hash_elem *item,
            size_t max_depth)
{
    DECLARE_TIMING();
    uint64_t mod = (uint64_t)hash->meta->max_size;

    bool rot = false;

    for (size_t depth = 0; depth < max_depth; ++depth) {

        uint64_t ih1, ih2;
        if (hash->do_stats) START_TIMING();
        compute_hash(item->key, &ih1, &ih2, hash->compact_idx);
        if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

        uint64_t h2m = rot ? ih1 % mod : ih2 % mod;
        struct cuckoo_hash_elem *elem = bin_at(hash, h2m);

        struct cuckoo_hash_elem victim = *elem;

        *elem = *item;
        
        rot = ~rot;

        nvm_persist_struct(*elem);
        if (hash->do_stats) {
            INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(*elem));
        }

        uint64_t vh1, vh2;
        if (hash->do_stats) START_TIMING();
        compute_hash(victim.key, &vh1, &vh2, hash->compact_idx);
        if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

        uint64_t h1m = vh1 % mod;
        if (h1m != h2m) {
            assert(depth >= max_depth);

            return true;
        }

        *item = victim;
        nvm_persist_struct(*item);
        if (hash->do_stats) {
            INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(*item));
        }
    }

    return false;
}


static inline
bool
insert(struct cuckoo_hash *hash, struct cuckoo_hash_elem *item)
{
    DECLARE_TIMING();
    size_t max_depth = 128;//(size_t) hash->meta.max_size;

    uint64_t mod = (uint64_t) hash->meta->max_size;

    bool rot = false;

    for (size_t depth = 0; depth < max_depth; ++depth) {
        uint64_t ih1, ih2;
        if (hash->do_stats) START_TIMING();
        compute_hash(item->key, &ih1, &ih2, hash->compact_idx);
        if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

        uint64_t h1m = rot ? ih2 % mod : ih1 % mod;
        cuckoo_elem_t *elem = bin_at(hash, h1m);

        if (elem->key == 0) {
            *elem = *item;
            nvm_persist_struct(*elem);
            if (hash->do_stats) {
                INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(*elem));
            }

	        //if (depth > 0) printf("depth: %lu\n", depth);	

            return true;
        }

        cuckoo_elem_t victim = *elem;
        uint64_t vh1, vh2;
        if (hash->do_stats) START_TIMING();
        compute_hash(victim.key, &vh1, &vh2, hash->compact_idx);
        if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

        if (vh1 % mod == h1m) rot = true;
        else rot = false;

        //printf("%lx (h1m = %x, h2m = %x)\n", item->key, ih1 % mod, ih2 % mod);

        *elem = *item;
        nvm_persist_struct(*elem);
        if (hash->do_stats) {
            INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(*elem));
        }

        *item = victim;
        nvm_persist_struct(*item);
        if (hash->do_stats) {
            INCR_NR_CACHELINE(&cstats, ncachelines_written, sizeof(*item));
        }
    }

    panic("Reached max depth!!!\n");

    return undo_insert(hash, item, max_depth);
}


int
cuckoo_hash_insert(struct cuckoo_hash *hash, paddr_t key, paddr_t value, 
                   uint32_t index, uint32_t range)
{
    uint64_t h1, h2;
    DECLARE_TIMING();
    if (hash->do_stats) START_TIMING();
    compute_hash(key, &h1, &h2, hash->compact_idx);
    if (hash->do_stats) UPDATE_TIMING(&cstats, compute_hash);

    unsigned status;
    static int tsx_success = 0;
    struct cuckoo_hash_elem new_elem = {
       .key = key, 
       .value_hi = (uint16_t)(value >> 32), 
       .value_lo = (uint32_t)(value),
       .index = index, 
       .range = range,
    };

    if ((status = _xbegin ()) == _XBEGIN_STARTED) {
        struct cuckoo_hash_elem *elem = lookup(hash, key, h1, h2);
        if (elem) {
            _xabort(1);
            return 0;
        }

        if (insert(hash, &new_elem)) {
            _xend();
            return 1;
        }
        _xabort(1);
    } else {
        pmlock_wr_lock(&hash->meta->rwlock);

        struct cuckoo_hash_elem *elem = lookup(hash, key, h1, h2);
        if (elem) {
            pmlock_wr_unlock(&hash->meta->rwlock);
            return 0;
        }

        if (insert(hash, &new_elem)) {
            pmlock_wr_unlock(&hash->meta->rwlock);
            return 1;
        }
        pmlock_wr_unlock(&hash->meta->rwlock);
    }

    //assert(new_elem.key == key);
    //assert(new_elem.value_hi == (uint16_t)(value >> 32));
    //assert(new_elem.value_lo == (uint32_t)(value));
    //assert(new_elem.index == index);
    //assert(new_elem.range == range);
    //assert(new_elem.hash1 == h1);
    //assert(new_elem.hash2 == h2);

    return -1;
}
