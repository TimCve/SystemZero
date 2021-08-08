#include <stdint.h>
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"

void main(uint32_t free_mem_addr, char* input_buffer) {
	static uint8_t read_target[512];

	if(atoi(splice(input_buffer, 2, 0x20)) >= 0) {
		for(int i = 0; i < 512; i++) read_target[i] = 0;
		file_read(splice(input_buffer, 1, 0x20), read_target, 1, atoi(splice(input_buffer, 2, 0x20)));
		print(read_target);
		print_newline();
	} else {
		uint32_t sectors_size = get_file_info(splice(input_buffer, 1, 0x20)).size / 512;

		for(int i = 0; i <= sectors_size; i++) {
			for(int j = 0; j < 512; j++) read_target[j] = 0;
			if(file_read(splice(input_buffer, 1, 0x20), read_target, 1, i) == 2) break;
			print(read_target);
		}
	}
}

