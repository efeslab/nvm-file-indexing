/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */
#include <stdlib.h>
#include <assert.h>
#include <string.h>  /* memset */
#include <stdint.h>

#include <emmintrin.h>
#include <immintrin.h>

#include "ghash.h"

// stats
uint64_t reads;
uint64_t writes;
uint64_t blocks;

#if 1
#define pthread_rwlock_rdlock(x) 0
#define pthread_rwlock_wrlock(x) 0
#define pthread_rwlock_unlock(x) 0
#elif 0
#define pthread_rwlock_rdlock(x) do { printf("rdlk\n"); pthread_rwlock_rdlock(x); } while (0)
#define pthread_rwlock_wrlock(x) do { printf("wrlk\n"); pthread_rwlock_wrlock(x); } while (0)
#define pthread_rwlock_unlock(x) do { printf("unlk\n"); pthread_rwlock_unlock(x); } while (0)
#elif 0
#define pthread_rwlock_rdlock(x) if_then_panic(0 != pthread_rwlock_tryrdlock(x), "Could not acquire rdlock!")
#define pthread_rwlock_wrlock(x) if_then_panic(0 != pthread_rwlock_trywrlock(x), "Could not acquire wrlock!")
#endif



#define HASH_TABLE_MIN_SHIFT 3  /* 1 << 3 == 8 buckets */

/* Each table size has an associated prime modulo (the first prime
 * lower than the table size) used to find the initial bucket. Probing
 * then works modulo 2^n. The prime modulo is necessary to get a
 * good distribution with poor hash functions.
 */
static const int prime_mod [] = {
  1,          /* For 1 << 0 */
  2,
  3,
  7,
  13,
  31,
  61,
  127,
  251,
  509,
  1021,
  2039,
  4093,
  8191,
  16381,
  32749,
  65521,      /* For 1 << 16 */
  131071,
  262139,
  524287,
  1048573,
  2097143,
  4194301,
  8388593,
  16777213,
  33554393,
  67108859,
  134217689,
  268435399,
  536870909,
  1073741789,
  2147483647  /* For 1 << 31 */
};

static void
nvm_hash_table_set_shift (nvm_hash_idx_t *hash_table, int shift) {
  int i;
  uint32_t mask = 0;

  hash_table->size = 1 << shift;
  hash_table->mod  = prime_mod [shift];

  for (i = 0; i < shift; i++) {
    mask <<= 1;
    mask |= 1;
  }

  hash_table->mask = mask;
}

static int
nvm_hash_table_find_closest_shift (int n) {
  int i;

  for (i = 0; n; i++) {
    n >>= 1;
  }

  return i;
}

static void
nvm_hash_table_set_shift_from_size(nvm_hash_idx_t *hash_table, int size) {
  int shift;

  shift = nvm_hash_table_find_closest_shift(size);
  shift = MAX(shift, HASH_TABLE_MIN_SHIFT);

  nvm_hash_table_set_shift(hash_table, shift);
}

/*
 * nvm_hash_table_lookup_node:
 * @hash_table: our #nvm_hash_idx_t
 * @key: the key to lookup against
 * @hash_return: key hash return location
 *
 * Performs a lookup in the hash table, preserving extra information
 * usually needed for insertion.
 *
 * This function first computes the hash value of the key using the
 * user's hash function.
 *
 * If an entry in the table matching @key is found then this function
 * returns the index of that entry in the table, and if not, the
 * index of an unused node (empty or tombstone) where the key can be
 * inserted.
 *
  The computed hash value is returned in the variable pointed to
 * by @hash_return. This is to save insertions from having to compute
 * the hash record again for the new record.
 *
 * Returns: index of the described node
 */
#define SEQ_STEP
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
static uint32_t
nvm_hash_table_lookup_node_simd (nvm_hash_idx_t *hash_table,
                                 paddr_t         key,
                                 hash_ent_t    **ent_return,
                                 uint32_t       *hash_return, 
                                 int start);
static inline uint32_t
nvm_hash_table_lookup_node (nvm_hash_idx_t    *hash_table,
                          paddr_t        key,
                          hash_ent_t  **ent_return,
                          uint32_t      *hash_return,
                          bool           force) {
  uint32_t node_index;
  uint32_t hash_value;
  uint32_t first_tombstone = 0;
  int have_tombstone = FALSE;
  *ent_return = NULL;
#ifdef SEQ_STEP
  uint32_t step = 1;
#else
  uint32_t step = 0;
#endif
  hash_ent_t *buffer;
  hash_ent_t *cur;

  DECLARE_TIMING();
  if (hash_table->enable_stats) START_TIMING();
  hash_value = hash_table->hash_func(key);
  if (hash_table->compact) {
      hash_value = (uint32_t)(key % hash_table->mod);
  }
  if (hash_table->enable_stats) UPDATE_TIMING(&hash_table->stats, compute_hash);
#if 0
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }
#endif

  if (unlikely(hash_table->enable_stats)) hash_table->stats.n_lookups++;
  uint64_t count = 0;

  //pthread_rwlock_rdlock(hash_table->cache_lock);

  *hash_return = hash_value;

  node_index = hash_value % hash_table->mod;
  //node_index = hash_value & hash_table->mask;
