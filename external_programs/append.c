#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	static char filename[512];
	static char data[2000];

	for(int i = 0; i < 2000; i++) data[i] = 0;

	char chr;
	int input_i = 0;
	int buffer_i = 0;

	do {
		chr = input_buffer[input_i];
		input_i++;
	} while(chr != 0x20);

	if(strcmp(splice(input_buffer, 1, 0x20), "-n") == 0) input_i += 3;

	do {
		chr = input_buffer[input_i];
		filename[buffer_i] = chr;
		input_i++;
		buffer_i++;
	} while(chr != 0x20);
	
	filename[buffer_i - 1] = 0;

	buffer_i = 0;
	do {
		chr = input_buffer[input_i];
		data[buffer_i] = chr;
		input_i++;
		buffer_i++;				
	} while(chr || input_i >= 2000);

	if(strcmp(splice(input_buffer, 1, 0x20), "-n") == 0) {
		data[buffer_i] = 0;
		data[buffer_i - 1] = 0xA;
	}
	
	file_write(filename, data);
}
