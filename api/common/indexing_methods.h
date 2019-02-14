#ifndef __NVMIDX_COMMON_INDEXING_METHODS_H__
#define __NVMIDX_COMMON_INDEXING_METHODS_H__

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

int persist_updates();
int clear_caches();

/*
 * Section: Profiling (and statistics)
 *
 * Functions for monitoring performance and profiling structures.
 */


/*******************************************************************************
 * Section: Data Types
 ******************************************************************************/

typedef struct indexing_functions {
    init_structure_fn_t im_init;
    lookup_index_fn_t im_lookup;
    create_index_fn_t im_create;
    remove_index_fn_t im_remove;
} idx_fns_t;

#endif  // __INDEXING_METHODS_H__
