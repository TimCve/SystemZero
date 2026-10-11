#ifndef SYSTEMZERO_TERMINAL_H
#define SYSTEMZERO_TERMINAL_H
#include <stdint.h>

#define TERM_COLS 96u
#define TERM_ROWS 48u
#define TERM_CELLS (TERM_COLS * TERM_ROWS)
#define TERM_FONT_WIDTH 8u
#define TERM_FONT_HEIGHT 8u
/* Two pixels above and below each glyph keep the compact font readable. */
#define TERM_CELL_HEIGHT 12u
#define TERM_FONT_TOP 2u
#define TERM_NO_CURSOR 0xffffffffu

typedef struct { uint8_t ch, color; } terminal_cell_t;
typedef struct {
    /* Keep cells first: diagnostics can inspect logical output independently of pixels. */
    terminal_cell_t cells[TERM_CELLS], shown[TERM_CELLS];
    volatile uint32_t *pixels;
    const uint8_t *font;
    uint32_t width, height, pitch, origin_x, origin_y;
    uint32_t cursor, shown_cursor, updates, rendered_cells, presentations;
    uint8_t color, scrolling, cursor_visible, clear_canvas;
} terminal_t;

int terminal_init(terminal_t *t, volatile uint32_t *pixels, uint32_t width,
                  uint32_t height, uint32_t pitch, const uint8_t *font, uint8_t color);
void terminal_present(terminal_t *t);
void terminal_begin(terminal_t *t);
void terminal_end(terminal_t *t);
void terminal_cell(terminal_t *t, uint32_t row, uint32_t col, uint8_t ch, uint8_t color);
void terminal_clear(terminal_t *t);
int terminal_scroll(terminal_t *t);
int terminal_char(terminal_t *t, uint8_t ch);
void terminal_cursor(terminal_t *t, uint32_t index);
uint32_t terminal_palette(uint8_t index);
#endif
