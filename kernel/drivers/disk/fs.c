#include "fs.h"
#include "../io/screen.h"
#include "ata.h"
#include "../utils/mem.h"
#include "../../libc/strings.h"

superblock_t superblock = { 0xf0f03410, 0, 0, 0 };
inode_t inode;
int superblock_block = 150;

void init_fs(int disk_size) {
	disk_size -= (superblock_block * 512);

	superblock.blocks = disk_size / 512;
	superblock.inode_blocks = (disk_size / 512) / 10;
	superblock.inodes = ((disk_size / 512) / 10) * 8;
}

void set_superblock() {
	uint32_t superblock_contents[128];
	read_sectors_ATA_PIO(superblock_contents, superblock_block, 1);

	superblock.magic = superblock_contents[0];
	superblock.blocks = superblock_contents[1];
	superblock.inode_blocks = superblock_contents[2];
	superblock.inodes = superblock_contents[3];
}

void format_disk() {
	print("Formatting disk with ESFS..."); print_newline();
	uint8_t bwrite[512];
	int i;

	for(i = 0; i < 512; i++) bwrite[i] = 0;

	print("Writing superblock..."); print_newline();

	memcpy(bwrite, &superblock, sizeof(superblock));

	write_sectors_ATA_PIO(superblock_block, 1, bwrite);
	print("Superblock written!"); print_newline();

	print("Writing inodes..."); print_newline();

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
		write_sectors_ATA_PIO((superblock_block + i), 1, bwrite);
		i++;
	}

	print_dec(superblock.inodes); print(" inodes written to "); print_dec(superblock.inode_blocks); print(" blocks!"); print_newline();
	print_dec((superblock.blocks - superblock.inode_blocks) * 512); print(" bytes avaliable for file creation!"); print_newline(); 
}

