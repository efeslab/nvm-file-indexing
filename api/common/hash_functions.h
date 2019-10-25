#ifndef __NVM_IDX_HASH_FUNCTIONS_H__
#define __NVM_IDX_HASH_FUNCTIONS_H__

#include <stdint.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "common/common.h"
#include "xxhash.h"

// SIMD stuff

typedef union {__m512i vec; uint64_t arr[8];} u512i_64_t;
typedef union {__m256i vec; uint32_t arr[8];} u256i_32_t;

typedef laddr_t (*hash_func_t)(paddr_t key);

static inline uint64_t
hash64_64(uint64_t x)
{
#if 0
    // score = 1.9091610493100439
    x ^= x >> 43;
    x *= UINT64_C(0x39752ae8694f2a0d);
    x ^= UINT64_C(0x949431f79ced2fbd);
    x -= x << 29;
    x ^= x >> 44;
    x  = (x << 63) | (x >> 1);
    x ^= UINT64_C(0x6418a53824bc556e);
    x *= UINT64_C(0x3a26c6c5babdd14d);
    x  = (x << 4) | (x >> 60);
    x ^= x >> 21;
    x  = (x << 40) | (x >> 24);
    x += UINT64_C(0x4b36939ae63f4138);
#else
    // score = 99.779449191701261
    x *= UINT64_C(0x8c98cab1667ed515);
    x ^= x >> 57;
    x ^= x >> 21;
    x ^= UINT64_C(0xac274618482b6398);
    x ^= x >> 3;
    x *= UINT64_C(0x6908cb6ac8ce9a09);
#endif
    return x;
}

static inline 
void hash64_simd64(u512i_64_t *keys, u512i_64_t *hash_values) {
  __m512i x = keys->vec;

  x = _mm512_mullo_epi64(x, _mm512_set1_epi64(UINT64_C(0x8c98cab1667ed515)));
  x = _mm512_xor_epi64(x, _mm512_srli_epi64(x, 57));
  x = _mm512_xor_epi64(x, _mm512_srli_epi64(x, 21));
  x = _mm512_xor_epi64(x, _mm512_set1_epi64(UINT64_C(0xac274618482b6398)));
  x = _mm512_xor_epi64(x, _mm512_srli_epi64(x, 3));
  x = _mm512_mullo_epi64(x, _mm512_set1_epi64(UINT64_C(0x6908cb6ac8ce9a09)));

  hash_values->vec = x;
}

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

static inline laddr_t mix_seed(paddr_t c, paddr_t a)
{
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


static
void print_vec64(u512i_64_t *a)
{
    for (int i = 0; i < 8; ++i) {
        printf("[%d] %lu\n", i, a->arr[i]);
    }
}

static
void print_vec32(u256i_32_t *a)
{
    for (int i = 0; i < 8; ++i) {
        printf("[%d] %u\n", i, a->arr[i]);
    }
}

static inline
void mod_simd32(uint64_t mod, u512i_64_t *vals, u256i_32_t *ret) {
  for(int i = 0; i < 8; ++i) {
    ret->arr[i] = (uint32_t)(vals->arr[i] % mod);
  }
}

static void
nvm_mixHash_simd64_helper(__m512i *first, __m512i *second, __m512i *third, int right, uint32_t shiftCount) {
  *first = _mm512_sub_epi64(*first, *second); //first = first - second
  *first = _mm512_sub_epi64(*first, *third); //first = first - third
  __m512i bsTemp;
  if(right) {
    bsTemp = _mm512_srli_epi64(*third, shiftCount); //>>
  }
  else {
    bsTemp = _mm512_slli_epi64(*third, shiftCount); //<<
  }
  *first = _mm512_xor_epi64(*first, bsTemp); //first = first XOR (third (<< || >>) shiftcount)
}


static void nvm_mixHash_simd64(uint32_t mod, u512i_64_t *keys, u256i_32_t *node_indices) {
  int RIGHT = 1;
  int LEFT = 0;

  __m512i a = _mm512_set1_epi64(0xff51afd7ed558ccdL);
  __m512i b = _mm512_set1_epi64(0xc4ceb9fe1a85ec53L);
  __m512i *c = &keys->vec;

  nvm_mixHash_simd64_helper(&a, &b, c, RIGHT, 13);
  nvm_mixHash_simd64_helper(&b, c, &a, LEFT, 8);
  nvm_mixHash_simd64_helper(c, &a, &b, RIGHT, 13);
  nvm_mixHash_simd64_helper(&a, &b, c, RIGHT, 12);
  nvm_mixHash_simd64_helper(&b, c, &a, LEFT, 16);
  nvm_mixHash_simd64_helper(c, &a, &b, RIGHT, 5);
  nvm_mixHash_simd64_helper(&a, &b, c, RIGHT, 3);
  nvm_mixHash_simd64_helper(&b, c, &a, LEFT, 10);
  nvm_mixHash_simd64_helper(c, &a, &b, RIGHT, 15);

  u256i_32_t hash_values;
  hash_values.vec = _mm512_cvtepi64_epi32(*c); // direct hash with truncation to 32-bit
  //mod_simd32(mod, &hash_values, node_indices);
}
#endif  //__HASH_FUNCTIONS_H__
