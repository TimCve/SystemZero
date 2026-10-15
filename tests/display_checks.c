#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shared/terminal.h"
#include "../shared/text.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/video.h"
static uint32_t pixels[VIDEO_WIDTH * VIDEO_HEIGHT], saved[VIDEO_WIDTH * VIDEO_HEIGHT];
static uint8_t font[256 * TERM_FONT_HEIGHT];
static terminal_t input_terminal;
static uint8_t input_keys[200];
static uint32_t input_count,input_at;
static int input_poll(void) { assert(input_at<input_count); return input_keys[input_at++]; }
uint8_t port_byte_in(uint16_t port) { (void)port; assert(0); return 0; }
uint32_t screen_rows(void) { return TERM_ROWS; }
uint32_t screen_columns(void) { return TERM_COLS; }
uint8_t get_term_color(void) { return input_terminal.color; }
int get_cursor_position(void) { return input_terminal.cursor*2; }
void set_cursor_position(int offset) { assert(offset>=0); terminal_cursor(&input_terminal,offset/2); }
void screen_begin_update(void) { terminal_begin(&input_terminal); }
void screen_line_wrapped(uint32_t row,int wrap) { if(row<TERM_ROWS) input_terminal.wraps[row]=!!wrap; }
void screen_end_update(void) { terminal_end(&input_terminal); }
void screen_put_cell(uint32_t row,uint32_t col,uint8_t ch,uint8_t color) { terminal_cell(&input_terminal,row,col,ch,color); }
int scroll_terminal(void) { return terminal_scroll(&input_terminal); }
int print_newline(void) { return terminal_char(&input_terminal,'\n'); }
void screen_idle(void) { assert(input_at<input_count); }
static const char *recall(void *context,int direction,const char *current) {
    (void)current; return context?(const char *)context:direction<0?"abc":"x";
}
static void line_input(const uint8_t *keys,uint32_t count,const char *expected,uint32_t origin) {
    assert(count<=sizeof input_keys); memcpy(input_keys,keys,count); input_at=0; input_count=count;
    assert(terminal_init(&input_terminal,pixels,VIDEO_WIDTH,VIDEO_HEIGHT,VIDEO_WIDTH,font,0x1f));
    terminal_cursor(&input_terminal,origin);
    env_vars_t environment={.keyboard_poll=input_poll}; keyboard_bind_env(&environment);
    char text[128]; assert(kbd_readline_recall(text,sizeof text,recall,0));
    assert(!strcmp(text,expected) && input_at==input_count);
    for(uint32_t i=0;i<TERM_CELLS;i++) assert(input_terminal.cells[i].color==0x1f);
    keyboard_bind_env(0);
}
static void readline_checks(void) {
    const uint8_t edit[]={U_ARROW,L_ARROW,DELETE,0x2d,ENTER};
    line_input(edit,sizeof edit,"abx",5);
    const uint8_t shorter[]={U_ARROW,D_ARROW,ENTER};
    line_input(shorter,sizeof shorter,"x",5);
    assert(input_terminal.cells[5].ch=='x' && input_terminal.cells[6].ch==' ' && input_terminal.cells[7].ch==' ');
    const uint8_t wrapped[]={U_ARROW,HOME,DELETE,END,0x2d,ENTER};
    line_input(wrapped,sizeof wrapped,"bcx",TERM_CELLS-2);
    assert(input_terminal.cells[TERM_CELLS-TERM_COLS*2-2].ch=='b');
    assert(input_terminal.cells[TERM_CELLS-TERM_COLS*2-1].ch=='c');
    assert(input_terminal.cells[TERM_CELLS-TERM_COLS*2].ch=='x');
    const uint8_t capacity[]={U_ARROW,0x2d,ENTER};
    memcpy(input_keys,capacity,sizeof capacity); input_count=sizeof capacity; input_at=0;
    terminal_cursor(&input_terminal,0); env_vars_t env={.keyboard_poll=input_poll}; keyboard_bind_env(&env);
    char small[3]; assert(kbd_readline_recall(small,sizeof small,recall,0)); assert(!strcmp(small,"ab"));
    const uint8_t huge_keys[]={U_ARROW,HOME,DELETE,END,ENTER};
    memcpy(input_keys,huge_keys,sizeof huge_keys); input_count=sizeof huge_keys; input_at=0;
    char huge[5001],result[5001];
    for(uint32_t i=0;i<sizeof huge-1;i++) huge[i]='a'+i%26;
    huge[sizeof huge-1]=0; terminal_cursor(&input_terminal,100);
    assert(kbd_readline_recall(result,sizeof result,recall,huge));
    assert(!strcmp(result,huge+1)); /* Input exceeds the full screen; Home/End still edit the correct bytes. */
    const uint8_t cancel[]={U_ARROW,HOME,ESC};
    memcpy(input_keys,cancel,sizeof cancel); input_count=sizeof cancel; input_at=0;
    terminal_cursor(&input_terminal,TERM_CELLS-2);
    assert(!kbd_readline_recall(result,sizeof result,recall,0));
    assert(input_terminal.cursor==TERM_CELLS-TERM_COLS+1); /* Cancellation leaves the cursor after the complete wrapped input. */
    keyboard_bind_env(0);
}
static uint32_t nested_before,nested_after;
static void nested_present(void *context) {
    terminal_t *t=context; assert(t->presenting); nested_before++;
    terminal_present(t); /* A timer-driven refresh must not recurse mid-frame. */
}
static void finished_present(void *context) {
    assert(!((terminal_t *)context)->presenting); nested_after++;
}
static void terminal_checks(void) {
    terminal_t t;
    assert(!terminal_init(&t, pixels, 320, 200, 320, font, 0x17));
    memset(font + 'A' * TERM_FONT_HEIGHT, 0x80, TERM_FONT_HEIGHT);
    assert(terminal_init(&t, pixels, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH, font, 0x17));
    assert(t.origin_x == 16 && t.origin_y == 12 && t.rendered_cells == TERM_CELLS);
    t.overlay=&t; t.before_present=nested_present; t.after_present=finished_present;
    terminal_present(&t);
    assert(nested_before==1 && nested_after==1 && !t.presenting);
    t.before_present=t.after_present=0; t.overlay=0;
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
    for (uint32_t row = 0; row < TERM_ROWS; row++) {
        t.writing_secondary = row & 1;
        terminal_cell(&t, row, 0, 'A' + row, 0x17);
    }
    t.writing_secondary = 0;
    t.cursor = TERM_CELLS; assert(terminal_scroll(&t)); terminal_end(&t);
    assert(t.cells[0].ch == 'B' && t.cells[TERM_CELLS - TERM_COLS].ch == ' ' &&
           t.cursor == TERM_CELLS - TERM_COLS);
    assert(t.secondary[0] && !t.secondary[TERM_COLS] && !t.secondary[TERM_CELLS - TERM_COLS]);
    /* Identical colors do not erase the distinction between primary and secondary text. */
    terminal_cell(&t, 0, 0, 'B', 0x17); assert(!t.secondary[0]);
    t.writing_secondary = 1; terminal_cursor(&t, 0); terminal_char(&t, 'B');
    assert(t.secondary[0]); t.writing_secondary = 0;
    terminal_cell(&t, TERM_ROWS - 1, TERM_COLS - 1, 'A', 0x1f); terminal_present(&t);
    uint32_t last_glyph = (t.origin_y + (TERM_ROWS - 1) * TERM_CELL_HEIGHT + TERM_FONT_TOP)
                          * VIDEO_WIDTH + t.origin_x + (TERM_COLS - 1) * TERM_FONT_WIDTH;
    assert(pixels[last_glyph] == 0xffffff); /* The expanded bottom-right cell is drawable. */
    t.scrolling = 0; terminal_cursor(&t, TERM_CELLS); terminal_char(&t, 'A');
    assert(t.cursor == TERM_CELLS); /* A full-screen footer cannot write beyond the grid. */
    pixels[0] = pixels[100 * VIDEO_WIDTH + 100] = 0xffffff;
    terminal_clear(&t); assert(pixels[0] == 0x0000aa && pixels[100 * VIDEO_WIDTH + 100] == 0x0000aa);
    for (uint32_t i = 0; i < TERM_CELLS; i++) assert(!t.secondary[i]);
    /* GOP can expose RGB bytes instead of VGA's BGR bytes. */
    t.red_first = 1; terminal_clear(&t);
    assert(pixels[0] == 0xaa0000);
    terminal_cell(&t, 0, 0, 'A', 0x14); terminal_present(&t);
    assert(pixels[glyph_origin] == 0x0000aa && pixels[glyph_origin + 1] == 0xaa0000);
    assert(terminal_init(&t, pixels, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH, font, 0x17));
    assert(t.red_first == 0 && pixels[0] == 0x0000aa);
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
    terminal_checks(); text_checks(); keyboard_checks(); readline_checks();
    puts("Framebuffer, incremental rendering, text navigation, and keyboard checks passed.");
    return 0;
}
