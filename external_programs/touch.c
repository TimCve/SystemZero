#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	if(!file_create(splice(input_buffer, 1, 0x20), env_vars_ptr)) {
		print("File "); print(splice(input_buffer, 1, 0x20)); print(" successfully created!"); print_newline();
	} else {
		print("File already exists!"); print_newline();
	}
}
