#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

static char disk_name[512];
static const int block_size = 512;
int superblock_block = 200;
superblock_t superblock = { 0xf0f03410, 0, 0, 0 };
inode_t inode;

void read_sector(uint32_t* target_address, uint32_t LBA);
void write_sector(uint32_t LBA, uint32_t* bytes);

void init_fs(int disk_size) {
	disk_size -= (superblock_block * 512);
	disk_size = 104770560;	

	superblock.blocks = disk_size / 512;
	superblock.inode_blocks = (disk_size / 512) / 10;
	superblock.inodes = ((disk_size / 512) / 10) * 8;
}

void format_disk() {
	printf("Formatting disk with ESFS...\n");
	uint8_t bwrite[512];
	int i;

	for(i = 0; i < 512; i++) bwrite[i] = 0;

	printf("Writing superblock...\n");

	memcpy(bwrite, &superblock, sizeof(superblock));

	write_sector(superblock_block, bwrite);
	printf("Superblock written!\n");

	printf("Writing inodes...\n");

	for(i = 0; i < 512; i++) bwrite[i] = 0;
	inode.valid = 0;
	inode.size = 0;
	for(i = 0; i < 14; i++) inode.indirect_pointers[i] = 0;

	i = 0;
	while(i < 512) {
		memcpy(bwrite + i, &inode, sizeof(inode));
		i += 64;
	} 

	i = 1;
	while(i <= superblock.inode_blocks) {
		write_sector((superblock_block + i), bwrite);
		i++;
	}

	printf("%d inodes written to %d blocks!\n", superblock.inodes, superblock.inode_blocks);
}

int check_disk_fs() {
	printf("Scanning disk for ESFS filesystem...\n");
	uint32_t bread[128];
	int i;

	for(i = 0; i < 128; i++) bread[i] = 0;

	read_sector(bread, superblock_block);
	if(bread[0] == superblock.magic) {
		printf("ESFS superblock found!\n");
		return 1;
	} else {
		printf("Disk not formatted with ESFS!\n");
		return 0;
	}
}

inode_t get_file_info(uint8_t* name) {
	uint32_t bread[128];
	uint32_t dbread[128];
	uint8_t name_buffer[512];
	int i;
	int j;
	int null_file_cnt = 0;

	inode_t file_info;

	for(i = 0; i < 128; i++) {
		bread[i] = 0;
		dbread[i] = 0;
	}

	for(i = 0; i < 512; i++) name_buffer[i] = 0;

	file_info.valid = 0;
	file_info.size = 0;
	for(i = 0; i < 14; i++) file_info.indirect_pointers[i] = 0;

	i = 1;
	while(null_file_cnt < 100) {
		read_sector(bread, superblock_block + i);

		j = 0;
		while(j <= 128) {
			if(bread[j] == 1) {
				null_file_cnt = 0;
				read_sector(dbread, bread[j + 2]);
				read_sector(dbread, dbread[0]);
				memcpy(name_buffer, &dbread, sizeof(dbread));

				int set_to_zero = 0;
				for(int k = 0; k < 512; k++) {
					if(name_buffer[k] == 0) set_to_zero = 1;
					if(set_to_zero > 0) name_buffer[k] = 0;
				}

				if(strcmp(name_buffer, name) == 0) {
					file_info.valid = 1;
					file_info.size = bread[j + 1];
					for(int a = 2; a < 16; a++) file_info.indirect_pointers[a - 2] = bread[j + a];

					return file_info;
				}
			} else null_file_cnt++;
			j += 16;
		}

		i++;
	}
}

void set_file_info(uint8_t* name, inode_t file_info) {
	uint32_t bread[128];
	uint32_t dbread[128];
	uint8_t name_buffer[512];
	int i;
	int j;
	int null_file_cnt = 0;

	i = 1;
	while(null_file_cnt < 100) {
		read_sector(bread, superblock_block + i);

		j = 0;
		while(j <= 128) {
			if(bread[j] == 1) {
				null_file_cnt = 0;
				read_sector(dbread, bread[j + 2]);
				read_sector(dbread, dbread[0]);
				memcpy(name_buffer, &dbread, sizeof(dbread));
	
				int set_to_zero = 0;
				for(int k = 0; k < 512; k++) {
					if(name_buffer[k] == 0) set_to_zero = 1;
					if(set_to_zero > 0) name_buffer[k] = 0;
				}

				if(strcmp(name_buffer, name) == 0) {
					bread[j] = file_info.valid;
					bread[j + 1] = file_info.size;
					for(int a = 2; a < 16; a++) bread[j + a] = file_info.indirect_pointers[a - 2];

					write_sector(superblock_block + i, bread);
				}
			} else null_file_cnt++;
			j += 16;
		}

		i++;
	}
}

