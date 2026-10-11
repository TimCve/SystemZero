#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/disk/ata.h"
static uint8_t command;
static uint32_t word_reads, word_writes, status_reads, absent, stuck, error, wait_drq, flushes;
static uint16_t written[512];
unsigned char port_byte_in(unsigned short port) {
    if (port == 0x1f7 || port == 0x177) {
        status_reads++;
        if (absent) return 0;
        if (stuck) return 0x80;
        if (error) return 0x41;
        if (wait_drq) { wait_drq--; return 0x40; }
        return 0x48;
    }
    if (port == 0x1f1 || port == 0x171) return 4;
    return 0;
}
void port_byte_out(unsigned short port, unsigned char value) {
    if (port == 0x1f7 || port == 0x177) {
        command = value; word_reads = 0;
        if (value == 0xe7) flushes++;
    }
}
unsigned short port_word_in(unsigned short port) {
    (void)port; uint32_t i = word_reads++;
    if (command == 0xec) {
        if (i == 49) return 1u << 9;
        if (i == 60) return 8192;
        return 0;
    }
    return i;
}
void port_word_out(unsigned short port, unsigned short value) {
    (void)port; assert(word_writes < 512); written[word_writes++] = value;
}
int main(void) {
    select_drive(0); assert(identify_drive(0xa0) == 1);
    assert(command == 0xec && word_reads == 256 && ata_sector_count() == 8192);
    uint16_t input[512], output[512];
    for (int i = 0; i < 512; i++) input[i] = i + 7;
    wait_drq = 5; status_reads = 0;
    assert(write_sectors_ATA_PIO(1, 2, input));
    assert(word_writes == 512 && !__builtin_memcmp(input, written, sizeof input) && status_reads > 5);
    assert(flushes == 0 && command == 0x30);
    assert(ata_flush() && flushes == 1 && command == 0xe7);
    error = 1; assert(!ata_flush()); error = 0;
    assert(read_sectors_ATA_PIO(output, 1, 2));
    for (int i = 0; i < 512; i++) assert(output[i] == i);
    /* Batched file reads may start at an odd payload offset in the destination. */
    uint8_t unaligned[1025];
    assert(read_sectors_ATA_PIO(unaligned + 1, 1, 2));
    for (int i = 0; i < 512; i++) {
        assert(unaligned[1 + i * 2] == (uint8_t)i);
        assert(unaligned[2 + i * 2] == (uint8_t)(i >> 8));
    }
    word_writes = 0;
    assert(write_sectors_ATA_PIO(1, 2, unaligned + 1));
    for (int i = 0; i < 512; i++) assert(written[i] == i);
    assert(!read_sectors_ATA_PIO(output, 8191, 2));
    assert(!read_sectors_ATA_PIO(output, 0, 0));
    error = 1; assert(!read_sectors_ATA_PIO(output, 0, 1)); assert(ATA_get_ERROR()); error = 0;
    stuck = 1; status_reads = 0; assert(!read_sectors_ATA_PIO(output, 0, 1)); assert(status_reads <= 1000); stuck = 0;
    absent = 1; select_drive(1); assert(!identify_drive(0xb0)); assert(!ata_sector_count());
    select_drive(255); assert(!ata_sector_count());
    puts("ATA checks passed."); return 0;
}
