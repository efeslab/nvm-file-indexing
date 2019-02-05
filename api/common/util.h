#ifndef __OPTIMIZATIONS_H__
#define __OPTIMIZATIONS_H__

#include <stdlib.h>
#include <stdio.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define UNUSED(name) ({(void)(name);})

#define GDB_TRAP asm("int $3;");

void _panic(void);

#define panic(str) \
    do { \
        fprintf(stdout, "%s:%d %s(): %s\n",  \
                __FILE__, __LINE__, __func__, str); \
        fflush(stdout); \
        GDB_TRAP; \
        _panic(); \
    } while (0)

#define if_then_panic(a, str) if (1 == (a)) panic(str)

#endif
