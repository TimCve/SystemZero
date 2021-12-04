#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	uint8_t background = atoi(splice(input_buffer, 1, 0x20));
	uint8_t foreground = atoi(splice(input_buffer, 2, 0x20));

	if(((background > 15) || (background < 0)) || ((foreground > 15) || (foreground < 0))) {
		print("Invalid color values given!"); print_newline();
		print("Format: color <0 - 15 (background)> <0 - 15 (foreground)>"); print_newline();
		return 1;
	} else {
		env_vars_ptr->term_color = (background * 16) + foreground;
		set_term_color(env_vars_ptr->term_color);
		return 0;
	}
}
