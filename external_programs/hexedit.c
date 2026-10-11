#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
#define HEX_COLUMNS 16u
#define HEX_ROWS (ROWS - 3u)
#define HEX_PAGE (HEX_COLUMNS * HEX_ROWS)
#define ASCII_BORDER (10u + HEX_COLUMNS * 3u + 1u)
static char digit(uint8_t value) { return "0123456789ABCDEF"[value & 15]; }
static void render(const uint8_t *data, uint32_t size, uint32_t offset, uint32_t cursor,
                   const char *filename, uint8_t color, int dirty, const char *notice) {
    screen_begin_update(); screen_cursor_visible(0);
    for (uint32_t row = 0; row < ROWS; row++)
        for (uint32_t col = 0; col < COLS; col++) screen_put_cell(row, col, ' ', color);
    screen_put_text(0, 0, "HEX ", color); screen_put_text(0, 4, filename, color);
    for (uint32_t row = 0; row < HEX_ROWS; row++) {
        uint32_t address = offset + row * HEX_COLUMNS;
        for (uint32_t i = 0; i < 8; i++) screen_put_cell(row + 1, i, digit(address >> ((7 - i) * 4)), color);
        screen_put_text(row + 1, 8, ": ", color);
        screen_put_cell(row + 1, ASCII_BORDER, '|', color);
        screen_put_cell(row + 1, ASCII_BORDER + HEX_COLUMNS + 1, '|', color);
        for (uint32_t col = 0; col < HEX_COLUMNS; col++) {
            uint32_t pos = address + col;
            uint8_t attr = pos == cursor ? (color << 4) | (color >> 4) : color;
            char high = ' ', low = ' ', ascii = ' ';
            if (pos < size) {
                high = digit(data[pos] >> 4); low = digit(data[pos]);
                ascii = data[pos] >= 32 && data[pos] < 127 ? data[pos] : '.';
            } else if (pos == cursor) high = low = '-';
            screen_put_cell(row + 1, 10 + col * 3, high, attr);
            screen_put_cell(row + 1, 11 + col * 3, low, attr);
            screen_put_cell(row + 1, ASCII_BORDER + 1 + col, ascii, attr);
        }
    }
    screen_put_text(ROWS - 2, 0, "Arrows/PgUp/PgDn: move | c: change | i: insert | d: delete | w: save | Esc", color);
    screen_put_text(ROWS - 1, 0, notice && *notice ? notice : dirty ? "* Modified" : "Ready.", color);
    screen_end_update();
}
static int ask_byte(uint8_t color, int calibration, uint8_t *value) {
    screen_begin_update();
    uint32_t footer = ROWS - 1;
    for (uint32_t col = 0; col < COLS; col++) screen_put_cell(footer, col, ' ', color);
    screen_put_text(footer, 0, "Byte value (hex): ", color);
    set_cursor_position(footer * COLS * 2 + 17 * 2); screen_cursor_visible(1); screen_end_update();
    char input[4] = {0};
    if (!kbd_readline(input, calibration, sizeof input)) return 0;
    int length = strlen(input);
    if (length < 1 || length > 2) return 0;
    for (int i = 0; i < length; i++) if (!((input[i] >= '0' && input[i] <= '9') ||
        (input[i] >= 'a' && input[i] <= 'f') || (input[i] >= 'A' && input[i] <= 'F'))) return 0;
    *value = htoi(input); return 1;
}
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive);
    char filename[512]; char *part = splice(args, 1, ' '); memcpy(filename, part, strlen(part) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    uint8_t *file = (void *)malloc(ESFS_MAX_FILE + 512, 1, 0);
    if (!file || file_read(filename, file, (info.size + 511) / 512, 0) == 2) return 1;
    uint32_t size = info.size, cursor = 0, offset = 0; int dirty = 0;
    keyboard_state_t keyboard = {0}; const char *notice = "";
    screen_state_t *console = screen_save_state();
    if (!console) { print("Not enough memory to save the console."); print_newline(); return 1; }
    set_term_color(env->term_color);
    set_scrolling(0); render(file, size, offset, cursor, filename, env->term_color, dirty, notice);
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard, &event); uint8_t key = event.code;
        if (key == ESC) break;
        if (key == L_ARROW && cursor) cursor--;
        else if (key == R_ARROW && cursor < size) cursor++;
        else if (key == U_ARROW && cursor >= HEX_COLUMNS) cursor -= HEX_COLUMNS;
        else if (key == D_ARROW) cursor += size - cursor > HEX_COLUMNS ? HEX_COLUMNS : size - cursor;
        else if (key == PGUP) {
            cursor = cursor > HEX_PAGE ? cursor - HEX_PAGE : 0;
            offset = offset > HEX_PAGE ? offset - HEX_PAGE : 0;
        } else if (key == PGDN) {
            cursor += size - cursor > HEX_PAGE ? HEX_PAGE : size - cursor;
            offset += size - offset > HEX_PAGE ? HEX_PAGE : size - offset;
            offset = offset / HEX_COLUMNS * HEX_COLUMNS;
        }
        else if (key == HOME) cursor = event.control ? 0 : cursor / HEX_COLUMNS * HEX_COLUMNS;
        else if (key == END) {
            uint32_t end = cursor / HEX_COLUMNS * HEX_COLUMNS + HEX_COLUMNS - 1;
            cursor = event.control || end > size ? size : end;
        } else if ((key == 0x20 || key == DELETE || key == BACKSPACE) &&
                   (key == BACKSPACE ? cursor > 0 : cursor < size)) {
            if (key == BACKSPACE) cursor--;
            for (uint32_t i = cursor; i + 1 < size; i++) file[i] = file[i + 1];
            size--; dirty = 1; notice = "";
        } else if ((key == 0x2e && cursor < size) || (key == 0x17 && size < ESFS_MAX_FILE)) {
            uint8_t value;
            if (ask_byte(env->term_color, env->tty_calibration, &value)) {
                if (key == 0x17) { for (uint32_t i = size; i > cursor; i--) file[i] = file[i - 1]; size++; }
                file[cursor] = value; dirty = 1; notice = "";
            } else notice = "Byte entry cancelled or invalid.";
        } else if (key == 0x11 || (event.control && key == 0x1f)) {
            if (file_replace(filename, file, size)) { dirty = 0; notice = "Saved."; }
            else notice = "Save failed.";
        } else continue;
        if (cursor > size) cursor = size;
        if (cursor < offset) offset = cursor / HEX_COLUMNS * HEX_COLUMNS;
        if (cursor >= offset + HEX_PAGE) offset = (cursor / HEX_COLUMNS - HEX_ROWS + 1) * HEX_COLUMNS;
        render(file, size, offset, cursor, filename, env->term_color, dirty, notice);
    }
    screen_restore_state(console); return 0;
}
