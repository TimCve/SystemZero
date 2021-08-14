#include "../kernel/drivers/disk/fs.h"
#include "../kernel/libc/strings.h"

void main(uint32_t free_mem_addr, char* input_buffer) {
	set_superblock();
	
	file_create(splice(input_buffer, 1, 0x20));
}
