#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	file_list();
	return 0;
}
