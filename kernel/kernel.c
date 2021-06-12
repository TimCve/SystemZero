#include "drivers/ports.h"
#include "drivers/screen.h"
#include "drivers/ata.h"

#include <stdint.h>

void main() {
	// system init message
	clear();
	print("Memory address of kernel main: 0x"); print_hex(main); print_newline();
	
	print_newline();

	print("Reading from disk sector 0..."); print_newline();	
	
	// memory that data will be read to
	uint32_t* target;

	// invoke read command from sector 0x0
	read_sectors_ATA_PIO(target, 0x0, 1);
	
	// print out data that has been read
	int i = 0;
	while(i < 128) {
		print_hex(target[i] & 0xFF); print_char(' ');
		print_hex((target[i] >> 8) & 0xFF); print_char(' ');
		i++;
	}

	print_newline();

	// declare buffer == size of a disk sector
	char write_buffer[512];

	// fill the buffer with 0x41 (A's) 
	for(i = 0; i < 512; i++) {
		write_buffer[i] = 0x41;
	}
	
	print("Writing 0x41 to disk sector 0..."); print_newline(); print_newline();
	write_sectors_ATA_PIO(0x0, 1, write_buffer);
	
	print("Reading from disk sector 0..."); print_newline();
	read_sectors_ATA_PIO(target, 0x0, 1);
	
	i = 0;
	while(i < 128) {
		print_hex(target[i] & 0xFF); print_char(' ');
		print_hex((target[i] >> 8) & 0xFF); print_char(' ');
		i++;
	}
}
