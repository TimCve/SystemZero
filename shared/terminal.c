#include "terminal.h"

uint32_t terminal_palette(uint8_t index) {
    static const uint32_t colors[16] = {
        0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
        0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff
    };
    return colors[index & 15];
}
static void render_cell(terminal_t *t, uint32_t index) {
    terminal_cell_t cell = t->cells[index];
    uint32_t x = t->origin_x + index % TERM_COLS * TERM_FONT_WIDTH;
    uint32_t y = t->origin_y + index / TERM_COLS * TERM_CELL_HEIGHT;
    uint32_t foreground = terminal_palette(cell.color), background = terminal_palette(cell.color >> 4);
    const uint8_t *glyph = t->font + cell.ch * TERM_FONT_HEIGHT;
    for (uint32_t row = 0; row < TERM_CELL_HEIGHT; row++) {
        uint8_t bits = cell.ch && row >= TERM_FONT_TOP && row < TERM_FONT_TOP + TERM_FONT_HEIGHT
                       ? glyph[row - TERM_FONT_TOP] : 0;
        for (uint32_t col = 0; col < TERM_FONT_WIDTH; col++)
            t->pixels[(y + row) * t->pitch + x + col] = bits & (0x80u >> col) ? foreground : background;
    }
    t->shown[index] = cell; t->rendered_cells++;
}
int terminal_init(terminal_t *t, volatile uint32_t *pixels, uint32_t width,
                  uint32_t height, uint32_t pitch, const uint8_t *font, uint8_t color) {
    if (!t || !pixels || !font || width < TERM_COLS * TERM_FONT_WIDTH ||
        height < TERM_ROWS * TERM_CELL_HEIGHT || pitch < width ||
        height > 0x3fffffffu / pitch) return 0;
    t->pixels = pixels; t->font = font; t->width = width; t->height = height; t->pitch = pitch;
    t->origin_x = (width - TERM_COLS * TERM_FONT_WIDTH) / 2;
    t->origin_y = (height - TERM_ROWS * TERM_CELL_HEIGHT) / 2;
    t->cursor = 0; t->shown_cursor = TERM_NO_CURSOR; t->updates = 0;
    t->color = color; t->scrolling = 1; t->cursor_visible = 1;
    t->clear_canvas = 0;
    t->rendered_cells = t->presentations = 0;
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) t->pixels[y * pitch + x] = terminal_palette(color >> 4);
    for (uint32_t i = 0; i < TERM_CELLS; i++) {
        t->cells[i] = (terminal_cell_t){' ', color};
        t->shown[i] = (terminal_cell_t){0xff, (uint8_t)~color};
    }
    terminal_present(t); return 1;
}
void terminal_present(terminal_t *t) {
    if (!t || t->updates) return;
    uint32_t cursor = t->cursor_visible && t->cursor < TERM_CELLS ? t->cursor : TERM_NO_CURSOR;
    int draw_cursor = cursor != t->shown_cursor;
    uint32_t before = t->rendered_cells;
    if (t->clear_canvas) {
        /* Clear margins too, including pixels drawn outside the text viewport. */
        for (uint32_t y = 0; y < t->height; y++)
            for (uint32_t x = 0; x < t->width; x++)
                if (y < t->origin_y || y >= t->origin_y + TERM_ROWS * TERM_CELL_HEIGHT ||
                    x < t->origin_x || x >= t->origin_x + TERM_COLS * TERM_FONT_WIDTH)
                    t->pixels[y * t->pitch + x] = terminal_palette(t->color >> 4);
        t->clear_canvas = 0;
    }
    if (draw_cursor && t->shown_cursor < TERM_CELLS) render_cell(t, t->shown_cursor);
    for (uint32_t i = 0; i < TERM_CELLS; i++) {
        if (t->cells[i].ch == t->shown[i].ch && t->cells[i].color == t->shown[i].color) continue;
        render_cell(t, i);
        if (i == cursor) draw_cursor = 1;
    }
    if (draw_cursor && cursor < TERM_CELLS) {
        uint32_t x = t->origin_x + cursor % TERM_COLS * TERM_FONT_WIDTH;
        uint32_t y = t->origin_y + cursor / TERM_COLS * TERM_CELL_HEIGHT;
        uint8_t color = t->cells[cursor].color & 15;
        if (color == (t->cells[cursor].color >> 4)) color ^= 15;
        for (uint32_t row = TERM_CELL_HEIGHT - 1; row < TERM_CELL_HEIGHT; row++)
            for (uint32_t col = 0; col < TERM_FONT_WIDTH; col++)
                t->pixels[(y + row) * t->pitch + x + col] = terminal_palette(color);
    }
    if (before != t->rendered_cells || cursor != t->shown_cursor) t->presentations++;
    t->shown_cursor = cursor;
}
void terminal_begin(terminal_t *t) { if (t) t->updates++; }
void terminal_end(terminal_t *t) {
    if (t && t->updates && !--t->updates) terminal_present(t);
}
void terminal_cell(terminal_t *t, uint32_t row, uint32_t col, uint8_t ch, uint8_t color) {
    if (!t || row >= TERM_ROWS || col >= TERM_COLS) return;
    t->cells[row * TERM_COLS + col] = (terminal_cell_t){ch, color};
}
void terminal_clear(terminal_t *t) {
    if (!t) return;
    for (uint32_t i = 0; i < TERM_CELLS; i++) {
        t->cells[i] = (terminal_cell_t){' ', t->color}; t->shown[i].ch = 0xff;
    }
    t->clear_canvas = 1;
    t->cursor = 0; terminal_present(t);
}
int terminal_scroll(terminal_t *t) {
    if (!t || t->cursor < TERM_CELLS) return 0;
    for (uint32_t i = 0; i < TERM_CELLS - TERM_COLS; i++) t->cells[i] = t->cells[i + TERM_COLS];
    for (uint32_t i = TERM_CELLS - TERM_COLS; i < TERM_CELLS; i++)
        t->cells[i] = (terminal_cell_t){' ', t->color};
    t->cursor = TERM_CELLS - TERM_COLS; terminal_present(t); return 1;
}
int terminal_char(terminal_t *t, uint8_t ch) {
    if (!t || ch == '\r') return 0;
    if (ch == '\n') t->cursor = (t->cursor / TERM_COLS + 1) * TERM_COLS;
    else if (t->cursor < TERM_CELLS) t->cells[t->cursor++] = (terminal_cell_t){ch, t->color};
    int scrolled = t->scrolling ? terminal_scroll(t) : 0;
    terminal_present(t); return scrolled;
}
void terminal_cursor(terminal_t *t, uint32_t index) {
    if (!t) return;
    t->cursor = index > TERM_CELLS ? TERM_CELLS : index; terminal_present(t);
}
