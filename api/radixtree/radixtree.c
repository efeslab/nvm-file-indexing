#include "radixtree.h"

#include <math.h>

#define PRESENT_BIT (1llu << 63)
_Static_assert(PRESENT_BIT != 0llu, "Too much shift!");
#define ENT_IS_PRESENT(e) ((e) & PRESENT_BIT)
#define ENT_PADDR(e) ((e) & ~PRESENT_BIT)

#define MKADDR(i, l) ( ((paddr_t) i) << 32 | ((paddr_t)l) )

/*******************************************************************************
 * Section: IO interface for radix trees.
 ******************************************************************************/

static paddr_t block_number(radixtree_meta_t *radix, size_t entry_idx) {
    size_t entry_idx_bytes = entry_idx * radix->ent_size;
    return (paddr_t)(entry_idx_bytes / radix->blksz);
}

static paddr_t block_offset(radixtree_meta_t *radix, size_t entry_idx) {
    size_t entry_idx_bytes = entry_idx * radix->ent_size;
    return (paddr_t)(entry_idx_bytes % radix->blksz);
}

static int read_page(radixtree_meta_t *radix, paddr_t page, paddr_t *cache_page) 
{
    ssize_t ret = CB(radix, cb_read,
                     page, 0, radix->node_nbytes, (char*)cache_page);

    if (ret != radix->node_nbytes) return -EIO;
    return 0;
}

static int read_entry(radixtree_meta_t *radix, 
                      paddr_t page, size_t idx, paddr_t *cache_page) 
{
    paddr_t paddr = page + block_number(radix, idx);
    off_t  offset = block_offset(radix, idx);
    paddr_t *entp = cache_page + idx;
    ssize_t   ret = CB(radix, cb_read,
                       paddr, offset, radix->ent_size, (char*)entp);

    if (ret != radix->ent_size) return -EIO;
    return 0;
}

static int write_page(radixtree_meta_t *radix, paddr_t page, paddr_t *cache_page) {
    ssize_t ret = CB(radix, cb_write,
                     page, 0, radix->node_nbytes, (char*)cache_page);

    if (ret != radix->node_nbytes) return -EIO;
    return 0;
}

static int write_entry(radixtree_meta_t *radix, 
                      paddr_t page, size_t idx, paddr_t *cache_page) {
    paddr_t paddr = page + block_number(radix, idx);
    off_t  offset = block_offset(radix, idx);
    paddr_t *entp = cache_page + idx;
    ssize_t   ret = CB(radix, cb_write,
                       paddr, offset, radix->ent_size, (char*)entp);

    if (ret != radix->ent_size) return -EIO;
    return 0;
}

static int get_page(radixtree_meta_t *radix, paddr_t page, paddr_t **page_ptr) {
    ssize_t err = CB(radix, cb_get_addr,
                     page, 0, (char**)page_ptr);
    return (int)err;
}

// Returns -EIO if could not read, 0 if no metadata, 1 if metadata.
static int read_metadata(radixtree_meta_t *radix) {
    ondev_radix_meta_t ondev;
    paddr_range_t *meta = &(radix->metadata_loc);
    if_then_panic(meta->pr_nbytes < sizeof(ondev), 
            "Not enough space for metadata!\n");
    ssize_t ret = CB(radix, cb_read,
                     meta->pr_start, meta->pr_blk_offset, 
                     sizeof(ondev), (char*)&ondev);

    if (ret != sizeof(ondev)) return -EIO;
    if (ondev.magic == RADIX_MAGIC) {
        radix->top_page = ondev.top_page;
        radix->nlevels  = ondev.nlevels;
        radix->nentries = ondev.nentries;
        return 1;
    }
    return 0;
}

// Returns 0 on success, or -errno otherwise.
static int write_metadata(radixtree_meta_t *radix) {
    ondev_radix_meta_t ondev = {
        .magic    = RADIX_MAGIC,
        .nlevels  = radix->nlevels,
        .nentries = radix->nentries,
        .top_page = radix->top_page
    };

    paddr_range_t *meta = &(radix->metadata_loc);
    if_then_panic(meta->pr_nbytes < sizeof(ondev), 
            "Not enough space for metadata!\n");

    ssize_t ret = CB(radix, cb_write,
                     meta->pr_start, meta->pr_blk_offset, 
                     sizeof(ondev), (char*)&ondev);

    if (ret != sizeof(ondev)) return -EIO;
    return 0;
}

