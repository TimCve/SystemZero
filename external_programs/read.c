#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	static char filename[512];
	int sector = -1;
	int as_hex = 0;

	if(strcmp(splice(input_buffer, 1, 0x20), "-x") == 0) {
		memcpy(filename, splice(input_buffer, 2, 0x20), strlen(splice(input_buffer, 2, 0x20)) * 4);
		if(atoi(splice(input_buffer, 3, 0x20)) >= 0) sector = atoi(splice(input_buffer, 3, 0x20));
		as_hex = 1;
	} else {
		memcpy(filename, splice(input_buffer, 1, 0x20), strlen(splice(input_buffer, 1, 0x20)) * 4);
		if(atoi(splice(input_buffer, 2, 0x20)) >= 0) sector = atoi(splice(input_buffer, 2, 0x20));
		as_hex = 0;
	} 

	static uint8_t read_target[512];

	if(sector >= 0) {
		for(int i = 0; i < 512; i++) read_target[i] = 0;
		file_read(filename, read_target, 1, sector);
		if(as_hex == 0) {
			print(read_target);
			print_newline();
		} else {
			for(int chr = 0; chr < 512; chr++) {
				print_hex(read_target[chr]);
				print_char(' ');
			}
		}
	} else {
		uint32_t sectors_size = get_file_info(filename).size / 512;
		for(int sec = 0; sec <= sectors_size; sec++) {
			for(int chr = 0; chr < 512; chr++) read_target[chr] = 0;
			if(file_read(filename, read_target, 1, sec) == 2) break;		
			if(as_hex == 0) {
				print(read_target);
			} else {	
				for(int chr = 0; chr < 512; chr++) {
					print_hex(read_target[chr]);
					print_char(' ');
				}
			}
		}
	}

	return 0;
}

