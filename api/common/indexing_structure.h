#ifndef __NVMIDX_GLOBAL_H__
#define __NVMIDX_GLOBAL_H__

#include "types.h"
#include "indexing_methods.h"

typedef struct indexing_structure_specification {
    callback_fns_t *idx_callbacks;
    mem_man_fns_t  *idx_mem_man;
} idx_spec_t;

typedef struct indexing_structure {
    idx_fns_t      *idx_fns;
    idx_metadata_t *idx_metadata;
    callback_fns_t *idx_callbacks;
    mem_man_fns_t  *idx_mem_man;
} idx_struct_t;

/*
 * Some convenience callbacks.
 */

#define CB(i, c, ...) (i)->idx_callbacks->c(__VA_ARGS__)
#define FN(i, c, ...) (i)->idx_fns->c(__VA_ARGS__)
#define MALLOC(i, s)  (i)->idx_mem_man->mm_malloc(s)
#define FREE(i, p)    (i)->idx_mem_man->mm_free(p)
#define ZALLOC(i, s)  memset(MALLOC(i, s), 0, (s))

#endif  // __NVMIDX_GLOBAL_H__
