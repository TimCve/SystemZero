#ifndef SYSTEMZERO_SYS_API_H
#define SYSTEMZERO_SYS_API_H
#include <stdint.h>

/* Stable i386 ABI: append services, never renumber existing entries. */
#define SZ_API_MAGIC 0x535a4150u
#define SZ_API_VERSION 1u
#define SZ_ENV_API_OFFSET 24u
#define SZ_API_FUNCTIONS_OFFSET 12u
enum { SZ_INT = 1, SZ_CHAR = 2, SZ_VOID = 3, SZ_POINTER = 4,
       SZ_INT_PTR = 5, SZ_CHAR_PTR = 6, SZ_VOID_PTR = 7 };
enum {
#define SZ_SERVICE(id, name, result, argc, a, b, c, d, e) SZ_API_##name = id,
#include "sys_api.def"
#undef SZ_SERVICE
    SZ_API_COUNT
};
struct env_vars;
typedef int32_t (*systemzero_service_t)(struct env_vars *, uint32_t, uint32_t,
                                      uint32_t, uint32_t, uint32_t);
typedef struct systemzero_api {
    uint32_t magic, version, count;
    systemzero_service_t functions[SZ_API_COUNT];
} systemzero_api_t;
#endif
