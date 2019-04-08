#include "radixtree.h"

#include <math.h>

#define PRESENT_BIT (1llu << 63)
_Static_assert(PRESENT_BIT != 0llu, "Too much shift!");
#define ENT_IS_PRESENT(e) ((e) & PRESENT_BIT)
#define ENT_PADDR(e) ((e) & ~PRESENT_BIT)

#define MKADDR(i, l) ( ((paddr_t) i) << 32 | ((paddr_t)l) )

#define GET_RADIX(i) radixtree_meta_t *radix = (radixtree_meta_t*)(i)->idx_metadata

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
    ssize_t ret = CB(radix, cb_read,
                     radix->metadata_blk, 0, sizeof(ondev), (char*)&ondev);

    if (ret != sizeof(ondev)) return -EIO;
    if (ondev.magic == RADIX_MAGIC) {
        radix->top_page = ondev.top_page;
        return 1;
    }
    return 0;
}

// Returns 0 on success, or -errno otherwise.
static int write_metadata(radixtree_meta_t *radix) {
    ondev_radix_meta_t ondev = {
        .magic    = RADIX_MAGIC,
        .top_page = radix->top_page
    };

    ssize_t ret = CB(radix, cb_write,
                     radix->metadata_blk, 0, sizeof(ondev), (char*)&ondev);

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
    } else {
        err = get_page(radix, node->rn_page, &(node->rn_dev_contents));
        if (err) return err;
    }

    for (size_t i = 0; i < radix->ents_per_node; ++i) {
        radix_node_t *n = &(node->rn_cache_tree[i]);

        paddr_t entry = node->rn_dev_contents[i];
        int depth = node->rn_depth + 1;
        n->rn_depth = depth;
        n->rn_page  = entry;
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
                  "Bad index calculation! (%llu)\n", idx);
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
        int ret = write_entry(radix, node->rn_page, idx, node->rn_dev_contents);
        if (ret) return NULL;
    }

    if (!child->rn_dev_contents) {
        int err = create_radix_node(radix, entry, child->rn_depth, child);
        if (err) return NULL;
    }

    return child;
}

static int insert_entry(radixtree_meta_t *radix, radix_node_t *node, 
                        size_t idx, paddr_t entry)
{
    if_then_panic(node->rn_depth != radix->max_depth, 
                  "Don't use this for data blocks!\n");

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
    if_then_panic(node->rn_depth != radix->max_depth, 
                  "Don't use this for data blocks!\n");

    int rerr = read_radix_node_entry(radix, node, idx);
    if (rerr) return rerr;

    *entry = node->rn_dev_contents[idx];
    return 0;
}


/*******************************************************************************
 * Section: Public API Functions.
 ******************************************************************************/
int radixtree_init(const idx_spec_t* idx_spec, idx_struct_t* idx_struct, 
                   paddr_t *metadata_location) {

    if_then_panic(!*metadata_location, "Nowhere to put metadata!\n");
    
    radixtree_meta_t *radix = ZALLOC(idx_spec, sizeof(*radix));
    if_then_panic(!radix, "Could not allocate in-memory structure!\n");

    device_info_t di;
    int err = CB(idx_spec, cb_get_dev_info, &di);
    if_then_panic(err, "Could not get device size!");

    radix->blksz         = di.di_block_size;
    radix->ent_size      = sizeof(paddr_t);
    radix->ents_per_node = 1 << 9; // 8 bits worth
    radix->max_depth     = 4;
    radix->node_nbytes   = radix->ents_per_node * radix->ent_size;
    radix->nblk_per_node = radix->node_nbytes / radix->blksz;
    radix->ent_idx_mask  = radix->ents_per_node - 1;
    radix->ent_shift     = (size_t)log2((double)radix->ents_per_node);
    radix->metadata_blk  = *metadata_location; 
    radix->idx_callbacks = idx_spec->idx_callbacks;
    radix->idx_mem_man   = idx_spec->idx_mem_man; 

    // Check status of metadata.
    int merr = read_metadata(radix);
    if (merr < 0) return merr;

    if (!merr) {
        // Need to first allocate the top page.
        ssize_t nalloc = CB(radix, cb_alloc_metadata, 
                            radix->nblk_per_node, &(radix->top_page));
        if (nalloc != radix->nblk_per_node) return -ENOMEM;
        int werr = write_metadata(radix);
        if (werr) return werr;
    }

    // Read the top page.
    radix->cached_tree  = ZALLOC(radix, sizeof(*radix->cached_tree));
    if (!radix->cached_tree) return -ENOMEM;
    err = create_radix_node(radix, radix->top_page, 1, radix->cached_tree);
    if (err) return err;

    // If all of that is successful, finally set up structure.
    idx_struct->idx_metadata  = (void*)radix;
    idx_struct->idx_callbacks = idx_spec->idx_callbacks;
    idx_struct->idx_mem_man   = idx_spec->idx_mem_man;
    idx_struct->idx_fns       = &radixtree_fns;

    return 0;
}