#if 1
  if (hash_table->do_lock) pthread_rwlock_rdlock(hash_table->locks + node_index);
#endif

    nvm_read_entry(hash_table, node_index, &cur, force);

    count++;

    START_TIMING();
#if 0
  if (HASH_ENT_IS_EMPTY(*cur)) goto end;

    if (likely(cur->key == key && HASH_ENT_IS_VALID(*cur))) {
      *ent_return = cur;
#if 1
      if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
      //pthread_rwlock_unlock(hash_table->cache_lock);
      
      // Only update on valid
      if (hash_table->enable_stats) {
          hash_table->stats.n_ents += count;
          if (!hash_table->stats.n_min_ents_per_lookup) {
              hash_table->stats.n_min_ents_per_lookup = count;
          }

          hash_table->stats.n_min_ents_per_lookup = min(count,
                  hash_table->stats.n_min_ents_per_lookup);
          hash_table->stats.n_max_ents_per_lookup = max(count,
                  hash_table->stats.n_max_ents_per_lookup);
      }
#endif
      return node_index;
    } 
    /*
    else if (unlikely(HASH_ENT_IS_TOMBSTONE(*cur) && !have_tombstone)) {
      first_tombstone = node_index;
      have_tombstone = TRUE;
    }
    */
#if 1
#ifndef seq_step
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;
#if 1
    if (unlikely(hash_table->do_lock)) {
      pthread_rwlock_unlock(hash_table->locks + node_index);
      pthread_rwlock_rdlock(hash_table->locks + new_idx);
    }
#endif
    node_index = new_idx;

    nvm_read_entry(hash_table, node_index, &cur, force);

    count++;
#else
    if (!hash_table->do_lock)
        return nvm_hash_table_lookup_node_simd(hash_table, key, ent_return, hash_return, 1);

#ifndef SEQ_STEP
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;
  pthread_rwlock_unlock(hash_table->locks + node_index);
  pthread_rwlock_rdlock(hash_table->locks + new_idx);
    node_index = new_idx;

    nvm_read_entry(hash_table, node_index, &cur, force);

    count++;
#endif
#endif

  while (!HASH_ENT_IS_EMPTY(*cur)) {
    if (likely(cur->key == key && HASH_ENT_IS_VALID(*cur))) {
      *ent_return = cur;

      if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
      //pthread_rwlock_unlock(hash_table->cache_lock);
      
      // Only update on valid
      if (hash_table->enable_stats) {
          hash_table->stats.n_ents += count;
          if (!hash_table->stats.n_min_ents_per_lookup) {
              hash_table->stats.n_min_ents_per_lookup = count;
          }

          hash_table->stats.n_min_ents_per_lookup = MIN(count,
                  hash_table->stats.n_min_ents_per_lookup);
          hash_table->stats.n_max_ents_per_lookup = MAX(count,
                  hash_table->stats.n_max_ents_per_lookup);

          UPDATE_TIMING(&(hash_table->stats), loop_time);
      }

      return node_index;
    } else if (unlikely(HASH_ENT_IS_TOMBSTONE(*cur) && !have_tombstone)) {
      first_tombstone = node_index;
      have_tombstone = TRUE;
    }

#ifndef SEQ_STEP
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;
#if 1
    if (unlikely(hash_table->do_lock)) {
      pthread_rwlock_unlock(hash_table->locks + node_index);
      pthread_rwlock_rdlock(hash_table->locks + new_idx);
    }
#endif
    node_index = new_idx;

    nvm_read_entry(hash_table, node_index, &cur, force);

    count++;
      UPDATE_TIMING(&(hash_table->stats), loop_time);
  }

end:
  if (have_tombstone) {
    nvm_read_entry(hash_table, first_tombstone, ent_return, false);
#if 1
    if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
      //pthread_rwlock_unlock(hash_table->cache_lock);
#endif
    return first_tombstone;
  }

  nvm_read_entry(hash_table, node_index, ent_return, false);
#if 1
  if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
#endif

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return node_index;
}
#pragma GCC pop_options

/*
 * Send help.
 *
 * Also I think we can only use this in libfs.
 */

static const int simd_offsets[] = {
    0,
1 * 16,
3 * 16,
6 * 16,
10 * 16,
15 * 16,
21 * 16,
28 * 16,
36 * 16,
45 * 16,
55 * 16,
66 * 16,
78 * 16,
91 * 16,
105 * 16,
120 * 16,
136 * 16,
153 * 16,
171 * 16,
190 * 16,
210 * 16,
231 * 16,
253 * 16,
276 * 16,
300 * 16,
325 * 16,
351 * 16,
378 * 16,
406 * 16,
435 * 16,
465 * 16,
496 * 16,
528 * 16,
561 * 16,
595 * 16,
630 * 16,
666 * 16,
703 * 16,
741 * 16,
780 * 16,
820 * 16,
861 * 16,
903 * 16,
946 * 16,
990 * 16,
1035 * 16,
1081 * 16,
1128 * 16,
1176 * 16,
1225 * 16
};

