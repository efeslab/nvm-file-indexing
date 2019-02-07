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
#include <pthread.h>

#include "ghash.h"

#define G_DISABLE_ASSERT

#define MAX(x,y) (x > y ? x : y)

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
#define pthread_rwlock_rdlock(x) if (0 != pthread_rwlock_tryrdlock(x)) printf("WUT\n")
#define pthread_rwlock_wrlock(x) if (0 != pthread_rwlock_trywrlock(x)) printf("WUT\n")
#endif



#define HASH_TABLE_MIN_SHIFT 3  /* 1 << 3 == 8 buckets */

#define TRUE 1
#define FALSE 0

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
g_hash_table_set_shift (GHashTable *hash_table, int shift) {
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
g_hash_table_find_closest_shift (int n) {
  int i;

  for (i = 0; n; i++) {
    n >>= 1;
  }

  return i;
}

static void
g_hash_table_set_shift_from_size (GHashTable *hash_table, int size) {
  int shift;

  shift = g_hash_table_find_closest_shift(size);
  shift = MAX(shift, HASH_TABLE_MIN_SHIFT);

  g_hash_table_set_shift(hash_table, shift);
}

/*
 * g_hash_table_lookup_node:
 * @hash_table: our #GHashTable
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
 * The computed hash value is returned in the variable pointed to
 * by @hash_return. This is to save insertions from having to compute
 * the hash record again for the new record.
 *
 * Returns: index of the described node
 */
static inline uint32_t
g_hash_table_lookup_node (GHashTable    *hash_table,
                          paddr_t        key,
                          hash_entry_t  *ent_return,
                          uint32_t      *hash_return,
                          bool           force) {
  uint32_t node_index;
  uint32_t hash_value;
  uint32_t first_tombstone = 0;
  int have_tombstone = FALSE;
  uint32_t step = 0;
  hash_entry_t *buffer;
  hash_entry_t cur;

  hash_value = hash_table->hash_func(key);
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }

  //pthread_rwlock_rdlock(hash_table->cache_lock);

  *hash_return = hash_value;

  //node_index = hash_value % hash_table->mod;
  node_index = hash_value & hash_table->mask;
  pthread_rwlock_rdlock(hash_table->locks + node_index);

  /*
  nvram_read(hash_table, NV_IDX(node_index), &buffer, force);
  cur = buffer[BUF_IDX(node_index)];
  */
  nvram_read_entry(hash_table, node_index, &cur, force);
  //cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];

  while (!HASH_ENTRY_IS_EMPTY(cur)) {
    if (cur.key == key && HASH_ENTRY_IS_VALID(cur)) {
      *ent_return = cur;
      pthread_rwlock_unlock(hash_table->locks + node_index);
      //pthread_rwlock_unlock(hash_table->cache_lock);
      return node_index;
    } else if (HASH_ENTRY_IS_TOMBSTONE(cur) && !have_tombstone) {
      first_tombstone = node_index;
      have_tombstone = TRUE;
    }

    step++;
    uint32_t new_idx = (node_index + step) & hash_table->mask;
    //uint32_t new_idx = (node_index + step) % hash_table->mod;
    pthread_rwlock_unlock(hash_table->locks + node_index);
    pthread_rwlock_rdlock(hash_table->locks + new_idx);

    node_index = new_idx;
    /*
    nvram_read(hash_table, NV_IDX(node_index), &buffer, force);
    cur = buffer[BUF_IDX(node_index)];
    */
    nvram_read_entry(hash_table, node_index, &cur, force);
  }

  if (have_tombstone) {
    //*ent_return = hash_table->cache[NV_IDX(first_tombstone)][BUF_IDX(first_tombstone)];
    nvram_read_entry(hash_table, first_tombstone, ent_return, false);
    pthread_rwlock_unlock(hash_table->locks + node_index);
    //pthread_rwlock_unlock(hash_table->cache_lock);
    return first_tombstone;
  }

  //*ent_return = buffer[BUF_IDX(node_index)];
  //*ent_return = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];
  nvram_read_entry(hash_table, node_index, ent_return, false);
  pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return node_index;
}

