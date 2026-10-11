#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
#include "text_view.h"
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive); set_term_color(env->term_color);
    char filename[512]; char *part = splice(args, 1, ' '); memcpy(filename, part, strlen(part) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    uint8_t *file = (void *)malloc(ESFS_MAX_FILE + 512, 1, 0);
    if (!file) return 1;
    memset(file, 0, ESFS_MAX_FILE + 512);
    if (file_read(filename, file, (info.size + 511) / 512, 0) == 2) return 1;
    uint32_t size = info.size, cursor = 0, line = 0;
    int shift = 0, control = 0; uint8_t previous = 0; char *notice = "";
    for (;;) {
        text_render(file, size, text_start(file, size, line), cursor, env->term_color);
        print("Ctrl+S: save | Ctrl+R: top | Esc: quit "); print(notice);
        uint8_t key;
        do { key = get_input_keycode(); } while (!key || key == previous);
        previous = key;
        if (key == ESC) break;
        if (key == 0x2a || key == 0x36) shift = 1;
        if (key == 0xaa || key == 0xb6) shift = 0;
        if (key == 0x1d) control = 1;
        if (key == 0x9d) control = 0;
        if (key & 0x80) continue;
        char ch = get_printable_char(key, 0, shift);
        if (control) {
            if (ch == 's') notice = file_replace(filename, file, size) ? "Saved." : "Save failed.";
            if (ch == 'r') { cursor = 0; line = 0; }
            continue;
        }
        if (key == L_ARROW && cursor) cursor--;
        else if (key == R_ARROW && cursor < size) cursor++;
        else if (key == U_ARROW) { if (cursor > 80) cursor -= 80; else cursor = 0; }
        else if (key == D_ARROW) { cursor = size - cursor > 80 ? cursor + 80 : size; }
        else if (key == PGUP && line) line--;
        else if (key == PGDN && text_start(file, size, line + 1) < size) line++;
        else if (key == BACKSPACE && cursor) {
            cursor--; for (uint32_t i = cursor; i < size; i++) file[i] = file[i + 1]; size--;
        } else if ((ch || key == ENTER) && size < ESFS_MAX_FILE) {
            for (uint32_t i = size; i > cursor; i--) file[i] = file[i - 1];
            file[cursor++] = key == ENTER ? '\n' : ch; size++;
        }
    }
    set_term_color(env->term_color); clear(); return 0;
}