/*******************************************************************************
 * Section: Radix tree setup.
 ******************************************************************************/

static int create_radix_node(radixtree_meta_t *radix, paddr_t page, 
                             int depth, radix_node_t *node);

static int read_radix_node(radixtree_meta_t *radix, radix_node_t *node) {
    int err = 0;
    if (radix->cached) {
        err = read_page(radix, node->rn_page, node->rn_dev_contents);
        if (err) return err;

        for (size_t i = 0; i < radix->ents_per_node; ++i) {
            radix_node_t *n = &(node->rn_cache_tree[i]);

            paddr_t entry = node->rn_dev_contents[i];
            int depth = node->rn_depth + 1;
            n->rn_depth = depth;
            n->rn_page  = entry;
        }
    } else {
        err = get_page(radix, node->rn_page, &(node->rn_dev_contents));
        if (err) return err;
    }

    return err;
}

static int create_radix_node(radixtree_meta_t *radix, paddr_t page, 
                             int depth, radix_node_t *node) 
{
    if_then_panic(!node, "Must be preallocated!\n");

    node->rn_page = page;
    node->rn_depth = depth;
    node->rn_cache_tree = ZALLOC(radix, radix->ents_per_node * sizeof(*node));
    if (!node->rn_cache_tree) return -ENOMEM;

    if (radix->cached) {
        node->rn_dev_contents = ZALLOC(radix, radix->node_nbytes);
        node->rn_cache_state = ZALLOC(radix, radix->ents_per_node * sizeof(int8_t));
        if (!node->rn_dev_contents || 
            !node->rn_cache_tree ||
            !node->rn_cache_state) {
            return -ENOMEM;
        }
    }

    if (page > 0) {
        if (radix->cached) {
            memset(node->rn_cache_state, -1, radix->ents_per_node); 
        }
        return read_radix_node(radix, node);
    }

    return 0;
}

static int write_radix_node(radixtree_meta_t *radix, radix_node_t *node) {
    if (!radix->cached) return 0;

    int ret = write_page(radix, node->rn_page, node->rn_dev_contents);
    if (ret) return ret;
}

static int read_radix_node_entry(radixtree_meta_t *radix, 
                                 radix_node_t *node, size_t idx) 
{
    int err = 0;
    if (radix->cached && node->rn_cache_state[idx] < 0) {
        err = read_entry(radix, node->rn_page, idx, node->rn_dev_contents);
        if (!err) node->rn_cache_state[idx] = 0;
    } else {
        err = get_page(radix, node->rn_page, &(node->rn_dev_contents));
    }

    return err;
}

static int write_radix_node_entry(radixtree_meta_t *radix, 
                                 radix_node_t *node, size_t idx) 
{
    int err = 0;
    if (radix->cached && node->rn_cache_state[idx] > 0) {
        int err = write_entry(radix, node->rn_page, idx, node->rn_dev_contents);
        if (!err) node->rn_cache_state[idx] = 0;
    }

    return err;
}

static radix_node_t* index_node(radixtree_meta_t *radix, 
                                radix_node_t *node, size_t idx) 
{
    if_then_panic(idx >= radix->ents_per_node, 
                  "Bad index calculation! (%lu)\n", idx);
    if_then_panic(!node, "Can't index nullptr!\n");
    if_then_panic(node->rn_depth >= radix->max_depth, 
                  "Cannot index further!\n");

    int rerr = read_radix_node_entry(radix, node, idx);
    if (rerr) return NULL;

    radix_node_t *child = &(node->rn_cache_tree[idx]);

    paddr_t entry = node->rn_dev_contents[idx];
    if (!entry) {
        // We need to allocate this page!
        ssize_t nalloc = CB(radix, cb_alloc_metadata,
                            radix->nblk_per_node, &entry);
        if_then_panic(nalloc != radix->nblk_per_node, "Could not allocate!");

        node->rn_dev_contents[idx] = entry;
        if (radix->cached) {
            node->rn_cache_state[idx] = -1;
        }

        child->rn_page = entry;
        child->rn_depth = node->rn_depth + 1;
        int ret = write_entry(radix, node->rn_page, idx, node->rn_dev_contents);
        if (ret) return NULL;
    }

    if (!child->rn_dev_contents) {
        int err = create_radix_node(radix, entry, child->rn_depth, child);
        if (err) return NULL;
    }

    return child;
}

