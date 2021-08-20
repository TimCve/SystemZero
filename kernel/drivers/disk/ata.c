#include <stdint.h>

#include "ata.h"
#include "../utils/ports.h"
#include "../io/screen.h"

#define STATUS_BSY 0x80
#define STATUS_RDY 0x40
#define STATUS_DRQ 0x08
#define STATUS_DF 0x20
#define STATUS_ERR 0x01

static void ATA_wait_BSY();
static void ATA_wait_DRQ();

uint16_t io_base;
uint16_t ctrl_base;
uint8_t slavebit;

void select_drive(uint8_t drive) {
	switch(drive) {
		case 0: {
			slavebit = 0;
			io_base = 0x1F0;
			ctrl_base = 0x3F6;
			break;
		}
		case 1: {
			slavebit = 1;
			io_base = 0x1F0;
			ctrl_base = 0x3F6;
		}
		case 2: {
			slavebit = 0;
			io_base = 0x170;
			ctrl_base = 0x376;
			break;
		}
		case 3: {
			slavebit = 1;
			io_base = 0x170;
			ctrl_base = 0x376;
			break;
		}
		default: break;
	}
}

int identify_drive(uint8_t drive_code) {
	ATA_wait_BSY();
	port_byte_out(io_base + 6, drive_code); // select drive
	port_byte_out(io_base + 2, 0);
	port_byte_out(io_base + 3, 0);
	port_byte_out(io_base + 4, 0);
	port_byte_out(io_base + 5, 0);
	port_byte_out(io_base + 7, 0xEC); // IDENTIFY COMMAND

	if(port_byte_in(io_base + 7)) {
		ATA_wait_BSY();

		if(port_byte_in(io_base + 4) || port_byte_in(io_base + 5)) {
			// drive is not ATA
			return 2;
		} else {
			ATA_wait_DRQ();
		
			uint8_t read_target[512];
			read_sectors_ATA_PIO(read_target, 0, 1);

			if(!ATA_get_ERROR()) return 1; // drive exists for sure
			else return 0; // status says drive exists, but it is probably just invalid
	}
	} else {
		// drive doesn't exist
		return 0;
	}
}

void read_sectors_ATA_PIO(uint32_t* target_address, uint32_t LBA, uint8_t sector_count) {
	ATA_wait_BSY();

	port_byte_out(io_base + 6, (0xE0 | (slavebit <<  4) | (LBA >> 24 & 0x0F)));
	port_byte_out(io_base + 2, sector_count);
	port_byte_out(io_base + 3, (uint8_t) LBA);
	port_byte_out(io_base + 4, (uint8_t)(LBA >> 8));
	port_byte_out(io_base + 5, (uint8_t)(LBA >> 16)); 
	port_byte_out(io_base + 7, 0x20); // READ command

	uint16_t *target = (uint16_t*) target_address;

	for(int j = 0; j < sector_count; j++) {
		ATA_wait_BSY();
		ATA_wait_DRQ();

		for(int i = 0; i < 256; i++)
			target[i] = port_word_in(io_base);

		target += 256;
	}
}

void write_sectors_ATA_PIO(uint32_t LBA, uint8_t sector_count, uint32_t* bytes) {
	ATA_wait_BSY();
	
	port_byte_out(io_base + 6, (0xE0 | (slavebit <<  4) | (LBA >> 24 & 0x0F)));
	port_byte_out(io_base + 2, sector_count);
	port_byte_out(io_base + 3, (uint8_t) LBA);
	port_byte_out(io_base + 4, (uint8_t)(LBA >> 8));
	port_byte_out(io_base + 5, (uint8_t)(LBA >> 16)); 
	port_byte_out(io_base + 7, 0x30); // READ command

	for(int j = 0; j < sector_count; j++) {
		ATA_wait_BSY();
		ATA_wait_DRQ();

		for(int i = 0; i < 256; i++) port_long_out(io_base, bytes[i]);
	}
}

static void ATA_wait_BSY() {
	while(port_byte_in(io_base + 7)&STATUS_BSY);
}

static void ATA_wait_DRQ() {
	while(!(port_byte_in(io_base + 7)&STATUS_RDY));
}

uint8_t ATA_get_ERROR() {
	return port_byte_in(io_base + 1);
}
