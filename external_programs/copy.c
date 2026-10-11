#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env, char *args) {
    char source[512], dest[512], source_name[512], dest_name[512];
    char *token = splice(args, 1, ' '); memcpy(source, token, strlen(token) + 1);
    token = splice(args, 2, ' '); memcpy(dest, token, strlen(token) + 1);
    if (source[0] < '0' || source[0] > '3' || source[1] != ':' ||
        dest[0] < '0' || dest[0] > '3' || dest[1] != ':' || !source[2] || !dest[2]) {
        print("Usage: copy DRIVE:SOURCE DRIVE:DESTINATION"); print_newline(); return 1;
    }
    memcpy(source_name, source + 2, strlen(source + 2) + 1);
    memcpy(dest_name, dest + 2, strlen(dest + 2) + 1);
    if (source[0] == dest[0] && !strcmp(source_name, dest_name)) return 1;
    select_drive(source[0] - '0');
    inode_t info = get_file_info(source_name);
    if (!info.valid) { print("Source file unavailable."); print_newline(); return 1; }
    uint32_t sectors = (info.size + 511) / 512;
    uint8_t *data = (void *)malloc(sectors ? sectors * 512 : 1, 1, 0);
    if (!data || file_read(source_name, data, sectors, 0) == 2) return 1;
    select_drive(dest[0] - '0');
    if (!set_superblock()) { print("Destination filesystem unavailable."); print_newline(); return 1; }
    if (get_file_info(dest_name).valid) { print("Destination already exists."); print_newline(); return 1; }
    if (file_create(dest_name, env)) return 1;
    if (file_write(dest_name, data, info.size, env) != (int)info.size) {
        file_delete(dest_name, env); print("Copy failed."); print_newline(); return 1;
    }
    print("Copied "); print_dec(info.size); print(" bytes."); print_newline(); return 0;
}