static paddr_t index_dev_node(radixtree_meta_t *radix, paddr_t node, size_t idx) 
{
    if (!node) return 0;

    paddr_t *node_contents;
    int rerr = get_page(radix, node, &node_contents);
    if (rerr) return 0;

    paddr_t entry = node_contents[idx];
    return entry;
}

/**
 * Index the given node at index idx. If the resulting page has yet to be 
 * initialized, initialize it. 
 * 
 * Returns the entry at idx.
 */
static paddr_t index_create_dev_node(radixtree_meta_t *radix, paddr_t node, size_t idx) 
{
    if (!node) return 0;

    paddr_t *node_contents;
    int rerr = get_page(radix, node, &node_contents);
    if (rerr) return 0;

    paddr_t entry = node_contents[idx];
    if (!entry) {
        // We need to allocate this page!
        ssize_t nalloc = CB(radix, cb_alloc_metadata,
                            radix->nblk_per_node, &entry);
        if_then_panic(nalloc != radix->nblk_per_node, "Could not allocate!");
        //if_then_panic(idx % 2, "You ruined it Scott!\n");

        node_contents[idx] = entry;
        //node_contents[idx + 1] = entry;
    }

    return entry;
}

static int insert_entry(radixtree_meta_t *radix, radix_node_t *node, 
                        size_t idx, paddr_t entry)
{
    if (radix->cached) {
        if_then_panic(node->rn_depth != radix->max_depth, 
                      "Don't use this for data blocks!\n");
    }

    int rerr = read_radix_node_entry(radix, node, idx);
    if (rerr) return rerr;

    node->rn_dev_contents[idx] = entry;
    if (radix->cached) {
        node->rn_cache_state[1] = 1;
    }

    return write_radix_node_entry(radix, node, idx);
}

static int lookup_entry(radixtree_meta_t *radix, radix_node_t *node, 
                        size_t idx, paddr_t *entry)
{
    if (radix->cached) {
        if_then_panic(node->rn_depth != radix->max_depth, 
                      "Don't use this for data blocks!\n");
    }

    int rerr = read_radix_node_entry(radix, node, idx);
    if (rerr) return rerr;

    *entry = node->rn_dev_contents[idx];
    return 0;
}

static int lookup_dev_entry(radixtree_meta_t *radix, paddr_t node, 
                            size_t idx, paddr_t *entry)
{
    *entry = 0;

    paddr_t *node_contents;
    int rerr = get_page(radix, node, &node_contents);
    if (rerr) return rerr;

    *entry = node_contents[idx];
    return 0;
}

/**
 * Inserts the entry into the node at index == idx.
 * Equivalent to: node[idx] = entry
 */
static int insert_dev_entry(radixtree_meta_t *radix, paddr_t node, 
                            size_t idx, paddr_t entry)
{
    paddr_t *node_contents;
    int rerr = get_page(radix, node, &node_contents);
    if (rerr) return rerr;

    node_contents[idx] = entry;
    return 0;
}

/*******************************************************************************
 * Section: Public API Functions.
 ******************************************************************************/