int check_disk_fs() {
	print("Scanning disk for ESFS filesystem..."); print_newline();
	uint32_t bread[128];
	int i;

	for(i = 0; i < 128; i++) bread[i] = 0;

	read_sectors_ATA_PIO(bread, superblock_block, 1);
	if(bread[0] == superblock.magic) {
		print("ESFS superblock found!"); print_newline();
		return 1;
	} else {
		print("Disk not formatted with ESFS!"); print_newline();
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
		read_sectors_ATA_PIO(bread, superblock_block + i, 1);

		j = 0;
		while(j <= 128) {
			if(bread[j] == 1) {
				null_file_cnt = 0;
				read_sectors_ATA_PIO(dbread, bread[j + 2], 1);
				read_sectors_ATA_PIO(dbread, dbread[0], 1);
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
		read_sectors_ATA_PIO(bread, superblock_block + i, 1);

		j = 0;
		while(j <= 128) {
			if(bread[j] == 1) {
				null_file_cnt = 0;
				read_sectors_ATA_PIO(dbread, bread[j + 2], 1);
				read_sectors_ATA_PIO(dbread, dbread[0], 1);
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

					write_sectors_ATA_PIO(superblock_block + i, 1, bread);
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
		read_sectors_ATA_PIO(data_block_read, block_i, 1);
		
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
				read_sectors_ATA_PIO(inode_block_read, superblock_block + block_i_2, 1);

				int inode_i = 0;

				while(inode_i <= 128) {
					if(inode_block_read[inode_i] == 1) {
						null_files = 0;

						int indirect_ptr_i = 2;

						while(indirect_ptr_i < 16) {
							read_sectors_ATA_PIO(data_block_read, inode_block_read[inode_i + indirect_ptr_i], 1);
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


// create a file
void file_create(char* name) {
	inode_t file_info = get_file_info(name);
	if(file_info.valid == 1) {
		print("File already exists!"); print_newline();
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

		read_sectors_ATA_PIO(bread, superblock_block + i, 1);

		while(j < 128) {
			if(bread[j] == 0) {
				inode.valid = 1;
				inode.size = name_size + 1;

				memcpy(bwrite, &bread, sizeof(bread));

				uint32_t free_block = allocate_data_block();

				inode.indirect_pointers[0] = free_block;
				memcpy(bwrite + (j * 4), &inode, sizeof(inode));

				write_sectors_ATA_PIO(superblock_block + i, 1, bwrite);

				write_sectors_ATA_PIO(free_block, 1, bwrite);
				for(int v = 0; v < 512; v++) bwrite[v] = 0;

				uint32_t free_block_2 = allocate_data_block();
				write_sectors_ATA_PIO(free_block, 1, bwrite);

				memcpy(bwrite, &free_block_2, sizeof(free_block_2));

				write_sectors_ATA_PIO(free_block, 1, bwrite);
				write_sectors_ATA_PIO(free_block_2, 1, name);

				print("File successfully created!"); print_newline();
				return;
			}
			j += 16;
		}
		i++;
	}

	print("No free inodes available for file creation!"); print_newline();
}

// print the names of all existing files to stdout
void file_list() {
	uint32_t inode_block_read[128];
	uint32_t data_block_read[128];
	uint8_t data_block_read_bytes[512];
	int null_files = 0;

	int	block_i = 1;
	while(null_files <= 100) {
		read_sectors_ATA_PIO(inode_block_read, superblock_block + block_i, 1);

		int inode_i = 0;
		while(inode_i <= 128) {
			if(inode_block_read[inode_i] == 1) {
				null_files = 0;

				int file_size = inode_block_read[inode_i + 1];
				read_sectors_ATA_PIO(data_block_read, inode_block_read[inode_i + 2], 1);
				read_sectors_ATA_PIO(data_block_read, data_block_read[0], 1);
	
				memcpy(data_block_read_bytes, &data_block_read, sizeof(data_block_read));

				char chr = 0;
				int chr_i = 0;

				char name[512];

				do {
					chr = data_block_read_bytes[chr_i];
					name[chr_i] = chr;
					chr_i++;
				} while(chr);

				file_read(name, data_block_read_bytes, 1, 0);

				if(data_block_read_bytes[0] == 0x7F && data_block_read_bytes[1] == 0x45 && data_block_read_bytes[2] == 0x4C && data_block_read_bytes[3] == 0x46) {
					print("[x] ");
				} else {
					print("[-] ");
				}

				print(" ["); print_dec(file_size); print(" bytes]  ");

				print(name);

				print_newline();
				inode_i += 16;
			} else {
				null_files++;
				inode_i += 16;
				continue;
			}
		}
		block_i++;
	}	
}

// write an arbitrary amount of data to an existing file
void file_write(uint8_t* name, uint8_t* data) {
	inode_t file_info = get_file_info(name);
	uint32_t ptr_block_read[128];
	uint32_t data_block_read[128];
	uint8_t data_block_read_bytes[512];
	int write_size = 0;

	if(file_info.valid != 1) {
		print("File doesn't exist!"); print_newline();
		return;
	}

	// iterate through all the pointers in the inode
	for(int ptr_i = 0; ptr_i < 14; ptr_i++)	{
		// if pointer exists, read pointer block into memory and iterate through it
		if(file_info.indirect_pointers[ptr_i]) {
			read_sectors_ATA_PIO(ptr_block_read, file_info.indirect_pointers[ptr_i], 1);
			for(int ptr_block_i = 0; ptr_block_i < 128; ptr_block_i++) {
				// if a pointer in the pointer block exists, read it into memory and check if it is full
				// --> if full, continue onto next pointer
				// --> if not full, write data to it until either data ends (break) or data block fills up (continue onto next pointer)
				if(ptr_block_read[ptr_block_i]) {
					read_sectors_ATA_PIO(data_block_read, ptr_block_read[ptr_block_i], 1);
					memcpy(data_block_read_bytes, &data_block_read, sizeof(data_block_read));
					if(data_block_read_bytes[511]) { // data block is full
						if(ptr_block_read[ptr_block_i + 1]) continue;
						else { // if no more data blocks exist, create one
							ptr_block_read[ptr_block_i + 1] = allocate_data_block();
							write_sectors_ATA_PIO(file_info.indirect_pointers[ptr_i], 1, ptr_block_read);
							continue;
						} 
					} else { // data block is not full
						int name_end; // first block in first pointer block also contains the file name, we need to check that in order
									  // to not overwrite the file name
						if(ptr_i == 0 && ptr_block_i == 0) name_end = 0;
						if(ptr_i != 0 || ptr_block_i != 0) name_end = 1;

						// iterate through the bytes of the data block
						for(int data_block_i = 0; data_block_i < 512; data_block_i++) {
							// if the name has already been passed, write data to any null byte
							if(data_block_read_bytes[data_block_i] == 0 && name_end == 1) {
								if(*data) {
									data_block_read_bytes[data_block_i] = *data;
									*data++;
									write_size++;
								}
							}
							// if name has not been passed and there is a null byte, ignore it and set name_end = 1
							// that way the next null byte will have data written to it
							if(data_block_read_bytes[data_block_i] == 0 && name_end == 0) name_end = 1;
						}
						// write the data that we stored in memory back to the disk
						write_sectors_ATA_PIO(ptr_block_read[ptr_block_i], 1, data_block_read_bytes);

						// if this is the last pointer in the pointer block and it is full, create a new pointer block
						if(data_block_read_bytes[511] && ptr_block_i == 127) {
							file_info.indirect_pointers[ptr_i] = allocate_data_block();
							// write those changes to the inode to the disk
							set_file_info(name, file_info);
						}
						
						// if there is no more data to be written, break from the loop
						if(!*data) break;	
					}
				} else {
					ptr_block_read[ptr_block_i] = allocate_data_block();
					write_sectors_ATA_PIO(file_info.indirect_pointers[ptr_i], 1, ptr_block_read);
					ptr_block_i--;
					continue;
				}
			}
		}
		// if there is no more data to be written, break from the loop
		if(!*data) break;
	}
	
	// update the file size in the inode
	file_info.size += write_size;
	set_file_info(name, file_info);
	print("Wrote "); print_dec(write_size); print(" bytes to "); print(name); print_newline();
}

// reads a specified amount of a file's contents, starting at a specified offset to a specified location in memory
// (read_size & read_offset are in 512 byte sectors)
int file_read(uint8_t* name, uint32_t* target_address, uint32_t read_size, uint32_t read_offset) {
	inode_t file_info = get_file_info(name);

	if(file_info.valid != 1)  {
		print("File doesn't exist!"); print_newline();
		return 2;
	}

	uint32_t ptr_block_read[128];
	uint32_t data_block_read[128];
	uint8_t data_block_read_bytes[512];

	int ret_value;
	if(file_info.size <= ((read_size * 512) + (read_offset * 512))) ret_value = 0;
	else if(file_info.size > ((read_size * 512) + (read_offset * 512))) ret_value = 1;

	// set up a pointer to a byte array in memory where the data will be read to
	uint8_t* target = (uint8_t*) target_address;
	int read_start = read_offset * 512;
	int read_i = 0;
	int target_i = 0;

	// the filename is stored in the file contents separated from the actual contents by a null byte,
	// this variable gets set to 1 when the first null byte is encountered	
	int name_passed = 0;
	if(read_offset > 0) name_passed = 1;

	// check if the file actually exists
	if(file_info.indirect_pointers[0]) {
		// iterate through all indirect pointers in the inode	
		for(int ptr_i = 0; ptr_i < 14; ptr_i++) {
			if(file_info.indirect_pointers[ptr_i]) {
				// read contents of pointer block into memory
				read_sectors_ATA_PIO(ptr_block_read, file_info.indirect_pointers[ptr_i], 1);
				// iterate through all the pointers in the read pointer block
				for(int ptr_block_i = 0; ptr_block_i < 128; ptr_block_i++) {
					// if a block exists, read it into memory and iterate through it
					if(ptr_block_read[ptr_block_i]) {
						read_sectors_ATA_PIO(data_block_read, ptr_block_read[ptr_block_i], 1);
						memcpy(data_block_read_bytes, &data_block_read, sizeof(data_block_read));
						for(int data_block_i = 0; data_block_i < 512; data_block_i++) {
							// write file bytes to the target addess in memory
							if(name_passed == 1) {
								if(read_i > read_start) {
									target[target_i] = data_block_read_bytes[data_block_i];
									target_i++;
								}

								if(read_i >= (read_offset * 512) + (read_size * 512)) return ret_value;
							}
							// enables reading after the filename has been passed (so only the actual contents are read)
							if(!data_block_read_bytes[data_block_i] && name_passed == 0) name_passed = 1;
							read_i++;
						}
					}	
				}			
			}
		}
		return ret_value;
	} else {
		print("File doesn't exist!"); print_newline();	
		return 2;
	}
}

void file_delete(uint8_t* name) {
	inode_t file_info = get_file_info(name);
	static inode_t updated_file_info;
	uint32_t ptr_block_read[128];

	// set up a static 512 array of null bytes
	static uint8_t null_block[512];
	for(int i = 0; i < 512; i++) null_block[i] = 0;

	// check if the file actually exists
	if(file_info.valid == 1) {
		// delete records of the file in the inode blocks
		updated_file_info.valid = 0;
		updated_file_info.size = 0;
		for(int i = 0; i < 14; i++) updated_file_info.indirect_pointers[i] = 0;
		
		set_file_info(name, updated_file_info);

		// iterate through all the pointers
		for(int ptr_i = 0; ptr_i < 14; ptr_i++) {
			// read the data pointed to by vaid pointers into memory 
			if(file_info.indirect_pointers[ptr_i]) {
				read_sectors_ATA_PIO(ptr_block_read, file_info.indirect_pointers[ptr_i], 1);
				
				// delete all data from those data blocks
				for(int ptr_block_i = 0; ptr_block_i < 128; ptr_block_i++) 
					if(ptr_block_read[ptr_block_i]) write_sectors_ATA_PIO(ptr_block_read[ptr_block_i], 1, null_block);
			}
		}
		print("File successfully deleted!"); print_newline();
	} else {
		print("File doesn't exist!"); print_newline();
	}
}
