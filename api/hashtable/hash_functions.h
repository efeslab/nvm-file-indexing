#ifndef __NVM_IDX_HASH_FUNCTIONS_H__
#define __NVM_IDX_HASH_FUNCTIONS_H__

#include <stdint.h>

#include "common/common.h"
#include "xxhash.h"

typedef laddr_t (*hash_func_t)(paddr_t key);

// https://gist.github.com/badboy/6267743
static inline laddr_t nvm_idx_hash6432shift(paddr_t key) {
  key = (~key) + (key << 18); // key = (key << 18) - key - 1;
  key = key ^ (key >> 31);
  key = key * 21; // key = (key + (key << 2)) + (key << 4);
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (laddr_t) key;
}

static inline laddr_t jenkins_hash(paddr_t a) {
    a = (a+0x7ed55d16) + (a<<12);
    a = (a^0xc761c23c) ^ (a>>19);
    a = (a+0x165667b1) + (a<<5);
    a = (a+0xd3a2646c) ^ (a<<9);
    a = (a+0xfd7046c5) + (a<<3);
    a = (a^0xb55a4f09) ^ (a>>16);
    return (laddr_t)a;
}

static inline laddr_t murmur64(paddr_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53L;
    h ^= h >> 33;
    return (laddr_t)h;
}

static inline laddr_t mix(paddr_t c)
{
    paddr_t a = 0xff51afd7ed558ccdL;
    paddr_t b = 0xc4ceb9fe1a85ec53L;
	a=a-b;  a=a-c;  a=a^(c >> 13);
	b=b-c;  b=b-a;  b=b^(a << 8);
	c=c-a;  c=c-b;  c=c^(b >> 13);
	a=a-b;  a=a-c;  a=a^(c >> 12);
	b=b-c;  b=b-a;  b=b^(a << 16);
	c=c-a;  c=c-b;  c=c^(b >> 5);
	a=a-b;  a=a-c;  a=a^(c >> 3);
	b=b-c;  b=b-a;  b=b^(a << 10);
	c=c-a;  c=c-b;  c=c^(b >> 15);
	return (laddr_t)c;
}

static inline laddr_t nvm_idx_direct_hash (paddr_t v) {
  return (laddr_t)(v & 0xFFFFFFFF);
}

static inline laddr_t nvm_idx_combo_hash (paddr_t v) {
    return (laddr_t)((v & 0xFFFFFFFF) ^ ((v >> 32) & 0xFFFFFFFF));
}

static inline laddr_t nvm_xxhash(paddr_t v) {
    static uint64_t seed = 0;   /* or any other value */
    return (laddr_t)XXH32(&v, sizeof(v), seed);
}

#endif  //__HASH_FUNCTIONS_H__
