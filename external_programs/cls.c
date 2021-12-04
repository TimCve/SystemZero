#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	clear();
	return 0;
}
