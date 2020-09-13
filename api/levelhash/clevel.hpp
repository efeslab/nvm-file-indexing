#ifndef __CLEVEL_LAYER__

#ifdef __cplusplus
extern "C" {
#endif
#include "common/common.h"

int clevel_init(void **clevel);

ssize_t clevel_lookup(void *clevel, laddr_t blk, paddr_t *pblk);
ssize_t clevel_create(void *clevel, laddr_t blk, paddr_t *pblk, size_t n);
ssize_t clevel_remove(void *clevel, laddr_t blk, size_t n);

#ifdef __cplusplus
}
#endif

#endif