int radixtree_init(const idx_spec_t *idx_spec,
                   const paddr_range_t *metadata_location,
                   idx_struct_t *idx_struct) 
{

    if_then_panic(!metadata_location, "Nowhere to put metadata!\n");
    
    radixtree_meta_t *radix = ZALLOC(idx_spec, sizeof(*radix));
    if_then_panic(!radix, "Could not allocate in-memory structure!\n");

    device_info_t di;
    int err = CB(idx_spec, cb_get_dev_info, &di);
    if_then_panic(err, "Could not get device size!");

    radix->blksz         = di.di_block_size;
    radix->ent_size      = sizeof(paddr_t);
    radix->ents_per_node = 1 << 9; // 9 bits worth
    radix->max_depth     = 4;
    radix->node_nbytes   = di.di_size_blocks;
    radix->nblk_per_node = 1;
    radix->ent_idx_mask  = 0x1FF;
    radix->ent_shift     = 9;
    radix->metadata_loc  = *metadata_location; 
    radix->idx_callbacks = idx_spec->idx_callbacks;
    radix->idx_mem_man   = idx_spec->idx_mem_man; 
    radix->cached        = false;

    // Check status of metadata.
    int merr = read_metadata(radix);
    if (merr < 0) return merr;

    // Read the top page.
    #if 0
    radix->cached_tree  = ZALLOC(radix, sizeof(*radix->cached_tree));
    if (!radix->cached_tree) return -ENOMEM;
    ssize_t err = create_radix_node(radix, radix->top_page, 1, radix->cached_tree);
    if (err) return err;
    #endif

    // If all of that is successful, finally set up structure.
    idx_struct->idx_metadata  = (void*)radix;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_fns       = &radixtree_fns;

    return 0;
}

ssize_t index_and_find(radixtree_meta_t *radix, paddr_t page, uint16_t level, size_t n, 
                     laddr_t laddr, paddr_t *paddr, bool *cont) 
{
    laddr_t start_idx = laddr & radix->ent_idx_mask;
    ssize_t nfound = 0;
    *cont = false;

    if (level == 1) {
        for (laddr_t l = 0; l < (laddr_t)n; ++l) {
            laddr_t new_idx = (laddr + l) & radix->ent_idx_mask;

            // Since we look up sequentially for ranges, we don't want to
            // wrap around.
            if (new_idx < l) {
                *cont = true;
                break;
            }

            paddr_t new_paddr;

            int err = lookup_dev_entry(radix, page, new_idx, &new_paddr);
            if (err) return (ssize_t)err;

            if (!new_paddr) {
                return nfound;
            } else if (nfound && new_paddr != *paddr + nfound) {
                return nfound;
            } else if (!nfound) {
                *paddr = new_paddr;
            } 

            ++nfound;
        }

        return nfound;
    }

    laddr_t level_shift = (level - 1) * radix->ent_shift;

    laddr_t index = (laddr >> level_shift) & radix->ent_idx_mask;
    laddr_t max_index = ((~0u) >> level_shift) & radix->ent_idx_mask;


    while (nfound < n) {
        for (laddr_t i = index; i <= max_index; ++i) {  
            laddr_t new_laddr = laddr + nfound;
            paddr_t new_paddr;
            bool do_cont;

            paddr_t subpage = index_dev_node(radix, page, i);
            if (!subpage) return nfound;
            
            ssize_t ret = index_and_find(radix, subpage, level - 1, 
                                n - nfound, new_laddr, &new_paddr, &do_cont);

            if (ret < 0) return ret;

            if (nfound && new_paddr != *paddr + nfound) {
                *cont = false;
                return nfound;
            } else if (!nfound && !*paddr) {
                *paddr = new_paddr;
            }

            nfound += ret;

            if (!do_cont) {
                *cont = false;
                return nfound;
            }
        }
    }

    *cont = true;
    return nfound;
}