#if 0
#define SIM_W 8
#define SIM_TYPE __m512i
typedef union {
    SIM_TYPE v;
    paddr_t a[8];
} simd64_t;

#endif

int countSetBits(unsigned int n)
{
    unsigned int c; // the total bits set in n
    for (c = 0; n; n = n & (n-1)) {
        c++;
    }
    return c;
}
#if 0
static simd64_t incr = {.a = {
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t),
    8 * sizeof(hash_ent_t)
}};

static simd64_t bitmask = {.a = {
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF,
    0x0000FFFFFFFFFFFF
}};


#define OFFSETS(n) \
static simd64_t offsets ## n = {.a = { \
    ((n * 8) + 0) * sizeof(hash_ent_t), \
    ((n * 8) + 1) * sizeof(hash_ent_t), \
    ((n * 8) + 2) * sizeof(hash_ent_t), \
    ((n * 8) + 3) * sizeof(hash_ent_t), \
    ((n * 8) + 4) * sizeof(hash_ent_t), \
    ((n * 8) + 5) * sizeof(hash_ent_t), \
    ((n * 8) + 6) * sizeof(hash_ent_t), \
    ((n * 8) + 7) * sizeof(hash_ent_t), \
}}

OFFSETS(0);
OFFSETS(1);
OFFSETS(2);
OFFSETS(3);
OFFSETS(4);

#define VOFFSETS(n) \
static simd64_t voffsets ## n = {.a = { \
    ((n * 8) + 0) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 1) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 2) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 3) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 4) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 5) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 6) * sizeof(hash_ent_t) + 8, \
    ((n * 8) + 7) * sizeof(hash_ent_t) + 8, \
}}

VOFFSETS(0);
VOFFSETS(1);
VOFFSETS(2);
VOFFSETS(3);
VOFFSETS(4);
#endif

static uint32_t
nvm_hash_table_lookup_node_simd (nvm_hash_idx_t *hash_table,
                                 paddr_t         key,
                                 hash_ent_t    **ent_return,
                                 uint32_t       *hash_return, 
                                 int start) {
    uint32_t node_index;
    uint32_t hash_value;
    hash_ent_t *cur;

  DECLARE_TIMING();
  if (hash_table->enable_stats) START_TIMING();
  hash_value = hash_table->hash_func(key);
  if (hash_table->compact) {
      hash_value = (uint32_t)(key % hash_table->mod);
  }
  if (hash_table->enable_stats) UPDATE_TIMING(&hash_table->stats, compute_hash);
#if 0
    if (unlikely (!HASH_IS_REAL (hash_value))) {
        hash_value = 2;
    }
#endif

    *hash_return = hash_value;

    //node_index = hash_value & hash_table->mask;
    node_index = (hash_value + start) % hash_table->mod;

    nvm_read_entry(hash_table, node_index, &cur, false);

    *ent_return = NULL;
#undef CL
#define CL(n) (n * 64)
    __builtin_prefetch((void*)( ((char*)cur) + CL(0) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(1) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(2) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(3) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(4) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(5) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(6) ));
    __builtin_prefetch((void*)( ((char*)cur) + CL(7) ));

    int inc = 0;
#if 0
    SIM_TYPE tombstone = bitmask.v;
    SIM_TYPE inval = _mm512_set1_epi64(0);

    simd64_t keys;
    keys.v = _mm512_set1_epi64(key);
    //offsets.v = _mm512_set1_epi64(0);
    //val_offsets.v = _mm512_set1_epi64(0);

        simd64_t ent_keys, ent_vals;
#if 1

        ent_keys.v = _mm512_i64gather_epi64(offsets0.v, (void*)cur, 1);

        __mmask8 res = _mm512_cmpeq_epi64_mask(keys.v,
                            _mm512_i64gather_epi64(offsets0.v, (void*)cur, 1));

        // Now also mask which entries are valid
        ent_vals.v = _mm512_i64gather_epi64(voffsets0.v, (void*)cur, 1);
        ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask.v);


        __mmask8 is_tomb = _mm512_cmpeq_epi64_mask(ent_vals.v, tombstone);
        __mmask8 is_empty = _mm512_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, _kandn_mask8(is_empty, res));

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets0.v);
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } 

        //---------------------------------------------------------------------

        ent_keys.v = _mm512_i64gather_epi64(offsets1.v, (void*)cur, 1);

        res = _mm512_cmpeq_epi64_mask(keys.v,
                            _mm512_i64gather_epi64(offsets1.v, (void*)cur, 1));

        // Now also mask which entries are valid
        ent_vals.v = _mm512_i64gather_epi64(voffsets1.v, (void*)cur, 1);
        ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask.v);


        is_tomb = _mm512_cmpeq_epi64_mask(ent_vals.v, tombstone);
        is_empty = _mm512_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, _kandn_mask8(is_empty, res));

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets1.v);
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } 

        //---------------------------------------------------------------------

        ent_keys.v = _mm512_i64gather_epi64(offsets2.v, (void*)cur, 1);

        res = _mm512_cmpeq_epi64_mask(keys.v,
                            _mm512_i64gather_epi64(offsets2.v, (void*)cur, 1));

        // Now also mask which entries are valid
        ent_vals.v = _mm512_i64gather_epi64(voffsets2.v, (void*)cur, 1);
        ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask.v);


        is_tomb = _mm512_cmpeq_epi64_mask(ent_vals.v, tombstone);
        is_empty = _mm512_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, _kandn_mask8(is_empty, res));

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets2.v);
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } 

        //---------------------------------------------------------------------

        ent_keys.v = _mm512_i64gather_epi64(offsets3.v, (void*)cur, 1);

        res = _mm512_cmpeq_epi64_mask(keys.v,
                            _mm512_i64gather_epi64(offsets3.v, (void*)cur, 1));

        // Now also mask which entries are valid
        ent_vals.v = _mm512_i64gather_epi64(voffsets3.v, (void*)cur, 1);
        ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask.v);


        is_tomb = _mm512_cmpeq_epi64_mask(ent_vals.v, tombstone);
        is_empty = _mm512_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, _kandn_mask8(is_empty, res));

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets3.v);
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } 

        //---------------------------------------------------------------------

        ent_keys.v = _mm512_i64gather_epi64(offsets4.v, (void*)cur, 1);

        res = _mm512_cmpeq_epi64_mask(keys.v,
                            _mm512_i64gather_epi64(offsets4.v, (void*)cur, 1));

        // Now also mask which entries are valid
        ent_vals.v = _mm512_i64gather_epi64(voffsets4.v, (void*)cur, 1);
        ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask.v);


        is_tomb = _mm512_cmpeq_epi64_mask(ent_vals.v, tombstone);
        is_empty = _mm512_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, _kandn_mask8(is_empty, res));

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets4.v);
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } 

        return 0;
