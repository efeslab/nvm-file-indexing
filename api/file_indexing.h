#ifndef __NVMIDX_FILE_INDEXING_H__
#define __NVMIDX_FILE_INDEXING_H__

/*
 * Here we just glue together all the submodules with different indexing
 * structures.
 */

#include "hashtable.h"

/*
 * Section: Initialization
 *
 * Here we specify the parameters for the indexing structures.
 */

int init_structure(idx_struct_spec_t*, callback_methods_t*);
int set_backing_structure(idx_struct_spec_t*, idx_struct_spec_t*);

/*
 * Section: Function Macros
 *
 * For convenience.
 */

#endif  // __NVMIDX_FILE_INDEXING_H__
