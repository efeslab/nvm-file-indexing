#ifndef __NVMIDX_FILE_INDEXING_H__
#define __NVMIDX_FILE_INDEXING_H__

#include "global.h"

/**
 * PUBLIC API for NVM File-System Indexing Structures
 */

/*
 * Section: Initialization
 *
 * Here we specify the parameters for the indexing structures.
 */

int init_structure(idx_struct_spec_t*, callback_methods_t*);
int set_backing_structure(idx_struct_spec_t*, idx_struct_spec_t*);

/*
 * Section: General operations
 *
 * Here we specify what you can do with an indexing structure.
 */

int lookup_index();
int create_index();
int remove_indices();

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

int

#endif  // __NVMIDX_FILE_INDEXING_H__
