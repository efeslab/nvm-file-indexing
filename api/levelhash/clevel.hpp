#ifndef __CLEVEL_LAYER__

#ifdef __cplusplus
extern "C" {
#endif
#include "common/common.h"

int clevel_init(void);

ssize_t clevel_lookup(inum_t inum, laddr_t blk, paddr_t *pblk);
ssize_t clevel_create(inum_t inum, laddr_t blk, paddr_t pblk);
ssize_t clevel_remove(inum_t inum, laddr_t blk);

#ifdef __cplusplus
}
#endif

#endif
