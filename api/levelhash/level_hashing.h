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

#define ASSOC_NUM 4                       // The number of slots in a bucket
//#define KEY_LEN 16                        // The maximum length of a key
//#define VALUE_LEN 15                      // The maximum length of a value
#define KEY_LEN sizeof(laddr_t)
#define VALUE_LEN sizeof(paddr_t)

typedef struct entry {                    // A slot storing a key-value item 
    //uint8_t key[KEY_LEN];
    //uint8_t value[VALUE_LEN];
    laddr_t e_key;
    paddr_t e_val;
} entry_t;

#define MAGIC 0xcafebabe
typedef struct on_device_level_hash {  //
    uint64_t init_magic;               // So we know if stuff is initialized.
    paddr_t  dev_levels[2];            // API: Device location for the blocks.
    size_t   dev_sizes[2];
    uint64_t level_item_num[2];        // The numbers of items stored in the top and bottom levels respectively

    uint64_t addr_capacity;            // The number of buckets in the top level
    uint64_t total_capacity;           // The number of all buckets in the Level hash table    
    uint64_t level_size;               // level_size = log2(addr_capacity)
    uint64_t f_seed;
    uint64_t s_seed;                   // Two randomized seeds for hash functions
    size_t   block_size;               // size of block on underlying device.
} dev_level_hash_t;

typedef struct level_bucket            // A bucket
{
    uint8_t token[ASSOC_NUM];          // A token indicates whether its corresponding slot is empty, which can also be implemented using 1 bit
    entry_t slot[ASSOC_NUM];
} level_bucket_t;

typedef struct level_hash {            // A Level hash table
    level_bucket_t *buckets[2];        // The top level and bottom level in the Level hash table
    int8_t *cache_state[2];            // API: Tri-state (-1 for not present, 0 for clean, 1 dirty)
    paddr_t dev_levels[2];             // API: Device location for the blocks.
    size_t  dev_sizes[2];
    uint64_t level_item_num[2];        // The numbers of items stored in the top and bottom levels respectively
    uint64_t addr_capacity;            // The number of buckets in the top level
    uint64_t total_capacity;           // The number of all buckets in the Level hash table    
    uint64_t level_size;               // level_size = log2(addr_capacity)
    uint8_t level_expand_time;         // Indicate whether the Level hash table was expanded, ">1 or =1": Yes, "0": No;
    uint8_t resize_state;              // Indicate the resizing state of the level hash table, ‘0’ means the hash table is not during resizing; 
                                       // ‘1’ means the hash table is being expanded; ‘2’ means the hash table is being shrunk.
    uint64_t f_seed;
    uint64_t s_seed;                   // Two randomized seeds for hash functions

    /* API shenanigans */
    const idx_spec_t *idx_spec;        // Callbacks for memory, persistence
    bool do_cache;                     // If not cache, reread all the time.
    size_t block_size;                 // size of block on underlying device.
    paddr_range_t range;
} level_hash_t;

int read_metadata(const idx_spec_t *idx_spec, 
                  const paddr_range_t *loc, 
                  dev_level_hash_t *dhash);

int write_metadata(level_hash_t *level);

level_hash_t *level_init(const idx_spec_t *idx_spec, 
                         const paddr_range_t *loc,
                         uint64_t level_size);     

uint8_t level_insert(level_hash_t *level, laddr_t key, paddr_t value);          

paddr_t level_static_query(level_hash_t *level, laddr_t key);

paddr_t level_dynamic_query(level_hash_t *level, laddr_t key);

uint8_t level_delete(level_hash_t *level, laddr_t key);

uint8_t level_update(level_hash_t *level, laddr_t key, paddr_t new_value);

void level_expand(level_hash_t *level);

void level_shrink(level_hash_t *level);

uint8_t try_movement(level_hash_t *level, uint64_t idx, uint64_t level_num, 
                     laddr_t key, paddr_t value);

int b2t_movement(level_hash_t *level, uint64_t idx);

void level_destroy(level_hash_t *level);

/* 
 * API functions 
 */

/* Level hash table bulk persist */
int level_persist(level_hash_t *level);
int level_cache_invalidate(level_hash_t *level);
