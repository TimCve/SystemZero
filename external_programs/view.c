#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
#include "../kernel/vga_colors.h"

#define MAX_CHARS 2000

void print_buffer(char* buffer, int offset, int as_hex) {
	int line_ctr = 0;
	int char_in_line_ctr = 0;
	int index = offset;

	while(line_ctr < 24) {
		if(buffer[index] == 0xA && as_hex == 0) {
			print_newline();
			line_ctr++;
			char_in_line_ctr = 0;
		} else {
			if(as_hex == 0) {
				print_char(buffer[index]);
				char_in_line_ctr++;
			} else {
				print_hex(buffer[index]);
				print_char(' ');
				char_in_line_ctr += 3;
			}
			if(char_in_line_ctr >= 80) {
				char_in_line_ctr = 0;
				line_ctr++;
			}
		}
		index++;
	}
}

void reprint(uint32_t* file, int read_offset, int as_hex) {
	clear();
	print_buffer(file, read_offset, as_hex);
	print("[ESC] exit [PGUP/PGDN] scroll");
}

int main(env_vars_t* env_vars_ptr , char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	static char filename[512];
	int as_hex = 0;

	if(strcmp(splice(input_buffer, 1, 0x20), "-x") == 0) {
		memcpy(filename, splice(input_buffer, 2, 0x20), strlen(splice(input_buffer, 2, 0x20)) * 4);
		as_hex = 1;
	} else {
		memcpy(filename, splice(input_buffer, 1, 0x20), strlen(splice(input_buffer, 1, 0x20)) * 4);
		as_hex = 0;
	} 

	uint32_t phy_addr;
	set_free_ptr(env_vars_ptr->free_mem_ptr);

	// allocate memory for file buffer
	uint32_t file_memory = malloc(229376, 1, &phy_addr);
	
	// create pointer to start of file buffer in memory
	uint32_t* file = file_memory;

	for(int i = 0; i < 2500; i++) file[i] = 0;

	// read file contents into memory
	int file_size = get_file_info(filename).size;
	file_read(filename, file_memory, (file_size / 512) + 1, 0);

	int read_offset = 0;

	// do an initial print of the buffer
	reprint(file, read_offset, as_hex);

	uint8_t keycode;
	uint8_t prev_keycode;
	while(1) {
		keycode = get_input_keycode();

		if(keycode != prev_keycode) {
			prev_keycode = keycode;

			if(keycode == 0x1) break; // exit if ESC pressed

			switch(keycode) {
				case 0x49: { // pgup
					if(read_offset > 0) read_offset -= 80;
					reprint(file, read_offset, as_hex);
					break;
				}
				case 0x51: { // pgdn
					if(read_offset <= file_size) read_offset += 80;
					reprint(file, read_offset, as_hex);
					break;
				}
				default: break;
			}
		}
	}

	print_newline();

	// FREE & DEALLOCATE MEMORY USED FOR FILE BUFFER
	for(int i = 0; i < (file_size / 4); i++) file[i] = 0; // free
	set_free_ptr(get_free_ptr() - 233472); // deallocate

	return 0;
}
