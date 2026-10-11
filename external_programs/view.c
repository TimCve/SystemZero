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
    uint32_t sectors = (info.size + 511) / 512;
    uint8_t *file = (void *)malloc(sectors ? sectors * 512 : 1, 1, 0);
    if (!file || file_read(filename, file, sectors, 0) == 2) return 1;
    int initial = atoi(splice(args, 2, ' ')); uint32_t line = initial > 0 ? initial : 0;
    uint8_t previous = 0;
    for (;;) {
        text_render(file, info.size, text_start(file, info.size, line), 0xffffffffu, env->term_color);
        print("PgUp/PgDn: scroll | Esc: quit");
        uint8_t key;
        do { key = get_input_keycode(); } while (!key || key == previous);
        previous = key;
        if (key == ESC) break;
        if ((key == PGUP || key == U_ARROW) && line) line--;
        if ((key == PGDN || key == D_ARROW) && text_start(file, info.size, line + 1) < info.size) line++;
    }
    clear(); return 0;
}