#else
        for (int i = 0; i < SIM_W; ++i) {
            offsets.a[i] = simd_offsets[(inc * SIM_W) + i];
            val_offsets.a[i] = simd_offsets[(inc * SIM_W) + i] + 8;
        }
        inc++;

        keys.v = _mm_set1_epi64x(key);

        ent_keys.v = _mm_i64gather_epi64((int64_t*)cur, offsets.v, 1);
        //ent_keys.v = _mm512_i64gather_epi64(offsets.v, (void*)cur, 1);

        __mmask8 res = _mm_cmpeq_epi64_mask(keys.v, ent_keys.v);

        // Now also mask which entries are valid
        //ent_vals.v = _mm_i64gather_epi64(val_offsets.v, (void*)cur, 1);
        ent_vals.v = _mm_i64gather_epi64((void*)cur, val_offsets.v, 1);
        //SIM_TYPE bitmask = _mm_set1_epi64(0x0000FFFFFFFFFFFF);
        SIM_TYPE bitmask = _mm_set1_epi64x(0x0000FFFFFFFFFFFF);
        //ent_vals.v = _mm512_and_epi64(ent_vals.v, bitmask);
        ent_vals.v = _mm_and_si128(ent_vals.v, bitmask);

        SIM_TYPE tombstone = bitmask;
        SIM_TYPE inval = _mm_set1_epi64x(0);

        __mmask8 is_tomb = _mm_cmpeq_epi64_mask(ent_vals.v, tombstone);
        __mmask8 is_empty = _mm_cmpeq_epi64_mask(ent_vals.v, inval);

        // Remove things from the res mask
        res = _kandn_mask8(is_tomb, res);
        res = _kandn_mask8(is_empty, res);

        if (countSetBits(res) == 1) {
            // Find the one remaining thing
            //uint64_t offset = _mm512_mask_reduce_add_epi64(res, offsets.v);
            offsets.v = _mm_maskz_compress_epi64(res, offsets.v);
            uint64_t offset = offsets.a[0];
            *ent_return = (hash_ent_t*)(((char*)cur) + offset);
            return node_index + (uint32_t)offset;
        } else if (countSetBits(is_tomb) == 0) {
            return 0;
        }
#endif
#endif

    return 0;
}

