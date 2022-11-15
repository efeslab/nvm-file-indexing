#ifndef __NVMIDX_FILE_INDEXING_H__
#define __NVMIDX_FILE_INDEXING_H__

/*
 * Here we just glue together all the submodules with different indexing
 * structures.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "common/common.h"
#include "extents/extents.h"
#include "cuckoohash/cuckoohash.h"
#include "hashtable/hashtable.h"
#include "levelhash/levelhash.h"
#include "radixtree/radixtree.h"

#ifdef __cplusplus
}
#endif

#endif  // __NVMIDX_FILE_INDEXING_H__
