#ifndef __NVM_IDX_COMMON_STATS_H__
#define __NVM_IDX_COMMON_STATS_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#if defined(__i386__)

static inline unsigned long long _asm_rdtscp(void)
{
	//unsigned hi, lo;
	//__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
	//return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );

	unsigned long long int x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}
#elif defined(__x86_64__)

static inline unsigned long long _asm_rdtscp(void)
{
	unsigned hi, lo;
	//__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
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

#define PFIELD(p, name) \
    printf("\t%s: (tsc/op) %lu / %lu (%lf)\n", xstr(name), \
            (p)->TSC(name), (p)->NR(name),\
            (double)(p)->TSC(name) / (double)(p)->NR(name));

#define xstr(s) #s
#define STR(name, s, i) xstr(s)
#define SEP(X, I, rec, res) rec, res
#define STAT_SEP(X, I, rec, res) rec; res
#define SSEP(X, I, rec, res) rec; res


#define XP(name, s, i) printf("%s, %d\n", s, i)

#define DECLARE_TIMING() uint64_t _start_tsc
#define START_TIMING() _start_tsc = _asm_rdtscp()

#if 0
#define UPDATE_TIMING(s, f) do { \
    (void)__sync_add_and_fetch(&((s)->f ## _tsc), _asm_rdtscp() - _start_tsc);\
    (void)__sync_add_and_fetch(&((s)->f ## _nr), 1); } while (0)

#define UPDATE_STAT(s, f, v) \
    (void)__sync_add_and_fetch(&((s)->f ## _tsc), _asm_rdtscp() - v);\
    (void)__sync_add_and_fetch(&((s)->f ## _nr), 1)

#define ADD_STAT(s, f, n) (void)__sync_add_and_fetch(&((s)->f), (n))
#else
#define UPDATE_TIMING(s, f) do { \
    (s)->f ## _tsc +=  _asm_rdtscp() - _start_tsc;\
    (s)->f ## _nr +=  1; } while (0)

#define UPDATE_STAT(s, f, v) \
    (s)->f ## _tsc +=  _asm_rdtscp() - v;\
    (s)->f ## _nr += 1

#define ADD_STAT(s, f, n) (s)->f += n
#endif

#define INCR_STAT(s, f) ADD_STAT(s, f, 1)
#define INCR_NR_CACHELINE(s, f, sz) ADD_STAT(s, f, ((sz) / 64) > 1 ? ((sz) / 64) : 1)

#define INIT_STATS(s) memset(s, 0, sizeof(*s))

#ifdef __cplusplus
}
#endif

#endif//__NVM_IDX_COMMON_STATS_H__
