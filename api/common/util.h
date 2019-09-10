#ifndef __NVM_IDX_COMMON_UTIL_H__
#define __NVM_IDX_COMMON_UTIL_H__

#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define UNUSED(name) ({(void)(name);})

#define GDB_TRAP asm("int $3;");

void _panic(void);
#ifdef panic
#undef panic
#endif
#define panic(str, ...) \
    do { \
        fprintf(stdout, "%s:%d %s(): ",  \
                __FILE__, __LINE__, __func__); \
        fprintf(stdout, str, ## __VA_ARGS__); \
        fflush(stdout); \
        GDB_TRAP; \
        _panic(); \
    } while (0)

#define if_then_panic(a, str, ...) if (1 == (a)) panic(str, ## __VA_ARGS__)

#ifndef js_add_int64
#define js_add_int64(obj, name, val) json_object_object_add(obj, name, json_object_new_int64(val));
#endif

#ifndef js_add_double
#define js_add_double(obj, name, val) json_object_object_add(obj, name, json_object_new_double(val));
#endif

#endif
