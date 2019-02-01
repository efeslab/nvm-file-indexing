#ifndef __HASH_FUNCTIONS_H__
#define __HASH_FUNCTIONS_H__

#include <stdint.h>

#include "global.h"

typedef laddr_t (*hash_func_t)(paddr_t key);

// https://gist.github.com/badboy/6267743
static inline laddr_t hash6432shift(paddr_t key)
{
  key = (~key) + (key << 18); // key = (key << 18) - key - 1;
  key = key ^ (key >> 31);
  key = key * 21; // key = (key + (key << 2)) + (key << 4);
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (uint32_t) key;
}

static inline laddr_t g_direct_hash (paddr_t v) {
  return (laddr_t)(v & 0xFFFFFFFF);
}

#endif  //__HASH_FUNCTIONS_H__
