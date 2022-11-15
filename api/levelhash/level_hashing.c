#include "level_hashing.h"
#include "common/hash_functions.h"

level_stats_t level_stats = {0, };

/*
Function: F_HASH()
        Compute the first hash value of a key-value item
*/
uint64_t F_HASH(level_hash_t *level, laddr_t key) {
    DECLARE_TIMING();
    if (level->enable_stats) START_TIMING();
    //uint64_t h = (hash((void *)&key, sizeof(key), level->f_seed));
    //uint64_t h = ((uint64_t)key) ^ level->f_seed;
    uint64_t h = mix_seed((paddr_t)key, level->ondev->f_seed);
    if (level->enable_stats) UPDATE_TIMING(&level_stats, compute_hash);
    return h;
}

/*
Function: S_HASH() 
        Compute the second hash value of a key-value item
*/
uint64_t S_HASH(level_hash_t *level, laddr_t key) {
    DECLARE_TIMING();
    if (level->enable_stats) START_TIMING();
    //uint64_t h = (hash((void *)&key, sizeof(key), level->s_seed));
    //uint64_t h = ((uint64_t)key) ^ level->s_seed;
    uint64_t h = mix_seed((paddr_t)key, level->ondev->s_seed);
    if (level->enable_stats) UPDATE_TIMING(&level_stats, compute_hash);
    return h;
}

/*
Function: F_IDX() 
        Compute the second hash location
*/
uint64_t F_IDX(uint64_t hashKey, uint64_t capacity) {
    return hashKey % (capacity / 2);
}

/*
Function: S_IDX() 
        Compute the second hash location
*/
uint64_t S_IDX(uint64_t hashKey, uint64_t capacity) {
    return (hashKey % (capacity / 2)) + (capacity / 2);
}

/***
 * API related things.
 */
paddr_t idx_to_paddr_blkno(level_hash_t *level, uint64_t bucket_idx) {
    uint64_t bucket_idx_bytes = bucket_idx * sizeof(level_bucket_t);
    return bucket_idx_bytes / level->block_size;
}

off_t idx_to_paddr_offset(level_hash_t *level, uint64_t bucket_idx) {
    uint64_t bucket_idx_bytes = bucket_idx * sizeof(level_bucket_t);
    return bucket_idx_bytes % level->block_size;
}

int do_bucket_read(level_hash_t *level, int which, uint64_t bucket_idx) {
    if (!level->do_cache) return 0;

    paddr_t paddr  = level->dev_levels[which] + 
                     idx_to_paddr_blkno(level, bucket_idx);
    off_t   offset = idx_to_paddr_offset(level, bucket_idx);
    ssize_t ret    = CB(level->idx_spec, cb_read,
                        paddr, offset, sizeof(level_bucket_t), 
                        (char*)(level->buckets[which] + bucket_idx)); 

    if (ret != sizeof(level_bucket_t)) return -EIO;

    return 0;
}

int do_bucket_write(level_hash_t *level, int which, uint64_t bucket_idx) {
    if (!level->do_cache) return 0;

    paddr_t paddr  = level->dev_levels[which] + 
                     idx_to_paddr_blkno(level, bucket_idx);
    off_t   offset = idx_to_paddr_offset(level, bucket_idx);
    ssize_t ret    = CB(level->idx_spec, cb_write,
                        paddr, offset, sizeof(level_bucket_t), 
                        (const char*)(level->buckets[which] + bucket_idx)); 

    if (ret != sizeof(level_bucket_t)) return -EIO;

    return 0;
}

int get_buckets(level_hash_t *level, int which) {
    ssize_t ret = CB(level->idx_spec, cb_get_addr,
                     level->dev_levels[which], 0, (char**)&(level->buckets[which]));
    return (int)ret;
}

/*
 * This one is for reads. Writes are explicit persist calls.
 */
int ensure_bucket_uptodate(level_hash_t *level, int l, uint64_t bucket_idx) {
    int ret;
    DECLARE_TIMING();
    if (level->enable_stats) START_TIMING();

    if (level->do_cache && level->cache_state[l][bucket_idx] < 0) {
        ret = do_bucket_read(level, l, bucket_idx);
        level->cache_state[l][bucket_idx] = 0;
    } else {
        ret = get_buckets(level, l);
    }

    if (level->enable_stats) UPDATE_TIMING(&level_stats, misc_callbacks);

    return ret;
}

/*
 * For writes. Non-cached this does persist, for cache this marks dirty.
 */
int mark_bucket_dirty(level_hash_t *level, int l, uint64_t bucket_idx) {
    if (!level->do_cache) return 0;

    int ret = do_bucket_write(level, l, bucket_idx);
    level->cache_state[l][bucket_idx] = 1;
    return ret;
}