/*
 * g_hash_table_remove_node:
 * @hash_table: our #GHashTable
 * @node: pointer to node to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 *
 * Removes a node from the hash table and updates the node count.
 * The node is replaced by a tombstone. No table resize is performed.
 *
 * If @notify is %TRUE then the destroy notify functions are called
 * for the key and value of the hash node.
 */
static void g_hash_table_remove_node (GHashTable  *hash_table,
                                      int          i,
                                      int          notify) {
  hash_entry_t ent;
  UNUSED(notify);

  //pthread_rwlock_wrlock(hash_table->locks + i);

  //nvram_read_entry(hash_table, i, &ent);
  HASH_ENTRY_SET_TOMBSTONE(ent);
  ent.size = 0;

  /* Erect tombstone */
  nvram_update(hash_table, i, &ent);

  hash_table->nnodes--;
  // update metadata on disk
  //nvram_write_metadata(hash_table, hash_table->size);

  //pthread_rwlock_unlock(hash_table->locks + i);

}

/*
 * g_hash_table_maybe_resize:
 * @hash_table: our #GHashTable
 *
 * Resizes the hash table, if needed.
 *
 * Essentially, calls g_hash_table_resize() if the table has strayed
 * too far from its ideal size for its number of nodes.
 *
 * iangneal: Hijacking this function to assure that we haven't over-committed.
 */
static inline void
g_hash_table_maybe_resize (GHashTable *hash_table) {
  int noccupied = hash_table->noccupied;
  int size = hash_table->size;

  assert(noccupied <= size);
#if 0
  if ((size > hash_table->nnodes * 4 && size > 1 << HASH_TABLE_MIN_SHIFT) ||
      (size <= noccupied + (noccupied / 16))) {
    g_hash_table_resize (hash_table);
  }
#endif
}

/**
 * g_hash_table_new:
 * @hash_func: a function to create a hash value from a key
 * @key_equal_func: a function to check two keys for equality
 *
 * Creates a new #GHashTable with a reference count of 1.
 *
 * Hash values returned by @hash_func are used to determine where keys
 * are stored within the #GHashTable data structure. The g_direct_hash(),
 * g_int_hash(), g_int64_hash(), g_double_hash() and g_str_hash()
 * functions are provided for some common types of keys.
 * If @hash_func is %NULL, g_direct_hash() is used.
 *
 * @key_equal_func is used when looking up keys in the #GHashTable.
 * The g_direct_equal(), g_int_equal(), g_int64_equal(), g_double_equal()
 * and g_str_equal() functions are provided for the most common types
 * of keys. If @key_equal_func is %NULL, keys are compared directly in
 * a similar fashion to g_direct_equal(), but without the overhead of
 * a function call. @key_equal_func is called with the key from the hash table
 * as its first parameter, and the user-provided key to check against as
 * its second.
 *
 * Returns: a new #GHashTable
 */
