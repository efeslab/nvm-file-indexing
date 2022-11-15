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
compute_hash(const void *key, size_t key_len,
             uint32_t *h1, uint32_t *h2)
{
    extern void hashlittle2(const void *key, size_t length,
                          uint32_t *pc, uint32_t *pb);

    /* Initial values are arbitrary.  */
    *h1 = 0x3ac5d673;
    *h2 = 0x6d7839d0;
    hashlittle2(key, key_len, h1, h2);
    if (*h1 == *h2) {
        *h2 = ~*h2;
    }
}

int
cuckoo_hash_init(nvm_cuckoo_idx_t **ht, paddr_t meta_block, 
                 size_t max_entries, idx_spec_t *idx_spec)
{
    *ht = NULL;
    nvm_cuckoo_idx_t *hash = ZALLOC(idx_spec, sizeof(*hash));
    if (!hash) return -ENOMEM;

    // Read metadata to see if it exists or not.
    ssize_t err = CB(idx_spec, cb_read, meta_block, 0, 
                                        sizeof(hash->meta), &(hash->meta));
    if (err) return err;

    if (hash->meta.magic != CUCKOO_MAGIC) {
        size_t blksz = 0;

        hash->meta.elem_start_blk = 0;
        hash->meta.max_size_blks = blksz;

        // Allocate the element array.
        size_t nblk = (max_entries * sizeof(*hash->table)) / blksz;
        ssize_t nalloc = CB(idx_spec, cb_alloc_metadata, nblk, 
                                      &(hash->meta.elem_start_blk));
        if_then_panic(nalloc < nblocks, "no large contiguous region!");
    }

    // Always have to retrieve the pointer.
    ssize_t perr = CB(ht, cb_get_addr, hash->meta.elem_start_blk, 0, &(hash->table));
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
struct cuckoo_hash_item *
lookup(const struct cuckoo_hash *hash, const void *key, size_t key_len,
       uint32_t h1, uint32_t h2)
{
  uint32_t mask = (1U << hash->power) - 1;

  struct cuckoo_hash_elem *elem, *end;

  elem = bin_at(hash, (h1 & mask));
  end = elem + hash->bin_size;
  while (elem != end)
    {
      if (elem->hash2 == h2 && elem->hash1 == h1
          && elem->hash_item.key_len == key_len
          && memcmp(elem->hash_item.key, key, key_len) == 0)
        {
          return &elem->hash_item;
        }

      ++elem;
    }

  elem = bin_at(hash, (h2 & mask));
  end = elem + hash->bin_size;
  while (elem != end) {
      if (elem->hash2 == h1 && elem->hash1 == h2
          && elem->hash_item.key_len == key_len
          && memcmp(elem->hash_item.key, key, key_len) == 0)
        {
          return &elem->hash_item;
        }

      ++elem;
  }

    return NULL;
}


struct cuckoo_hash_item *
cuckoo_hash_lookup(const struct cuckoo_hash *hash,
                   const void *key, size_t key_len)
{
  uint32_t h1, h2;
  compute_hash(key, key_len, &h1, &h2);

  return lookup(hash, key, key_len, h1, h2);
}


void
cuckoo_hash_remove(struct cuckoo_hash *hash,
                   const struct cuckoo_hash_item *hash_item)
{
  if (hash_item)
    {
      struct cuckoo_hash_elem *elem =
        ((struct cuckoo_hash_elem *)
         ((char *) hash_item - offsetof(struct cuckoo_hash_elem, hash_item)));
      elem->hash1 = elem->hash2 = 0;
      --hash->count;
    }
}


static
bool
grow_table(struct cuckoo_hash *hash)
{
  size_t size =
    ((size_t) hash->bin_size << hash->power) * sizeof(*hash->table);
  struct cuckoo_hash_elem *table = realloc(hash->table, size * 2);
  if (! table)
    return false;

  hash->table = table;
  memcpy((char *) hash->table + size, hash->table, size);
  ++hash->power;

  return true;
}


static
bool
grow_bin_size(struct cuckoo_hash *hash)
{
    size_t size =
      ((size_t) hash->bin_size << hash->power) * sizeof(*hash->table);
    uint32_t bin_count = 1U << hash->power;
    size_t add = bin_count * sizeof(*hash->table);
    struct cuckoo_hash_elem *table = realloc(hash->table, size + add);
    if (! table)
      return false;

    hash->table = table;
    for (uint32_t bin = bin_count - 1; bin > 0; --bin) {
        struct cuckoo_hash_elem *old = bin_at(hash, bin);
        struct cuckoo_hash_elem *new = old + bin;
        memmove(new, old, hash->bin_size * sizeof(*hash->table));
        memset(new + hash->bin_size, 0, sizeof(*hash->table));
    }
    
    memset(hash->table + hash->bin_size, 0, sizeof(*hash->table));

    ++hash->bin_size;

    return true;
}


static
bool
undo_insert(struct cuckoo_hash *hash, struct cuckoo_hash_elem *item,
            size_t max_depth, uint32_t offset, int phase)
{
    uint32_t mask = (1U << hash->power) - 1;

    for (size_t depth = 0; depth < max_depth * phase; ++depth) {
        if (offset-- == 0)
            offset = hash->bin_size - 1;

        uint32_t h2m = item->hash2 & mask;
        struct cuckoo_hash_elem *beg = bin_at(hash, h2m);

        struct cuckoo_hash_elem victim = beg[offset];

        beg[offset].hash_item = item->hash_item;
        beg[offset].hash1 = item->hash2;
        beg[offset].hash2 = item->hash1;

        uint32_t h1m = victim.hash1 & mask;
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
  size_t max_depth = (size_t) hash->power << 5;
  if (max_depth > (size_t) hash->bin_size << hash->power)
    max_depth = (size_t) hash->bin_size << hash->power;

  uint32_t offset = 0;
  int phase = 0;
  while (phase < 2)
    {
      uint32_t mask = (1U << hash->power) - 1;

      for (size_t depth = 0; depth < max_depth; ++depth)
        {
          uint32_t h1m = item->hash1 & mask;
          struct cuckoo_hash_elem *beg = bin_at(hash, h1m);
          struct cuckoo_hash_elem *end = beg + hash->bin_size;
          for (struct cuckoo_hash_elem *elem = beg; elem != end; ++elem)
            {
              if (elem->hash1 == elem->hash2 || (elem->hash1 & mask) != h1m)
                {
                  *elem = *item;

                  return true;
                }
            }

          struct cuckoo_hash_elem victim = beg[offset];

          beg[offset] = *item;

          item->hash_item = victim.hash_item;
          item->hash1 = victim.hash2;
          item->hash2 = victim.hash1;

          if (++offset == hash->bin_size)
            offset = 0;
        }

      ++phase;

      if (phase == 1 && !grow_table(hash)) break;
    }

  if (grow_bin_size(hash))
    {
      uint32_t mask = (1U << hash->power) - 1;
      struct cuckoo_hash_elem *last =
        bin_at(hash, (item->hash1 & mask) + 1) - 1;

      *last = *item;

      return true;
    }
  else
    {
      return undo_insert(hash, item, max_depth, offset, phase);
    }
}


struct cuckoo_hash_item *
cuckoo_hash_insert(struct cuckoo_hash *hash,
                   const void *key, size_t key_len, void *value)
{
    uint32_t h1, h2;
    compute_hash(key, key_len, &h1, &h2);

    struct cuckoo_hash_item *item = lookup(hash, key, key_len, h1, h2);
    if (item) {
        return item;
    }

    struct cuckoo_hash_elem elem = {
      .hash_item = { .key = key, .key_len = key_len, .value = value },
      .hash1 = h1,
      .hash2 = h2
    };

    if (insert(hash, &elem)) {
        ++hash->count;
        return NULL;
    }

    assert(elem.hash_item.key == key);
    assert(elem.hash_item.key_len == key_len);
    assert(elem.hash_item.value == value);
    assert(elem.hash1 == h1);
    assert(elem.hash2 == h2);

    return CUCKOO_HASH_FAILED;
}


struct cuckoo_hash_item *
cuckoo_hash_next(const struct cuckoo_hash *hash,
                 const struct cuckoo_hash_item *hash_item)
{
  struct cuckoo_hash_elem *elem =
    (hash_item != NULL
     ? ((struct cuckoo_hash_elem *)
        ((char *) hash_item - offsetof(struct cuckoo_hash_elem, hash_item))
        + 1)
     : hash->table);

    uint32_t bin_count = 1U << hash->power;
    struct cuckoo_hash_elem *end = bin_at(hash, bin_count);
    uint32_t mask = bin_count - 1;
    while (elem != end) {
        if (elem->hash1 != elem->hash2) {
          /*
            Test that the element is valid, i.e., its hash1 matches
            the bin index it resides in.
          */
            struct cuckoo_hash_elem *bin = bin_at(hash, (elem->hash1 & mask));
            if (bin <= elem && elem < bin + hash->bin_size)
                return &elem->hash_item;
        }

      ++elem;
    }

    return NULL;
}