// TODO might want to have a max lookup field.
ssize_t radixtree_lookup(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         paddr_t *paddr_ret) 
{
    GET_RADIX(idx_struct);
    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;

    radix_node_t *l2_node = index_node(radix, radix->cached_tree, l1_current);
    radix_node_t *l3_node = index_node(radix, l2_node, l2_current);

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    radix_node_t *l4_node = index_node(radix, l3_node, l3_current);
    
    *paddr_ret = 0; 
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    for (laddr_t l = 0; ; ++l) {
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_node(radix, radix->cached_tree, l1);
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_node(radix, l2_node, l2);
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_node(radix, l3_node, l3_current);
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        
        paddr_t paddr;
        int err = lookup_entry(radix, l4_node, l4, &paddr);
        if (err) return err;

        if (last_paddr != 0 && paddr != 1 + last_paddr) {
            return ncontiguous;
        } else if (paddr == 0) {
            return ncontiguous;
        } else {
            if (last_paddr == 0) *paddr_ret = paddr;
            last_paddr = paddr;
            ncontiguous++;
        }
    }

    return ncontiguous;
}

ssize_t radixtree_create(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages, paddr_t *paddr)
{
    GET_RADIX(idx_struct);

    // Do block allocation
    ssize_t nalloc = CB(radix, cb_alloc_data, npages, paddr);
    if (nalloc == 0) return -ENOMEM;
    if (nalloc < 0) return nalloc;

    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;

    radix_node_t *l2_node = index_node(radix, radix->cached_tree, l1_current);
    radix_node_t *l3_node = index_node(radix, l2_node, l2_current);

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    radix_node_t *l4_node = index_node(radix, l3_node, l3_current);
    
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    for (laddr_t l = 0; l < npages; ++l) {
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_node(radix, radix->cached_tree, l1);
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_node(radix, l2_node, l2);
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_node(radix, l3_node, l3_current);
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        
        int err = insert_entry(radix, l4_node, l4, *paddr + (size_t)l);
        if (err) return err;
    }

    return nalloc;
}

ssize_t radixtree_remove(idx_struct_t *idx_struct, inum_t inum, laddr_t laddr, 
                         size_t npages) 
{
    GET_RADIX(idx_struct);

    size_t l1_current = (((size_t)laddr) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
    size_t l2_current = (((size_t)laddr) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;

    radix_node_t *l2_node = index_node(radix, radix->cached_tree, l1_current);
    radix_node_t *l3_node = index_node(radix, l2_node, l2_current);

    // The following will change as we iterate.
    size_t l3_current = (laddr >> radix->ent_shift) & radix->ent_idx_mask;
    radix_node_t *l4_node = index_node(radix, l3_node, l3_current);
    
    size_t ncontiguous = 0;
    paddr_t last_paddr = 0;
    for (laddr_t l = 0; l < npages; ++l) {
        size_t l1 = (((size_t)laddr + l) >> (3 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l1 != l1_current) {
            l1_current = l1; 
            l2_node = index_node(radix, radix->cached_tree, l1);
        }

        size_t l2 = ((laddr + l) >> (2 * radix->ent_shift)) & radix->ent_idx_mask;
        if (l2 != l2_current) {
            l2_current = l2;
            l3_node = index_node(radix, l2_node, l2);
        }

        size_t l3 = ((laddr + l) >> radix->ent_shift) & radix->ent_idx_mask;
        if (l3 != l3_current) {
            l3_current = l3;
            l4_node = index_node(radix, l3_node, l3_current);
        }

        size_t l4 = (laddr + l) & radix->ent_idx_mask;
        
        int err = insert_entry(radix, l4_node, l4, 0);
        if (err) return err;

        // TODO deallocate page if no longer needed
    }

    return npages;
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
    .im_init          = radixtree_init,
    .im_init_prealloc = NULL,
    .im_lookup        = radixtree_lookup,
    .im_create        = radixtree_create,
    .im_remove        = radixtree_remove,

    .im_set_caching   = radixtree_set_caching,
    .im_persist       = radixtree_persist,
    .im_invalidate    = radixtree_invalidate
};
