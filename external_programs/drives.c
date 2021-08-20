#include "../kernel/env_vars.h"
#include <stdint.h>

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	print("0 : [IDE0]  available ? "); print_dec(env_vars_ptr->master_exists); print_newline();
	print("1 : [IDE1]  available ? "); print_dec(env_vars_ptr->slave_exists); print_newline();
}
