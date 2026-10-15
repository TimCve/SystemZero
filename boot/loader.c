#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/vga_colors.h"
#include "../shared/elf.h"
static env_vars_t env;
static void fail(char *message) {
    print("[LOADER] "); print(message); print_newline();
    for (;;) __asm__ volatile("cli; hlt");
}
void loadkernel(uint32_t bios_drive,uint32_t volume_first) {
    set_term_color(STD_COLOR); clear();
    if (bios_drive < 0x80 || bios_drive > 0x83) fail("Unsupported boot drive.");
    env = (env_vars_t){.system_drive = bios_drive - 0x80,
                       .selected_drive = bios_drive - 0x80, .term_color = STD_COLOR};
    select_drive(env.system_drive);
    uint32_t manifest[128];
    if (!read_sectors_ATA_PIO(manifest, volume_first+63, 1) || manifest[0] != 0x535a4b31 ||
        manifest[1] != 64 || !manifest[2] || manifest[2] > (200 - 64) * 512)
        fail("Invalid kernel manifest or disk read failed.");
    uint32_t bytes = manifest[2], sectors = (bytes + 511) / 512;
    uint32_t source = malloc(sectors * 512, 1, 0);
    if (!source || !read_sectors_ATA_PIO((void *)source, volume_first+manifest[1], sectors)) fail("Cannot read kernel.");
    elf_plan_t plan;
    if (!elf_plan((void *)source, bytes, &plan)) fail("Invalid kernel ELF.");
    uint32_t memory = malloc(plan.size, 1, 0);
    if (!memory || !elf_load((void *)source, bytes, &plan, (void *)memory, plan.size)) fail("Cannot load kernel ELF.");
    env.free_mem_ptr = get_free_ptr();
    print("[LOADER] Kernel segments loaded."); print_newline();
    ((void (*)(env_vars_t *))(memory + plan.entry))(&env);
    fail("Kernel returned.");
}
