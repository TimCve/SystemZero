#include "screen.h"
#include "../../vga_colors.h"
#include "../utils/ports.h"
#include "../../env_vars.h"
#include "video.h"

static terminal_t *display;
#define VGA_COLS 80
#define VGA_ROWS 25
uint32_t screen_rows(void) { return display ? TERM_ROWS : VGA_ROWS; }
uint32_t screen_columns(void) { return display ? TERM_COLS : VGA_COLS; }
void screen_bind_env(env_vars_t *env) { display = env ? env->terminal : 0; }
int screen_init(env_vars_t *env) {
    static terminal_t terminal;
    volatile uint32_t *pixels = video_init();
    if (!pixels || !terminal_init(&terminal, pixels, VIDEO_WIDTH, VIDEO_HEIGHT,
                                  VIDEO_WIDTH, (void *)VIDEO_FONT_ADDRESS, env->term_color)) return 0;
    env->terminal = &terminal; screen_bind_env(env); return 1;
}
void screen_begin_update(void) { terminal_begin(display); }
void screen_end_update(void) { terminal_end(display); }
void screen_put_cell(uint32_t row, uint32_t col, uint8_t ch, uint8_t color) {
    if (row >= ROWS || col >= COLS) return;
    if (display) { terminal_cell(display, row, col, ch, color); terminal_present(display); }
    else {
        volatile uint8_t *video = (void *)VIDEO_ADDRESS;
        uint32_t index = (row * VGA_COLS + col) * 2; video[index] = ch; video[index + 1] = color;
    }
}
void screen_put_text(uint32_t row, uint32_t col, const char *text, uint8_t color) {
    screen_begin_update();
    while (text && *text && col < COLS) screen_put_cell(row, col++, *text++, color);
    screen_end_update();
}
void screen_cursor_visible(int visible) {
    if (display) { display->cursor_visible = !!visible; terminal_present(display); }
    else { port_byte_out(REG_SCREEN_CTRL, 10); port_byte_out(REG_SCREEN_DATA, visible ? 14 : 0x20); }
}
int screen_draw_pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (!display || x >= display->width || y >= display->height) return 1;
    display->pixels[y * display->pitch + x] = terminal_palette(color); return 0;
}

void set_cursor_position(int offset);
int get_cursor_position();

int print(char* string);
int print_newline();

int scrolling_enabled = 1;

uint8_t term_color; 

void set_term_color(uint8_t color) {
	term_color = color;
	if (display) display->color = color;
}

uint8_t get_term_color() {
	return display ? display->color : term_color;
}

void set_scrolling(int value) {
	if(value <= 0) scrolling_enabled = 0;
	else if(value > 0) scrolling_enabled = 1;
	if (display) display->scrolling = scrolling_enabled;
}

// clears screen & sets cursor position to screen start
void clear() {
	if (display) { terminal_clear(display); return; }
	char* video_memory = (char*) VIDEO_ADDRESS;

	for(int i = 0; i < VGA_COLS * VGA_ROWS * 2; i += 2) {
		video_memory[i] = 0x00;
		video_memory[i + 1] = term_color;
	}	

	set_cursor_position(0x00);
}

int scroll_terminal() {
	if (display) return terminal_scroll(display);
	char* video_memory = (char*) VIDEO_ADDRESS;
	int cursor_pos = get_cursor_position();

	if(cursor_pos >= (VGA_ROWS * VGA_COLS) * 2) {
		for(int i = VGA_COLS * 2; i < (VGA_ROWS * VGA_COLS) * 2; i++) {
			video_memory[i - (VGA_COLS * 2)] = video_memory[i];
		}

		for(int i = ((VGA_COLS * (VGA_ROWS - 1)) * 2); i < (VGA_ROWS * VGA_COLS) * 2; i += 2) {
			video_memory[i] = 0x00;
			video_memory[i + 1] = term_color;
		}

		set_cursor_position(VGA_COLS * (VGA_ROWS - 1) * 2);
		return 1;
	} else return 0;
}

// print single character at cursor position
int print_char(char ch) {
    if (display) return terminal_char(display, (uint8_t)ch);
    if (ch == '\n') return print_newline();
    if (ch == '\r') return 0;
	char* video_memory = (char*) VIDEO_ADDRESS;
	int cursor_pos = get_cursor_position();

	if (cursor_pos >= VGA_COLS * VGA_ROWS * 2) return 0;
	video_memory[cursor_pos] = ch;
	video_memory[cursor_pos + 1] = term_color;
	cursor_pos += 2;
	set_cursor_position(cursor_pos);

	if(scrolling_enabled == 1) return scroll_terminal();
	else return 0;
}

// prints ASCII string
int print(char* string) {
	screen_begin_update();
	int pos = 0;
	int scroll_lines = 0;
	
	while(string[pos] != 0x00) {
		if(string[pos] == 0xA) scroll_lines += print_newline();
		else scroll_lines += print_char(string[pos]);
		pos++;
	}

	screen_end_update(); return scroll_lines;
}

// recursive function to print decimal integer
void print_dec(int num) {
	screen_begin_update();
	if(num / 10) 
		print_dec(num / 10);
	print_char(48 + (num % 10));
	screen_end_update();
}

void print_hex(int num) {
	screen_begin_update();
	if(num / 16)
		print_hex(num / 16);
	if((num % 16) <= 9) {
		print_char(48 + (num % 16));
	} else {
		print_char(55 + (num % 16));
	}
	screen_end_update();
}

// new line & carriage return
int print_newline() {
    if (display) return terminal_char(display, '\n');
    int row = get_cursor_position() / (VGA_COLS * 2);
    set_cursor_position((row + 1) * VGA_COLS * 2);
    return scrolling_enabled ? scroll_terminal() : 0;
}

char char_under_cursor() {
	if (display) return display->cursor < TERM_CELLS ? display->cells[display->cursor].ch : 0;
	char* video_memory = (char*) VIDEO_ADDRESS;
	return video_memory[get_cursor_position()];
}

int get_cursor_position() {
	if (display) return display->cursor * 2;
	port_byte_out(REG_SCREEN_CTRL, 14);
	int position = port_byte_in(REG_SCREEN_DATA) << 8;

	port_byte_out(REG_SCREEN_CTRL, 15);
	position += port_byte_in(REG_SCREEN_DATA);

	return position * 2;
}

void set_cursor_position(int offset) {
	if (display) { terminal_cursor(display, offset < 0 ? 0 : (uint32_t)offset / 2); return; }
	offset /= 2;
	port_byte_out(REG_SCREEN_CTRL, 14);
	port_byte_out(REG_SCREEN_DATA, offset >> 8);

	port_byte_out(REG_SCREEN_CTRL, 15);
	port_byte_out(REG_SCREEN_DATA, offset & 0xff);
}