int mark_new_bucket_dirty(level_hash_t *level,
                          level_bucket_t *new_bucket,
                          int8_t *cache_state,
                          paddr_t new_bucket_addr,
                          uint64_t bucket_idx) {
    if (!level->do_cache) return 0;

    paddr_t paddr  = new_bucket_addr +
                     idx_to_paddr_blkno(level, bucket_idx);
    off_t   offset = idx_to_paddr_offset(level, bucket_idx);
    ssize_t ret    = CB(level->idx_spec, cb_write,
                        paddr, offset, sizeof(level_bucket_t), 
                        (const char*)(new_bucket + bucket_idx)); 

    if (ret != sizeof(level_bucket_t)) ret = -EIO;

    cache_state[bucket_idx] = 1;
    return (int)ret;
}

/*
 * Only called during explicit cache operation.
 */
int persist_dirty_bucket(level_hash_t *level, int l, uint64_t bucket_idx) {
    int ret = 0;

    if (level->do_cache && level->cache_state[l][bucket_idx] > 0){
        ret = do_bucket_write(level, l, bucket_idx);
        level->cache_state[l][bucket_idx] = 0;
    } 

    return ret;
}

/*
Function: generate_seeds() 
        Generate two randomized seeds for hash functions
*/
void generate_seeds(level_hash_t *level)
{
    srand(time(NULL));
    do
    {
        level->ondev->f_seed = (((uint64_t) rand() <<  0) & 0x000000000000FFFFull) | 
                        (((uint64_t) rand() << 16) & 0x00000000FFFF0000ull) | 
                        (((uint64_t) rand() << 32) & 0x0000FFFF00000000ull) |
                        (((uint64_t) rand() << 48) & 0xFFFF000000000000ull);
        level->ondev->s_seed = (((uint64_t) rand() <<  0) & 0x000000000000FFFFull) | 
                        (((uint64_t) rand() << 16) & 0x00000000FFFF0000ull) | 
                        (((uint64_t) rand() << 32) & 0x0000FFFF00000000ull) |
                        (((uint64_t) rand() << 48) & 0xFFFF000000000000ull);
    } while (level->ondev->f_seed == level->ondev->s_seed);

    nvm_persist_struct_ptr(level->ondev);
}


static inline 
int get_ondev_ptr(const idx_spec_t *idx_spec, 
                  const paddr_range_t *loc, 
                  dev_level_hash_t **dhash) {
    if_then_panic(loc->pr_nbytes < sizeof(**dhash), 
                  "region is too small! Only have %lu bytes, but need %lu!\n",
                  loc->pr_nbytes, sizeof(**dhash));

    ssize_t ret = CB(idx_spec, cb_get_addr,
                     loc->pr_start, loc->pr_blk_offset, (char**)dhash);

    return (int)ret;
}



/*
Function: level_init() 
        Initialize a level hash table
*/
level_hash_t *level_init(const idx_spec_t *idx_spec, 
                         const paddr_range_t *loc,
                         uint64_t level_size) {
    level_hash_t *level = ZALLOC(idx_spec, sizeof(level_hash_t));
    if (!level) {
        printf("The level hash table initialization fails:1\n");
        exit(1);
    }

    int ret = get_ondev_ptr(idx_spec, loc, &level->ondev);

    if (ret) {
        printf("The level hash table initialization fails:2\n");
        exit(1);
    }

    bool already_exists = !!level->ondev->dev_levels[0] && 
                          !!level->ondev->dev_levels[1];

    ssize_t sret = CB(idx_spec, cb_get_addr, 0, 0, (char**)&(level->dev_ptr));
    if_then_panic(sret, "init failed: 3");


    level->range = *loc;
    
    if (!already_exists) {
        generate_seeds(level);
    }

    level->do_cache = false;
    
    size_t top_size = pow(2, level_size);
    size_t top_size_bytes = top_size * sizeof(level_bucket_t);
    size_t bot_size = pow(2, level_size - 1);
    size_t bot_size_bytes = bot_size * sizeof(level_bucket_t);

    level->level_expand_time = 0;
    level->resize_state = 0;

    /* API Init */
    level->idx_spec = idx_spec;
    level->enable_stats = false;
    //level->stats = ZALLOC(idx_spec, sizeof(*level->stats));
    memset(&level_stats, 0, sizeof(level_stats));

    device_info_t di;
    int err = CB(idx_spec, cb_get_dev_info, &di);
    if (err) {
        printf("The level hash table initialization fails: 3\n");
        return NULL;
    }

    level->block_size = di.di_block_size;

    size_t top_blocks = top_size_bytes / level->block_size;
    if (top_size_bytes % level->block_size != 0) top_blocks++;
    size_t bot_blocks = bot_size_bytes / level->block_size;
    if (bot_size_bytes % level->block_size != 0) bot_blocks++;

    if (!already_exists) {
        level->ondev->level_size = level_size;
        level->ondev->dev_sizes[0] = top_blocks;
        level->ondev->dev_sizes[1] = bot_blocks;

        paddr_t top_block_start;
        ssize_t top_blocks_alloced = CB(idx_spec, cb_alloc_metadata,
                                        top_blocks, &top_block_start);
        if_then_panic(top_blocks_alloced != top_blocks, "could not alloc!");
        level->ondev->dev_levels[0] = top_block_start;

        paddr_t bot_block_start;
        ssize_t bot_blocks_alloced = CB(idx_spec, cb_alloc_metadata,
                                        bot_blocks, &bot_block_start);
        if_then_panic(bot_blocks_alloced != bot_blocks, "could not alloc!");
        level->ondev->dev_levels[1] = bot_block_start;

        nvm_persist_struct(level->ondev);
    }

    level->addr_capacity = pow(2, level->ondev->level_size);
    level->total_capacity = pow(2, level->ondev->level_size) 
                            + pow(2, level->ondev->level_size - 1);

    return level;
}

