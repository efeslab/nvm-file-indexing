#ifndef __NVM_IDX_COMMON_STATS_H__
#define __NVM_IDX_COMMON_STATS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "p99/p99_for.h"

#if defined(__i386__)

static inline unsigned long long _asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );

}
#elif defined(__x86_64__)

static inline unsigned long long _asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#else
#error "Only support for X86 architecture"
#endif

#define NARG(...) (int)(sizeof((int[]){__VA_ARGS__})/sizeof(int))

#define STAT_FIELD(name) \
    uint64_t name ## _tsc; \
    uint64_t name ## _nr

#define STAT_FIELD_VAR(X, name, i) \
    uint64_t name ## _tsc; \
    uint64_t name ## _nr

#define TSC(name) name ## _tsc
#define NR(name)  name ## _nr
#define SFIELD(X, name, i) \
    uint64_t TSC(name); \
    uint64_t NR(name)

#define PFIELD(p, name, i) \
    printf("\t%s: (tsc/op) %llu / %llu (%lf)\n", xstr(name), \
            (p)->TSC(name), (p)->NR(name),\
            (double)(p)->TSC(name) / (double)(p)->NR(name));

#define xstr(s) #s
#define STR(name, s, i) xstr(s)
#define SEP(X, I, rec, res) rec, res
#define STAT_SEP(X, I, rec, res) rec; res
#define SSEP(X, I, rec, res) rec; res

#define STAT_FIELDS(...) \
    int nfields = P99_NARG(__VA_ARGS__); \
    char *field_names[P99_NARG(__VA_ARGS__)] = \
        { P99_FOR(uint64_t, P99_NARG(__VA_ARGS__), SEP, STR, __VA_ARGS__) }; \
    P99_FOR(uint64_t, P99_NARG(__VA_ARGS__), SSEP, SFIELD, __VA_ARGS__); 

#define XP(name, s, i) printf("%s, %d\n", s, i)
#define P(...)\
    P99_FOR(uint64_t, P99_NARG(__VA_ARGS__), STAT_SEP, XP, __VA_ARGS__); 
//\ P99_FOR(uint64_t, P99_NARG(__VA_ARGS__), SEP, STAT_FIELD_VAR, __VA_ARGS__); 

#define DECLARE_TIMING() uint64_t _start_tsc
#define START_TIMING() _start_tsc = _asm_rdtscp()
#define UPDATE_TIMING(s, f) \
    (s)->f ## _tsc += _asm_rdtscp() - _start_tsc;\
    (s)->f ## _nr++

#define INIT_STATS(s) memset(s, 0, sizeof(*s))

#define STAT_STRUCT(name, ...) \
    typedef struct name { \
        P99_FOR(uint64_t, P99_NARG(__VA_ARGS__), SSEP, SFIELD, __VA_ARGS__); \
    } name  ## _t; \
    static void print_##name(struct name *s) { \
        printf("struct %s: \n", #name); \
        P99_FOR(s, P99_NARG(__VA_ARGS__), SSEP, PFIELD, __VA_ARGS__); \
    }


#ifdef __cplusplus
}
#endif

#endif//__NVM_IDX_COMMON_STATS_H__
