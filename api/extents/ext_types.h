#ifndef __NVM_IDX_EXT_TYPES_H__
#define __NVM_IDX_EXT_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common/common.h"

STAT_STRUCT(ext_stats,
            read_metadata_blocks,
            );

typedef struct nvm_api_extent_tree_metadata {
    paddr_range_t et_direct_range;
    // stats struct
    bool et_enable_stats;
    ext_stats_t *et_stats;
    // could be extent leaf or branch nodes
    void *et_direct_data;
} ext_meta_t;

#ifdef __cplusplus
}
#endif

#endif
