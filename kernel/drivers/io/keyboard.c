#include "keyboard.h"

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

char get_ascii_char(char keycode, int i, uint8_t shift) {
	if(shift == 0) {
		if(keycode == std_keycode_table[i].keycode) {
			return std_keycode_table[i].chr;
		} else {
			if(std_keycode_table[i].keycode == 0x0) {
				return 0x0;
			}
			return get_ascii_char(keycode, i + 1, shift);
		}
	} else if(shift == 1) {
		if(keycode == shft_keycode_table[i].keycode) {
			return shft_keycode_table[i].chr;
		} else {
			if(shft_keycode_table[i].keycode == 0x0) {
				return 0x0;
			}
			return get_ascii_char(keycode, i + 1, shift);
		}	
	}
}

char get_input_scancode() {
	unsigned char scancode;

    scancode = port_byte_in(0x60);

    return scancode;
}

void sleep(int time) {   
    volatile int i, j;
    for(i = 0; i < time; i++)
        for(j = 0; j < 250000; j++)
            __asm__("NOP");
}

int handle_special_keypress(unsigned char scancode) {
	switch(scancode) {
		case BACKSPACE:
			set_cursor_position(get_cursor_position() - 2);
			print_char(0x0);
			set_cursor_position(get_cursor_position() - 2);
			return 0;
		case R_ARROW:
			set_cursor_position(get_cursor_position() + 2);
			return 0;
		case L_ARROW:
			set_cursor_position(get_cursor_position() - 2);
			return 0;
		case U_ARROW:
			set_cursor_position(get_cursor_position() - COLS * 2);
			return 0;
		case D_ARROW:
			set_cursor_position(get_cursor_position() + COLS * 2);
			return 0;
		case ENTER:
			print_newline();
			return 0;
		case TAB:
			print("    ");
			return 0;
		default:
			return 1;
	}
}

void kbd_input() {
	unsigned char prev_scancode = 0x0;
	int hold_index = 0;
	char prev_held_char = 0x0;
	char prev_held_special_key = 0x0;

	uint8_t shift_pressed = 0;

	while(1) {
		unsigned char scancode = get_input_scancode();

		// handles shift key press
		if(scancode == 0x2a) {
			shift_pressed = 1;
		} else if(scancode == 0xaa) {
			shift_pressed = 0;
		}

		// handles single key pressess
		if(scancode != prev_scancode) {
			prev_held_char = 0x0;
			prev_held_special_key = 0x0;
			hold_index = 0;
			char chr = get_ascii_char(scancode, 0, shift_pressed);
			if(chr) {
				print_char(chr);
			}
			handle_special_keypress(scancode);
		// handles held key pressess
		} else if((scancode & 128) != 128) {
			hold_index++;
		}

		int hold_init_threshold;
		int hold_cont_threshold;

		// weird bug where the top row sends scancodes faster (this code accounts for that)
		if(scancode == 0x29 || scancode == 0x2 || scancode == 0x3 || scancode == 0x4 || scancode == 0x5 || scancode == 0x6 || scancode == 0x7 || scancode == 0x8 || scancode == 0x9 || scancode == 0xA || scancode == 0xB || scancode == 0xC || scancode == 0xD) {
			hold_init_threshold = 640000;
			hold_cont_threshold = 101000;
		} else {
			hold_init_threshold = 140000;
			hold_cont_threshold = 21000;
		}

		if((hold_index >= hold_init_threshold && get_ascii_char(scancode, 0, shift_pressed) != prev_held_char && get_ascii_char(scancode, 0, shift_pressed) != 0) ||
		   (hold_index >= hold_cont_threshold && get_ascii_char(scancode, 0, shift_pressed) == prev_held_char && get_ascii_char(scancode, 0, shift_pressed) != 0) ||
		   (get_ascii_char(scancode, 0, shift_pressed) == 0 && hold_index >= hold_init_threshold && prev_held_special_key != scancode) ||
		   (get_ascii_char(scancode, 0, shift_pressed) == 0 && hold_index >= hold_cont_threshold && prev_held_special_key == scancode)) {
			char chr = get_ascii_char(scancode, 0, shift_pressed);
			if(chr) {
				print_char(chr);
				prev_held_char = chr;
				prev_held_special_key = 0x0;
				hold_index = 0;
			} else {
				handle_special_keypress(scancode);
				prev_held_special_key = scancode;
				prev_held_char = 0x0;
				hold_index = 0;
			}
		}

		prev_scancode = scancode;
	}
}