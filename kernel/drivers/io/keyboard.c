#include "keyboard.h"
#include <stdint.h>

char get_printable_char(char keycode, int i, uint8_t shift) {
	if(shift == 0) {
		if(keycode == std_keycode_table[i].keycode) {
			return std_keycode_table[i].chr;
		} else {
			if(std_keycode_table[i].keycode == 0x0) {
				return 0x0;
			}
			return get_printable_char(keycode, i + 1, shift);
		}
	} else if(shift == 1) {
		if(keycode == shft_keycode_table[i].keycode) {
			return shft_keycode_table[i].chr;
		} else {
			if(shft_keycode_table[i].keycode == 0x0) {
				return 0x0;
			}
			return get_printable_char(keycode, i + 1, shift);
		}	
	}
}

char get_input_keycode() {
	unsigned char keycode;

    keycode = port_byte_in(0x60);

    return keycode;
}

int INIT_HOLD_ITERATIONS;
int CONT_HOLD_ITERATIONS;

void kbd_readline(char* buffer, int tty_calibration, int buffer_bytes) {
	if(tty_calibration >= 75) {
		INIT_HOLD_ITERATIONS = 300000;
		CONT_HOLD_ITERATIONS = 70000;
	} else {
		INIT_HOLD_ITERATIONS = 1200000;
		CONT_HOLD_ITERATIONS = 350000;
	}
	
	uint8_t keycode = 0;
	uint8_t prev_keycode = ENTER; // so enter key doesn't instantly get spammed upon command execution
	uint8_t cached_prev_keycode = 0;
	uint8_t ascii_code = 0;

	int shift_pressed = 0;
	int hold_iterations = 0;

	int buffer_i = 0;

	while(ascii_code != ENTER) {
		// get keycode from keyboard port
		keycode = get_input_keycode();

		// handle shift key press & release
		if(keycode == 0x2a) {
			shift_pressed = 1;
		} else if(keycode == 0xaa) {
			shift_pressed = 0;
		}	

		if(keycode != prev_keycode) {
			// reset hold iterations
			hold_iterations = 0;

			// set previous scancode variable for key holding detection
			prev_keycode = keycode;

			// attempt translation of recieved keycode to printable character
			ascii_code = get_printable_char(keycode, 0, shift_pressed);

			if(ascii_code && buffer_i < buffer_bytes) { // character is printable
				if(!buffer[buffer_i]) { // insert character at end of text
					buffer[buffer_i] = ascii_code;
					print_char(buffer[buffer_i]);
					buffer_i++;
				} else { // insert character between existing text
					int counter = 0;

					// seek to end of text
					while(buffer[buffer_i]) {
						buffer_i++;
						set_cursor_position(get_cursor_position() + 2);
						counter++;
					}

					// shift characters on right of insertion to right by 1 place
					// also seek back to initial point
					while(counter > 0) {
						buffer[buffer_i] = buffer[buffer_i - 1];
						print_char(buffer[buffer_i]);
						set_cursor_position(get_cursor_position() - 2);
						counter--;
						buffer_i--;
						set_cursor_position(get_cursor_position() - 2);
					}

					// insert desired character
					buffer[buffer_i] = ascii_code;
					print_char(buffer[buffer_i]);
					buffer_i++;
				}
			} else { // character is not printable
				ascii_code = keycode;

				// special character handlers
				switch(ascii_code) {
					case L_ARROW: {
						if(buffer[buffer_i - 1]) {
							buffer_i--;
							set_cursor_position(get_cursor_position() - 2);
						}
						break;
					}
					case R_ARROW: {
						if(buffer[buffer_i]) {
							buffer_i++;
							set_cursor_position(get_cursor_position() + 2);
						}
						break;
					}
					case BACKSPACE: {
						if(buffer[buffer_i - 1]) {
							int counter = 0;

							buffer_i--;
							set_cursor_position(get_cursor_position() - 2);

							// seek to end of text & shift characters left by 1 place
							while(buffer[buffer_i]) {
								buffer[buffer_i] = buffer[buffer_i + 1];
								print_char(buffer[buffer_i]);
								buffer_i++;
								counter++;
							}

							// reset cursor and buffer index
							while(counter > 0) {
								buffer_i--;
								set_cursor_position(get_cursor_position() - 2);
								counter--;
							}
						}
						break;
					}
					case ENTER: {
						print_newline();
						break;
					}
					default: break;
				}
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
}