int allocate_data_block() {
	int free_block = 0;
	uint32_t data_block_read[128];

	// iterate through all possible data blocks
	for(int block_i = (superblock_block) + superblock.inode_blocks; block_i <= superblock.blocks; block_i++) {
		// read the contents of every block into memory
		read_sector(data_block_read, block_i);
		
		// iterate through the contents and see if there is any data stored in the block
		int data_i;
		for(data_i = 0; data_i < 128; data_i++) if(data_block_read[data_i]) break;

		// if there are no contents in the block, data_i would have reached the end
		// now check if this block is used in storing data for files
		if(data_i >= 127) {
			free_block = block_i;

			uint32_t inode_block_read[128];

			int block_i_2 = 1;
			int null_files = 0;

			while(null_files <= 100) {
				read_sector(inode_block_read, superblock_block + block_i_2);

				int inode_i = 0;

				while(inode_i <= 128) {
					if(inode_block_read[inode_i] == 1) {
						null_files = 0;

						int indirect_ptr_i = 2;

						while(indirect_ptr_i < 16) {
							read_sector(data_block_read, inode_block_read[inode_i + indirect_ptr_i]);
							for(int i = 0; i < 128; i++) {
								if(data_block_read[i] == free_block) {
									free_block = 0;
									break;
								}
							}

							if(free_block == 0) break;
							indirect_ptr_i++;
						}

						inode_i += 16;
					} else {
						null_files++;
						inode_i += 16;
						continue;
					}

					if(free_block == 0) break;
				}

				if(free_block == 0) break;

				block_i_2++;
			}
		}

		if(free_block != 0) return free_block;
	}
}

void file_create(char* name) {
	inode_t file_info = get_file_info(name);
	if(file_info.valid == 1) {
		printf("File already exists!\n");
		return;
	}

	uint32_t bread[128];
	uint32_t dread[128];
	uint8_t bwrite[512];
	int i;
	int j;

	int name_size = 0;
	i = 0;
	while(name[i] > 0) {
		name_size++;
		i++;
	}

	for(i = 0; i < 128; i++) bread[i] = 0;

	i = 1;
	while(i <= superblock.inode_blocks) {
		j = 0;

		read_sector(bread, superblock_block + i);

		while(j < 128) {
			if(bread[j] == 0) {
				inode.valid = 1;
				inode.size = name_size + 1;

				memcpy(bwrite, &bread, sizeof(bread));

				uint32_t free_block = allocate_data_block();

				inode.indirect_pointers[0] = free_block;
				memcpy(bwrite + (j * 4), &inode, sizeof(inode));

				write_sector(superblock_block + i, bwrite);

				write_sector(free_block, bwrite);
				for(int v = 0; v < 512; v++) bwrite[v] = 0;

				uint32_t free_block_2 = allocate_data_block();
				write_sector(free_block, bwrite);

				memcpy(bwrite, &free_block_2, sizeof(free_block_2));

				write_sector(free_block, bwrite);
				write_sector(free_block_2, name);

				printf("File successfully created!\n");
				return;
			}
			j += 16;
		}
		i++;
	}

	printf("No free inodes available for file creation!\n");
}

