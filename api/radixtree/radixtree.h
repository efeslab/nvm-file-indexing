#ifndef __NVM_IDX_RADIXTREE_RADIXTREE_H__
#define __NVM_IDX_RADIXTREE_RADIXTREE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radixtree_metadata {
    bool cached;
    paddr_t top_level_page;
    paddr_t *cached_tree;


} radixtree_meta_t;

int radixtree_init(const idx_spec_t*, const paddr_range_t*, idx_struct_t*);

ssize_t radixtree_lookup(idx_struct_t*, inum_t, laddr_t, paddr_t*);

ssize_t radixtree_create(idx_struct_t*, inum_t, laddr_t, size_t, paddr_t*);

ssize_t radixtree_remove(idx_struct_t*, inum_t, laddr_t, size_t);

#ifdef __cplusplus
}
#endif

#endif
