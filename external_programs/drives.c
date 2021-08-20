#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/ata.h"
#include <stdint.h>

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);

	select_drive(0);
	if(identify_drive(0xA0) == 1); print("[IDE0]"); print_newline();
	if(identify_drive(0xB0) == 1); print("[IDE1]"); print_newline();

	select_drive(2);
	if(identify_drive(0xA0) == 1); print("[IDE2]"); print_newline();
	if(identify_drive(0xB0) == 1); print("[IDE3]"); print_newline();
}
