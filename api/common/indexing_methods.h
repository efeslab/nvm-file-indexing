#ifndef __NVMIDX_COMMON_INDEXING_METHODS_H__
#define __NVMIDX_COMMON_INDEXING_METHODS_H__

#include <stdbool.h>
#include "types.h"

/*******************************************************************************
 * Indexing Structure Methods
 *
 * All indexing structures must implement methods with these prototypes.
 ******************************************************************************/


/*******************************************************************************
 * Section: Initialization and options
 *
 *
 ******************************************************************************/

typedef struct indexing_structure_specification idx_spec_t;

typedef int (*init_structure_fn_t)(const idx_spec_t*, idx_struct_t*, paddr_t*);

// Pre-alloc doesn't mean it can't grow, but rather this is where the metadata should live.
typedef int (*init_struct_prealloc_fn_t)(const idx_spec_t*, const paddr_range_t*, idx_struct_t*);

/*******************************************************************************
 * Section: General operations
 *
 * Here we specify what you can do with an indexing structure.
 ******************************************************************************/

/*
 * Lookup Index: Given an inum and a laddr of the file, find paddr of the
 * device that maps to the laddr.
 *
 * Returns -ERRNO on failure, and the number of contiguous blocks found with
 * paddr on success.
 */
typedef ssize_t (*lookup_index_fn_t)(idx_struct_t*, inum_t, laddr_t, paddr_t*);

typedef ssize_t (*create_index_fn_t)(idx_struct_t*, inum_t, laddr_t, size_t, paddr_t*);

typedef ssize_t (*remove_index_fn_t)(idx_struct_t*, inum_t, laddr_t, size_t);

/*
 * Section: Persistence (and caching)
 *
 * Specifies how indices are persisted or potentially cached.
 */

typedef int (*set_caching_fn_t)(idx_struct_t*, bool);
typedef int (*persist_updates_fn_t)(idx_struct_t*);
typedef int (*invalidate_caches_fn_t)(idx_struct_t*);

/*
 * Section: Profiling (and statistics)
 *
 * Functions for monitoring performance and profiling structures.
 */

typedef void (*set_stats_t)(idx_struct_t*, bool);
typedef void (*print_stats_t)(idx_struct_t*);


/*******************************************************************************
 * Section: Data Types
 ******************************************************************************/

typedef struct indexing_functions {
    init_structure_fn_t       im_init;
    init_struct_prealloc_fn_t im_init_prealloc;
    lookup_index_fn_t         im_lookup;
    create_index_fn_t         im_create;
    remove_index_fn_t         im_remove;

    set_caching_fn_t          im_set_caching;
    persist_updates_fn_t      im_persist;
    invalidate_caches_fn_t    im_invalidate;

    set_stats_t               im_set_stats;
    print_stats_t             im_print_stats;
} idx_fns_t;

#endif  // __INDEXING_METHODS_H__
