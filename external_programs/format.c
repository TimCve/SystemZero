#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include <stdint.h>

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	select_drive(env_vars_ptr->selected_drive);

	print("Formatting drive IDE"); print_dec(env_vars_ptr->selected_drive); print_newline();

	// determine size of drive
	print("Determining size of drive...");	

	uint8_t ATA_err;
	uint8_t read_target[512];
	int lba = 0;
	int bytes = 0;

	while(!ATA_err) {
		read_sectors_ATA_PIO(read_target, lba, 1);
		ATA_err = ATA_get_ERROR();
		lba += 10;
		bytes += 5120;
	}

	print(" ["); print_dec(bytes); print(" bytes]");
	print_newline();

	// initialize superblock
	init_fs(bytes);

	// format drive
	format_disk();
	return 0;
}
