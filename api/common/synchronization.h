#ifndef _SYNC_H_
#define _SYNC_H_

#ifdef __cplusplus
extern "C" {
#endif

// Synchronization primitives for libFS
// This part heavily depends on APIs provided by low level APIs
// This implementation uses pthread APIs

#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/futex.h>

#define m_barrier() asm volatile("": : :"memory")

/* Pause instruction to prevent excess processor bus usage */ 
#define cpu_relax() asm volatile("pause\n": : :"memory")

#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define atomic_inc(P) __sync_add_and_fetch((P), 1)
#define atomic_dec(P) __sync_add_and_fetch((P), -1) 
#define atomic_add(P, V) __sync_add_and_fetch((P), (V))
#define atomic_set_bit(P, V) __sync_or_and_fetch((P), 1<<(V))
#define atomic_clear_bit(P, V) __sync_and_and_fetch((P), ~(1<<(V)))

/*
inline int sys_futex(void *addr1, int op, int val1, 
		struct timespec *timeout, void *addr2, int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}
*/
#define sys_futex(addr1, op, val1, timeout, addr2, val3) \
	syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);

/* Atomic exchange (of various sizes) */
static inline void *xchg_64(void *ptr, void *x)
{
	__asm__ __volatile__("xchgq %0,%1"
			:"=r" ((unsigned long long) x)
			:"m" (*(volatile long long *)ptr), "0" ((unsigned long long) x)
			:"memory");

	return x;
}

static inline unsigned xchg_32(void *ptr, unsigned x)
{
	__asm__ __volatile__("xchgl %0,%1"
			:"=r" ((unsigned) x)
			:"m" (*(volatile unsigned *)ptr), "0" (x)
			:"memory");

	return x;
}

static inline unsigned short xchg_16(void *ptr, unsigned short x)
{
	__asm__ __volatile__("xchgw %0,%1"
			:"=r" ((unsigned short) x)
			:"m" (*(volatile unsigned short *)ptr), "0" (x)
			:"memory");

	return x;
}

static inline unsigned char xchg_8(void *ptr, unsigned char x)
{
	__asm__ __volatile__("xchgb %0,%1"
			:"=r" ((unsigned char) x)
			:"m" (*(volatile unsigned char*)ptr), "0" (x)
			:"memory");

	return x;
}

/* Test and set a bit */
static inline char atomic_bitsetandtest(void *ptr, int x)
{
	char out;
	__asm__ __volatile__("lock; bts %2,%1\n"
			"sbb %0,%0\n"
			:"=r" (out), "=m" (*(volatile long long *)ptr)
			:"Ir" (x)
			:"memory");

	return out;
}

#ifdef __cplusplus
}
#endif

#endif
