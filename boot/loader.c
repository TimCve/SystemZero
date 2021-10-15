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
	env_vars.term_color = STD_COLOR;

	set_term_color(env_vars.term_color);

	clear();
	
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
	
	// determine which drive is the boot drive
	for(int drive_num = 0; drive_num < 4; drive_num++) {
		uint16_t drive_read[256];
		select_drive(drive_num);
		read_sectors_ATA_PIO(drive_read, 0, 1);
		if(drive_read[255] == 0xAA55) {
			print("[LOADER] Loading kernel from drive "); print_dec(drive_num); print_newline();
			env_vars_ptr->selected_drive = drive_num;
			break;
		} else if(drive_read[255] != 0xAA55 && drive_num == 3) {
			set_term_color(((env_vars_ptr->term_color >> 4) * 16) + 0x04);
			print("[LOADER] ERROR! Kernel not found on any existing drive!"); print_newline();
			while(1) continue;
		}
	}

    read_sectors_ATA_PIO(page, 18, 60);

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
