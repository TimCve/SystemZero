#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	uint8_t drive = atoi(splice(input_buffer, 1, 0x20));

	select_drive(drive);

	if(((drive == 0 || drive == 2) && identify_drive(0xA0) == 1) || ((drive == 1 || drive == 3) && identify_drive(0xB0) == 1))
		env_vars_ptr->selected_drive = drive;
	else {
		print("Drive doesn't exist"); print_newline();
	}
}
