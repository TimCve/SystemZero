#include "mem.h"
static uint32_t free_mem_addr = HEAP_START;
uint32_t malloc(uint32_t size, int align, uint32_t *physical) {
    uint32_t start = free_mem_addr;
    if (physical) *physical = 0;
    if (align) start = (start + 4095u) & ~4095u;
    else start = (start + 3u) & ~3u;
    if (!size || start < HEAP_START || start >= HEAP_END || size > HEAP_END - start) return 0;
    free_mem_addr = start + size;
    if (physical) *physical = start;
    return start;
}
uint32_t get_free_ptr(void) { return free_mem_addr; }
void set_free_ptr(uint32_t value) {
    if (value >= HEAP_START && value <= HEAP_END) free_mem_addr = value;
}
void heap_release(uint32_t marker) {
    if (marker >= HEAP_START && marker <= free_mem_addr) {
        memset((void *)marker, 0, free_mem_addr - marker);
        free_mem_addr = marker;
    }
}
void *memcpy(void *dst, const void *src, uint32_t count) {
    uint8_t *d = dst; const uint8_t *s = src; while (count--) *d++ = *s++; return dst;
}
void *memset(void *dst, int value, uint32_t count) {
    uint8_t *d = dst; while (count--) *d++ = (uint8_t)value; return dst;
}
