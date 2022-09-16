#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"

uint32_t print_text_buffer(env_vars_t* env_vars_ptr, uint8_t* buffer, uint32_t line_offset, uint32_t selected_char) {
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

	uint32_t actual_selected_char = char_index + selected_char;

	// print out text buffer
	line_counter = 0;
	
	while(line_counter < 24) {
		for(int i = 0; i < 80; i++) {
			set_term_color(env_vars_ptr->term_color);
			if(buffer[char_index] == '\n') {
				if(char_index == actual_selected_char) set_term_color(((env_vars_ptr->term_color) >> 4) | ((env_vars_ptr->term_color) << 4));
				print_char(' ');
				set_term_color(env_vars_ptr->term_color);
				print_newline();
				char_index++;
				break;
			} else {
				if(char_index == actual_selected_char) set_term_color(((env_vars_ptr->term_color) >> 4) | ((env_vars_ptr->term_color) << 4));
				print_char(buffer[char_index]);
			}
			char_index++;
		}
		line_counter++;
	}

	return actual_selected_char;
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

	int shift_pressed = 0;
	int ctrl_pressed = 0;
	int hold_iterations = 0;

	uint32_t line_offset = 0;
	uint32_t selected_char = 0;
	uint32_t actual_selected_char = 0;
		
	actual_selected_char = print_text_buffer(env_vars_ptr, file, line_offset, selected_char);

	while(keycode != ESC) {
		keycode = get_input_keycode();
		
		if(keycode == 0x2a) shift_pressed = 1;
		else if(keycode == 0xaa) shift_pressed = 0;

		if(keycode == 0x1d) ctrl_pressed = 1;
		else if(keycode == 0x9d) ctrl_pressed = 0;
		
		if(keycode != prev_keycode) {	
			// reset hold iterations
			hold_iterations = 0;

			// set previous scancode variable for key holding detection
			prev_keycode = keycode;

			// attempt translation of recieved keycode to printable character
			ascii_code = get_printable_char(keycode, 0, shift_pressed);
			
			// printable character
			if(ascii_code) {
				int status = 0;

				if(ctrl_pressed) {
					switch(ascii_code) {
						case 'r': {
							selected_char = 0;
							line_offset = 0;
							status = 1;
							break;
						}
						case 's': {	
							SAVE:
								print("Saving changes...");
								file_delete(splice(input_buffer, 1, 0x20), env_vars_ptr);
								file_create(splice(input_buffer, 1, 0x20), env_vars_ptr);
								int wrote = file_write(splice(input_buffer, 1, 0x20), file, file_size - strlen(splice(input_buffer, 1, 0x20)), env_vars_ptr);
								if(!wrote) {
									actual_selected_char = print_text_buffer(env_vars_ptr, file, line_offset, selected_char);
									goto SAVE;
								}
								print(" Wrote "); print_dec(wrote); print(" bytes!");

							status = 1;
							break;
						}
					}
					
				}
				
				if(status == 0) {
					int counter = file_size;
					while(counter > (actual_selected_char - 1)) {
						file[counter + 1] = file[counter];
						counter--;	
					}
					file[actual_selected_char] = ascii_code;
					selected_char++;
					file_size++;
				}
				actual_selected_char = print_text_buffer(env_vars_ptr, file, line_offset, selected_char);
			} else {
				int status = 0;
				if(!ctrl_pressed) {
					switch(keycode) {
						case BACKSPACE: {
							int counter = actual_selected_char - 1;
							while(counter < file_size) {
								file[counter] = file[counter + 1];
								counter++;
							}
							file_size--;
							selected_char--;
							status = 1;
							break;
						}
						case L_ARROW: {
							selected_char--;
							status = 1;
							break;
						}
						case R_ARROW: {
							selected_char++;
							status = 1;
							break;
						}
						case ENTER: {
							int counter = file_size;
							while(counter > (actual_selected_char - 1)) {
								file[counter + 1] = file[counter];
								counter--;	
							}
							file[actual_selected_char] = '\n';
							selected_char++;
							file_size++;
							status = 1;
							break;
						}
						case U_ARROW: {
							int newlines = 0;
							while(newlines < 2) {
								selected_char--;
								actual_selected_char--;
								if(file[actual_selected_char] == '\n') newlines++;
								if(actual_selected_char == 0) break;
							}
							if(actual_selected_char != 0) selected_char++;
							status = 1;
							break;
						}
						case D_ARROW: {
							while(file[actual_selected_char] != '\n') {
								selected_char++;
								actual_selected_char++;
							}
							selected_char++;
							status = 1;
							break;
						}
						case PGDN: {
							line_offset++;
							status = 1;
							break;
						}
						case PGUP: {
							if(line_offset > 0) {
								line_offset--;
								status = 1;
							}
							break;
						}
					}
				} else {	
					switch(keycode) {
						case U_ARROW: {
							if(line_offset > 0) {
								line_offset--;
								status = 1;
							}
							break;
						}
						case D_ARROW: {
							line_offset++;
							status = 1;
							break;
						}
					}
				}

				if(status != 0) actual_selected_char = print_text_buffer(env_vars_ptr, file, line_offset, selected_char);
			}
		
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
