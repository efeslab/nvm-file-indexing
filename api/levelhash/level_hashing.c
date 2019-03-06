#include "level_hashing.h"

/*
Function: F_HASH()
        Compute the first hash value of a key-value item
*/
uint64_t F_HASH(level_hash_t *level, laddr_t key) {
    return (hash((void *)&key, sizeof(key), level->f_seed));
}

/*
Function: S_HASH() 
        Compute the second hash value of a key-value item
*/
uint64_t S_HASH(level_hash_t *level, laddr_t key) {
    return (hash((void *)&key, sizeof(key), level->s_seed));
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
    return hashKey % (capacity / 2) + capacity / 2;
}

/*
void* alignedmalloc(size_t size) {
  void* ret;
  posix_memalign(&ret, 64, size);
  return ret;
}
*/

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
    paddr_t paddr  = level->dev_levels[which] + 
                     idx_to_paddr_blkno(level, bucket_idx);
    off_t   offset = idx_to_paddr_offset(level, bucket_idx);
    ssize_t ret    = CB(level->idx_spec, cb_write,
                        paddr, offset, sizeof(level_bucket_t), 
                        (const char*)(level->buckets[which] + bucket_idx)); 

    if (ret != sizeof(level_bucket_t)) return -EIO;

    return 0;
}

/*
 * This one is for reads. Writes are explicit persist calls.
 */
int ensure_bucket_uptodate(level_hash_t *level, int l, uint64_t bucket_idx) {
    if (!level->do_cache || level->cache_state[l][bucket_idx] < 0) {
        int ret = do_bucket_read(level, l, bucket_idx);
        level->cache_state[l][bucket_idx] = 0;
        return ret;
    } 

    return 0;
}

/*
 * For writes. Non-cached this does persist, for cache this marks dirty.
 */
int mark_bucket_dirty(level_hash_t *level, int l, uint64_t bucket_idx) {
    int ret = 0;

    if (!level->do_cache){
        ret = do_bucket_write(level, l, bucket_idx);
    } 

    level->cache_state[l][bucket_idx] = 1;
    return ret;
}

int mark_new_bucket_dirty(level_hash_t *level,
                          level_bucket_t *new_bucket,
                          int8_t *cache_state,
                          paddr_t new_bucket_addr,
                          uint64_t bucket_idx) {
    int ret = 0;

    if (!level->do_cache){
        paddr_t paddr  = new_bucket_addr +
                         idx_to_paddr_blkno(level, bucket_idx);
        off_t   offset = idx_to_paddr_offset(level, bucket_idx);
        ssize_t ret    = CB(level->idx_spec, cb_write,
                            paddr, offset, sizeof(level_bucket_t), 
                            (const char*)(new_bucket + bucket_idx)); 

        if (ret != sizeof(level_bucket_t)) ret = -EIO;
    } 

    cache_state[bucket_idx] = 1;
    return ret;
}

/*
Function: generate_seeds() 
        Generate two randomized seeds for hash functions
*/
void generate_seeds(level_hash_t *level)
{
    //srand(time(NULL));
    do
    {
        level->f_seed = rand();
        level->s_seed = rand();
        level->f_seed = level->f_seed << (rand() % 63);
        level->s_seed = level->s_seed << (rand() % 63);
    } while (level->f_seed == level->s_seed);
}

int read_metadata(const idx_spec_t *idx_spec, 
                  const paddr_range_t *loc, 
                  dev_level_hash_t *dhash) {
    if_then_panic(loc->pr_nbytes < sizeof(*dhash), "region is too small!");

    ssize_t ret = CB(idx_spec, cb_read,
                     loc->pr_start, loc->pr_blk_offset, sizeof(*dhash),
                     (char*)dhash);

    if (ret != sizeof(*dhash)) return -EIO;
    return 0;
}

