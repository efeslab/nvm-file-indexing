#ifndef __NVMIDX_GLOBAL_H__
#define __NVMIDX_GLOBAL_H__

#include <stdint.h>

/*
 * Section: Globally-used definitions and typedefs.
 */

typedef void* (*malloc_fn_t)(size_t);
typedef void  (*free_fn_t)(void*);

typedef uint32_t inum_t;
typedef uint32_t laddr_t;
typedef uint64_t paddr_t;


/*******************************************************************************
 * Section: Data structures used globally.
 ******************************************************************************/

typedef struct indexing_methods {
} idx_methods_t;

typedef struct callback_methods {
} callback_methods_t;

/*
 * Each indexing structure defines a global specification object, which is then
 * used to initialize the instance structure, indexing_structure_t.
 */
typedef struct indexing_structure_specification {
  idx_methods_t *iss_methods;
} idx_struct_spec_t;


typedef struct indexing_structure {

} idx_struct_t;


#endif  // __NVMIDX_GLOBAL_H__
