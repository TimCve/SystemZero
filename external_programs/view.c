#include <stdint.h>
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
#include "../kernel/vga_colors.h"

#define MAX_CHARS 2000

void print_buffer(char* buffer, int offset) {
	int line_ctr = 0;
	int char_in_line_ctr = 0;
	int index = offset;

	while(line_ctr < 24) {
		if(buffer[index] == 0xA) {
			print_newline();
			line_ctr++;
			char_in_line_ctr = 0;
		} else {
			print_char(buffer[index]);
			char_in_line_ctr++;
			if(char_in_line_ctr >= 80) {
				char_in_line_ctr = 0;
				line_ctr++;
			}
		}
		index++;
	}
}

void reprint(uint32_t* file, int read_offset) {
	clear();
	print_buffer(file, read_offset);
	print("[ESC] exit [PGUP/PGDN] scroll");
}

void main(uint32_t free_mem_addr, char* input_buffer) {
	uint32_t phy_addr;
	set_free_ptr(free_mem_addr);

	// allocate memory for file buffer
	uint32_t file_memory = malloc(229376, 1, &phy_addr);

	// read file contents into memory
	int file_size = get_file_info(splice(input_buffer, 1, 0x20)).size;
	file_read(splice(input_buffer, 1, 0x20), file_memory, (file_size / 512) + 1, 0);

	// create pointer to start of file buffer in memory
	uint32_t* file = file_memory;
	int read_offset = 0;

	// do an initial print of the buffer
	reprint(file, read_offset);

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
					reprint(file, read_offset);
					break;
				}
				case 0x51: { // pgdn
					if(read_offset <= file_size) read_offset += 80;
					reprint(file, read_offset);
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
}