static void 
nvm_hash_table_update_internal(nvm_hash_idx_t *hash_table,
                               hash_ent_t     *ent,
                               uint32_t        node_index,
                               size_t          new_range) 
{
#ifndef SIMPLE_ENTRIES
    ent->size = new_range;
    nvm_persist_struct(ent->size);
    //nvm_update(hash_table, node_index);
#endif
}
// Very similar to lookup, but we also update the entry at the end.
int nvm_hash_table_update(nvm_hash_idx_t *hash_table,
                          paddr_t         key,
                          size_t          new_range) {
  uint32_t node_index;
  uint32_t hash_value;
#ifdef SEQ_STEP
  uint32_t step = 1;
#else
  uint32_t step = 0;
#endif
  hash_ent_t *buffer;
  hash_ent_t *cur;

  DECLARE_TIMING();
  if (hash_table->enable_stats) START_TIMING();
  hash_value = hash_table->hash_func(key);
  if (hash_table->compact) {
      hash_value = (uint32_t)(key % hash_table->mod);
  }
  if (hash_table->enable_stats) UPDATE_TIMING(&hash_table->stats, compute_hash);
#if 0
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }
#endif

  //pthread_rwlock_rdlock(hash_table->cache_lock);

  node_index = hash_value % hash_table->mod;
  //node_index = hash_value & hash_table->mask;
  if (hash_table->do_lock) pthread_rwlock_rdlock(hash_table->locks + node_index);

  nvm_read_entry(hash_table, node_index, &cur, false);
  //cur = hash_table->cache[BLK_NUM(node_index)][BLK_IDX(node_index)];

  while (!HASH_ENT_IS_EMPTY(*cur)) {
    if (cur->key == key && HASH_ENT_IS_VALID(*cur)) {
      nvm_hash_table_update_internal(hash_table, cur, node_index, new_range);
      if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
      //pthread_rwlock_unlock(hash_table->cache_lock);
      return 1;
    } 
#ifndef SEQ_STEP
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;
    if (hash_table->do_lock) {
      pthread_rwlock_unlock(hash_table->locks + node_index);
      pthread_rwlock_rdlock(hash_table->locks + new_idx);
    }
    node_index = new_idx;
    nvm_read_entry(hash_table, node_index, &cur, false);
  }

  if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return 0;
}

/*
 * nvm_hash_table_remove_node:
 * @hash_table: our #nvm_hash_idx_t
 * @node: pointer to node to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 *
 * Removes a node from the hash table and updates the node count.
 * The node is replaced by a tombstone. No table resize is performed.
 *
 * If @notify is %TRUE then the destroy notify functions are called
 * for the key and value of the hash node.
 */
static void nvm_hash_table_remove_node (nvm_hash_idx_t  *hash_table,
                                        int              i,
                                        paddr_t         *pblk,
                                        size_t          *old_precursor,
                                        size_t          *old_size) {
  hash_ent_t *ent;

  //pthread_rwlock_wrlock(hash_table->locks + i);

  nvm_read_entry(hash_table, i, &ent, true);
  *pblk = HASH_ENT_VAL(*ent);
#ifdef SIMPLE_ENTRIES
  *old_size = 1;
  *old_precursor = 0;
#else
  *old_size = (size_t)ent->size;
  *old_precursor = (size_t)ent->index;
#endif

  HASH_ENT_SET_TOMBSTONE(*ent);
#ifndef SIMPLE_ENTRIES
  ent->size = 0;
  ent->index = 0;
#endif

  /* Erect tombstone */
  //nvm_update(hash_table, i);
  nvm_persist_struct(*ent);

  hash_table->nnodes--;

  //pthread_rwlock_unlock(hash_table->locks + i);
}

/**
 * nvm_hash_table_new:
 * @hash_func: a function to create a hash value from a key
 * @key_equal_func: a function to check two keys for equality
 *
 * Creates a new #nvm_hash_idx_t with a reference count of 1.
 *
 * Hash values returned by @hash_func are used to determine where keys
 * are stored within the #nvm_hash_idx_t data structure. The direct_hash(),
 * g_int_hash(), g_int64_hash(), g_double_hash() and g_str_hash()
 * functions are provided for some common types of keys.
 * If @hash_func is %NULL, direct_hash() is used.
 *
 * @key_equal_func is used when looking up keys in the #nvm_hash_idx_t.
 * The g_direct_equal(), g_int_equal(), g_int64_equal(), g_double_equal()
 * and g_str_equal() functions are provided for the most common types
 * of keys. If @key_equal_func is %NULL, keys are compared directly in
 * a similar fashion to g_direct_equal(), but without the overhead of
 * a function call. @key_equal_func is called with the key from the hash table
 * as its first parameter, and the user-provided key to check against as
 * its second.
 *
 * Returns: a new #nvm_hash_idx_t
 */
