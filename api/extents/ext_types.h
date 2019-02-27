#ifndef __NVM_IDX_EXT_TYPES_H__
#define __NVM_IDX_EXT_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common/common.h"

typedef struct extent_stats {
    STAT_FIELD(read_metadata_blocks);
    STAT_FIELD(read_from_device);
} ext_stats_t;

static void print_ext_stats(ext_stats_t *s) {
    printf("extent tree stats: \n");
    PFIELD(s, read_metadata_blocks);
    PFIELD(s, read_from_device);
}

#define MAX_DEPTH 10

typedef struct nvm_api_extent_tree_metadata {
    paddr_range_t et_direct_range;
    // stats struct
    bool et_enable_stats;
    ext_stats_t *et_stats;
    // buffers for the paths
    char **et_buffers;
    // could be extent leaf or branch nodes
    void *et_direct_data;
} ext_meta_t;

#ifdef __cplusplus
}
#endif

#endif