/*
Function: level_expand()
        Expand a level hash table in place;
        Put a new level on top of the old hash table and only rehash the
        items in the bottom level of the old hash table;
*/
void level_expand(level_hash_t *level) 
{
    if (!level) {
        printf("The expanding fails: 1\n");
        exit(1);
    }

    level->resize_state = 1;
    level->addr_capacity = pow(2, level->ondev->level_size + 1);
    size_t new_buckets_bytes = level->addr_capacity * sizeof(level_bucket_t);
    size_t new_buckets_blocks = new_buckets_bytes / level->block_size;
    if(new_buckets_bytes % level->block_size != 0) new_buckets_blocks++;
    //level_bucket_t *newBuckets = alignedmalloc(level->addr_capacity*sizeof(level_bucket_t));

    paddr_t new_buckets_paddr;
    ssize_t nalloced = CB(level->idx_spec, cb_alloc_metadata,
                          new_buckets_blocks, &new_buckets_paddr);
    if (nalloced != new_buckets_blocks) {
        // try again
        paddr_t to_delete = new_buckets_paddr;
        ssize_t old_size = nalloced;
        nalloced = CB(level->idx_spec, cb_alloc_metadata,
                      new_buckets_blocks, &new_buckets_paddr);
        ssize_t ndeleted = CB(level->idx_spec, cb_dealloc_metadata,
                              old_size, to_delete);
        if_then_panic(ndeleted != old_size, "could not delete!");
    }

    if_then_panic(nalloced != new_buckets_blocks, 
            "could not alloc metadata! Wanted %lu, got %lu!\n",
            new_buckets_blocks, nalloced);

    level_bucket_t *newBuckets;
    int8_t *new_cache_state;

    (void)CB(level->idx_spec, cb_get_addr, new_buckets_paddr, 0, (char**)&newBuckets);
    pmem_memset_persist(newBuckets, 0, new_buckets_bytes);

    uint64_t new_level_item_num = 0;
    
    uint64_t old_idx;
    for (old_idx = 0; old_idx < pow(2, level->ondev->level_size - 1); old_idx ++) {
        uint64_t i, j;
        for(i = 0; i < ASSOC_NUM; i ++){
            if (level_bucket(level, 1)[old_idx].token[i] == 1)
            {
                laddr_t  key   = level_bucket(level, 1)[old_idx].slot[i].e_key;
                paddr_t  value = level_bucket(level, 1)[old_idx].slot[i].e_val;
                size_t   size  = level_bucket(level, 1)[old_idx].slot[i].e_size;
                size_t   idx   = level_bucket(level, 1)[old_idx].slot[i].e_idx;
                uint64_t f_idx = F_IDX(F_HASH(level, key), level->addr_capacity);
                uint64_t s_idx = S_IDX(S_HASH(level, key), level->addr_capacity);

                uint8_t insertSuccess = 0;
                for(j = 0; j < ASSOC_NUM; j ++){                            
                    /*  The rehashed item is inserted into the less-loaded bucket between 
                        the two hash locations in the new level
                    */
                    if (newBuckets[f_idx].token[j] == 0) {
                        newBuckets[f_idx].slot[j].e_key  = key;
                        newBuckets[f_idx].slot[j].e_val  = value;
                        newBuckets[f_idx].slot[j].e_size = size;
                        newBuckets[f_idx].slot[j].e_idx  = idx;
                        newBuckets[f_idx].token[j] = 1;
                        insertSuccess = 1;
                        new_level_item_num ++;

                        nvm_persist_struct(newBuckets[f_idx]);
                        break;
                    }
                    if (newBuckets[s_idx].token[j] == 0) {
                        newBuckets[s_idx].slot[j].e_key  = key;
                        newBuckets[s_idx].slot[j].e_val  = value;
                        newBuckets[s_idx].slot[j].e_size = size;
                        newBuckets[s_idx].slot[j].e_idx  = idx;
                        newBuckets[s_idx].token[j] = 1;
                        insertSuccess = 1;
                        new_level_item_num ++;

                        nvm_persist_struct(newBuckets[s_idx]);
                        break;
                    }
                }

                if(!insertSuccess){
                    printf("The expanding fails: 3\n");
                    printf("\tWas expanding to level size %u\n",
                            level->ondev->level_size + 1);
                    exit(1);                    
                }
               
                /*
                level_bucket(level, 1)[old_idx].token[i] = 0;
                int oerr = mark_bucket_dirty(level, 1, old_idx);
                if_then_panic(oerr, "couldn't write!");
                */
            }
        }
    }

    level->ondev->level_size ++;
    level->total_capacity = pow(2, level->ondev->level_size) + pow(2, level->ondev->level_size - 1);

    if (level->do_cache) {
        FREE(level->idx_spec, level->buckets[1]);
        FREE(level->idx_spec, level->cache_state[1]);
    }

    ssize_t freed = CB(level->idx_spec, cb_dealloc_metadata,
                       level->ondev->dev_sizes[1], level->ondev->dev_levels[1]); 
    if_then_panic(freed != level->ondev->dev_sizes[1], "could not free metadata!");

    level->ondev->dev_levels[1] = level->ondev->dev_levels[0];
    level->ondev->dev_levels[0] = new_buckets_paddr;

    level->ondev->dev_sizes[1] = level->ondev->dev_sizes[0];
    level->ondev->dev_sizes[0] = new_buckets_blocks;
    
    level->ondev->level_item_num[1] = level->ondev->level_item_num[0];
    level->ondev->level_item_num[0] = new_level_item_num;

    level->level_expand_time++;
    level->resize_state = 0;

    nvm_persist_struct_ptr(level->ondev);
}

