#include "../kernel/drivers/utils/ports.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"

#include <stdint.h>

void loadkernel() {
	clear();

	print("[LOADER] Memory address of kernel loader: 0x"); print_hex((int)loadkernel); print_newline();
	
	print_newline();

	uint32_t phy_addr;
    uint32_t page = malloc(1000, 1, &phy_addr); // allocate memory for kernel
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

    void (*func)(void) = page + kernelFile[6]; // create pointer to kmain
    func(); // execute kmain

}
