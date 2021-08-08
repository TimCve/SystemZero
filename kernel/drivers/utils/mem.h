#include <stdint.h>

uint32_t malloc(uint32_t size, int align, uint32_t *phys_addr); 

void set_free_ptr(uint32_t amount);
uint32_t get_free_ptr();

void *memcpy(void *dst, const void *src, uint32_t cnt);