GHashTable *
g_hash_table_new (hash_func_t       hash_func,
                  size_t            max_entries,
                  size_t            block_size,
                  size_t            range_size,
                  paddr_t           metadata_location,
                  const idx_spec_t *idx_spec) {
  GHashTable *ht;

  ht = MALLOC(idx_spec, sizeof(*ht));

  if_then_panic(ht == NULL, "malloc callback failed!");

  ht->hash_func          = hash_func ? hash_func : g_direct_hash;
  ht->ref_count          = 1;
  ht->nnodes             = 0;
  ht->noccupied          = 0;
  ht->blksz              = block_size;
  // Number of entries in nvram.
  ht->nvram_size         = max_entries / range_size;
  ht->range_size         = range_size;
  ht->callbacks          = idx_spec->idx_callbacks;
  ht->metadata           = metadata_location;

  if (max_entries % range_size) {
    // If there's a partial range, need to not under-allocate.
    ht->nvram_size++;
  }

  g_hash_table_set_shift_from_size(ht, ht->nvram_size);
  // Make sure the mod size doesn't go out of range.
  ht->nvram_size = max(ht->nvram_size, ht->size);
  size_t scaled_size = ht->nvram_size;

  if (max_entries < scaled_size) {
    max_entries = scaled_size;
  }

  size_t nblocks = NV_IDX(ht, scaled_size);
  if (BUF_IDX(ht, scaled_size)) {
    // Partial block.
    nblocks++;
  }
  ht->nblocks = nblocks;

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

  if (!nvram_read_metadata(ht)) {
    ssize_t nalloc = CB(idx_spec, cb_alloc_metadata, nblocks * block_size, &(ht->data));

    if_then_panic(nalloc < nblocks * block_size, "no large contiguous region!");

    nvram_write_metadata(ht);
  }

  // cache
#ifdef HASHCACHE
  // cache lock
  ht->cache_lock = mem_fns->mm_malloc(sizeof(pthread_rwlock_t));
  assert(ht->cache_lock);
  int err = pthread_rwlock_init(ht->cache_lock, NULL);
  if_then_panic(err, "Could not init rwlock!");

  ht->cache = mem_fns->mm_malloc(nblocks * sizeof(hash_entry_t*));
  assert(ht->cache);

  // allocate cache bitmap -- unset for valid, set for invalid
  size_t cache_bitmap_size = (nblocks / BITS_PER_LONG) * sizeof(unsigned long);
  ht->cache_bitmap = mem_fns->mm_malloc(cache_bitmap_size);
  assert(ht->cache_bitmap);

  for (int i = 0; i < nblocks; ++i) {
    hash_entry_t *unused;
    ht->cache[i] = mem_fns->mm_malloc(block_size);
    // load from NVRAM (force flag)
    nvram_read(ht, i, &unused, 1);
  }

#endif

  return ht;
}

/*
 * g_hash_table_insert_node:
 * @hash_table: our #GHashTable
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
 * passed in via a g_hash_table_insert/replace) call, then @reusing_key
 * should be %TRUE.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static int
g_hash_table_insert_node (GHashTable    *hash_table,
                          uint32_t       node_index,
                          uint32_t       key_hash,
                          paddr_t   new_key,
                          paddr_t   new_value,
                          paddr_t   new_range)
{
  int already_exists;
  hash_entry_t ent;

  nvram_read_entry(hash_table, node_index, &ent, false);
  already_exists = HASH_ENTRY_IS_VALID(ent);

  // TODO consider bookkeeping (nnodes, noccupied?)
  if (unlikely(already_exists)) {
    printf("Already exists: %lx %lx (trying to insert: %lx %lx)\n",
        ent.key, HASH_ENTRY_VAL(ent), new_key, new_value);
  } else {
    ent.key = new_key;
    HASH_ENTRY_SET_VAL(ent, new_value);
    ent.size = new_range;
    nvram_update(hash_table, node_index, &ent);
  }

  return !already_exists;
}

/**
 * g_hash_table_destroy:
 * @hash_table: a #GHashTable
 *
 * Destroys all keys and values in the #GHashTable and decrements its
 * reference count by 1. If keys and/or values are dynamically allocated,
 * you should either free them first or create the #GHashTable with destroy
 * destruction phase.
 */
void
g_hash_table_destroy (GHashTable *hash_table)
{
  assert (hash_table != NULL);

  for (size_t i = 0; i < hash_table->nvram_size; ++i) {
    int err = pthread_rwlock_destroy(hash_table->locks + i);
    if (err) panic("Could not destroy rwlock!");
  }
}

/**
 * g_hash_table_lookup:
 * @hash_table: a #GHashTable
 * @key: the key to look up
 *
 * Looks up a key in a #GHashTable. Note that this function cannot
 * distinguish between a key that is not present and one which is present
 * and has the value %NULL. If you need this distinction, use
 * g_hash_table_lookup_extended().
 *
 * Returns: (nullable): the associated value, or %NULL if the key is not found
 */
void g_hash_table_lookup(GHashTable *hash_table, paddr_t key,
    paddr_t *val, paddr_t *size, bool force) {
  uint32_t node_index;
  uint32_t hash_return;
  hash_entry_t ent;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node(hash_table, key, &ent, &hash_return, force);

  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  paddr_t ent_val = HASH_ENTRY_VAL(ent);
  *val = !HASH_ENTRY_IS_TOMBSTONE(ent) ? ent_val : 0;
  *size = ent.size;

  //pthread_rwlock_unlock(hash_table->locks + node_index);
}

