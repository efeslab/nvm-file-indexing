#ifndef __NVMIDX_HASHTABLE_HASHTABLE_H__
#define __NVMIDX_HASHTABLE_HASHTABLE_H__

#include "common.h"

#include "inode_hash.h"

#define HASHTABLE_METADATA(i, n) GHashTable n = (GHashTable*)i->idx_metadata

#endif //__NVMIDX_HASHTABLE_HASHTABLE_H__