nvm_hash_idx_t *
nvm_hash_table_new(hash_func_t       hash_func,
                   size_t            max_entries,
                   size_t            block_size,
                   size_t            range_size,
                   paddr_t           metadata_location,
                   const idx_spec_t *idx_spec) {
  nvm_hash_idx_t *ht;

  ht = MALLOC(idx_spec, sizeof(*ht));

  if_then_panic(ht == NULL, "malloc callback failed!");
#if 0
  printf("max_entries %llu, block size %llu, range_size %llu, "
         "metadata block %llu\n", max_entries, block_size, range_size,
         metadata_location);
#endif
  //max_entries *= 1.1;
  //max_entries *= 100;

  ht->hash_func          = hash_func ? hash_func : nvm_idx_direct_hash;
  ht->ref_count          = 1;
  ht->nnodes             = 0;
  ht->noccupied          = 0;
  ht->blksz              = block_size;
  // Number of entries in nvram.
  ht->nvram_size         = max_entries / range_size;
  ht->range_size         = range_size;
  ht->idx_callbacks      = idx_spec->idx_callbacks;
  ht->idx_mem_man        = idx_spec->idx_mem_man;
  ht->metadata           = metadata_location;

  memset(&(ht->stats), 0, sizeof(ht->stats));
  ht->enable_stats = false;

  if (max_entries % range_size) {
    // If there's a partial range, need to not under-allocate.
    ht->nvram_size++;
  }

  //nvm_hash_table_set_shift_from_size(ht, ht->nvram_size);
  //printf("--------%llx\n", ht->mask);
  // Make sure the mod size doesn't go out of range.
  //ht->nvram_size = max(ht->nvram_size, ht->size);
  ht->mod = ht->nvram_size;
  // printf("--------%llx\n", ht->mod);
  size_t scaled_size = ht->nvram_size;

  if (max_entries < scaled_size) {
    max_entries = scaled_size;
  }

  size_t nblocks = BLK_NUM(ht, scaled_size);
  if (BLK_IDX(ht, scaled_size)) {
    // Partial block.
    nblocks++;
  }
  ht->nblocks = nblocks;

  size_t entries_per_block = block_size / sizeof(hash_ent_t);

  // initialize read-writer locks
  ht->locks = MALLOC(idx_spec, max_entries * sizeof(pthread_rwlock_t));
  assert(ht->locks);
  for (size_t i = 0; i < max_entries; ++i) {
    int err = pthread_rwlock_init(ht->locks + i, NULL);
    if_then_panic(err, "Could not init rwlock!");
  }

  // init metadata lock
  ht->metalock = MALLOC(idx_spec, sizeof(pthread_mutex_t));
  assert(ht->metalock);
  pthread_mutex_init(ht->metalock, NULL);

    if (!nvm_read_metadata(ht)) {
        ssize_t nalloc = CB(idx_spec, cb_alloc_metadata, nblocks, &(ht->data));
        if_then_panic(nalloc < nblocks, "no large contiguous region!");

        nvm_write_metadata(ht);
    }

  ssize_t perr = CB(ht, cb_get_addr, ht->data, 0, &(ht->data_ptr));
  if_then_panic(perr, "Could not get data_ptr!");
  // -- CACHING
  ht->do_cache = false;
  ht->do_lock  = false;
  // cache lock
  ht->cache_lock = MALLOC(idx_spec, sizeof(pthread_rwlock_t));
  assert(ht->cache_lock);
  int err = pthread_rwlock_init(ht->cache_lock, NULL);
  if_then_panic(err, "Could not init rwlock!");

  ht->cache = MALLOC(idx_spec, max_entries * sizeof(hash_ent_t));
  assert(ht->cache);

  // allocate cache bitmap -- unset for valid, set for invalid
  //size_t cache_bitmap_size = (max_entries / BITS_PER_LONG) * sizeof(unsigned long);
  size_t cache_state_size = max_entries * sizeof(int8_t);
  ht->cache_state = ZALLOC(ht, cache_state_size);
  assert(ht->cache_state);
  memset(ht->cache_state, -1, cache_state_size);

#if 0
  for (int i = 0; i < nblocks; ++i) {
    hash_ent_t *unused;
    ht->cache[i] = MALLOC(idx_spec, block_size);
    // load from NVRAM (force flag)
    nvm_read(ht, i, &unused, 1);
  }
#endif

  return ht;
}

/*
 * nvm_hash_table_insert_node:
 * @hash_table: our #nvm_hash_idx_t
 * @node_index: pointer to node to insert/replace
 * @key_hash: key hash
 * @key: (nullable): key to replace with, or %NULL
 * @value: value to replace with
 * @keep_new_key: whether to replace the key in the node with @key
 * @reusing_key: whether @key was taken out of the existing node
 *
 * Inserts a value at @node_index in the hash table and updates it.
 *
 * If @key has been taken out of the existing node (ie it is not
 * passed in via a nvm_hash_table_insert/replace) call, then @reusing_key
 * should be %TRUE.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static int
nvm_hash_table_insert_node(nvm_hash_idx_t *hash_table,
                           uint32_t node_index, uint32_t key_hash,
                           paddr_t new_key, paddr_t new_value, 
                           size_t new_index, size_t new_range)
{
  int already_exists;
  hash_ent_t *ent;

  nvm_read_entry(hash_table, node_index, &ent, false);
  already_exists = HASH_ENT_IS_VALID(*ent);

  // todo consider bookkeeping (nnodes, noccupied?)
  if (unlikely(already_exists)) {
    printf("already exists: %lx %lx (trying to insert: %lx %lx)\n",
        ent->key, HASH_ENT_VAL(*ent), new_key, new_value);
  } else {
    ent->key = new_key;
    HASH_ENT_SET_VAL(*ent, new_value);
#ifndef SIMPLE_ENTRIES
    ent->index = new_index;
    ent->size = new_range;
#endif
    //nvm_update(hash_table, node_index);
    nvm_persist_struct(*ent);
  }

  return !already_exists;
}

/**
 * nvm_hash_table_lookup:
 * @hash_table: a #nvm_hash_idx_t
 * @key: the key to look up
 *
 * Looks up a key in a #nvm_hash_idx_t. Note that this function cannot
 * distinguish between a key that is not present and one which is present
 * and has the value %NULL. If you need this distinction, use
 * nvm_hash_table_lookup_extended().
 *
 * Returns: (nullable): the associated value, or %NULL if the key is not found
 */