/*
Function: level_shrink()
        Shrink a level hash table in place;
        Put a new level at the bottom of the old hash table and only rehash the
        items in the top level of the old hash table;
*/
void level_shrink(level_hash_t *level)
{
    if_then_panic(!level, "level hash table cannot be null!");

    #define SHRINK_THRESHOLD 0.2
    // The shrinking is performed only when the hash table has very few items.
    if(level->ondev->level_item_num[0] + level->ondev->level_item_num[1] 
            > level->total_capacity*ASSOC_NUM*SHRINK_THRESHOLD || 
       level->total_capacity*ASSOC_NUM*SHRINK_THRESHOLD == 0){
        return;
    }

    if ((level->ondev->level_size - 1) <= 2) return;

    size_t new_size = pow(2, level->ondev->level_size - 1);
    size_t new_buckets_bytes = new_size * sizeof(level_bucket_t);
    size_t new_buckets_blocks = new_buckets_bytes / level->block_size;
    if(new_buckets_bytes % level->block_size != 0) new_buckets_blocks++;

    paddr_t new_buckets_paddr;
    ssize_t nalloced = CB(level->idx_spec, cb_alloc_metadata,
                          new_buckets_blocks, &new_buckets_paddr);
    if (nalloced != new_buckets_blocks) {
        ssize_t err = CB(level->idx_spec, cb_dealloc_metadata,
                         nalloced, new_buckets_paddr);
        if_then_panic(err != nalloced, "Could not dealloc!");
        //printf("Warning, could not shrink!\IAsked for %llu blocks, got %llu\n",
        //        new_buckets_blocks, nalloced);
        return;
    }

    level->resize_state = 2;
    level->ondev->level_size--;

    level_bucket_t *newBuckets = ((level_bucket_t*)(level->dev_ptr 
                + (new_buckets_paddr * level->block_size)));

    level_bucket_t *interimBuckets = level_bucket(level, 0);

    level->ondev->level_item_num[0] = level->ondev->level_item_num[1];
    level->ondev->level_item_num[1] = 0;

    level->addr_capacity = pow(2, level->ondev->level_size);
    level->total_capacity = pow(2, level->ondev->level_size) +
                            pow(2, level->ondev->level_size - 1);

    size_t interimDevSize = level->ondev->dev_sizes[0];
    level->ondev->dev_sizes[0] = level->ondev->dev_sizes[1];
    level->ondev->dev_sizes[1] = new_buckets_blocks;

    paddr_t interimDevLoc = level->ondev->dev_levels[0];
    level->ondev->dev_levels[0] = level->ondev->dev_levels[1];
    level->ondev->dev_levels[1] = new_buckets_paddr;

    nvm_persist_struct_ptr(level->ondev);

    uint64_t old_idx, i;
    for (old_idx = 0; old_idx < pow(2, level->ondev->level_size+1); old_idx ++) {
        for(i = 0; i < ASSOC_NUM; i ++){
            if (interimBuckets[old_idx].token[i] == 1)
            {
                if(level_insert(level, interimBuckets[old_idx].slot[i].e_key, 
                                       interimBuckets[old_idx].slot[i].e_val,
                                       interimBuckets[old_idx].slot[i].e_idx,
                                       interimBuckets[old_idx].slot[i].e_size))
                {
                    printf("The shrinking fails: 3\n");
                    exit(1);   
                }

                interimBuckets[old_idx].token[i] = 0;

                nvm_persist_struct(interimBuckets[old_idx]);
            }
        }
    } 

    ssize_t nfreed = CB(level->idx_spec, cb_dealloc_metadata,
                        interimDevSize, interimDevLoc);
    if_then_panic(nfreed != interimDevSize, "could not dealloc!");
    level->level_expand_time = 0;
    level->resize_state = 0;
}

