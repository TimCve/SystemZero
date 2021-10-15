#include "screen.h"
#include "../../vga_colors.h"
#include "../utils/ports.h"
#include "../../env_vars.h"

void set_cursor_position(int offset);
int get_cursor_position();

int print(char* string);
int print_newline();

int scrolling_enabled = 1;

uint8_t term_color; 

void set_term_color(uint8_t color) {
	term_color = color;
}

uint8_t get_term_color() {
	return term_color;
}

void set_scrolling(int value) {
	if(value <= 0) scrolling_enabled = 0;
	else if(value > 0) scrolling_enabled = 1;
}

// clears screen & sets cursor position to screen start
void clear() {
	char* video_memory = (char*) VIDEO_ADDRESS;

	for(int i = 0; i < COLS * ROWS * 2; i += 2) {
		video_memory[i] = 0x00;
		video_memory[i + 1] = term_color;
	}	

	set_cursor_position(0x00);
}

int scroll_terminal() {
	char* video_memory = (char*) VIDEO_ADDRESS;
	int cursor_pos = get_cursor_position();

	if(cursor_pos >= (ROWS * COLS) * 2) {
		for(int i = COLS * 2; i < (ROWS * COLS) * 2; i++) {
			video_memory[i - (COLS * 2)] = video_memory[i];
		}

		for(int i = ((COLS * (ROWS - 1)) * 2); i < (ROWS * COLS) * 2; i += 2) {
			video_memory[i] = 0x00;
			video_memory[i + 1] = term_color;
		}

		set_cursor_position(COLS * (ROWS - 1) * 2);
		return 1;
	} else return 0;
}

// print single character at cursor position
int print_char(char ch) {
	char* video_memory = (char*) VIDEO_ADDRESS;
	int cursor_pos = get_cursor_position();
	
	video_memory[cursor_pos] = ch;
	video_memory[cursor_pos + 1] = term_color;
	cursor_pos += 2;
	
	set_cursor_position(cursor_pos);
	
	if(scrolling_enabled == 1) return scroll_terminal();
	else return 0;
}

// prints ASCII string
int print(char* string) {
	int pos = 0;
	int scroll_lines = 0;
	
	while(string[pos] != 0x00) {
		if(string[pos] == 0xA) scroll_lines += print_newline();
		else scroll_lines += print_char(string[pos]);
		pos++;
	}

	return scroll_lines;
}

// recursive function to print decimal integer
void print_dec(int num) {
	if(num / 10) 
		print_dec(num / 10);
	print_char(48 + (num % 10));
}

void print_hex(int num) {
	if(num / 16)
		print_hex(num / 16);
	if((num % 16) <= 9) {
		print_char(48 + (num % 16));
	} else {
		print_char(55 + (num % 16));
	}
}

// new line & carriage return
int print_newline() {
	char* video_memory = (char*) VIDEO_ADDRESS;
	
	set_cursor_position(get_cursor_position() + (COLS * 2));

	int index;

	for(index = 0; index <= get_cursor_position(); index += (COLS * 2));
		set_cursor_position(get_cursor_position() - ((COLS * 2) - (index - get_cursor_position())));

	if(scrolling_enabled == 1) return scroll_terminal();
	else return 0;
}

char char_under_cursor() {
	char* video_memory = (char*) VIDEO_ADDRESS;
	return video_memory[get_cursor_position()];
}

int get_cursor_position() {
	port_byte_out(REG_SCREEN_CTRL, 14);
	int position = port_byte_in(REG_SCREEN_DATA) << 8;

	port_byte_out(REG_SCREEN_CTRL, 15);
	position += port_byte_in(REG_SCREEN_DATA);

	return position * 2;
}

void set_cursor_position(int offset) {
	offset /= 2;
	port_byte_out(REG_SCREEN_CTRL, 14);
	port_byte_out(REG_SCREEN_DATA, offset >> 8);

	port_byte_out(REG_SCREEN_CTRL, 15);
	port_byte_out(REG_SCREEN_DATA, offset & 0xff);
}
