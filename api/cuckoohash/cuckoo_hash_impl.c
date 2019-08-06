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

static inline
void
compute_hash(paddr_t key, uint32_t *h1, uint32_t *h2)
{
    extern void hashlittle2(const void *key, size_t length,
                            uint32_t *pc, uint32_t *pb);

    /* Initial values are arbitrary.  */
    *h1 = 0x3ac5d673;
    *h2 = 0x6d7839d0;
    // TODO: might be inefficient
    hashlittle2(&key, sizeof(key), h1, h2);
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
    ssize_t err = CB(idx_spec, cb_read, meta_block, 0, 
                                        sizeof(hash->meta), (char*)&(hash->meta));
    if (err != sizeof(hash->meta)) return err;

    if (hash->meta.magic != CUCKOO_MAGIC) {
        device_info_t devinfo;
        int ret = CB(idx_spec, cb_get_dev_info, &devinfo);
        if_then_panic(ret, "Could not get device info!");

        hash->meta.max_size = max_entries;
        hash->meta.magic = CUCKOO_MAGIC;

        // Allocate the element array.
        size_t nblk = (max_entries * sizeof(*hash->table)) / devinfo.di_block_size;
        ssize_t nalloc = CB(idx_spec, cb_alloc_metadata, nblk, 
                                      &(hash->meta.elem_start_blk));
        if_then_panic(nalloc < nblk, "no large contiguous region!");

        // Now write back the metadata.
        ssize_t werr = CB(idx_spec, cb_write, meta_block, 0, 
                          sizeof(hash->meta), (char*)&(hash->meta));
        if (werr != sizeof(hash->meta)) return err;
    }

    // Always have to retrieve the pointer.
    ssize_t perr = CB(idx_spec, cb_get_addr, 
                      hash->meta.elem_start_blk, 0, (char**)&(hash->table));
    if (perr) return perr;

    *ht = hash;
    
    return 0;
}

void
cuckoo_hash_destroy(const struct cuckoo_hash *hash)
{
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
lookup(const struct cuckoo_hash *hash, paddr_t key, uint32_t h1, uint32_t h2)
{
    uint32_t mod = hash->meta.max_size;

    struct cuckoo_hash_elem *elem = NULL;

    elem = bin_at(hash, (h1 % mod));

    if (elem->hash2 == h2 && elem->hash1 == h1
        && elem->hash_item.key == key)
    {
        return elem;
    }

    elem = bin_at(hash, (h2 % mod));
    if (elem->hash2 == h1 && elem->hash1 == h2
        && elem->hash_item.key == key)
    {
        return elem;
    }

    return NULL;
}


int
cuckoo_hash_lookup(const struct cuckoo_hash *hash,
                   paddr_t key, paddr_t *value, uint32_t *size)
{
    *value = *size = 0;

    uint32_t h1, h2;
    compute_hash(key, &h1, &h2);

    cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
    if (!elem) return -ENOENT;

    *value = elem->hash_item.value;
    *size = elem->hash_item.range;
    return 0;
}

int
cuckoo_hash_update(const struct cuckoo_hash *hash, paddr_t key, uint32_t size)
{
    uint32_t h1, h2;
    compute_hash(key, &h1, &h2);

    cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
    if (!elem) return -ENOENT;

    // TODO: make pmem
    elem->hash_item.range = size;
    return 0;
}

int
cuckoo_hash_remove(struct cuckoo_hash *hash, paddr_t key, paddr_t *value,
                   uint32_t *index, uint32_t *range)
{
    uint32_t h1, h2;
    compute_hash(key, &h1, &h2);

    cuckoo_elem_t *elem = lookup(hash, key, h1, h2);
    if (!elem) return -ENOENT;

    *value = elem->hash_item.value;
    *index = elem->hash_item.index;
    *range = elem->hash_item.range;

    // TODO: make pmem persistent
    memset((void*)elem, 0, sizeof(*elem));

    return 0;
}

static
bool
undo_insert(struct cuckoo_hash *hash, struct cuckoo_hash_elem *item,
            size_t max_depth)
{
    uint32_t mod = (uint32_t) hash->meta.max_size;

    for (size_t depth = 0; depth < max_depth; ++depth) {
        uint32_t h2m = item->hash2 % mod;
        struct cuckoo_hash_elem *elem = bin_at(hash, h2m);

        struct cuckoo_hash_elem victim = *elem;

        elem->hash_item = item->hash_item;
        elem->hash1     = item->hash2;
        elem->hash2     = item->hash1;

        uint32_t h1m = victim.hash1 % mod;
        if (h1m != h2m) {
            assert(depth >= max_depth);

            return true;
        }

        *item = victim;
    }

    return false;
}


static inline
bool
insert(struct cuckoo_hash *hash, struct cuckoo_hash_elem *item)
{
    size_t max_depth = (size_t) hash->meta.max_size;

    uint32_t mod = (uint32_t) hash->meta.max_size;

    for (size_t depth = 0; depth < max_depth; ++depth) {
        uint32_t h1m = item->hash1 % mod;
        cuckoo_elem_t *elem = bin_at(hash, h1m);

        if (elem->hash1 == elem->hash2 || (elem->hash1 % mod) != h1m) {
            *elem = *item;

            return true;
        }

        cuckoo_elem_t victim = *elem;

        *elem = *item;

        item->hash_item = victim.hash_item;
        item->hash1     = victim.hash2;
        item->hash2     = victim.hash1;
    }

    return undo_insert(hash, item, max_depth);
}


int
cuckoo_hash_insert(struct cuckoo_hash *hash, paddr_t key, paddr_t value, 
                   uint32_t index, uint32_t range)
{
    uint32_t h1, h2;
    compute_hash(key, &h1, &h2);

    struct cuckoo_hash_elem *elem = lookup(hash, key, h1, h2);
    if (elem) {
        return 0;
    }

    struct cuckoo_hash_elem new_elem = {
      .hash_item = { .key = key, .value = value, .index = index, .range = range },
      .hash1 = h1,
      .hash2 = h2
    };

    if (insert(hash, &new_elem)) {
        return -1;
    }

    assert(new_elem.hash_item.key == key);
    assert(new_elem.hash_item.value == value);
    assert(new_elem.hash_item.index == index);
    assert(new_elem.hash_item.range == range);
    assert(new_elem.hash1 == h1);
    assert(new_elem.hash2 == h2);

    return -1;
}