/*
Function: level_dynamic_query() 
        Lookup a key-value item in level hash table via danamic search scheme;
        First search the level with more items;
*/
size_t level_dynamic_query(level_hash_t *level, laddr_t key, paddr_t *value)
{  
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);

    *value = 0;

    level->addr_capacity = pow(2, level->ondev->level_size);
    level->total_capacity = pow(2, level->ondev->level_size) 
                            + pow(2, level->ondev->level_size - 1);

    uint64_t i, j, f_idx, s_idx;
    if (level->enable_stats) INCR_STAT(&level_stats, nreads);

    if(level->level_item_num[0] > level->level_item_num[1]){
        f_idx = F_IDX(f_hash, level->addr_capacity);
        s_idx = S_IDX(s_hash, level->addr_capacity); 

        for(i = 0; i < 2; i ++){
            if (level->do_cache) {
                int f_err = ensure_bucket_uptodate(level, i, f_idx);
                if (f_err) return 0;
            }

            for(j = 0; j < ASSOC_NUM; j ++){
                DECLARE_TIMING();
                if (level->enable_stats) {
                    START_TIMING();
                    INCR_STAT(&level_stats, nchecked);
                }

                if (level_bucket(level, i)[f_idx].token[j] == 1 &&
                    level_bucket(level, i)[f_idx].slot[j].e_key == key)
                {
                    *value = level_bucket(level, i)[f_idx].slot[j].e_val;
                    if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
                    return level_bucket(level, i)[f_idx].slot[j].e_size;
                }

                if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
            }

            if (level->do_cache) {
                int s_err = ensure_bucket_uptodate(level, i, s_idx);
                if (s_err) return 0;
            }

            for(j = 0; j < ASSOC_NUM; j ++) {
                DECLARE_TIMING();
                if (level->enable_stats) {
                    START_TIMING();
                    INCR_STAT(&level_stats, nchecked);
                }

                if (level_bucket(level, i)[s_idx].token[j] == 1 &&
                    level_bucket(level, i)[s_idx].slot[j].e_key == key) 
                {
                    *value = level_bucket(level, i)[s_idx].slot[j].e_val;
                    if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
                    return level_bucket(level, i)[s_idx].slot[j].e_size;
                }

                if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
            }

            f_idx = F_IDX(f_hash, level->addr_capacity / 2);
            s_idx = S_IDX(s_hash, level->addr_capacity / 2);
        }
    } else {
        f_idx = F_IDX(f_hash, level->addr_capacity/2);
        s_idx = S_IDX(s_hash, level->addr_capacity/2);

        for(i = 2; i > 0; i --){
            int f_err = ensure_bucket_uptodate(level, i-1, f_idx);
            if (f_err) return 0;
            for(j = 0; j < ASSOC_NUM; j ++){
                DECLARE_TIMING();
                if (level->enable_stats) {
                    START_TIMING();
                    INCR_STAT(&level_stats, nchecked);
                }

                if (level_bucket(level, i-1)[f_idx].token[j] == 1 &&
                    level_bucket(level, i-1)[f_idx].slot[j].e_key == key)
                {
                    *value = level_bucket(level, i-1)[f_idx].slot[j].e_val;
                    if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
                    return level_bucket(level, i-1)[f_idx].slot[j].e_size;
                }

                if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
            }

            int s_err = ensure_bucket_uptodate(level, i-1, s_idx);
            if (s_err) return 0;
            for(j = 0; j < ASSOC_NUM; j ++){
                DECLARE_TIMING();
                if (level->enable_stats) {
                    START_TIMING();
                    INCR_STAT(&level_stats, nchecked);
                }

                if (level_bucket(level, i-1)[s_idx].token[j] == 1 &&
                    level_bucket(level, i-1)[s_idx].slot[j].e_key == key)
                {
                    *value = level_bucket(level, i-1)[s_idx].slot[j].e_val;
                    if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
                    return level_bucket(level, i-1)[s_idx].slot[j].e_size;
                }

                if (level->enable_stats) UPDATE_TIMING(&level_stats, per_read);
            }
            f_idx = F_IDX(f_hash, level->addr_capacity);
            s_idx = S_IDX(s_hash, level->addr_capacity);
        }
    }

    return 0;
}

