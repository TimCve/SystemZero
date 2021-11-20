#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	if(!file_delete(splice(input_buffer, 1, 0x20), env_vars_ptr), env_vars_ptr) {
		print("File successfully deleted!"); print_newline();
	} else {
		print("File doesn't exist!"); print_newline();
	}
}
