#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
#include "text_view.h"
static void render(const uint8_t *file, uint32_t size, uint32_t top, uint8_t color) {
    screen_begin_update(); text_render(file, size, top, TERM_NO_CURSOR, color);
    uint32_t footer = ROWS - 1;
    for (uint32_t col = 0; col < COLS; col++) screen_put_cell(footer, col, ' ', color);
    screen_put_text(footer, 0, "PgUp/PgDn: scroll | Arrows: one line | Esc: quit", color); screen_end_update();
}
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive); set_term_color(env->term_color);
    text_set_geometry(COLS, ROWS);
    char filename[512]; char *part = splice(args, 1, ' '); memcpy(filename, part, strlen(part) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    uint32_t sectors = (info.size + 511) / 512;
    uint8_t *file = (void *)malloc(sectors ? sectors * 512 : 1, 1, 0);
    if (!file || file_read(filename, file, sectors, 0) == 2) return 1;
    int initial = atoi(splice(args, 2, ' ')); uint32_t top = text_start(file, info.size, initial > 0 ? initial : 0);
    keyboard_state_t keyboard = {0}; set_scrolling(0); render(file, info.size, top, env->term_color);
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard, &event);
        if (event.code == ESC) break;
        if (event.code == PGUP || event.code == U_ARROW)
            top = text_previous_lines(file, info.size, top, event.code == PGUP ? ROWS - 1 : 1);
        else if (event.code == PGDN || event.code == D_ARROW) {
            uint32_t count = event.code == PGDN ? ROWS - 1 : 1;
            while (count--) { text_line_t line = text_line(file, info.size, top); if (!line.has_next) break; top = line.next; }
        } else if (event.code == HOME) top = 0;
        else continue;
        render(file, info.size, top, env->term_color);
    }
    clear(); return 0;
}