/*
Function: level_static_query() 
        Lookup a key-value item in level hash table via static search scheme;
        Always first search the top level and then search the bottom level;
*/
size_t level_static_query(level_hash_t *level, laddr_t key, paddr_t *value)
{
    level->addr_capacity = pow(2, level->ondev->level_size);
    level->total_capacity = pow(2, level->ondev->level_size) 
                            + pow(2, level->ondev->level_size - 1);

    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);

    *value = 0;
    
    uint64_t i, j;
    if (level->enable_stats) INCR_STAT(&level_stats, nreads);
    for(i = 0; i < 2; i ++){
        int f_err = ensure_bucket_uptodate(level, i, f_idx);
        if (f_err) return 0;
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->enable_stats) INCR_STAT(&level_stats, nchecked);
            if (level_bucket(level, i)[f_idx].token[j] == 1 &&
                level_bucket(level, i)[f_idx].slot[j].e_key == key)
            {
                *value = level_bucket(level, i)[f_idx].slot[j].e_val;
                return level_bucket(level, i)[f_idx].slot[j].e_size;
            }
        }

        int s_err = ensure_bucket_uptodate(level, i, s_idx);
        if (s_err) return 0;
        for (j = 0; j < ASSOC_NUM; j++) {
            if (level->enable_stats) INCR_STAT(&level_stats, nchecked);
            if (level_bucket(level, i)[s_idx].token[j] == 1 &&
                level_bucket(level, i)[s_idx].slot[j].e_key == key)
            {
                *value = level_bucket(level, i)[s_idx].slot[j].e_val;
                return level_bucket(level, i)[s_idx].slot[j].e_size;
            }
        }
        f_idx = F_IDX(f_hash, level->addr_capacity / 2);
        s_idx = S_IDX(s_hash, level->addr_capacity / 2);
    }

    return 0;
}


/*
Function: level_delete() 
        Remove a key-value item from level hash table;
        The function can be optimized by using the dynamic search scheme
*/
uint8_t level_delete(level_hash_t *level, laddr_t key, 
                     paddr_t *old_val, size_t *old_idx, size_t *old_size)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);
    
    uint64_t i, j;
    for(i = 0; i < 2; i ++){
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level_bucket(level, i)[f_idx].token[j] == 1 && 
                level_bucket(level, i)[f_idx].slot[j].e_key ==  key)
            {
                *old_val  = level_bucket(level, i)[f_idx].slot[j].e_val;
                *old_idx  = level_bucket(level, i)[f_idx].slot[j].e_idx;
                *old_size = level_bucket(level, i)[f_idx].slot[j].e_size;
                level_bucket(level, i)[f_idx].token[j] = 0;
                level->ondev->level_item_num[i] --;

                nvm_persist_struct(level_bucket(level, i)[f_idx].token[j]);
                nvm_persist_struct_ptr(level->ondev);
                return 0;
            }
        }
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level_bucket(level, i)[s_idx].token[j] == 1 &&
                level_bucket(level, i)[s_idx].slot[j].e_key == key)
            {
                *old_val  = level_bucket(level, i)[s_idx].slot[j].e_val;
                *old_idx  = level_bucket(level, i)[s_idx].slot[j].e_idx;
                *old_size = level_bucket(level, i)[s_idx].slot[j].e_size;
                level_bucket(level, i)[s_idx].token[j] = 0;
                level->ondev->level_item_num[i] --;

                nvm_persist_struct(level_bucket(level, i)[s_idx].token[j]);
                nvm_persist_struct_ptr(level->ondev);
                return 0;
            }
        }
        f_idx = F_IDX(f_hash, level->addr_capacity / 2);
        s_idx = S_IDX(s_hash, level->addr_capacity / 2);
    }

    return 1;
}

/*
Function: level_update() 
        Update the value of a key-value item in level hash table;
        The function can be optimized by using the dynamic search scheme
*/
uint8_t level_update(level_hash_t *level, laddr_t key, 
                     size_t new_idx, size_t new_size)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);
    
    uint64_t i, j;
    for(i = 0; i < 2; i ++){
        int f_err = ensure_bucket_uptodate(level, i, f_idx);
        if (f_err) return 1;
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level_bucket(level, i)[f_idx].token[j] == 1 &&
                level_bucket(level, i)[f_idx].slot[j].e_key == key)
            {
                level_bucket(level, i)[f_idx].slot[j].e_size = new_size;
                level_bucket(level, i)[f_idx].slot[j].e_idx  = new_idx;
                nvm_persist_struct(level_bucket(level, i)[f_idx].slot[j]);

                return 0;
            }
        }
        int s_err = ensure_bucket_uptodate(level, i, s_idx);
        if (s_err) return 1;
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level_bucket(level, i)[s_idx].token[j] == 1 &&
                level_bucket(level, i)[s_idx].slot[j].e_key == key)
            {
                level_bucket(level, i)[s_idx].slot[j].e_size = new_size;
                level_bucket(level, i)[s_idx].slot[j].e_idx  = new_idx;
                nvm_persist_struct(level_bucket(level, i)[s_idx].slot[j]);

                return 0;
            }
        }
        f_idx = F_IDX(f_hash, level->addr_capacity / 2);
        s_idx = S_IDX(s_hash, level->addr_capacity / 2);
    }

    return 1;
}

