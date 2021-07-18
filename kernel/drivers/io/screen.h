#include <stdint.h>

#define VIDEO_ADDRESS 0xb8000
#define ROWS 25
#define COLS 80

#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

void set_term_color(uint8_t color);
void clear();
void print(char* str);
void print_dec(int num);
void print_hex(int num);
void print_newline();
void print_char(char ch);
int get_cursor_position();
void set_cursor_position(int offset);