int write_metadata(level_hash_t *level) {
    paddr_range_t *loc = &(level->range);
    if_then_panic(loc->pr_nbytes < sizeof(dev_level_hash_t), 
                  "region is too small!");

    dev_level_hash_t dhash = {
        .init_magic     = MAGIC,
        .dev_levels     = { level->dev_levels[0], level->dev_levels[1] },
        .dev_sizes      = { level->dev_sizes[0], level->dev_sizes[1] },
        .level_item_num = { level->level_item_num[0], level->level_item_num[1]},
        .addr_capacity  = level->addr_capacity,
        .total_capacity = level->total_capacity,
        .level_size     = level->level_size,
        .f_seed         = level->f_seed,
        .s_seed         = level->s_seed,
        .block_size     = level->block_size
    };

    ssize_t ret = CB(level->idx_spec, cb_write,
                     loc->pr_start, loc->pr_blk_offset, sizeof(dhash),
                     (const char*)&dhash);

    if (ret != sizeof(dhash)) return -EIO;
    return 0;
}

/*
Function: level_init() 
        Initialize a level hash table
*/
level_hash_t *level_init(const idx_spec_t *idx_spec, 
                         const paddr_range_t *loc,
                         uint64_t level_size) {
    //level_hash_t *level = alignedmalloc(sizeof(level_hash_t));
    level_hash_t *level = ZALLOC(idx_spec, sizeof(level_hash_t));
    if (!level) {
        printf("The level hash table initialization fails:1\n");
        exit(1);
    }

    dev_level_hash_t dhash;
    int ret = read_metadata(idx_spec, loc, &dhash);
    bool already_exists = dhash.init_magic == MAGIC;

    level->level_size = already_exists ? dhash.level_size : level_size;
    level->addr_capacity = pow(2, level_size);
    level->total_capacity = pow(2, level_size) + pow(2, level_size - 1);

    level->range = *loc;
    
    if (already_exists) {
        level->f_seed = dhash.f_seed;
        level->s_seed = dhash.s_seed;
    } else {
        generate_seeds(level);
    }
    //level->buckets[0] = alignedmalloc(pow(2, level_size)*sizeof(level_bucket_t));
    //level->buckets[1] = alignedmalloc(pow(2, level_size - 1)*sizeof(level_bucket_t));
    size_t top_size = pow(2, level_size);
    size_t top_size_bytes = top_size * sizeof(level_bucket_t);
    size_t bot_size = pow(2, level_size - 1);
    size_t bot_size_bytes = bot_size * sizeof(level_bucket_t);
    level->buckets[0] = ZALLOC(idx_spec, top_size_bytes);
    level->buckets[1] = ZALLOC(idx_spec, bot_size_bytes);

    if (already_exists) {
        level->level_item_num[0] = dhash.level_item_num[0];
        level->level_item_num[1] = dhash.level_item_num[1];
    } else {
        level->level_item_num[0] = 0;
        level->level_item_num[1] = 0;
    }

    level->level_expand_time = 0;
    level->resize_state = 0;
    
    if (!level->buckets[0] || !level->buckets[1])
    {
        printf("The level hash table initialization fails:2\n");
        exit(1);
    }

    /* API Init */
    level->idx_spec = idx_spec;
    level->do_cache = false;
    level->cache_state[0] = ZALLOC(idx_spec, top_size*sizeof(int8_t));
    level->cache_state[1] = ZALLOC(idx_spec, bot_size*sizeof(int8_t));

    if (already_exists) {
        memset(level->cache_state[0], -1, top_size*sizeof(int8_t));
        memset(level->cache_state[1], -1, bot_size*sizeof(int8_t));
    }

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

    level->dev_sizes[0] = top_blocks;
    level->dev_sizes[1] = bot_blocks;

    if (!already_exists) {
        paddr_t top_block_start;
        ssize_t top_blocks_alloced = CB(idx_spec, cb_alloc_metadata,
                                        top_blocks, &top_block_start);
        if_then_panic(top_blocks_alloced != top_blocks, "could not alloc!");
        level->dev_levels[0] = top_block_start;

        paddr_t bot_block_start;
        ssize_t bot_blocks_alloced = CB(idx_spec, cb_alloc_metadata,
                                        bot_blocks, &bot_block_start);
        if_then_panic(bot_blocks_alloced != bot_blocks, "could not alloc!");
        level->dev_levels[1] = bot_block_start;
    } else {
        level->dev_levels[0] = dhash.dev_levels[0];
        level->dev_levels[1] = dhash.dev_levels[1];
    }

    /*
    printf("Level hashing: ASSOC_NUM %d, KEY_LEN %d, VALUE_LEN %d \n", ASSOC_NUM, KEY_LEN, VALUE_LEN);
    printf("The number of top-level buckets: %ld\n", level->addr_capacity);
    printf("The number of all buckets: %ld\n", level->total_capacity);
    printf("The number of all entries: %ld\n", level->total_capacity*ASSOC_NUM);
    printf("The level hash table initialization succeeds!\n");
    */
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

    //if_then_panic(true, "Have yet to implement this for the API!");

    level->resize_state = 1;
    level->addr_capacity = pow(2, level->level_size + 1);
    size_t new_buckets_bytes = level->addr_capacity * sizeof(level_bucket_t);
    size_t new_buckets_blocks = new_buckets_bytes / level->block_size;
    if(new_buckets_bytes % level->block_size != 0) new_buckets_blocks++;
    //level_bucket_t *newBuckets = alignedmalloc(level->addr_capacity*sizeof(level_bucket_t));
    level_bucket_t *newBuckets = ZALLOC(level->idx_spec, new_buckets_bytes);
    int8_t *new_cache_state = ZALLOC(level->idx_spec, level->addr_capacity*sizeof(int8_t));

    if (!newBuckets || !new_cache_state) {
        printf("The expanding fails: 2\n");
        exit(1);
    }

    paddr_t new_buckets_paddr;
    ssize_t nalloced = CB(level->idx_spec, cb_alloc_metadata,
                          new_buckets_blocks, &new_buckets_paddr);
    if_then_panic(nalloced != new_buckets_blocks, "could not alloc metadata!");

    // Ensure the new blocks are zeroed out.
    ssize_t nzeroed = CB(level->idx_spec, cb_write,
                         new_buckets_paddr, 0, new_buckets_bytes, 
                         (char*)newBuckets); 
    if_then_panic(nzeroed != new_buckets_bytes, "could not zero out metadata!");

    uint64_t new_level_item_num = 0;
    
    uint64_t old_idx;
    for (old_idx = 0; old_idx < pow(2, level->level_size - 1); old_idx ++) {
        uint64_t i, j;
        for(i = 0; i < ASSOC_NUM; i ++){
            if (level->buckets[1][old_idx].token[i] == 1)
            {
                laddr_t  key   = level->buckets[1][old_idx].slot[i].e_key;
                paddr_t  value = level->buckets[1][old_idx].slot[i].e_val;
                size_t   size  = level->buckets[1][old_idx].slot[i].e_size;
                uint64_t f_idx = F_IDX(F_HASH(level, key), level->addr_capacity);
                uint64_t s_idx = S_IDX(S_HASH(level, key), level->addr_capacity);

                uint8_t insertSuccess = 0;
                for(j = 0; j < ASSOC_NUM; j ++){                            
                    /*  The rehashed item is inserted into the less-loaded bucket between 
                        the two hash locations in the new level
                    */
                    if (newBuckets[f_idx].token[j] == 0)
                    {
                        //memcpy(newBuckets[f_idx].slot[j].key, key, KEY_LEN);
                        //memcpy(newBuckets[f_idx].slot[j].value, value, VALUE_LEN);
                        newBuckets[f_idx].slot[j].e_key  = key;
                        newBuckets[f_idx].slot[j].e_val  = value;
                        newBuckets[f_idx].slot[j].e_size = size;
                        newBuckets[f_idx].token[j] = 1;
                        insertSuccess = 1;
                        new_level_item_num ++;
                        int f_err = mark_new_bucket_dirty(level, newBuckets,
                                    new_cache_state, new_buckets_paddr, f_idx);
                        if_then_panic(f_err, "couldn't write!");
                        break;
                    }
                    if (newBuckets[s_idx].token[j] == 0)
                    {
                        //memcpy(newBuckets[s_idx].slot[j].key, key, KEY_LEN);
                        //memcpy(newBuckets[s_idx].slot[j].value, value, VALUE_LEN);
                        newBuckets[s_idx].slot[j].e_key  = key;
                        newBuckets[s_idx].slot[j].e_val  = value;
                        newBuckets[s_idx].slot[j].e_size = size;
                        newBuckets[s_idx].token[j] = 1;
                        insertSuccess = 1;
                        new_level_item_num ++;
                        int s_err = mark_new_bucket_dirty(level, newBuckets,
                                    new_cache_state, new_buckets_paddr, s_idx);
                        if_then_panic(s_err, "couldn't write!");
                        break;
                    }
                }
                if(!insertSuccess){
                    printf("The expanding fails: 3\n");
                    exit(1);                    
                }
                
                level->buckets[1][old_idx].token[i] = 0;
                int oerr = mark_bucket_dirty(level, 1, old_idx);
                if_then_panic(oerr, "couldn't write!");
            }
        }
    }

    level->level_size ++;
    level->total_capacity = pow(2, level->level_size) + pow(2, level->level_size - 1);

    FREE(level->idx_spec, level->buckets[1]);
    FREE(level->idx_spec, level->cache_state[1]);
    ssize_t freed = CB(level->idx_spec, cb_dealloc_metadata,
                       level->dev_sizes[1], level->dev_levels[1]); 
    if_then_panic(freed != level->dev_sizes[1], "could not free metadata!");

    level->buckets[1] = level->buckets[0];
    level->buckets[0] = newBuckets;
    newBuckets = NULL;

    level->dev_levels[1] = level->dev_levels[0];
    level->dev_levels[0] = new_buckets_paddr;

    level->dev_sizes[1] = level->dev_sizes[0];
    level->dev_sizes[0] = new_buckets_blocks;

    level->cache_state[1] = level->cache_state[0];
    level->cache_state[0] = new_cache_state;
    
    level->level_item_num[1] = level->level_item_num[0];
    level->level_item_num[0] = new_level_item_num;

    level->level_expand_time++;
    level->resize_state = 0;
}

