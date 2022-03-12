#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"

void print_text_buffer(uint8_t* buffer, uint32_t line_offset) {
	uint32_t line_counter = 0;
	uint32_t char_index = 0;

	// seek to correct part of text buffer
	while(line_counter != line_offset) {
		for(int i = 0; i < 80; i++) {
			if(buffer[char_index] == '\n') {
				char_index++;
				break;
			}
			char_index++;
		}
		line_counter++;
	}

	clear(); 

	// print out text buffer
	line_counter = 0;
	
	while(line_counter < 24) {
		for(int i = 0; i < 80; i++) {
			if(buffer[char_index] == '\n') {
				print_newline();
				char_index++;
				break;
			} else {
				print_char(buffer[char_index]);
			}
			char_index++;
		}
		line_counter++;
	}
}

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	uint32_t phy_addr;
	set_free_ptr(env_vars_ptr->free_mem_ptr);

	// allocate memory for file buffer
	uint32_t file_memory = malloc(229376, 1, &phy_addr);

	// create pointer to start of file buffer in memory
	uint8_t* file = file_memory;

	for(int i = 0; i < 10000; i++) file[i] = 0;

	// read file contents into memory
	int file_size = get_file_info(splice(input_buffer, 1, 0x20)).size;
	file_read(splice(input_buffer, 1, 0x20), file_memory, (file_size / 512) + 1, 0);

	int INIT_HOLD_ITERATIONS;
	int CONT_HOLD_ITERATIONS;
	
	if(env_vars_ptr->tty_calibration >= 75) {
		INIT_HOLD_ITERATIONS = PRESS_THRESH_FAST;
		CONT_HOLD_ITERATIONS = HOLD_THRESH_FAST;
	} else {
		INIT_HOLD_ITERATIONS = PRESS_THRESH_SLOW;
		CONT_HOLD_ITERATIONS = HOLD_THRESH_SLOW;
	}
	
	uint8_t keycode = 0;
	uint8_t prev_keycode = ENTER;
	uint8_t cached_prev_keycode = 0;
	uint8_t ascii_code = 0;

	int hold_iterations = 0;

	uint32_t line_offset = 0;
		
	print_text_buffer(file, line_offset);

	while(keycode != ESC) {
		keycode = get_input_keycode();
		
		if(keycode != prev_keycode) {	
			// reset hold iterations
			hold_iterations = 0;

			// set previous scancode variable for key holding detection
			prev_keycode = keycode;
		
			int status = 0;
			switch(keycode) {
				case PGUP:
				case D_ARROW: {
					line_offset++;
					status = 1;
					break;
				}
				case PGDN:
				case U_ARROW: {
					if(line_offset > 0) {
						line_offset--;
						status = 1;
					}
					break;
				}
			}

			if(status == 1) print_text_buffer(file, line_offset);
		
		} else {
			// continuous key holding
			if(prev_keycode == cached_prev_keycode && hold_iterations >= CONT_HOLD_ITERATIONS) {
				prev_keycode = 0;
				continue;
			}

			// initial key holding confirmation delay
			if(hold_iterations >= INIT_HOLD_ITERATIONS) {
				cached_prev_keycode = prev_keycode;
				prev_keycode = 0;
			} else hold_iterations++;
		}
	}

	// FREE & DEALLOCATE MEMORY USED FOR FILE BUFFER
	for(int i = 0; i < (file_size / 4); i++) file[i] = 0; // free
	set_free_ptr(get_free_ptr() - 233472); // deallocate

	return 0;
}
