#include <stdint.h>
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"
#include "../kernel/env_vars.h"
#include "../kernel/drivers/utils/mem.h"

void hex_pad_zeroes(uint32_t num, uint32_t maxnum) {
	if(num < (maxnum / 16)) {
		print_char('0');
		hex_pad_zeroes(num, maxnum / 16);
	} else {
		return;
	}
}

void reprint_buffer(env_vars_t* env_vars_ptr, uint8_t* buffer, uint32_t selected_byte, uint32_t read_offset) {
	set_term_color(env_vars_ptr->term_color);
	clear();
	print("SYSTEMZERO STANDARD HEX EDITOR"); print_newline();

	for(int i = 0; i < 20; i++) {
		set_term_color(env_vars_ptr->term_color);
		hex_pad_zeroes(read_offset + (i * 24), 0xFFFFFF);
		print_hex(read_offset+(i * 24)); print_char(186);
		for(int j = 0; j < 24; j++)	{
			if(selected_byte == (j + (i * 24))) 
				set_term_color(((env_vars_ptr->term_color) >> 4) | ((env_vars_ptr->term_color) << 4));
			else
				set_term_color(env_vars_ptr->term_color);
			if(buffer[j + (i * 24)] < 0x10) print_char('0');
			print_hex(buffer[j + (i * 24)]);
			print_char(' ');
		}
		print_newline();
	}

	set_term_color(env_vars_ptr->term_color);
	for(int i = 0; i < 6; i++) print_char(205);
	print_char(202);
	for(int i = 0; i < 73; i++) print_char(205);

	print("ESC: exit | PGUP/PGDN: scroll | c: change byte | d: delete byte | i: insert byte");
	print("w: write changes to file");
	print_newline();
}

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	uint32_t phy_addr;
	set_free_ptr(env_vars_ptr->free_mem_ptr);

	// allocate memory for file buffer
	uint32_t file_memory = malloc(229376, 1, &phy_addr);

	// read file contents into memory
	int file_size = get_file_info(splice(input_buffer, 1, 0x20)).size;
	file_read(splice(input_buffer, 1, 0x20), file_memory, (file_size / 512) + 1, 0);

	// create pointer to start of file buffer in memory
	uint8_t* file = file_memory;

	uint32_t selected_byte = 0;
	uint32_t read_offset = 0;

	uint8_t byte_buffer[480];

	uint8_t keycode;
	uint8_t prev_keycode;
	int status;
	static char byte_input[100];
	uint32_t keypress_limit;

	if(env_vars_ptr->tty_calibration >= 75) {
		keypress_limit = 100000;
	} else {
		keypress_limit = 500000;
	}

	while(1) {
		memcpy(byte_buffer, file + read_offset, sizeof(byte_buffer));
		reprint_buffer(env_vars_ptr, &byte_buffer, selected_byte, read_offset);

		if(status == 1) break;

		int keypress_counter = 0;

		while(1) {
			keycode = get_input_keycode();
			status = 2;
			
			if(keycode == prev_keycode) keypress_counter++;

			if(keypress_counter >= keypress_limit) {
				keypress_counter = 0;
				switch(keycode) {
					case PGUP: {
						if(read_offset >= 24) {
							read_offset -= 24;
							status = 0;
						}
						break;
					}
					case PGDN: {
						read_offset += 24;
						status = 0;
						break;
					}
					case R_ARROW: {
						if(selected_byte < 479) {
							selected_byte++;
							status = 0;
						}
						break;
					}
					case L_ARROW: {
						if(selected_byte > 0) {
							selected_byte--;
							status = 0;
						}
						break;
					}
					case U_ARROW: {
						if(selected_byte >= 24) {
							selected_byte -= 24;
							status = 0;
						}
						break;
					}
					case D_ARROW: {
						if(selected_byte < 454) {
							selected_byte += 24;
							status = 0;
						}
						break;
					}
					case 0x2e: { // c key
						while(get_input_keycode() == 0x2e) continue;
						set_cursor_position(3840);
						print("INPUT NEW BYTE VALUE: ");
						for(int i = 0; i < 100; i++) byte_input[i] = 0;
						kbd_readline(byte_input, env_vars_ptr->tty_calibration, 4);
						int byte_value = htoi(splice(byte_input, 0, 0x20));
						if(byte_value >= 0 && byte_value <= 0xFF) {
							file[read_offset + selected_byte] = (uint8_t)byte_value;
							status = 0;
						}
						break;
					}
					case 0x17: { // i key
						while(get_input_keycode() == 0x17) continue;	
						int counter = file_size;
						while(counter >= (read_offset + selected_byte)) {
							file[counter + 1] = file[counter];
							counter--;	
						}
						set_cursor_position(3840);
						print("INPUT BYTE VALUE: ");
						for(int i = 0; i < 100; i++) byte_input[i] = 0;
						kbd_readline(byte_input, env_vars_ptr->tty_calibration, 4);
						int byte_value = htoi(splice(byte_input, 0, 0x20));
						if(byte_value >= 0 && byte_value <= 0xFF) {
							file[read_offset + selected_byte] = (uint8_t)byte_value;
							status = 0;
						}
						file_size++;
						break;	
					}
					case 0x20: { // d key
						while(get_input_keycode() == 0x20) continue;	
						int counter = (read_offset + selected_byte);
						while(counter < file_size) {
							file[counter] = file[counter + 1];
							counter++;
						}
						status = 0;
						file_size--;	
						break;
					}
					case 0x11: { // w key
						file_delete(splice(input_buffer, 1, 0x20));
						file_create(splice(input_buffer, 1, 0x20));
						print("Writing data (will take a while for large files)...");
						file_write(splice(input_buffer, 1, 0x20), file, file_size);
						status = 0;
						break;
					} 
					case ESC: {
						status = 1;
						break;
					}
					default: {
						status = 2;
						break;
					}
				}
			}

			prev_keycode = keycode;

			if(status != 2) break;
		}
	}

	// FREE & DEALLOCATE MEMORY USED FOR FILE BUFFER
	for(int i = 0; i < (file_size / 4); i++) file[i] = 0; // free
	set_free_ptr(get_free_ptr() - 233472); // deallocate
}
