#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/io/screen.h"
int main(env_vars_t *env, char *args) {
    (void)args; set_term_color(env->term_color); select_drive(env->selected_drive);
    uint32_t sectors = ata_sector_count();
    if (!sectors) { print("Drive absent or IDENTIFY failed."); print_newline(); return 1; }
    print("Formatting IDE"); print_dec(env->selected_drive); print(" with ESFS..."); print_newline();
    if (!format_disk()) { print("Formatting failed."); print_newline(); return 1; }
    print("Filesystem initialized."); print_newline(); return 0;
}