/*
Function: level_shrink()
        Shrink a level hash table in place;
        Put a new level at the bottom of the old hash table and only rehash the
        items in the top level of the old hash table;
*/
void level_shrink(level_hash_t *level)
{
    if (!level)
    {
        printf("The shrinking fails: 1\n");
        exit(1);
    }
    if_then_panic(true, "Have yet to implement this for the API!");

    // The shrinking is performed only when the hash table has very few items.
    if(level->level_item_num[0] + level->level_item_num[1] > level->total_capacity*ASSOC_NUM*0.4){
        printf("The shrinking fails: 2\n");
        exit(1);
    }

    level->resize_state = 2;
    level->level_size --;
    //level_bucket_t *newBuckets = alignedmalloc(pow(2, level->level_size - 1)*sizeof(level_bucket_t));
    size_t new_size = pow(2, level->level_size - 1);
    level_bucket_t *newBuckets = ZALLOC(level->idx_spec, new_size*sizeof(level_bucket_t));
    level_bucket_t *interimBuckets = level->buckets[0];
    level->buckets[0] = level->buckets[1];
    level->buckets[1] = newBuckets;
    newBuckets = NULL;

    level->level_item_num[0] = level->level_item_num[1];
    level->level_item_num[1] = 0;

    level->addr_capacity = pow(2, level->level_size);
    level->total_capacity = pow(2, level->level_size) + pow(2, level->level_size - 1);

    uint64_t old_idx, i;
    for (old_idx = 0; old_idx < pow(2, level->level_size+1); old_idx ++) {
        for(i = 0; i < ASSOC_NUM; i ++){
            if (interimBuckets[old_idx].token[i] == 1)
            {
                if(level_insert(level, interimBuckets[old_idx].slot[i].e_key, 
                                       interimBuckets[old_idx].slot[i].e_val,
                                       interimBuckets[old_idx].slot[i].e_size))
                {
                    printf("The shrinking fails: 3\n");
                    exit(1);   
                }

                interimBuckets[old_idx].token[i] = 0;
            }
        }
    } 

    FREE(level->idx_spec, interimBuckets);
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

    uint64_t i, j, f_idx, s_idx;
    if(level->level_item_num[0] > level->level_item_num[1]){
        f_idx = F_IDX(f_hash, level->addr_capacity);
        s_idx = S_IDX(s_hash, level->addr_capacity); 

        for(i = 0; i < 2; i ++){
            int f_err = ensure_bucket_uptodate(level, i, f_idx);
            if (f_err) return 0;
            for(j = 0; j < ASSOC_NUM; j ++){
                if (level->buckets[i][f_idx].token[j] == 1 &&
                    //strcmp(level->buckets[i][f_idx].slot[j].key, key) == 0)
                    level->buckets[i][f_idx].slot[j].e_key == key)
                {
                    *value = level->buckets[i][f_idx].slot[j].e_val;
                    return level->buckets[i][f_idx].slot[j].e_size;
                }
            }

            int s_err = ensure_bucket_uptodate(level, i, s_idx);
            if (s_err) return 0;
            for(j = 0; j < ASSOC_NUM; j ++) {
                if (level->buckets[i][s_idx].token[j] == 1 &&
                    //strcmp(level->buckets[i][s_idx].slot[j].key, key) == 0)
                    level->buckets[i][s_idx].slot[j].e_key == key) 
                {
                    *value = level->buckets[i][s_idx].slot[j].e_val;
                    return level->buckets[i][s_idx].slot[j].e_size;
                }
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
                if (level->buckets[i-1][f_idx].token[j] == 1 &&
                    //strcmp(level->buckets[i-1][f_idx].slot[j].key, key) == 0)
                    level->buckets[i-1][f_idx].slot[j].e_key == key)
                {
                    *value = level->buckets[i-1][f_idx].slot[j].e_val;
                    return level->buckets[i-1][f_idx].slot[j].e_size;
                }
            }

            int s_err = ensure_bucket_uptodate(level, i-1, s_idx);
            if (s_err) return 0;
            for(j = 0; j < ASSOC_NUM; j ++){
                if (level->buckets[i-1][s_idx].token[j] == 1 &&
                    //strcmp(level->buckets[i-1][s_idx].slot[j].key, key) == 0)
                    level->buckets[i-1][s_idx].slot[j].e_key == key)
                {
                    *value = level->buckets[i-1][s_idx].slot[j].e_val;
                    return level->buckets[i-1][s_idx].slot[j].e_size;
                }
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
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);

    *value = 0;
    
    uint64_t i, j;
    for(i = 0; i < 2; i ++){
        int f_err = ensure_bucket_uptodate(level, i, f_idx);
        if (f_err) return 0;
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][f_idx].token[j] == 1 &&
                //strcmp(level->buckets[i][f_idx].slot[j].key, key) == 0)
                level->buckets[i][f_idx].slot[j].e_key == key)
            {
                *value = level->buckets[i][f_idx].slot[j].e_val;
                return level->buckets[i][f_idx].slot[j].e_size;
            }
        }

        int s_err = ensure_bucket_uptodate(level, i, s_idx);
        if (s_err) return 0;
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][s_idx].token[j] == 1 && 
                //strcmp(level->buckets[i][s_idx].slot[j].key, key) == 0)
                level->buckets[i][s_idx].slot[j].e_key == key)
            {
                *value = level->buckets[i][s_idx].slot[j].e_val;
                return level->buckets[i][s_idx].slot[j].e_size;
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
uint8_t level_delete(level_hash_t *level, laddr_t key)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);
    
    uint64_t i, j;
    for(i = 0; i < 2; i ++){
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][f_idx].token[j] == 1 && 
                //strcmp(level->buckets[i][f_idx].slot[j].key, key) == 0)
                level->buckets[i][f_idx].slot[j].e_key ==  key)
            {
                level->buckets[i][f_idx].token[j] = 0;
                level->level_item_num[i] --;

                int f_err = mark_bucket_dirty(level, i, f_idx);
                if (f_err) return 1;
                return 0;
            }
        }
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][s_idx].token[j] == 1 &&
                //strcmp(level->buckets[i][s_idx].slot[j].key, key) == 0)
                level->buckets[i][s_idx].slot[j].e_key == key)
            {
                level->buckets[i][s_idx].token[j] = 0;
                level->level_item_num[i] --;
                int s_err = mark_bucket_dirty(level, i, s_idx);
                if (s_err) return 1;
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
uint8_t level_update(level_hash_t *level, laddr_t key, paddr_t new_value)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);

    if_then_panic(true, "Not implemented for API path!");
    
    uint64_t i, j;
    for(i = 0; i < 2; i ++){
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][f_idx].token[j] == 1 &&
                //strcmp(level->buckets[i][f_idx].slot[j].key, key) == 0)
                level->buckets[i][f_idx].slot[j].e_key == key)
            {
                //memcpy(level->buckets[i][f_idx].slot[j].e_val, new_value, VALUE_LEN);
                level->buckets[i][f_idx].slot[j].e_val = new_value;
                return 0;
            }
        }
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[i][s_idx].token[j] == 1 &&
                //strcmp(level->buckets[i][s_idx].slot[j].key, key) == 0)
                level->buckets[i][s_idx].slot[j].e_key == key)
            {
                //memcpy(level->buckets[i][s_idx].slot[j].e_val, new_value, VALUE_LEN);
                level->buckets[i][s_idx].slot[j].e_val = new_value;
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
uint8_t level_insert(level_hash_t *level, laddr_t key, paddr_t value, size_t size)
{
    uint64_t f_hash = F_HASH(level, key);
    uint64_t s_hash = S_HASH(level, key);
    uint64_t f_idx = F_IDX(f_hash, level->addr_capacity);
    uint64_t s_idx = S_IDX(s_hash, level->addr_capacity);

    uint64_t i, j;
    int empty_location;

    for(i = 0; i < 2; i ++){
        for(j = 0; j < ASSOC_NUM; j ++){        
            /*  The new item is inserted into the less-loaded bucket between 
                the two hash locations in each level           
            */      
            int f_err = ensure_bucket_uptodate(level, i, f_idx);
            if (f_err) return 0;
            if (level->buckets[i][f_idx].token[j] == 0)
            {
                //memcpv(level->buckets[i][f_idx].slot[j].key, key, KEY_LEN);
                //memcpy(level->buckets[i][f_idx].slot[j].value, value, VALUE_LEN);
                level->buckets[i][f_idx].slot[j].e_key  = key;
                level->buckets[i][f_idx].slot[j].e_val  = value;
                level->buckets[i][f_idx].slot[j].e_size = size;
                level->buckets[i][f_idx].token[j] = 1;
                level->level_item_num[i] ++;
                int f_err = mark_bucket_dirty(level, i, f_idx);
                if (f_err) return 1;
                return 0;
            }
            int s_err = ensure_bucket_uptodate(level, i, s_idx);
            if (s_err) return 0;
            if (level->buckets[i][s_idx].token[j] == 0) 
            {
                //memcpy(level->buckets[i][s_idx].slot[j].key, key, KEY_LEN);
                //memcpy(level->buckets[i][s_idx].slot[j].value, value, VALUE_LEN);
                level->buckets[i][s_idx].slot[j].e_key  = key;
                level->buckets[i][s_idx].slot[j].e_val  = value;
                level->buckets[i][s_idx].slot[j].e_size = size;
                level->buckets[i][s_idx].token[j] = 1;
                level->level_item_num[i] ++;
                int s_err = mark_bucket_dirty(level, i, s_idx);
                if (s_err) return 1;
                return 0;
            }
        }
        
        f_idx = F_IDX(f_hash, level->addr_capacity / 2);
        s_idx = S_IDX(s_hash, level->addr_capacity / 2);
    }

    f_idx = F_IDX(f_hash, level->addr_capacity);
    s_idx = S_IDX(s_hash, level->addr_capacity);
    
    for(i = 0; i < 2; i++){
        if(!try_movement(level, f_idx, i, key, value, size)){
            return 0;
        }
        if(!try_movement(level, s_idx, i, key, value, size)){
            return 0;
        }

        f_idx = F_IDX(f_hash, level->addr_capacity/2);
        s_idx = S_IDX(s_hash, level->addr_capacity/2);        
    }
    
    if(level->level_expand_time > 0){
        empty_location = b2t_movement(level, f_idx);
        if(empty_location != -1){
            //memcpy(level->buckets[1][f_idx].slot[empty_location].key, key, KEY_LEN);
            //memcpy(level->buckets[1][f_idx].slot[empty_location].value, value, VALUE_LEN);
            level->buckets[1][f_idx].slot[empty_location].e_key  = key;
            level->buckets[1][f_idx].slot[empty_location].e_val  = value;
            level->buckets[1][f_idx].slot[empty_location].e_size = size;
            level->buckets[1][f_idx].token[empty_location] = 1;
            level->level_item_num[1] ++;
            int f_err = mark_bucket_dirty(level, 1, f_idx);
            if (f_err) return 1;
            return 0;
        }

        empty_location = b2t_movement(level, s_idx);
        if(empty_location != -1){
            //memcpy(level->buckets[1][s_idx].slot[empty_location].key, key, KEY_LEN);
            //memcpy(level->buckets[1][s_idx].slot[empty_location].value, value, VALUE_LEN);
            level->buckets[1][s_idx].slot[empty_location].e_key  = key;
            level->buckets[1][s_idx].slot[empty_location].e_val  = value;
            level->buckets[1][s_idx].slot[empty_location].e_size = size;
            level->buckets[1][s_idx].token[empty_location] = 1;
            level->level_item_num[1] ++;
            int s_err = mark_bucket_dirty(level, 1, s_idx);
            if (s_err) return 1;
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
                     laddr_t key, paddr_t value, size_t size)
{
    uint64_t i, j, jdx;

    for(i = 0; i < ASSOC_NUM; i ++){
        laddr_t  m_key = level->buckets[level_num][idx].slot[i].e_key;
        paddr_t  m_value = level->buckets[level_num][idx].slot[i].e_val;
        size_t   m_size = level->buckets[level_num][idx].slot[i].e_size;
        uint64_t f_hash = F_HASH(level, m_key);
        uint64_t s_hash = S_HASH(level, m_key);
        uint64_t f_idx = F_IDX(f_hash, level->addr_capacity/(1+level_num));
        uint64_t s_idx = S_IDX(s_hash, level->addr_capacity/(1+level_num));
        
        if(f_idx == idx)
            jdx = s_idx;
        else
            jdx = f_idx;

        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[level_num][jdx].token[j] == 0)
            {
                //memcpy(level->buckets[level_num][jdx].slot[j].key, m_key, KEY_LEN);
                //memcpy(level->buckets[level_num][jdx].slot[j].value, m_value, VALUE_LEN);
                level->buckets[level_num][jdx].slot[j].e_key  = m_key;
                level->buckets[level_num][jdx].slot[j].e_val  = m_value;
                level->buckets[level_num][jdx].slot[j].e_size = m_size;
                level->buckets[level_num][jdx].token[j] = 1;
                level->buckets[level_num][idx].token[i] = 0;
                // The movement is finished and then the new item is inserted

                //memcpy(level->buckets[level_num][idx].slot[i].key, key, KEY_LEN);
                //memcpy(level->buckets[level_num][idx].slot[i].value, value, VALUE_LEN);
                level->buckets[level_num][idx].slot[i].e_key  = key;
                level->buckets[level_num][idx].slot[i].e_val  = value;
                level->buckets[level_num][idx].slot[i].e_size = size;
                level->buckets[level_num][idx].token[i] = 1;
                level->level_item_num[level_num] ++;
                
                int i_err = mark_bucket_dirty(level, level_num, idx);
                int j_err = mark_bucket_dirty(level, level_num, jdx);
                if (i_err || j_err) return 1;

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
    size_t size;
    uint64_t s_hash, f_hash;
    uint64_t s_idx, f_idx;
    
    uint64_t i, j;
    for(i = 0; i < ASSOC_NUM; i ++){
        key = level->buckets[1][idx].slot[i].e_key;
        value = level->buckets[1][idx].slot[i].e_val;
        size = level->buckets[1][idx].slot[i].e_size;
        f_hash = F_HASH(level, key);
        s_hash = S_HASH(level, key);  
        f_idx = F_IDX(f_hash, level->addr_capacity);
        s_idx = S_IDX(s_hash, level->addr_capacity);
    
        for(j = 0; j < ASSOC_NUM; j ++){
            if (level->buckets[0][f_idx].token[j] == 0)
            {
                //memcpy(level->buckets[0][f_idx].slot[j].key, key, KEY_LEN);
                //memcpy(level->buckets[0][f_idx].slot[j].value, value, VALUE_LEN);
                level->buckets[0][f_idx].slot[j].e_key  = key;
                level->buckets[0][f_idx].slot[j].e_val  = value;
                level->buckets[0][f_idx].slot[j].e_size = size;
                level->buckets[0][f_idx].token[j] = 1;
                level->buckets[1][idx].token[i] = 0;
                level->level_item_num[0] ++;
                level->level_item_num[1] --;

                int t_err = mark_bucket_dirty(level, 0, f_idx);
                int b_err = mark_bucket_dirty(level, 1, idx);
                if (t_err || b_err) return -1;
                return i;
            }
            else if (level->buckets[0][s_idx].token[j] == 0)
            {
                //memcpy(level->buckets[0][s_idx].slot[j].key, key, KEY_LEN);
                //memcpy(level->buckets[0][s_idx].slot[j].value, value, VALUE_LEN);
                level->buckets[0][s_idx].slot[j].e_key  = key;
                level->buckets[0][s_idx].slot[j].e_val  = value;
                level->buckets[0][s_idx].slot[j].e_size = size;
                level->buckets[0][s_idx].token[j] = 1;
                level->buckets[1][idx].token[i] = 0;
                level->level_item_num[0] ++;
                level->level_item_num[1] --;

                int t_err = mark_bucket_dirty(level, 0, s_idx);
                int b_err = mark_bucket_dirty(level, 1, idx);
                if (t_err || b_err) return -1;
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
    FREE(level->idx_spec, level->buckets[0]);
    FREE(level->idx_spec, level->buckets[1]);
    level = NULL;
}

int level_persist(level_hash_t *level) {
    return -1;
}

int level_cache_invalidate(level_hash_t *level) {
    return -1;
}
