#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include "hash.h"

#include "common/common.h"
#ifdef __cplusplus
#define _Static_assert static_assert
#endif

#define ASSOC_NUM 4                       // The number of slots in a bucket
//#define KEY_LEN 16                        // The maximum length of a key
//#define VALUE_LEN 15                      // The maximum length of a value
#define KEY_LEN sizeof(laddr_t)
#define VALUE_LEN sizeof(paddr_t)

#pragma pack(push, 1)
typedef struct entry {                    // A slot storing a key-value item 
    //uint8_t key[KEY_LEN];
    //uint8_t value[VALUE_LEN];
    uint8_t e_idx;
    uint8_t e_size;
    laddr_t e_key;
    paddr_t e_val;
} entry_t;

//#define LH_MAX_SIZE UINT8_MAX
// iangneal: XXX more like how it actually works
#define LH_MAX_SIZE 1

typedef struct on_device_level_hash {  //
    paddr_t  dev_levels[2];            // API: Device location for the blocks.
    uint32_t dev_sizes[2];
    uint32_t level_item_num[2];        // The numbers of items stored in the top and bottom levels respectively

    uint32_t level_size;               // level_size = log2(addr_capacity)
    uint64_t f_seed;
    uint64_t s_seed;                   // Two randomized seeds for hash functions
} dev_level_hash_t;
_Static_assert(sizeof(dev_level_hash_t) <= 64, "level bucket is too big!");
#pragma pack(pop)

typedef struct level_bucket            // A bucket
{
    uint8_t token[ASSOC_NUM];          // A token indicates whether its corresponding slot is empty, which can also be implemented using 1 bit
    entry_t slot[ASSOC_NUM];
} level_bucket_t;

_Static_assert(sizeof(level_bucket_t) <= 64, "level bucket is too big!");

typedef struct level_hash_stats {
    STAT_FIELD(read_metadata);
    STAT_FIELD(read_entries);
    STAT_FIELD(compute_hash);
    STAT_FIELD(per_read);
    STAT_FIELD(per_loop);
    STAT_FIELD(misc_callbacks);
    size_t nchecked;
    size_t nreads;
    size_t nchecked_create;
    size_t nwrites;
} level_stats_t;

static void print_level_stats(level_stats_t *s) {
    printf("level hash stats: \n");
    PFIELD(s, read_metadata);
    PFIELD(s, compute_hash);
    PFIELD(s, misc_callbacks);
    PFIELD(s, per_read);
    PFIELD(s, per_loop);
    PFIELD(s, read_entries);
    printf("\tRead Ratio: %lu / %lu (%lf) \n", s->nchecked, s->nreads, 
            (double)s->nchecked / (double)s->nreads);
    printf("\tWrite Ratio: %lu / %lu (%lf) \n", s->nchecked_create, s->nwrites, 
            (double)s->nchecked_create / (double)s->nwrites);
}

#define METADATA_CACHING
//#undef METADATA_CACHING

extern level_stats_t level_stats;

typedef struct level_hash {            // A Level hash table
    level_bucket_t *buckets[2];        // The top level and bottom level in the Level hash table
    int8_t *cache_state[2];            // API: Tri-state (-1 for not present, 0 for clean, 1 dirty)
    int8_t meta_cache_state;           // API: Cache state for metadata.
    paddr_t dev_levels[2];             // API: Device location for the blocks.
    uint64_t level_item_num[2];        // The numbers of items stored in the top and bottom levels respectively
    uint64_t addr_capacity;            // The number of buckets in the top level
    uint64_t total_capacity;           // The number of all buckets in the Level hash table    
    uint64_t level_size;               // level_size = log2(addr_capacity)
    uint8_t level_expand_time;         // Indicate whether the Level hash table was expanded, ">1 or =1": Yes, "0": No;
    uint8_t resize_state;              // Indicate the resizing state of the level hash table, ‘0’ means the hash table is not during resizing; 
                                       // ‘1’ means the hash table is being expanded; ‘2’ means the hash table is being shrunk.

    // NVM direct access
    char *dev_ptr;
    dev_level_hash_t *ondev;

    /* API shenanigans */
    const idx_spec_t *idx_spec;        // Callbacks for memory, persistence
    bool do_cache;                     // If not cache, reread all the time.
    size_t block_size;                 // size of block on underlying device.
    paddr_range_t range;
    bool reread_meta;                  // If we need to reread metadata or not.
    // stats struct
    bool enable_stats;
} level_hash_t;

#define level_block(l, w) ((l)->ondev->dev_levels[w])
#define level_offset(l, w) (level_block(l, w) * (l)->block_size)
#define level_bucket(l, w) ((level_bucket_t*)((l)->dev_ptr + level_offset(l, w)))

int read_metadata(const idx_spec_t *idx_spec, 
                  const paddr_range_t *loc, 
                  dev_level_hash_t **dhash);

int reread_metadata(level_hash_t *level);
int write_metadata(level_hash_t *level);

level_hash_t *level_init(const idx_spec_t *idx_spec, 
                         const paddr_range_t *loc,
                         uint64_t level_size);     

uint8_t level_insert(level_hash_t *level, laddr_t key, paddr_t value, 
                     size_t idx, size_t size);          

size_t level_static_query(level_hash_t *level, laddr_t key, paddr_t *value);

size_t level_dynamic_query(level_hash_t *level, laddr_t key, paddr_t *value);

uint8_t level_delete(level_hash_t *level, laddr_t key, paddr_t *old_paddr, 
                     size_t *old_idx, size_t *old_size);

uint8_t level_update(level_hash_t *level, laddr_t key, 
                     size_t new_idx, size_t new_size);

void level_expand(level_hash_t *level);

void level_shrink(level_hash_t *level);

uint8_t try_movement(level_hash_t *level, uint64_t idx, uint64_t level_num, 
                     laddr_t key, paddr_t value, size_t p_idx, size_t size);

int b2t_movement(level_hash_t *level, uint64_t idx);

void level_destroy(level_hash_t *level);

/* 
 * API functions 
 */

/* Level hash table bulk persist */
int level_persist(level_hash_t *level);
int level_cache_invalidate(level_hash_t *level);
