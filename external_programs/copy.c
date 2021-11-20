#include <stdint.h>
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"

int get_str_size(char* string) {
	int pos = 0;

	while(string[pos] != 0x00) pos++;
	
	return pos + 1;
}

void main(env_vars_t* env_vars_ptr , char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);

	static char f1[512];
	uint8_t f1_drive;
	memcpy(f1, splice(input_buffer, 1, 0x20), get_str_size(splice(input_buffer, 1, 0x20)));
	f1_drive = atoi(splice(f1, 0, ':'));

	static char f2[512];
	uint8_t f2_drive;
	memcpy(f2, splice(input_buffer, 2, 0x20), get_str_size(splice(input_buffer, 2, 0x20)));
	f2_drive = atoi(splice(f2, 0, ':'));

	select_drive(f2_drive);

	// create new empty file with specified name
	file_create(splice(f2, 1, ':'), env_vars_ptr);

	select_drive(f1_drive);

	uint32_t phy_addr;
	set_free_ptr(env_vars_ptr->free_mem_ptr);
	
	// allocate memory for file buffer
	uint32_t file_memory = malloc(229376, 1, &phy_addr);

	// read file contents into memory
	int file_size = get_file_info(splice(f1, 1, ':')).size;
	file_read(splice(f1, 1, ':'), file_memory, (file_size / 512) + 1, 0);

	// create pointer to start of file buffer in memory
	uint32_t* file = file_memory;

	int write_size = get_file_info(splice(f1, 1, ':')).size - get_str_size(splice(f1, 1, ':'));

	select_drive(f2_drive);
	print("Copying data (will take a while for large files)... "); print_newline();
	// write copy data into empty file
	file_write(splice(f2, 1, ':'), file, write_size, env_vars_ptr);

	// FREE & DEALLOCATE MEMORY USED FOR FILE BUFFER
	for(int i = 0; i < (file_size / 4); i++) file[i] = 0; // free
	set_free_ptr(get_free_ptr() - 233472); // deallocate
}