/*
 * g_hash_table_insert_internal:
 * @hash_table: our #GHashTable
 * @key: the key to insert
 * @value: the value to insert
 * @keep_new_key: if %TRUE and this key already exists in the table
 *   then call the destroy notify function on the old key.  If %FALSE
 *   then call the destroy notify function on the new key.
 *
 * Implements the common logic for the g_hash_table_insert() and
 * g_hash_table_replace() functions.
 *
 * Do a lookup of @key. If it is found, replace it with the new
 * @value (and perhaps the new @key). If it is not found, create
 * a new node.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static inline int
g_hash_table_insert_internal (GHashTable *hash_table,
                              paddr_t    key,
                              paddr_t    value,
                              paddr_t    size)
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
  uint32_t step = 0;
  hash_entry_t *buffer;
  hash_entry_t cur;

  assert (hash_table->ref_count > 0);

  hash_value = hash_table->hash_func(key);
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }
  //pthread_rwlock_rdlock(hash_table->cache_lock);

  //node_index = hash_value % hash_table->mod;
  node_index = hash_value & hash_table->mask;
  pthread_rwlock_wrlock(hash_table->locks + node_index);

  /*
  nvram_read(hash_table, NV_IDX(node_index), &buffer, FALSE);
  cur = buffer[BUF_IDX(node_index)];
  */
  nvram_read_entry(hash_table, node_index, &cur, false);

  while (!HASH_ENTRY_IS_EMPTY(cur)) {
    if (cur.key == key && HASH_ENTRY_IS_VALID(cur)) {
      break;
    } else if (HASH_ENTRY_IS_TOMBSTONE(cur) && !have_tombstone) {
      // keep lock until we decide we don't need it
      first_tombstone = node_index;
      have_tombstone = TRUE;
    } else {
      pthread_rwlock_unlock(hash_table->locks + node_index);
    }

    step++;
    uint32_t new_idx = (node_index + step) & hash_table->mask;
    //uint32_t new_idx = (node_index + step) % hash_table->mod;
    pthread_rwlock_wrlock(hash_table->locks + new_idx);

    node_index = new_idx;
    //cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];
    /*
    nvram_read(hash_table, NV_IDX(node_index), &buffer, FALSE);
    cur = buffer[BUF_IDX(node_index)];
    */
    nvram_read_entry(hash_table, node_index, &cur, false);
  }

  if (have_tombstone) {
    pthread_rwlock_unlock(hash_table->locks + node_index);
    node_index = first_tombstone;
  }

  int res = g_hash_table_insert_node(
      hash_table, node_index, hash_value, key, value, size);

  pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return res;
}

/**
 * g_hash_table_insert:
 * @hash_table: a #GHashTable
 * @key: a key to insert
 * @value: the value to associate with the key
 *
 * Inserts a new key and value into a #GHashTable.
 *
 * If the key already exists in the #GHashTable its current
 * value is replaced with the new value. If you supplied a
 * @value_destroy_func when creating the #GHashTable, the old
 * value is freed using that function. If you supplied a
 * @key_destroy_func when creating the #GHashTable, the passed
 * key is freed using that function.
 *
 * Returns: %TRUE if the key did not exist yet
 */
int
g_hash_table_insert (GHashTable *hash_table,
                     paddr_t     key,
                     paddr_t     value,
                     paddr_t     size)
{
  return g_hash_table_insert_internal (hash_table, key, value, size);
}

/**
 * g_hash_table_replace:
 * @hash_table: a #GHashTable
 * @key: a key to insert
 * @value: the value to associate with the key
 *
 * Inserts a new key and value into a #GHashTable similar to
 * g_hash_table_insert(). The difference is that if the key
 * already exists in the #GHashTable, it gets replaced by the
 * new key. If you supplied a @value_destroy_func when creating
 * the #GHashTable, the old value is freed using that function.
 * If you supplied a @key_destroy_func when creating the
 * #GHashTable, the old key is freed using that function.
 *
 * Returns: %TRUE if the key did not exist yet
 */
