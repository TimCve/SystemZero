#include "keyboard.h"
#include <stdint.h>

typedef struct {
	char keycode;
	char chr;
} kbd_keycode;

static kbd_keycode std_keycode_table[] = {
	{0x29, '`'}, {0x2, '1'}, {0x3, '2'}, {0x4, '3'}, {0x5, '4'}, {0x6, '5'}, {0x7, '6'}, {0x8, '7'}, {0x9, '8'}, {0xA, '9'}, {0xB, '0'}, {0xC, '-'}, {0xD, '='},
	{0x10, 'q'}, {0x11, 'w'}, {0x12, 'e'}, {0x13, 'r'}, {0x14, 't'}, {0x15, 'y'}, {0x16, 'u'}, {0x17, 'i'}, {0x18, 'o'}, {0x19, 'p'}, {0x1A, '['}, {0x1B, ']'},
	{0x1e, 'a'}, {0x1f, 's'}, {0x20, 'd'}, {0x21, 'f'}, {0x22, 'g'}, {0x23, 'h'}, {0x24, 'j'}, {0x25, 'k'}, {0x26, 'l'}, {0x27, ';'}, {0x28, '\''}, {0x2B, '#'},
	{0x56, '\\'}, {0x2c, 'z'}, {0x2d, 'x'}, {0x2e, 'c'}, {0x2f, 'v'}, {0x30, 'b'}, {0x31, 'n'}, {0x32, 'm'}, {0x33, ','}, {0x34, '.'}, {0x35, '/'}, {0x39, ' '},
	//{0xE, 0x8},
	{0x0, 0x0}
};

static kbd_keycode shft_keycode_table[] = {
	{0x29, '¬'}, {0x2, '!'}, {0x3, '"'}, {0x5, '$'}, {0x6, '%'}, {0x7, '^'}, {0x8, '&'}, {0x9, '*'}, {0xA, '('}, {0xB, ')'}, {0xC, '_'}, {0xD, '+'},
	{0x10, 'Q'}, {0x11, 'W'}, {0x12, 'E'}, {0x13, 'R'}, {0x14, 'T'}, {0x15, 'Y'}, {0x16, 'U'}, {0x17, 'I'}, {0x18, 'O'}, {0x19, 'P'}, {0x1A, '{'}, {0x1B, '}'},
	{0x1e, 'A'}, {0x1f, 'S'}, {0x20, 'D'}, {0x21, 'F'}, {0x22, 'G'}, {0x23, 'H'}, {0x24, 'J'}, {0x25, 'K'}, {0x26, 'L'}, {0x27, ':'}, {0x28, '@'}, {0x2B, '~'},
	{0x56, '|'}, {0x2c, 'Z'}, {0x2d, 'X'}, {0x2e, 'C'}, {0x2f, 'V'}, {0x30, 'B'}, {0x31, 'N'}, {0x32, 'M'}, {0x33, '<'}, {0x34, '>'}, {0x35, '?'}, {0x39, ' '},
	//{0xE, 0x8},
	{0x0, 0x0}
};

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

void kbd_readline(char* buffer, int tty_calibration) {
	if(tty_calibration >= 75) {
		INIT_HOLD_ITERATIONS = 300000;
		CONT_HOLD_ITERATIONS = 70000;
	} else {
		INIT_HOLD_ITERATIONS = 1200000;
		CONT_HOLD_ITERATIONS = 350000;
	}
	
	uint8_t keycode;
	uint8_t prev_keycode = ENTER; // so enter key doesn't instantly get spammed upon command execution
	uint8_t cached_prev_keycode;
	uint8_t ascii_code;

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

			if(ascii_code) { // character is printable
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
