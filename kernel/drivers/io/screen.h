#include <stdint.h>

#define VIDEO_ADDRESS 0xb8000
#define ROWS 25
#define COLS 80

#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

int scroll_terminal();
void set_scrolling(int value);
void set_term_color(uint8_t color);
uint8_t get_term_color();
void clear();
int print(char* str);
void print_dec(int num);
void print_hex(int num);
int print_newline();
char char_under_cursor();
int print_char(char ch);
int get_cursor_position();
void set_cursor_position(int offset);