int
g_hash_table_replace (GHashTable *hash_table,
                      paddr_t key,
                      paddr_t value)
{
  return g_hash_table_insert_internal (hash_table, key, value, TRUE);
}

/*
 * g_hash_table_remove_internal:
 * @hash_table: our #GHashTable
 * @key: the key to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 * Returns: %TRUE if a node was found and removed, else %FALSE
 *
 * Implements the common logic for the g_hash_table_remove() and
 * g_hash_table_steal() functions.
 *
 * Do a lookup of @key and remove it if it is found, calling the
 * destroy notify handlers only if @notify is %TRUE.
 */
static int
g_hash_table_remove_internal (GHashTable    *hash_table,
                              paddr_t   key,
                              int            notify)
{
#if 0
  hash_entry_t ent;
  uint32_t node_index;
  uint32_t hash;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node (hash_table, key, &ent, &hash);

  if (!IS_VALID(ent.value)) return FALSE;

  g_hash_table_remove_node (hash_table, node_index, notify);

  return TRUE;
#else

  /*
   * iangneal: for concurrency reasons, we can't do lookup -> insert, as another
   * thread may come in and use the slot we just looked for.
   * This is basically a copy of lookup_node, but with write locks.
   */
  uint32_t node_index;
  uint32_t hash_value;
  uint32_t step = 0;
  hash_entry_t *buffer;
  hash_entry_t cur;

  assert (hash_table->ref_count > 0);

  hash_value = hash_table->hash_func(key);
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }

  //pthread_rwlock_rdlock(hash_table->cache_lock);
  //node_index = hash_value % hash_table->mod;
  node_index = hash_value & hash_table->mask;
  pthread_rwlock_wrlock(hash_table->locks + node_index);

  /*
  nvram_read(hash_table, NV_IDX(node_index), &buffer, FALSE);
  cur = buffer[BUF_IDX(node_index)];
  */
  nvram_read_entry(hash_table, node_index, &cur, false);
  /*
  cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];
  */

  while (!HASH_ENTRY_IS_EMPTY(cur)) {
    if (cur.key == key && HASH_ENTRY_IS_VALID(cur)) {
      break;
    }

    step++;
    uint32_t new_idx = (node_index + step) & hash_table->mask;
    //uint32_t new_idx = (node_index + step) % hash_table->mod;
    pthread_rwlock_unlock(hash_table->locks + node_index);
    pthread_rwlock_wrlock(hash_table->locks + new_idx);

    node_index = new_idx;
    //cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];
    /*
    nvram_read(hash_table, NV_IDX(node_index), &buffer, FALSE);
    cur = buffer[BUF_IDX(node_index)];
    */
    nvram_read_entry(hash_table, node_index, &cur, false);
  }

  if (HASH_ENTRY_IS_VALID(cur)) {
    g_hash_table_remove_node(hash_table, node_index, notify);
  }

  pthread_rwlock_unlock(hash_table->locks + node_index);

  //pthread_rwlock_unlock(hash_table->cache_lock);
  return HASH_ENTRY_IS_VALID(cur);
#endif
}

/**
 * g_hash_table_remove:
 * @hash_table: a #GHashTable
 * @key: the key to remove
 *
 * Removes a key and its associated value from a #GHashTable.
 *
 * If the #GHashTable was created using g_hash_table_new_full(), the
 * key and value are freed using the supplied destroy functions, otherwise
 * you have to make sure that any dynamically allocated values are freed
 * yourself.
 *
 * Returns: %TRUE if the key was found and removed from the #GHashTable
 */
int
g_hash_table_remove (GHashTable    *hash_table,
                     paddr_t  key)
{
  return g_hash_table_remove_internal (hash_table, key, TRUE);
}


/**
 * g_hash_table_size:
 * @hash_table: a #GHashTable
 *
 * Returns the number of elements contained in the #GHashTable.
 *
 * Returns: the number of key/value pairs in the #GHashTable.
 */
uint32_t g_hash_table_size (GHashTable *hash_table) {
  assert (hash_table != NULL);

  return hash_table->nnodes;
}

/*
 * Hash functions.
 */

