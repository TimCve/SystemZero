#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
static void render(const uint8_t *data, uint32_t size, uint32_t offset, uint32_t cursor, uint8_t color) {
    clear();
    for (uint32_t row = 0; row < 20; row++) {
        set_term_color(color); print_hex(offset + row * 24); print(": ");
        for (uint32_t col = 0; col < 24; col++) {
            uint32_t pos = offset + row * 24 + col;
            set_term_color(pos == cursor ? (color << 4) | (color >> 4) : color);
            if (pos < size) { if (data[pos] < 16) print_char('0'); print_hex(data[pos]); print_char(' '); }
            else print("   ");
        }
        set_cursor_position((row + 1) * 160);
    }
    set_term_color(color); print("Arrows/PgUp/PgDn: move | c: change | i: insert | d: delete"); print_newline();
    print("w: save | Esc: quit"); print_newline();
}
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive); set_term_color(env->term_color);
    char filename[512]; char *part = splice(args, 1, ' '); memcpy(filename, part, strlen(part) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    uint8_t *file = (void *)malloc(ESFS_MAX_FILE + 512, 1, 0);
    if (!file) return 1;
    memset(file, 0, ESFS_MAX_FILE + 512);
    if (file_read(filename, file, (info.size + 511) / 512, 0) == 2) return 1;
    uint32_t size = info.size, cursor = 0, offset = 0; uint8_t previous = 0; char *notice = "";
    for (;;) {
        render(file, size, offset, cursor, env->term_color); print(notice);
        uint8_t key;
        do { key = get_input_keycode(); } while (!key || key == previous);
        previous = key;
        if (key == ESC) break;
        if (key == L_ARROW && cursor) cursor--;
        if (key == R_ARROW && cursor < size) cursor++;
        if (key == U_ARROW && cursor >= 24) cursor -= 24;
        if (key == D_ARROW) cursor = size - cursor > 24 ? cursor + 24 : size;
        if (key == PGUP) cursor = cursor > 480 ? cursor - 480 : 0;
        if (key == PGDN) cursor = size - cursor > 480 ? cursor + 480 : size;
        if (cursor < offset) offset = cursor / 24 * 24;
        if (cursor >= offset + 480) offset = (cursor / 24 - 19) * 24;
        if (key == 0x20 && cursor < size) {
            for (uint32_t i = cursor; i + 1 < size; i++) file[i] = file[i + 1];
            size--;
        }
        if ((key == 0x2e && cursor < size) || (key == 0x17 && size < ESFS_MAX_FILE)) {
            char input[4] = {0}; print("Byte value (hex): "); kbd_readline(input, env->tty_calibration, sizeof input);
            int valid = strlen(input) > 0 && strlen(input) <= 2;
            for (int i = 0; input[i]; i++) if (!((input[i] >= '0' && input[i] <= '9') ||
                (input[i] >= 'a' && input[i] <= 'f') || (input[i] >= 'A' && input[i] <= 'F'))) valid = 0;
            if (valid) {
                if (key == 0x17) { for (uint32_t i = size; i > cursor; i--) file[i] = file[i - 1]; size++; }
                file[cursor] = htoi(input);
            }
        }
        if (key == 0x11) notice = file_replace(filename, file, size) ? "Saved." : "Save failed.";
    }
    clear(); return 0;
}
