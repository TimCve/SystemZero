#include "ata.h"
#include "../utils/ports.h"
#define BSY 0x80
#define DRQ 0x08
#define ERR 0x21
#ifndef ATA_POLL_LIMIT
#define ATA_POLL_LIMIT 1000000u
#endif
static uint16_t io_base = 0x1f0, ctrl_base = 0x3f6;
static uint8_t drive, slave, last_error;
static uint32_t capacities[4];
void select_drive(uint8_t value) {
    drive = value; slave = value & 1;
    io_base = value < 2 ? 0x1f0 : 0x170;
    ctrl_base = value < 2 ? 0x3f6 : 0x376;
}
uint8_t selected_ata_drive(void) { return drive; }
static void delay(void) { for (int i = 0; i < 4; i++) (void)port_byte_in(ctrl_base); }
static int wait_status(int data) {
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; i++) {
        uint8_t s = port_byte_in(io_base + 7);
        if (!s || s == 0xff) { last_error = 0xff; return 0; }
        if (s & BSY) continue;
        if (data >= 0 && (s & ERR)) { last_error = port_byte_in(io_base + 1); if (!last_error) last_error = s; return 0; }
        if (data <= 0 || (s & DRQ)) return 1;
    }
    last_error = 0xff; return 0;
}
int identify_drive(uint8_t code) {
    if (drive >= 4) return 0;
    last_error = 0; capacities[drive] = 0;
    port_byte_out(ctrl_base, 2);
    port_byte_out(io_base + 6, code & 0xf0); delay();
    port_byte_out(io_base + 2, 0); port_byte_out(io_base + 3, 0);
    port_byte_out(io_base + 4, 0); port_byte_out(io_base + 5, 0);
    port_byte_out(io_base + 7, 0xec); delay();
    if (!wait_status(1)) return 0;
    if (port_byte_in(io_base + 4) || port_byte_in(io_base + 5)) return 2;
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = port_word_in(io_base);
    delay();
    if (!(id[49] & (1u << 9)) || !wait_status(0)) return 0;
    capacities[drive] = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (capacities[drive] > 0x10000000u) capacities[drive] = 0x10000000u;
    return capacities[drive] ? 1 : 0;
}
uint32_t ata_sector_count(void) {
    if (drive >= 4) return 0;
    if (!capacities[drive]) identify_drive(0xa0 | (slave << 4));
    return capacities[drive];
}
static int command(uint32_t lba, uint8_t count, uint8_t cmd) {
    last_error = 0;
    uint32_t capacity = ata_sector_count();
    if (!count || !capacity || lba >= capacity || count > capacity - lba) { last_error = 4; return 0; }
    port_byte_out(io_base + 6, 0xe0 | (slave << 4) | ((lba >> 24) & 15)); delay();
    if (!wait_status(-1)) return 0;
    port_byte_out(io_base + 2, count); port_byte_out(io_base + 3, (uint8_t)lba);
    port_byte_out(io_base + 4, (uint8_t)(lba >> 8)); port_byte_out(io_base + 5, (uint8_t)(lba >> 16));
    port_byte_out(io_base + 7, cmd); delay(); return 1;
}
int read_sectors_ATA_PIO(void *target, uint32_t lba, uint8_t count) {
    if (!target || !command(lba, count, 0x20)) return 0;
    uint8_t *out = target;
    for (uint32_t s = 0; s < count; s++) {
        if (!wait_status(1)) return 0;
        for (int i = 0; i < 256; i++) {
            uint16_t word = port_word_in(io_base);
            *out++ = (uint8_t)word; *out++ = word >> 8;
        }
        delay();
    }
    return wait_status(0);
}
int write_sectors_ATA_PIO(uint32_t lba, uint8_t count, const void *source) {
    if (!source || !command(lba, count, 0x30)) return 0;
    const uint8_t *in = source;
    for (uint32_t s = 0; s < count; s++) {
        if (!wait_status(1)) return 0;
        for (int i = 0; i < 256; i++) {
            port_word_out(io_base, (uint16_t)in[0] | ((uint16_t)in[1] << 8)); in += 2;
        }
        delay();
    }
    return wait_status(0);
}
int ata_flush(void) {
    if (!ata_sector_count() || !wait_status(-1)) return 0;
    port_byte_out(io_base + 6, 0xe0 | (slave << 4)); delay();
    if (!wait_status(-1)) return 0;
    port_byte_out(io_base + 7, 0xe7); delay();
    return wait_status(0);
}
uint8_t ATA_get_ERROR(void) { return last_error; }