/*
Function: level_insert() 
        Insert a key-value item into level hash table;
*/
uint8_t level_insert(level_hash_t *level, laddr_t key, paddr_t value, 
                     size_t idx, size_t size)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);

    uint64_t i, j;
    int empty_location;
    if (level->enable_stats) INCR_STAT(&level_stats, nwrites);

    for(i = 0; i < 2; i ++){
        for(j = 0; j < ASSOC_NUM; j ++){        
            /*  The new item is inserted into the less-loaded bucket between 
                the two hash locations in each level           
            */      
            if (level->enable_stats) INCR_STAT(&level_stats, nchecked_create);

            int f_err = ensure_bucket_uptodate(level, i, f_idx);
            if (f_err) return 1;
            if (level_bucket(level, i)[f_idx].token[j] == 0) {
                level_bucket(level, i)[f_idx].slot[j].e_key  = key;
                level_bucket(level, i)[f_idx].slot[j].e_val  = value;
                level_bucket(level, i)[f_idx].slot[j].e_size = size;
                level_bucket(level, i)[f_idx].slot[j].e_idx  = idx;
                level_bucket(level, i)[f_idx].token[j] = 1;
                level->ondev->level_item_num[i] ++;
                
                nvm_persist_struct(level_bucket(level, i)[f_idx]);
                nvm_persist_struct_ptr(level->ondev);

                return 0;
            }

            if (level->enable_stats) INCR_STAT(&level_stats, nchecked_create);
            int s_err = ensure_bucket_uptodate(level, i, s_idx);
            if (s_err) return 1;
            if (level_bucket(level, i)[s_idx].token[j] == 0) {

                level_bucket(level, i)[s_idx].slot[j].e_key  = key;
                level_bucket(level, i)[s_idx].slot[j].e_val  = value;
                level_bucket(level, i)[s_idx].slot[j].e_size = size;
                level_bucket(level, i)[s_idx].slot[j].e_idx  = idx;
                level_bucket(level, i)[s_idx].token[j] = 1;
                level->ondev->level_item_num[i] ++;
                
                nvm_persist_struct(level_bucket(level, i)[s_idx]);
                nvm_persist_struct_ptr(level->ondev);

                return 0;
            }
        }
        
        f_idx = F_IDX(f_hash, level->addr_capacity / 2);
        s_idx = S_IDX(s_hash, level->addr_capacity / 2);
    }

    f_idx = F_IDX(f_hash, level->addr_capacity);
    s_idx = S_IDX(s_hash, level->addr_capacity);
    
    for(i = 0; i < 2; i++){
        if(!try_movement(level, f_idx, i, key, value, idx, size)){
            return 0;
        }
        if(!try_movement(level, s_idx, i, key, value, idx, size)){
            return 0;
        }

        f_idx = F_IDX(f_hash, level->addr_capacity/2);
        s_idx = S_IDX(s_hash, level->addr_capacity/2);        
    }
    
    if(level->level_expand_time > 0){
        empty_location = b2t_movement(level, f_idx);
        if (empty_location != -1) {
            level_bucket(level, 1)[f_idx].slot[empty_location].e_key  = key;
            level_bucket(level, 1)[f_idx].slot[empty_location].e_val  = value;
            level_bucket(level, 1)[f_idx].slot[empty_location].e_size = size;
            level_bucket(level, 1)[f_idx].slot[empty_location].e_idx  = idx;
            level_bucket(level, 1)[f_idx].token[empty_location] = 1;
            level->ondev->level_item_num[1] ++;

            nvm_persist_struct(level_bucket(level, 1)[f_idx]);
            nvm_persist_struct_ptr(level->ondev);

            return 0;
        }

        empty_location = b2t_movement(level, s_idx);
        if (empty_location != -1) {
            level_bucket(level, 1)[s_idx].slot[empty_location].e_key  = key;
            level_bucket(level, 1)[s_idx].slot[empty_location].e_val  = value;
            level_bucket(level, 1)[s_idx].slot[empty_location].e_size = size;
            level_bucket(level, 1)[s_idx].slot[empty_location].e_idx  = idx;
            level_bucket(level, 1)[s_idx].token[empty_location] = 1;
            level->ondev->level_item_num[1] ++;

            nvm_persist_struct(level_bucket(level, 1)[s_idx]);
            nvm_persist_struct_ptr(level->ondev);

            return 0;
        }
    }

    return 1;
}

