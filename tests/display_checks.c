#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shared/terminal.h"
#include "../shared/text.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/video.h"
static uint32_t pixels[VIDEO_WIDTH * VIDEO_HEIGHT], saved[VIDEO_WIDTH * VIDEO_HEIGHT];
static uint8_t font[256 * TERM_FONT_HEIGHT];
static void terminal_checks(void) {
    terminal_t t;
    assert(!terminal_init(&t, pixels, 320, 200, 320, font, 0x17));
    memset(font + 'A' * TERM_FONT_HEIGHT, 0x80, TERM_FONT_HEIGHT);
    assert(terminal_init(&t, pixels, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH, font, 0x17));
    assert(t.origin_x == 16 && t.origin_y == 12 && t.rendered_cells == TERM_CELLS);
    t.cursor_visible = 0; terminal_present(&t);
    memcpy(saved, pixels, sizeof pixels);
    uint32_t before = t.rendered_cells;
    terminal_begin(&t); terminal_cell(&t, 0, 0, 'A', 0x1f);
    assert(!memcmp(saved, pixels, sizeof pixels)); /* No intermediate erase or repaint. */
    terminal_end(&t); assert(t.rendered_cells == before + 1);
    uint32_t glyph_origin = (t.origin_y + TERM_FONT_TOP) * VIDEO_WIDTH + t.origin_x;
    assert(pixels[glyph_origin] == 0xffffff && pixels[glyph_origin + 1] == 0x0000aa);
    for (uint32_t y = 0; y < VIDEO_HEIGHT; y++)
        for (uint32_t x = 0; x < VIDEO_WIDTH; x++) {
            int in_glyph = y >= t.origin_y + TERM_FONT_TOP &&
                           y < t.origin_y + TERM_FONT_TOP + TERM_FONT_HEIGHT &&
                           x >= t.origin_x && x < t.origin_x + TERM_FONT_WIDTH;
            if (!in_glyph) assert(pixels[y * VIDEO_WIDTH + x] == saved[y * VIDEO_WIDTH + x]);
        }
    before = t.rendered_cells;
    terminal_begin(&t); terminal_cell(&t, 0, 0, ' ', 0x17); terminal_cell(&t, 0, 0, 'A', 0x1f);
    terminal_end(&t); assert(t.rendered_cells == before); /* Blank-then-rewrite stays in RAM. */
    terminal_present(&t); assert(t.rendered_cells == before);
    terminal_begin(&t); terminal_begin(&t); terminal_cell(&t, 0, 1, 'A', 0x1f);
    terminal_end(&t); assert(t.rendered_cells == before);
    terminal_end(&t); assert(t.rendered_cells == before + 1);
    t.cursor_visible = 1; terminal_cursor(&t, 0);
    uint32_t underline = (t.origin_y + TERM_CELL_HEIGHT - 1) * VIDEO_WIDTH + t.origin_x + 7;
    assert(pixels[underline] == 0xffffff);
    terminal_cursor(&t, 1); assert(pixels[underline] == 0x0000aa);
    t.cursor_visible = 0; terminal_present(&t);
    before = t.rendered_cells; terminal_cell(&t, 99, 99, 'A', 0x1f); terminal_present(&t);
    assert(t.rendered_cells == before);
    terminal_begin(&t);
    for (uint32_t row = 0; row < TERM_ROWS; row++) terminal_cell(&t, row, 0, 'A' + row, 0x17);
    t.cursor = TERM_CELLS; assert(terminal_scroll(&t)); terminal_end(&t);
    assert(t.cells[0].ch == 'B' && t.cells[TERM_CELLS - TERM_COLS].ch == ' ' &&
           t.cursor == TERM_CELLS - TERM_COLS);
    terminal_cell(&t, TERM_ROWS - 1, TERM_COLS - 1, 'A', 0x1f); terminal_present(&t);
    uint32_t last_glyph = (t.origin_y + (TERM_ROWS - 1) * TERM_CELL_HEIGHT + TERM_FONT_TOP)
                          * VIDEO_WIDTH + t.origin_x + (TERM_COLS - 1) * TERM_FONT_WIDTH;
    assert(pixels[last_glyph] == 0xffffff); /* The expanded bottom-right cell is drawable. */
    t.scrolling = 0; terminal_cursor(&t, TERM_CELLS); terminal_char(&t, 'A');
    assert(t.cursor == TERM_CELLS); /* A full-screen footer cannot write beyond the grid. */
    pixels[0] = pixels[100 * VIDEO_WIDTH + 100] = 0xffffff;
    terminal_clear(&t); assert(pixels[0] == 0x0000aa && pixels[100 * VIDEO_WIDTH + 100] == 0x0000aa);
}
static void text_checks(void) {
    uint8_t data[4096] = "abcdef\nx\nabcdef\n";
    text_buffer_t b = {.data = data, .size = 16, .capacity = sizeof data, .cursor = 5};
    text_move_rows(&b, 1); assert(b.cursor == 8);
    text_move_rows(&b, 1); assert(b.cursor == 14); /* Preserve the desired column past a short line. */
    text_move_rows(&b, -1); assert(b.cursor == 8);
    text_move_rows(&b, -1); assert(b.cursor == 5);
    assert(text_line_start(data, b.size, 7) == 7);
    memset(data, 'a', TERM_COLS * 2 + 1); b.size = TERM_COLS * 2 + 1;
    b.cursor = TERM_COLS; b.goal_valid = 0;
    assert(text_line_start(data, b.size, TERM_COLS) == TERM_COLS);
    text_move_rows(&b, 1); assert(b.cursor == TERM_COLS * 2);
    text_move_rows(&b, -1); assert(b.cursor == TERM_COLS);
    memcpy(data, "a\tb\nx\n", 6); b.size = 6; b.cursor = 2; b.goal_valid = 0;
    assert(text_column(data, 0, 2) == 4 && text_line(data, b.size, 0).columns == 5);
    text_move_rows(&b, 1); assert(b.cursor == 5);
    text_move_rows(&b, -1); assert(b.cursor == 2);
    for (uint32_t i = 0; i < TERM_ROWS * 2; i++) { data[i * 2] = 'a'; data[i * 2 + 1] = '\n'; }
    b.size = TERM_ROWS * 4; b.cursor = (TERM_ROWS + 4) * 2; b.top = 0; b.goal_valid = 0;
    text_ensure_visible(&b); assert(b.top == 12); /* Cursor is on the last visible content row. */
    assert(text_previous_lines(data, b.size, b.cursor, TERM_ROWS - 1) == 10);
    b.cursor = 4; text_ensure_visible(&b); assert(b.top == 4);
    b.size = b.cursor = b.top = 0;
    assert(text_insert(&b, 'x') && text_insert(&b, '\n') && text_insert(&b, '\t'));
    assert(b.size == 3 && b.cursor == 3 && !memcmp(data, "x\n\t", 3));
    assert(text_delete(&b, 1) && b.size == 2 && b.cursor == 2);
    b.cursor = 0; assert(!text_delete(&b, 1));
    assert(text_delete(&b, 0) && b.size == 1 && data[0] == '\n');
    b.capacity = b.size; assert(!text_insert(&b, 'y'));
    b.cursor = b.size; assert(!text_delete(&b, 0));
    assert(text_line_start(data, b.size, b.size) == b.size); /* Trailing newline has an editable EOF row. */
    /* The legacy text console still wraps at 80 and reserves its 25th row. */
    text_set_geometry(80, 25); memset(data, 'a', 161); b.size = 161;
    assert(text_line_start(data, b.size, 80) == 80);
    for (uint32_t i = 0; i < 60; i++) { data[i * 2] = 'a'; data[i * 2 + 1] = '\n'; }
    b.size = 120; b.cursor = 58; b.top = 0;
    text_ensure_visible(&b); assert(b.top == 12);
    text_set_geometry(TERM_COLS, TERM_ROWS);
}
static void keyboard_checks(void) {
    keyboard_state_t state = {0}; keyboard_event_t event;
    assert(!kbd_decode(&state, 0x2a, &event)); assert(!kbd_decode(&state, 0x36, &event));
    assert(!kbd_decode(&state, 0xaa, &event));
    assert(kbd_decode(&state, 0x1e, &event) && event.character == 'A');
    assert(!kbd_decode(&state, 0xb6, &event));
    assert(kbd_decode(&state, 0x1e, &event) && event.character == 'a');
    assert(kbd_decode(&state, 0x1e, &event)); /* Typematic makes are not suppressed. */
    assert(!kbd_decode(&state, 0x9e, &event));
    assert(!kbd_decode(&state, 0xe0, &event));
    assert(kbd_decode(&state, U_ARROW, &event) && event.code == U_ARROW);
    assert(!kbd_decode(&state, 0xe0, &event)); assert(!kbd_decode(&state, U_ARROW | 0x80, &event));
    assert(!kbd_decode(&state, CTRL, &event));
    assert(kbd_decode(&state, 0x1f, &event) && event.control);
    assert(!kbd_decode(&state, CTRL | 0x80, &event));
    assert(kbd_decode(&state, TAB, &event) && event.character == '\t' && !event.control);
}
int main(void) {
    terminal_checks(); text_checks(); keyboard_checks();
    puts("Framebuffer, incremental rendering, text navigation, and keyboard checks passed.");
    return 0;
}
