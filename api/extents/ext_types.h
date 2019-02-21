#ifndef __NVM_IDX_EXT_TYPES_H__
#define __NVM_IDX_EXT_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvm_api_extent_tree_metadata {
    paddr_range_t et_direct_range;
    // could be extent leaf or branch nodes
    void* et_direct_data;
} ext_meta_t;

#ifdef __cplusplus
}
#endif

#endif