ssize_t radixtree_lookup(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t max_ents, paddr_t *paddr) 
{
    GET_RADIX(idx_struct);
    *paddr = 0; 

    if (!radix->top_page) {
        // Check if someone else made this in the interrum.
        int merr = read_metadata(radix);
        if (merr < 0) return merr;
    }
    if (!radix->top_page) return 0;

    bool unused;
    ssize_t ret = index_and_find(radix, radix->top_page, radix->nlevels, 
                        max_ents, laddr, paddr, &unused);

    if (!*paddr) return -ENOENT;

    return ret;

#if 0
    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
    if (l1_current % 2) l2_current += 256;

    paddr_t l2_node = index_dev_node(radix, radix->top_page, l1_current);
    if (!l2_node) return 0;
    paddr_t l3_node = index_dev_node(radix, l2_node, l2_current);
    if (!l3_node) return 0;

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    if (l2_current % 2) l3_current += 256;
    paddr_t l4_node = index_dev_node(radix, l3_node, l3_current);
    if (!l4_node) return 0;
    
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    for (laddr_t l = 0; ; ++l) {
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_dev_node(radix, radix->top_page, l1);
            if (!l2_node) break;
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 % 2) l2 += 256;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_dev_node(radix, l2_node, l2);
            if (!l3_node) break;
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l2 % 2) l3 += 256;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_dev_node(radix, l3_node, l3_current);
            if (!l4_node) break;
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        if (l3 % 2) l4 += 256;
        
        paddr_t paddr;
        int err = lookup_dev_entry(radix, l4_node, l4, &paddr);
        if (err) return err;

        if (last_paddr != 0 && paddr != 1 + last_paddr) {
            break;
        } else if (paddr == 0) {
            break;
        } else {
            if (last_paddr == 0) *paddr_ret = paddr;
            last_paddr = paddr;
            ncontiguous++;
        }

        if (ncontiguous >= max_ents) break;
    }

    return ncontiguous;
#endif
}

int is_full(radixtree_meta_t *radix) {
    size_t max_size = radix->ents_per_node;
    for (int i = radix->nlevels; i > 1; --i) {
        max_size *= radix->ents_per_node;
    }

    return max_size == radix->nentries;
}

int can_shrink(radixtree_meta_t *radix) {
    if (!radix->nlevels) return 0;

    size_t max_size_on_shrink = 1;
    for (int i = radix->nlevels; i > 1; --i) {
        max_size_on_shrink *= radix->ents_per_node;
    }

    return max_size_on_shrink >= radix->nentries;
}

int can_grow(radixtree_meta_t *radix) {
    return radix->nlevels < radix->max_depth;
}

ssize_t index_and_insert(radixtree_meta_t *radix, paddr_t page, uint16_t level, size_t n, 
                     laddr_t laddr, paddr_t paddr) 
{
    laddr_t start_idx = laddr & radix->ent_idx_mask;
    ssize_t ninserted = 0;

    if (level == 1) {
        
        for (laddr_t l = 0; l < (laddr_t)n; ++l) {
            laddr_t new_idx = (laddr + l) & radix->ent_idx_mask;

            // Since we only fill forward, we never want to wrap around.
            if (new_idx < l) {
                break;
            }

            paddr_t new_paddr = paddr + l;

            int err = insert_dev_entry(radix, page, new_idx, new_paddr);
            if (err) return (ssize_t)err;

            ++ninserted;
        }

        return ninserted;
    }

    laddr_t level_shift = (level - 1) * radix->ent_shift;

    laddr_t index = (laddr >> level_shift) & radix->ent_idx_mask;
    laddr_t max_index = ((~0u) >> level_shift) & radix->ent_idx_mask;

    for (laddr_t i = index; i <= max_index && ninserted < n; ++i) {  
        laddr_t new_laddr = laddr + ninserted;
        paddr_t new_paddr = paddr + ninserted;

        paddr_t subpage = index_create_dev_node(radix, page, i);
        ssize_t ret = index_and_insert(radix, subpage, level - 1, 
                            n - ninserted, new_laddr, new_paddr);

        if (ret < 0) return ret;

        ninserted += ret;
    }

    return ninserted;
}

