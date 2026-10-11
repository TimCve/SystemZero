#include "mem.h"
#include "../../env_vars.h"
static uint32_t free_mem_addr = HEAP_START;
static env_vars_t *environment;
void mem_bind_env(env_vars_t *env) { environment = env; }
static uint32_t critical_enter(void) {
    uint32_t flags = 0;
#ifdef SYSTEMZERO_GUEST
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
#endif
    return flags;
}
static void critical_leave(uint32_t flags) {
#ifdef SYSTEMZERO_GUEST
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory");
#else
    (void)flags;
#endif
}
uint32_t malloc(uint32_t size, int align, uint32_t *physical) {
    uint32_t flags = critical_enter();
    if (environment && environment->free_mem_ptr > free_mem_addr) free_mem_addr = environment->free_mem_ptr;
    uint32_t start = free_mem_addr;
    if (physical) *physical = 0;
    if (align) start = (start + 4095u) & ~4095u;
    else start = (start + 3u) & ~3u;
    if (!size || start < HEAP_START || start >= HEAP_END || size > HEAP_END - start) {
        critical_leave(flags); return 0;
    }
    free_mem_addr = start + size;
    if (physical) *physical = start;
    if (environment) environment->free_mem_ptr = free_mem_addr;
    critical_leave(flags);
    return start;
}
uint32_t get_free_ptr(void) {
    if (environment && environment->free_mem_ptr > free_mem_addr) free_mem_addr = environment->free_mem_ptr;
    return free_mem_addr;
}
void set_free_ptr(uint32_t value) {
    if (value >= HEAP_START && value <= HEAP_END) free_mem_addr = value;
}
void heap_release(uint32_t marker) {
    get_free_ptr();
    if (marker >= HEAP_START && marker <= free_mem_addr) {
        memset((void *)marker, 0, free_mem_addr - marker);
        free_mem_addr = marker;
        if (environment) environment->free_mem_ptr = marker;
    }
}
void *memcpy(void *dst, const void *src, uint32_t count) {
    uint8_t *d = dst; const uint8_t *s = src; while (count--) *d++ = *s++; return dst;
}
void *memset(void *dst, int value, uint32_t count) {
    uint8_t *d = dst; while (count--) *d++ = (uint8_t)value; return dst;
}
