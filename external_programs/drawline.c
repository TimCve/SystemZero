#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/graphics.h"
#include "../kernel/libc/strings.h"

int main(env_vars_t* env_vars_ptr, char* input_buffer) {	
	draw_line(atoi(splice(input_buffer, 1, 0x20)), atoi(splice(input_buffer, 2, 0x20)), atoi(splice(input_buffer, 3, 0x20)), atoi(splice(input_buffer, 4, 0x20)), atoi(splice(input_buffer, 1, 0x20)));
	return 0;
}
