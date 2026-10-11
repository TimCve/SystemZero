#ifndef SYSTEMZERO_MEM_H
#define SYSTEMZERO_MEM_H
#include <stdint.h>
#define HEAP_START 0x100000u
#define HEAP_END 0x1000000u
uint32_t malloc(uint32_t size, int align, uint32_t *physical);
uint32_t get_free_ptr(void);
void set_free_ptr(uint32_t value);
void heap_release(uint32_t marker);
void *memcpy(void *dst, const void *src, uint32_t count);
void *memset(void *dst, int value, uint32_t count);
#endif
