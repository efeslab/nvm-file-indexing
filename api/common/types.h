#ifndef __NVMIDX_COMMON_TYPES_H__
#define __NVMIDX_COMMON_TYPES_H__

#include <stdlib.h>
#include <stdint.h>

typedef uint32_t inum_t;
typedef uint32_t laddr_t;
typedef uint64_t paddr_t;

/*
 * Section: Forward References
 */

typedef struct indexing_structure idx_struct_t;
// Do this so that each structure can interpret it's own metadata
typedef void* idx_metadata_t;

/*
 * Section: Globally-used definitions and typedefs.
 */

typedef void* (*malloc_fn_t)(size_t);
typedef void  (*free_fn_t)(void*);

typedef struct memory_management_functions {
    malloc_fn_t mm_malloc;
    free_fn_t   mm_free;
} mem_man_fns_t;

/*******************************************************************************
 * Section: Method prototypes needed globally.
 ******************************************************************************/

typedef ssize_t (*writeback_fn_t)(paddr_t, off_t, size_t, const char*);
typedef ssize_t (*read_fn_t)(paddr_t, off_t, size_t, char*);
typedef ssize_t (*alloc_metadata_t)(size_t, paddr_t*);
typedef ssize_t (*alloc_data_t)(size_t, paddr_t*);

typedef struct device_info {
    size_t di_size_blocks;
    size_t di_block_size;
} device_info_t;

typedef int (*get_dev_info_fn_t)(device_info_t*);

typedef struct callback_functions {
    writeback_fn_t    cb_write;
    read_fn_t         cb_read;
    alloc_metadata_t  cb_alloc_metadata;
    alloc_data_t      cb_alloc_data;
    get_dev_info_fn_t cb_get_dev_info;
} callback_fns_t;

#endif //__NVMIDX_COMMON_TYPES_H__