// write an arbitrary amount of data to an existing file
void file_write(uint8_t* name, uint8_t* data, int write_size) {
	inode_t file_info = get_file_info(name);
	uint32_t ptr_block_read[128];
	uint32_t data_block_read[128];
	uint8_t data_block_read_bytes[512];
	int block_write_size = 0;
	int total_write_size = 0;

	if(file_info.valid != 1) {
		printf("File doesn't exist!\n");
		return;
	}

	// iterate through all the pointers in the inode
	for(int ptr_i = 0; ptr_i < 14; ptr_i++)	{
		// if pointer exists, read pointer block into memory and iterate through it
		if(file_info.indirect_pointers[ptr_i]) {
			read_sector(ptr_block_read, file_info.indirect_pointers[ptr_i]);

			for(int ptr_block_i = 0; ptr_block_i < 128; ptr_block_i++) {				
				// if a pointer in the pointer block exists, read it into memory and check if it is full
				// --> if full, continue onto next pointer
				// --> if not full, write data to it until either data ends (break) or data block fills up (continue onto next pointer)
				if(ptr_block_read[ptr_block_i]) {
					printf("pointer %d -> block %d\n", (ptr_i * 128) + ptr_block_i, ptr_block_read[ptr_block_i]);

					read_sector(data_block_read, ptr_block_read[ptr_block_i]);

					memcpy(data_block_read_bytes, &data_block_read, sizeof(data_block_read));

					if(data_block_read_bytes[511]) { // data block is full
						if(ptr_block_read[ptr_block_i + 1]) continue;
						else { // if no more data blocks exist, create one
							ptr_block_read[ptr_block_i + 1] = allocate_data_block();
							write_sector(file_info.indirect_pointers[ptr_i], ptr_block_read);
							continue;
						} 
					} else { // data block is not full
						int name_end; // first block in first pointer block also contains the file name, we need to check that in order
									  // to not overwrite the file name
						if(ptr_i == 0 && ptr_block_i == 0) name_end = 0;
						if(ptr_i != 0 || ptr_block_i != 0) name_end = 1;

						block_write_size = 0;

						// iterate through the bytes of the data block
						for(int data_block_i = 0; data_block_i < 512; data_block_i++) {
							// if the name has already been passed, write data to any null byte
							if(data_block_read_bytes[data_block_i] == 0 && name_end == 1) {
								if(write_size > 0) {
									data_block_read_bytes[data_block_i] = *data;
									*data++;
									block_write_size++;
									total_write_size++;
									write_size--;
								} else break;
							}
							// if name has not been passed and there is a null byte, ignore it and set name_end = 1
							// that way the next null byte will have data written to it
							if(data_block_read_bytes[data_block_i] == 0 && name_end == 0) name_end = 1;
						}

						// update the file size in the inode
						file_info.size += block_write_size;
						set_file_info(name, file_info);

						// write the data that we stored in memory back to the disk
						write_sector(ptr_block_read[ptr_block_i], data_block_read_bytes);

						// if this is the last pointer in the pointer block and it is full, create a new pointer block
						if(data_block_read_bytes[511] && ptr_block_i == 127) {
							file_info.indirect_pointers[ptr_i] = allocate_data_block();
							// write those changes to the inode to the disk
							set_file_info(name, file_info);
						}
						
						// if there is no more data to be written, break from the loop
						if(write_size <= 0) break;	
					}
				} else {
					ptr_block_read[ptr_block_i] = allocate_data_block();
					write_sector(file_info.indirect_pointers[ptr_i], ptr_block_read);
					ptr_block_i--;
				}
			}
		}
		// if there is no more data to be written, break from the loop
		if(write_size <= 0) break;
	}
	
	file_info.size = total_write_size;
	set_file_info(name, file_info);
	printf("Wrote %d bytes to %s\n", total_write_size, name);
}

void read_sector(uint32_t* target_address, uint32_t LBA) {
	FILE *disk = fopen(disk_name, "rb");
	uint8_t *target = (uint8_t*) target_address;

	if(disk) {
		fseek(disk, (LBA * 512), SEEK_SET);
		fread(target, 4, block_size / 4, disk);
	}

	fclose(disk);
}

void write_sector(uint32_t LBA, uint32_t* bytes) {
	FILE *disk = fopen(disk_name, "r+b");

	if(disk) {
		fseek(disk, (LBA * 512), SEEK_SET);
		fwrite(bytes, 4, block_size / 4, disk);
	}

	fclose(disk);
}

int main(int argc, char **argv) {
	if(argc < 4) {
		printf("Incorrect syntax detected!\n");
		printf("./ESFS_raw_write <disk.bin> <filename_in_ESFS> <filename_on_local_machine>\n");
		return 1;
	}

	static char ESFS_file[512];
	static char local_file[512];

	for(int i = 0; i < 512; i++) {
		disk_name[i] = 0;
		ESFS_file[i] = 0;
		local_file[i] = 0;
	}

	strncpy(disk_name, argv[1], strlen(argv[1]));
	strncpy(ESFS_file, argv[2], strlen(argv[2]));
	strncpy(local_file, argv[3], strlen(argv[3]));

	FILE *file;
	file = fopen(local_file, "rb");

	static uint8_t buffer[1048576];

	fseek(file, 0L, SEEK_END);
	int file_size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	fread(buffer, file_size, 1, file);
	fclose(file);

	FILE *disk;
	disk = fopen(disk_name, "rb");
	fseek(disk, 0L, SEEK_END);
	int disk_size = ftell(disk);
	fseek(disk, 0L, SEEK_SET);
	fclose(disk);

	init_fs(disk_size);

	if(check_disk_fs() != 1) format_disk();
	if(get_file_info(ESFS_file).valid != 1) file_create(ESFS_file);

	file_write(ESFS_file, buffer, file_size);
	return 0;
}
