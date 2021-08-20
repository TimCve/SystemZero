#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	uint8_t drive = atoi(splice(input_buffer, 1, 0x20));

	switch(drive) {
		case 0: {
			if(env_vars_ptr->master_exists) {
				env_vars_ptr->selected_drive = drive;
				select_drive(env_vars_ptr->selected_drive);
				return;
			} else break;
		}
		
		case 1: {
			if(env_vars_ptr->slave_exists) {
				env_vars_ptr->selected_drive = drive;
				select_drive(env_vars_ptr->selected_drive);
				return;
			} else break;
		}

		default: break;
	}

	print("Drive "); print_dec(drive); print(" doesn't exist!"); print_newline();
}
