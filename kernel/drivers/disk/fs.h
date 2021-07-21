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

void format_disk();
int check_disk_fs();
void file_create(char* name);
void file_list();
inode_t get_file_info(uint8_t* name);
void set_file_info(uint8_t* name, inode_t file_info);
void file_write(uint8_t* name, uint8_t* data);
void file_read(uint8_t* name);
void file_delete(uint8_t* name);
