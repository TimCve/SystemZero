#ifndef SYSTEMZERO_ATA_H
#define SYSTEMZERO_ATA_H
#include <stdint.h>
void select_drive(uint8_t drive);
uint8_t selected_ata_drive(void);
int identify_drive(uint8_t drive_code);
uint32_t ata_sector_count(void);
int read_sectors_ATA_PIO(void *target, uint32_t lba, uint8_t count);
/* Write completion transfers data to the device; ata_flush() makes it durable. */
int write_sectors_ATA_PIO(uint32_t lba, uint8_t count, const void *source);
int ata_flush(void);
uint8_t ATA_get_ERROR(void);
#endif
