#include <stdint.h>

/* At this stage there is no 'free' implemented. */
uint32_t malloc(uint32_t size, int align, uint32_t *phys_addr); 

void *memcpy(void *dst, const void *src, uint32_t cnt);