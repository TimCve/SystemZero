#include <stdint.h>

// superblock structure (16 bytes)
typedef struct {
	uint32_t magic;
	uint32_t blocks;
	uint32_t inode_blocks;
	uint32_t inodes;
} superblock_t;

// inode structure (64 bytes)
typedef struct {
	uint32_t valid;
	uint32_t size;
	uint32_t indirect_pointers[14];
} inode_t;

void init_fs(int disk_size);
void set_superblock();
void format_disk();
int check_disk_fs();
int file_create(char* name, env_vars_t* env_vars_ptr); // returns 1 if file exists, 0 if successfully created
void file_list();
inode_t get_file_info(uint8_t* name);
void set_file_info(uint8_t* name, inode_t file_info);
int file_write(uint8_t* name, uint8_t* data, int write_size, env_vars_t* env_vars_ptr); // returns total write size (0 if failed)
int file_read(uint8_t* name, uint32_t* target_address, uint32_t read_size, uint32_t read_offset);
int file_delete(uint8_t* name, env_vars_t* env_vars_ptr); // returns 0 if success, 1 if failure
