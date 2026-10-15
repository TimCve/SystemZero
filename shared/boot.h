#ifndef SYSTEMZERO_BOOT_H
#define SYSTEMZERO_BOOT_H
#include <stdint.h>
#define SZ_BOOT_UEFI 0x535a5545u
/* Fixed-width handoff shared by the x64 firmware loader and the i386 kernel. */
typedef struct boot_info {
    uint32_t magic, size;
    uint32_t framebuffer, width, height, pitch, red_first, font;
    uint32_t heap_start, heap_end;
    uint32_t memory_map, memory_map_bytes, descriptor_bytes;
    uint32_t kernel_base;
} boot_info_t;
#endif
