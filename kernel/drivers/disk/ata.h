#include <stdint.h>

void select_drive(uint8_t drive);
int identify_drive(uint8_t drive_code);
void read_sectors_ATA_PIO(uint32_t* target_address, uint32_t LBA, uint8_t sector_count);
void write_sectors_ATA_PIO(uint32_t LBA, uint8_t sector_count, uint32_t* bytes);
uint8_t ATA_get_ERROR();