void nvm_hash_table_lookup(nvm_hash_idx_t *hash_table, paddr_t key,
    paddr_t *val, paddr_t *size, bool force) {
  uint32_t node_index;
  uint32_t hash_return;
  hash_ent_t *ent;

  assert(hash_table != NULL);
#if 0
  if (!hash_table->do_lock) {
      node_index = nvm_hash_table_lookup_node_simd(hash_table, key, &ent, &hash_return, 0);
  } else {
      node_index = nvm_hash_table_lookup_node(hash_table, key, &ent, &hash_return, force);
  }
#else
  node_index = nvm_hash_table_lookup_node(hash_table, key, &ent, &hash_return, force);
#endif

  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  if (ent) {
  paddr_t ent_val = HASH_ENT_VAL(*ent);
  *val = !HASH_ENT_IS_TOMBSTONE(*ent) ? ent_val : 0;
#ifdef SIMPLE_ENTRIES
  *size = 1;
#else
  *size = ent->size;
#endif
  } else {
      *val = 0;
      *size = 1;
  }

  //pthread_rwlock_unlock(hash_table->locks + node_index);
}

/*
 * nvm_hash_table_insert_internal:
 * @hash_table: our #nvm_hash_idx_t
 * @key: the key to insert
 * @value: the value to insert
 * @keep_new_key: if %TRUE and this key already exists in the table
 *   then call the destroy notify function on the old key.  If %FALSE
 *   then call the destroy notify function on the new key.
 *
 * Implements the common logic for the nvm_hash_table_insert() and
 * nvm_hash_table_replace() functions.
 *
 * Do a lookup of @key. If it is found, replace it with the new
 * @value (and perhaps the new @key). If it is not found, create
 * a new node.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static inline int
nvm_hash_table_insert_internal (nvm_hash_idx_t *hash_table,
                                paddr_t    key,
                                paddr_t    value,
                                size_t     index,
                                size_t     size)
{
  assert(hash_table != NULL);
  /*
   * iangneal: for concurrency reasons, we can't do lookup -> insert, as another
   * thread may come in and use the slot we just looked for.
   * This is basically a copy of lookup_node, but with write locks.
   */
  uint32_t node_index;
  uint32_t hash_value;
  uint32_t first_tombstone = 0;
  int have_tombstone = FALSE;
#ifdef SEQ_STEP
  uint32_t step = 1;
#else
  uint32_t step = 0;
#endif
  hash_ent_t *buffer;
  hash_ent_t *cur;

  assert (hash_table->ref_count > 0);

  DECLARE_TIMING();
  if (hash_table->enable_stats) START_TIMING();
  hash_value = hash_table->hash_func(key);
  if (hash_table->compact) {
      hash_value = (uint32_t)(key % hash_table->mod);
  }
  if (hash_table->enable_stats) UPDATE_TIMING(&hash_table->stats, compute_hash);
#if 0
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }
#endif
  //pthread_rwlock_rdlock(hash_table->cache_lock);

  node_index = hash_value % hash_table->mod;
  //node_index = hash_value & hash_table->mask;
  if (hash_table->do_lock) pthread_rwlock_wrlock(hash_table->locks + node_index);

  nvm_read_entry(hash_table, node_index, &cur, false);

    __builtin_prefetch((void*)( ((char*)cur) + 64 ), 1);
    __builtin_prefetch((void*)( ((char*)cur) + 128 ), 1);
    __builtin_prefetch((void*)( ((char*)cur) + 192 ), 1);
    __builtin_prefetch((void*)( ((char*)cur) + 256 ), 1);

  while (!HASH_ENT_IS_EMPTY(*cur)) {
    if (cur->key == key && HASH_ENT_IS_VALID(*cur)) {
      break;
    } else if (HASH_ENT_IS_TOMBSTONE(*cur) && !have_tombstone) {
      // keep lock until we decide we don't need it
      first_tombstone = node_index;
      have_tombstone = TRUE;
    } else {
      if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
    }
#ifndef SEQ_STEP
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;
    if (hash_table->do_lock) pthread_rwlock_wrlock(hash_table->locks + new_idx);

    node_index = new_idx;
    nvm_read_entry(hash_table, node_index, &cur, false);
  }

  if (have_tombstone) {
    if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
    node_index = first_tombstone;
  }

  int res = nvm_hash_table_insert_node(
      hash_table, node_index, hash_value, key, value, index, size);

  if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return res;
}