/*
Function: try_movement() 
        Try to move an item from the current bucket to its same-level alternative bucket;
*/
uint8_t try_movement(level_hash_t *level, uint64_t idx, uint64_t level_num, 
                     laddr_t key, paddr_t value, size_t p_idx, size_t size)
{
    uint64_t i, j, jdx;

    for(i = 0; i < ASSOC_NUM; i ++){
        laddr_t  m_key   = level_bucket(level, level_num)[idx].slot[i].e_key;
        paddr_t  m_value = level_bucket(level, level_num)[idx].slot[i].e_val;
        size_t   m_size  = level_bucket(level, level_num)[idx].slot[i].e_size;
        size_t   m_idx   = level_bucket(level, level_num)[idx].slot[i].e_idx;
        uint64_t f_hash = F_HASH(level, m_key);
        uint64_t s_hash = S_HASH(level, m_key);
        uint64_t f_idx = F_IDX(f_hash, level->addr_capacity/(1+level_num));
        uint64_t s_idx = S_IDX(s_hash, level->addr_capacity/(1+level_num));
        
        if (f_idx == idx) {
            jdx = s_idx;
        } else {
            jdx = f_idx;
        }

        for(j = 0; j < ASSOC_NUM; j ++) {
            if (level_bucket(level, level_num)[jdx].token[j] == 0) {
                level_bucket(level, level_num)[jdx].slot[j].e_key  = m_key;
                level_bucket(level, level_num)[jdx].slot[j].e_val  = m_value;
                level_bucket(level, level_num)[jdx].slot[j].e_size = m_size;
                level_bucket(level, level_num)[jdx].slot[j].e_idx  = m_idx;
                level_bucket(level, level_num)[jdx].token[j] = 1;
                level_bucket(level, level_num)[idx].token[i] = 0;
                // The movement is finished and then the new item is inserted

                level_bucket(level, level_num)[idx].slot[i].e_key  = key;
                level_bucket(level, level_num)[idx].slot[i].e_val  = value;
                level_bucket(level, level_num)[idx].slot[i].e_size = size;
                level_bucket(level, level_num)[idx].slot[i].e_idx  = p_idx;
                level_bucket(level, level_num)[idx].token[i] = 1;
                level->ondev->level_item_num[level_num] ++;

                nvm_persist_struct(level_bucket(level, level_num)[idx]);
                nvm_persist_struct(level_bucket(level, level_num)[jdx]);
                nvm_persist_struct_ptr(level->ondev);
                
                return 0;
            }
        }       
    }
    
    return 1;
}

/*
Function: b2t_movement() 
        Try to move a bottom-level item to its top-level alternative buckets;
*/
int b2t_movement(level_hash_t *level, uint64_t idx)
{
    laddr_t key; 
    paddr_t value;
    size_t size, p_idx;
    uint64_t s_hash, f_hash;
    uint64_t s_idx, f_idx;
    
    uint64_t i, j;
    for(i = 0; i < ASSOC_NUM; i ++){
        key   = level_bucket(level, 1)[idx].slot[i].e_key;
        value = level_bucket(level, 1)[idx].slot[i].e_val;
        size  = level_bucket(level, 1)[idx].slot[i].e_size;
        p_idx = level_bucket(level, 1)[idx].slot[i].e_idx;
        f_hash = F_HASH(level, key);
        s_hash = S_HASH(level, key);  
        f_idx = F_IDX(f_hash, level->addr_capacity);
        s_idx = S_IDX(s_hash, level->addr_capacity);
    
        for(j = 0; j < ASSOC_NUM; j ++) {
            if (level_bucket(level, 0)[f_idx].token[j] == 0) {
                level_bucket(level, 0)[f_idx].slot[j].e_key  = key;
                level_bucket(level, 0)[f_idx].slot[j].e_val  = value;
                level_bucket(level, 0)[f_idx].slot[j].e_size = size;
                level_bucket(level, 0)[f_idx].slot[j].e_idx  = p_idx;
                level_bucket(level, 0)[f_idx].token[j] = 1;
                level_bucket(level, 1)[idx].token[i] = 0;
                level->ondev->level_item_num[0] ++;
                level->ondev->level_item_num[1] --;

                nvm_persist_struct(level_bucket(level, 0)[f_idx]);
                nvm_persist_struct(level_bucket(level, 1)[idx]);
                nvm_persist_struct_ptr(level->ondev);

                return i;
            } else if (level_bucket(level, 0)[s_idx].token[j] == 0) {
                level_bucket(level, 0)[s_idx].slot[j].e_key  = key;
                level_bucket(level, 0)[s_idx].slot[j].e_val  = value;
                level_bucket(level, 0)[s_idx].slot[j].e_size = size;
                level_bucket(level, 0)[s_idx].slot[j].e_idx  = p_idx;
                level_bucket(level, 0)[s_idx].token[j] = 1;
                level_bucket(level, 1)[idx].token[i] = 0;
                level->ondev->level_item_num[0] ++;
                level->ondev->level_item_num[1] --;

                nvm_persist_struct(level_bucket(level, 0)[s_idx]);
                nvm_persist_struct(level_bucket(level, 1)[idx]);
                nvm_persist_struct_ptr(level->ondev);

                return i;
            }
        }
    }

    return -1;
}

/*
Function: level_destroy() 
        Destroy a level hash table
*/
void level_destroy(level_hash_t *level)
{
    panic("should never be here!");
}

int level_persist(level_hash_t *level) {
    return 0;
}

int level_cache_invalidate(level_hash_t *level) {
    return 0;
}
