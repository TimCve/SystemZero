#include <stdint.h>

uint32_t malloc(uint32_t size, int align, uint32_t *phys_addr); 
void move_free_ptr_back(uint32_t amount);

void *memcpy(void *dst, const void *src, uint32_t cnt);