/**
 * nvm_hash_table_insert:
 * @hash_table: a #nvm_hash_idx_t
 * @key: a key to insert
 * @value: the value to associate with the key
 *
 * Inserts a new key and value into a #nvm_hash_idx_t.
 *
 * If the key already exists in the #nvm_hash_idx_t its current
 * value is replaced with the new value. If you supplied a
 * @value_destroy_func when creating the #nvm_hash_idx_t, the old
 * value is freed using that function. If you supplied a
 * @key_destroy_func when creating the #nvm_hash_idx_t, the passed
 * key is freed using that function.
 *
 * Returns: %TRUE if the key did not exist yet
 */
int
nvm_hash_table_insert (nvm_hash_idx_t *hash_table,
                       paddr_t     key,
                       paddr_t     value,
                       size_t      index,
                       size_t      size)
{
  return nvm_hash_table_insert_internal(hash_table, key, value, index, size);
}

/*
 * nvm_hash_table_remove_internal:
 * @hash_table: our #nvm_hash_idx_t
 * @key: the key to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 * Returns: %TRUE if a node was found and removed, else %FALSE
 *
 * Implements the common logic for the nvm_hash_table_remove() and
 * nvm_hash_table_steal() functions.
 *
 * Do a lookup of @key and remove it if it is found, calling the
 * destroy notify handlers only if @notify is %TRUE.
 */
static int
nvm_hash_table_remove_internal (nvm_hash_idx_t *hash_table,
                                paddr_t         key,
                                paddr_t        *old_pblk,
                                size_t         *old_idx,
                                size_t         *old_size)
{
  /*
   * iangneal: for concurrency reasons, we can't do lookup -> insert, as another
   * thread may come in and use the slot we just looked for.
   * This is basically a copy of lookup_node, but with write locks.
   */
  uint32_t node_index;
  uint32_t hash_value;
#ifdef SEQ_STEP
  uint32_t step = 1;
#else
  uint32_t step = 0;
#endif
  hash_ent_t *buffer;
  hash_ent_t *cur;

  assert (hash_table->ref_count > 0);

  DECLARE_TIMING();
  if (hash_table->enable_stats) START_TIMING();
  hash_value = hash_table->hash_func(key);
  if (hash_table->compact) {
      hash_value = (uint32_t)(key % hash_table->mod);
  }
  if (hash_table->enable_stats) UPDATE_TIMING(&hash_table->stats, compute_hash);
#if 0
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }
#endif

  //pthread_rwlock_rdlock(hash_table->cache_lock);
  node_index = hash_value % hash_table->mod;
  //node_index = hash_value & hash_table->mask;
  if (hash_table->do_lock) pthread_rwlock_wrlock(hash_table->locks + node_index);

  nvm_read_entry(hash_table, node_index, &cur, false);

  while (!HASH_ENT_IS_EMPTY(*cur)) {
    if (cur->key == key && HASH_ENT_IS_VALID(*cur)) {
      break;
    }
#ifndef SEQ_STEP
    step++;
#endif
    //uint32_t new_idx = (node_index + step) & hash_table->mask;
    uint32_t new_idx = (node_index + step) % hash_table->mod;

    if (hash_table->do_lock) {
      pthread_rwlock_unlock(hash_table->locks + node_index);
      pthread_rwlock_wrlock(hash_table->locks + new_idx);
    }

    nvm_read_entry(hash_table, new_idx, &cur, false);

    node_index = new_idx;
  }

  if (HASH_ENT_IS_VALID(*cur)) {
      nvm_hash_table_remove_node(hash_table, node_index, old_pblk, old_idx, old_size);
      if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);
      return 1;
  }

  if (hash_table->do_lock) pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return HASH_ENT_IS_VALID(*cur);
}

/**
 * nvm_hash_table_remove:
 * @hash_table: a #nvm_hash_idx_t
 * @key: the key to remove
 *
 * Removes a key and its associated value from a #nvm_hash_idx_t.
 *
 * If the #nvm_hash_idx_t was created using nvm_hash_table_new_full(), the
 * key and value are freed using the supplied destroy functions, otherwise
 * you have to make sure that any dynamically allocated values are freed
 * yourself.
 *
 * Returns: %TRUE if the key was found and removed from the #nvm_hash_idx_t
 */
int
nvm_hash_table_remove (nvm_hash_idx_t *hash_table,
                       paddr_t         key,
                       paddr_t        *removed,
                       size_t         *nprevious,
                       size_t         *ncontiguous)
{
  return nvm_hash_table_remove_internal(hash_table, key, removed,
                                        nprevious, ncontiguous);
}


/**
 * nvm_hash_table_size:
 * @hash_table: a #nvm_hash_idx_t
 *
 * Returns the number of elements contained in the #nvm_hash_idx_t.
 *
 * Returns: the number of key/value pairs in the #nvm_hash_idx_t.
 */
uint32_t nvm_hash_table_size (nvm_hash_idx_t *hash_table) {
  assert (hash_table != NULL);

  return hash_table->nnodes;
}

/*
 * Hash functions.
 */