ssize_t radixtree_create(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages, paddr_t *paddr)
{
    GET_RADIX(idx_struct);

    ssize_t nexist = radixtree_lookup(idx_struct, inum, laddr, npages, paddr); 
    if (nexist > 0) return nexist;

    // Do block allocation
    ssize_t nalloc = CB(radix, cb_alloc_data, npages, paddr);
    if (nalloc == 0) return -ENOMEM;
    if (nalloc < 0) return nalloc;

    // Gotta create the top page if it disappeared.
    if (!radix->top_page) {
        // Need to first allocate the top page.
        ssize_t nalloc = CB(radix, cb_alloc_metadata, 
                            radix->nblk_per_node, &(radix->top_page));
        if (nalloc != radix->nblk_per_node) return -ENOMEM;

        radix->nlevels = 1;

        int werr = write_metadata(radix);
        if (werr) return werr;
    }

    ssize_t ninserted = 0;
    while (ninserted < nalloc) {
        laddr_t new_laddr = laddr + ninserted;
        paddr_t new_paddr = (*paddr) + ninserted;
        
        int ret = index_and_insert(radix, radix->top_page, radix->nlevels, nalloc - ninserted, new_laddr, new_paddr);

        if (ret < 0) return ret;
        if (!ret) break;

        ninserted += ret;
        radix->nentries += ret;

        if (is_full(radix) && can_grow(radix)) {
            paddr_t old_top = radix->top_page;
            ssize_t nalloc = CB(radix, cb_alloc_metadata, 
                            radix->nblk_per_node, &(radix->top_page));
            if (nalloc != radix->nblk_per_node) return -ENOMEM;
            
            int err = insert_dev_entry(radix, radix->top_page, 0, old_top);
            if (err) return err;

            ++radix->nlevels;
            
        } else if (is_full(radix)) {
            return ninserted ? ninserted : -ENOSPC;
        }
    }
    
    if_then_panic(write_metadata(radix), "Could not update metadata!");
    return nalloc;
#if 0
    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
    if (l1_current % 2) l2_current += 256;

    paddr_t l2_node = index_create_dev_node(radix, radix->top_page, l1_current);
    paddr_t l3_node = index_create_dev_node(radix, l2_node, l2_current);

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    if (l2_current % 2) l3_current += 256;
    paddr_t l4_node = index_create_dev_node(radix, l3_node, l3_current);
    
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    for (laddr_t l = 0; l < nalloc; ++l) {
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_create_dev_node(radix, radix->top_page, l1);
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 % 2) l2 += 256;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_create_dev_node(radix, l2_node, l2);
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l2 % 2) l3 += 256;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_create_dev_node(radix, l3_node, l3_current);
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        if (l3 % 2) l4 += 256;
        
        int err = insert_dev_entry(radix, l4_node, l4, *paddr + (size_t)l);
        if (err) return err;
    }

    return nalloc;
#endif
}

ssize_t index_and_remove(radixtree_meta_t *radix, paddr_t page, uint16_t level, 
                         size_t n, laddr_t laddr) 
{
    laddr_t start_idx = laddr & radix->ent_idx_mask;
    ssize_t nremoved = 0;

    if (level == 1) {
        
        for (laddr_t l = 0; l < (laddr_t)n; ++l) {
            laddr_t new_idx = (laddr + l) & radix->ent_idx_mask;

            if (new_idx < start_idx) {
                break;
            }

            paddr_t old_ent;
            int err = lookup_dev_entry(radix, page, new_idx, &old_ent);
            if (err) return err;
            if (!old_ent) break;

            ssize_t dealloc_ret = CB(radix, cb_dealloc_data, 1, old_ent);
            if_then_panic(dealloc_ret != 1, "Could not remove data page!");

            err = insert_dev_entry(radix, page, new_idx, 0);
            if (err) return err;

            ++nremoved;
        }

        return nremoved;
    }

    laddr_t level_shift = (level - 1) * radix->ent_shift;

    laddr_t index = (laddr >> level_shift) & radix->ent_idx_mask;
    laddr_t max_index = ((~0u) >> level_shift) & radix->ent_idx_mask;

    while (nremoved < n) {
        for (laddr_t i = index; i < max_index; ++i) {  
            laddr_t new_laddr = laddr + nremoved;

            paddr_t subpage = index_dev_node(radix, page, i);
            ssize_t ret = index_and_remove(radix, subpage, level - 1, 
                                n - nremoved, new_laddr);

            if (ret < 0) return ret;

            nremoved += ret;
        }
    }

    return nremoved;
}

