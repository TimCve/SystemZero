#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env, char *args) {
    select_drive(env->selected_drive); set_term_color(env->term_color);
    int hex = !strcmp(splice(args, 1, ' '), "-x"), index = hex ? 2 : 1;
    char filename[512]; char *token = splice(args, index, ' ');
    memcpy(filename, token, strlen(token) + 1);
    inode_t info = get_file_info(filename);
    if (!info.valid) { print("File not found."); print_newline(); return 1; }
    token = splice(args, index + 1, ' ');
    uint32_t first = *token ? atoi(token) : 0;
    uint32_t end = *token ? first + 1 : (info.size + 511) / 512;
    if (first >= (info.size + 511) / 512) return info.size ? 1 : 0;
    uint8_t data[512];
    for (uint32_t sector = first; sector < end; sector++) {
        if (file_read(filename, data, 1, sector) == 2) return 1;
        uint32_t n = info.size - sector * 512; if (n > 512) n = 512;
        for (uint32_t i = 0; i < n; i++) {
            if (hex) { if (data[i] < 16) print_char('0'); print_hex(data[i]); print_char(' '); }
            else print_char(data[i]);
        }
    }
    print_newline(); return 0;
}
