#ifndef SYSTEMZERO_SCREEN_H
#define SYSTEMZERO_SCREEN_H
#include <stdint.h>
#include "../../env_vars.h"
#include "../../../shared/terminal.h"

#define VIDEO_ADDRESS 0xb8000
#define ROWS (screen_rows())
#define COLS (screen_columns())

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
int screen_init(env_vars_t *env);
uint32_t screen_rows(void);
uint32_t screen_columns(void);
void screen_bind_env(env_vars_t *env);
void screen_idle(void);
void screen_secondary_text(int enabled);
/* Snapshots use the command's heap and are released when the command returns. */
typedef struct screen_state screen_state_t;
screen_state_t *screen_save_state(void);
void screen_restore_state(const screen_state_t *state);
void screen_begin_update(void);
void screen_end_update(void);
void screen_line_wrapped(uint32_t row,int wrap);
void screen_put_cell(uint32_t row, uint32_t col, uint8_t ch, uint8_t color);
void screen_put_text(uint32_t row, uint32_t col, const char *text, uint8_t color);
void screen_cursor_visible(int visible);
int screen_draw_pixel(uint32_t x, uint32_t y, uint8_t color);
#endif
