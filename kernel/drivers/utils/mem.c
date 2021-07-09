#include <stdint.h>
#include "mem.h"

uint32_t free_mem_addr = 0x10000;
/* Implementation is just a pointer to some free memory which
 * keeps growing */
uint32_t malloc(uint32_t size, int align, uint32_t *phys_addr) {
    /* Pages are aligned to 4K, or 0x1000 */
    if (align == 1 && (free_mem_addr & 0xFFFFF000)) {
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }
    /* Save also the physical address */
    if (phys_addr) *phys_addr = free_mem_addr;

    uint32_t ret = free_mem_addr;
    free_mem_addr += size; /* Remember to increment the pointer */
    return ret;
} 

void *memcpy(void *dst, const void *src, uint32_t cnt) {
    char *psz_dest = (char*)dst;
    const char *psz_source = (const char*)src;

    if((psz_dest != 0) && (psz_source != 0)) {
        while(cnt) {
            *(psz_dest++) = *(psz_source++);
            cnt--;
        }
    }

    return dst;
}