#include <stdint.h>
#include "mem.h"

// an address in memory that is known to be free
uint32_t free_mem_addr = 0x10000;

uint32_t malloc(uint32_t size, int align, uint32_t *phys_addr) {
    // pages alligned to 0x1000 (4K)
    if(align == 1 && (free_mem_addr & 0xFFFFF000)) {
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }
    
    // save physical address
    if(phys_addr) *phys_addr = free_mem_addr;

    // return the start of free memory
    uint32_t ret = free_mem_addr;

    // increment free memory pointer
    free_mem_addr += size; 
    return ret;
} 

void move_free_ptr_back(uint32_t amount) {
    free_mem_addr -= amount;
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