ssize_t radixtree_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages) 
{
    GET_RADIX(idx_struct);

    if_then_panic(laddr + npages != radix->nentries, "Cannot remove from the middle!");

    ssize_t ret = index_and_remove(radix, radix->top_page, radix->nlevels, 
                                   npages, laddr);

    if (ret < npages) return ret;

    radix->nentries -= ret;

    while (can_shrink(radix)) {
        paddr_t old_top = radix->top_page;

        if (radix->nlevels == 1) {
            radix->top_page = 0;
        } else {
            int err = lookup_dev_entry(radix, radix->top_page, 0, &(radix->top_page));
            if (err) return err;
        }

        ssize_t dealloc_ret = CB(radix, cb_dealloc_data, 1, old_top);
        if_then_panic(dealloc_ret != 1, "Could not remove data page!");

        radix->nlevels -= 1;
    }

    if_then_panic(write_metadata(radix), "Could not update metadata!");

    return ret;

    #if 0

    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
    if (l1_current % 2) l2_current += 256;

    paddr_t l2_node = index_dev_node(radix, radix->top_page, l1_current);
    paddr_t l3_node = index_dev_node(radix, l2_node, l2_current);

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    if (l2_current % 2) l3_current += 256;
    paddr_t l4_node = index_dev_node(radix, l3_node, l3_current);
    
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    // Go backwards so if we hit zero, we know we can free us up some metadata.
    for (laddr_t n = npages; n > 0; --n) {
        laddr_t l = n - 1;
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_dev_node(radix, radix->top_page, l1);
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 % 2) l2 += 256;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_dev_node(radix, l2_node, l2);
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l2 % 2) l3 += 256;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_dev_node(radix, l3_node, l3_current);
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        if (l3 % 2) l4 += 256;

        paddr_t old_ent;
        int err = lookup_dev_entry(radix, l4_node, l4, &old_ent);
        if (err) return err;
        if (!old_ent) break;

        ssize_t dealloc_ret = CB(radix, cb_dealloc_data, 1, old_ent);
        if_then_panic(dealloc_ret != 1, "Could not remove data page!");

        err = insert_dev_entry(radix, l4_node, l4, 0);
        if (err) return err;

        // De-allocate pages if no longer needed
        if (l4 == 0) {

            ssize_t ndealloc = CB(radix, cb_dealloc_metadata, 1, l4_node);
            if_then_panic(ndealloc != 1, "Could not deallocate!");

            err = insert_dev_entry(radix, l3_node, l3, 0);
            if (err) return err;

            if (l3 == 0) {

                ndealloc = CB(radix, cb_dealloc_metadata, 1, l3_node);
                if_then_panic(ndealloc != 1, "Could not deallocate!");

                err = insert_dev_entry(radix, l2_node, l2, 0);
                err = insert_dev_entry(radix, l2_node, l2 + 1, 0);
                if (err) return err;

                if (l2 == 0) {
                    ndealloc = CB(radix, cb_dealloc_metadata, 1, l2_node);
                    if_then_panic(ndealloc != 1, "Could not deallocate!");

                    err = insert_dev_entry(radix, radix->top_page, l1, 0);
                    err = insert_dev_entry(radix, radix->top_page, l1 + 1, 0);
                    if (err) return err;

                    if (l1 == 0) {
                        ndealloc = CB(radix, cb_dealloc_metadata, 1, radix->top_page);
                        if_then_panic(ndealloc != 1, "Could not deallocate!");

                        radix->top_page = 0;
                        if_then_panic(write_metadata(radix), "Could not update metadata!");
                    }
                }
            }
        }
    }

    return npages;
    #endif
}

int radixtree_set_caching(idx_struct_t *idx_struct, bool enable) {
    return 0;
}

int radixtree_persist(idx_struct_t *idx_struct) {
    return 0;
}

int radixtree_invalidate(idx_struct_t *idx_struct) {
    return 0;
}

idx_fns_t radixtree_fns = {
    .im_init          = NULL,
    .im_init_prealloc = radixtree_init,
    .im_lookup        = radixtree_lookup,
    .im_create        = radixtree_create,
    .im_remove        = radixtree_remove,

    .im_set_caching   = radixtree_set_caching,
    .im_persist       = radixtree_persist,
    .im_invalidate    = radixtree_invalidate
};
