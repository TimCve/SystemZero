#include "../kernel/env_vars.h"
#include "../kernel/drivers/utils/ports.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/vga_colors.h"

#include <stdint.h>

void loadkernel() {
	env_vars_t env_vars;

	env_vars.selected_drive = 0;
	env_vars.free_mem_ptr = get_free_ptr();
	env_vars.master_exists = 0;
	env_vars.slave_exists = 0;
	env_vars.term_color = STD_COLOR;

	set_term_color(env_vars.term_color);

	clear();

	// check for existence of master & slave drives

	print("[LOADER] ");
	if(identify_drive(0xA0)) {
		print("IDE0 found!");
		env_vars.master_exists = 1;
	} else print("IDE0 not found!");
	print_newline();

	print("[LOADER] ");
	if(identify_drive(0xB0)) {
		print("IDE1 found!");
		env_vars.slave_exists = 1;
	} else print("IDE1 not found!");
	print_newline();

	if(!env_vars.master_exists && env_vars.slave_exists) env_vars.selected_drive = 1;
	else env_vars.selected_drive = 0;

	select_drive(env_vars.selected_drive);

	uint32_t phy_addr;
	env_vars_t* env_vars_ptr = (env_vars_t*) malloc(sizeof(env_vars), 1, &phy_addr);
	memcpy(env_vars_ptr, &env_vars, sizeof(env_vars));

	print("[LOADER] Memory address of kernel loader: 0x"); print_hex((int)loadkernel); print_newline();
	
	print_newline();

    uint32_t page = malloc(40000, 1, &phy_addr); // allocate memory for kernel
    print("[LOADER] Page: 0x"); print_hex(page); print_newline();
    print("[LOADER] Physical address: 0x"); print_hex(phy_addr); print_newline();

    print("[LOADER] Loading kernel into memory at 0x"); print_hex(page); print_newline();

    read_sectors_ATA_PIO(page, 14, 50);

    uint32_t* kernelFile = page;

    if(kernelFile[0] == 0x464C457F) // read first 4 bytes of kernel to check if it is an ELF file
    {
        print("[LOADER] Kernel is ELF file"); print_newline();
    }
    int bits = (kernelFile[1] >> 0) & 0xFF; // 5th byte determines if kernel is 32 or 64 bit
    if(bits == 1)
    {
        print("[LOADER] Kernel is 32-bit"); print_newline();
    }
    int endian = (kernelFile[1] >> 8) & 0xFF; // 6th byte determines if kernel is little endian or big endian
    if(endian == 1)
    {
        print("[LOADER] Kernel is little endian"); print_newline();
    }

    print("[LOADER] Kernel entry position: 0x"); print_hex(page + kernelFile[6]); print_newline();
    print("[LOADER] Executing kernel from 0x"); print_hex(page + kernelFile[6]); print_newline();

    print_newline();

    void (*func)(uint32_t free_mem_addr) = page + kernelFile[6]; // create pointer to kmain

	env_vars_ptr->free_mem_ptr = get_free_ptr();	

    func(env_vars_ptr); // execute kmain

}
