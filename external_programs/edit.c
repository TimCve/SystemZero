#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
#include "text_view.h"

static void render(const text_buffer_t *buffer, uint8_t color, int dirty, const char *notice) {
    screen_begin_update();
    text_render(buffer,1,color);
    uint32_t footer = ROWS - 1, columns = COLS;
    for (uint32_t col = 0; col < columns; col++) screen_put_cell(footer, col, ' ', color);
    screen_put_text(footer, 0, "Ctrl+S: save | Ctrl+R: top | Esc: quit", color);
    const char *status = notice && *notice ? notice : dirty ? "* Modified" : "Ready.";
    uint32_t length = strlen((char *)status);
    screen_put_text(footer, length < columns - 38 ? columns - length : 38, status, color);
    screen_end_update();
}
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive);
    text_set_geometry(COLS, ROWS);
    char filename[512]; char *part = splice(args, 1, ' '); memcpy(filename, part, strlen(part) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    uint8_t *file = (void *)malloc(ESFS_MAX_FILE + 512, 1, 0);
    if (!file || file_read(filename, file, (info.size + 511) / 512, 0) == 2) return 1;
    text_buffer_t buffer = {.data = file, .size = info.size, .capacity = ESFS_MAX_FILE};
    keyboard_state_t keyboard = {0}; int dirty = 0; const char *notice = "";
    screen_state_t *console = screen_save_state();
    if (!console) { print("Not enough memory to save the console."); print_newline(); return 1; }
    set_term_color(env->term_color);
    set_scrolling(0); render(&buffer, env->term_color, dirty, notice);
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard, &event);
        if(text_mouse(&buffer,env,&event)) { render(&buffer,env->term_color,dirty,notice); continue; }
        uint8_t key = event.code;
        if(event.control && key==0x2e) { text_copy(&buffer,env); continue; }
        if(event.control && key==0x2f) {
            if(text_paste(&buffer,env)) { dirty=1; notice=""; }
            else notice="Paste empty or file size limit reached.";
            text_ensure_visible(&buffer); render(&buffer,env->term_color,dirty,notice); continue;
        }
        if(key==L_ARROW || key==R_ARROW || key==U_ARROW || key==D_ARROW || key==HOME || key==END || key==PGUP || key==PGDN) text_select_clear(&buffer);
        if (key == ESC) break;
        if (event.control) {
            if (key == 0x1f) {
                if (file_replace(filename, file, buffer.size)) { dirty = 0; notice = "Saved."; }
                else notice = "Save failed.";
            } else if (key == 0x13 || key == HOME) {
                buffer.cursor = buffer.top = 0; buffer.goal_valid = 0;
            } else if (key == END) { buffer.cursor = buffer.size; buffer.goal_valid = 0; }
            else continue;
        } else if (key == U_ARROW || key == D_ARROW || key == PGUP || key == PGDN) {
            int page_rows = ROWS - 1;
            int rows = key == U_ARROW ? -1 : key == D_ARROW ? 1 : key == PGUP ? -page_rows : page_rows;
            text_move_rows(&buffer, rows);
            if (key == PGUP) buffer.top = text_previous_lines(file, buffer.size, buffer.top, page_rows);
            if (key == PGDN) for (int i = 0; i < page_rows; i++) {
                text_line_t line = text_line(file, buffer.size, buffer.top);
                if (!line.has_next) break;
                buffer.top = line.next;
            }
        } else if (key == L_ARROW || key == R_ARROW || key == HOME || key == END) {
            if (key == L_ARROW && buffer.cursor) buffer.cursor--;
            if (key == R_ARROW && buffer.cursor < buffer.size) buffer.cursor++;
            if (key == HOME) buffer.cursor = text_line_start(file, buffer.size, buffer.cursor);
            if (key == END) buffer.cursor = text_line(file, buffer.size,
                                      text_line_start(file, buffer.size, buffer.cursor)).end;
            buffer.goal_valid = 0;
        } else if (key == BACKSPACE || key == DELETE) {
            if (text_delete(&buffer, key == BACKSPACE)) { dirty = 1; notice = ""; }
        } else if (event.character || key == ENTER) {
            if (text_insert(&buffer, key == ENTER ? '\n' : event.character)) { dirty = 1; notice = ""; }
            else notice = "File size limit reached.";
        } else continue;
        text_ensure_visible(&buffer); render(&buffer, env->term_color, dirty, notice);
    }
    screen_restore_state(console); return 0;
}
