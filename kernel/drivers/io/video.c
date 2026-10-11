#include "video.h"
#include "../utils/ports.h"
static uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    port_long_out(0xcf8, 0x80000000u | ((uint32_t)bus << 16) |
                  ((uint32_t)device << 11) | ((uint32_t)function << 8) | (offset & 0xfc));
    return port_long_in(0xcfc);
}
static void vbe_write(uint16_t index, uint16_t value) {
    port_word_out(0x1ce, index); port_word_out(0x1cf, value);
}
static uint16_t vbe_read(uint16_t index) {
    port_word_out(0x1ce, index); return port_word_in(0x1cf);
}
volatile uint32_t *video_init(void) {
    /* Discover the standard VGA PCI BAR instead of assuming its physical address. */
    uint32_t base = 0;
    for (uint32_t bus = 0; bus < 256 && !base; bus++)
        for (uint8_t device = 0; device < 32 && !base; device++) {
            if (pci_read(bus, device, 0, 0) == 0xffffffffu) continue;
            uint8_t functions = pci_read(bus, device, 0, 12) & 0x800000u ? 8 : 1;
            for (uint8_t function = 0; function < functions; function++) {
                if (pci_read(bus, device, function, 0) != 0x11111234u) continue;
                uint32_t bar = pci_read(bus, device, function, 16);
                if ((bar & 1) || (bar & 6) || !(bar & ~15u)) continue;
                uint16_t command = pci_read(bus, device, function, 4);
                port_word_out(0xcfc, command | 3);
                base = bar & ~15u; break;
            }
        }
    uint16_t id = vbe_read(0);
    if (!base || base > 0xffffffffu - VIDEO_WIDTH * VIDEO_HEIGHT * 4 ||
        id < 0xb0c0 || id > 0xb0c5) return 0;
    vbe_write(4, 0); vbe_write(1, VIDEO_WIDTH); vbe_write(2, VIDEO_HEIGHT);
    vbe_write(3, 32); vbe_write(6, VIDEO_WIDTH); vbe_write(8, 0); vbe_write(9, 0);
    vbe_write(4, 0x41); /* Enable the linear framebuffer. */
    if (vbe_read(1) != VIDEO_WIDTH || vbe_read(2) != VIDEO_HEIGHT || vbe_read(3) != 32) {
        vbe_write(4, 0); return 0;
    }
    return (volatile uint32_t *)base;
}
