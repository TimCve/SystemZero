#include "fs.h"
#include "../io/screen.h"
#include "ata.h"
#include "../utils/mem.h"
#include "../../libc/strings.h"

superblock_t superblock = { 0xf0f03410, 20000, 2000, 16000 };
inode_t inode;
int superblock_block = 150;

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
	for(i = 0; i < 12; i++) inode.direct_pointers[i] = 0;

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

void file_create(char* name) {
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

		while(j <= 128) {
			if(bread[j] == 0) {
				print("Free inode available in sector "); print_dec(superblock_block + i); print(", inode "); print_dec(j / 16); print_newline();

				inode.valid = 1;
				inode.size = name_size + 1;

				memcpy(bwrite, &bread, sizeof(bread));

				uint32_t free_block = 0;
				int l = 0;
				for(int k = (superblock_block) + superblock.inode_blocks; k <= superblock.blocks; k++) {
					read_sectors_ATA_PIO(bread, k, 1);
					l = 0;
					for(l = 0; l < 128; l++) {
						if(bread[l] > 0) break;
					}

					if(l >= 127) {
						free_block = k;
						break;
					} 
				}

				print("Free data block (block "); print_dec(free_block); print(") has been found!"); print_newline();

				inode.direct_pointers[0] = free_block;
				memcpy(bwrite + (j * 4), &inode, sizeof(inode));

				write_sectors_ATA_PIO(superblock_block + i, 1, bwrite);

				write_sectors_ATA_PIO(free_block, 1, name);

				print("File successfully created!"); print_newline();
				return;
			}
			j += 16;
		}
		i++;
	}

	print("No free inodes available for file creation!"); print_newline();
}

void file_list() {
	uint32_t bread[128];
	uint32_t dbread[128];
	int i;
	int j;
	int null_file_cnt = 0;

	i = 1;
	while(null_file_cnt < 100) {
		read_sectors_ATA_PIO(bread, superblock_block + i, 1);

		j = 0;
		while(j <= 128) {
			if((bread[j] == 1) && null_file_cnt < 100) {
				read_sectors_ATA_PIO(dbread, bread[j + 2], 1);
				if(dbread[0] > 0) {
					print("name: ");

					int k = 0;
					while(k < 128) {
						if((dbread[k] & 0xFF) > 0) {
							print_char(dbread[k] & 0xFF);
						} else break;

						if(((dbread[k] >> 8) & 0xFF) > 0) {
							print_char((dbread[k] >> 8) & 0xFF);
						} else break;

						if(((dbread[k] >> 16) & 0xFF) > 0) {
							print_char((dbread[k] >> 16) & 0xFF);
						} else break;

						if(((dbread[k] >> 24) & 0xFF) > 0) { 
							print_char((dbread[k] >> 24) & 0xFF);
						} else break;
						k++;
					}

					print("  ["); print_dec(bread[j + 1]); print(" bytes]"); print_newline();
				}
			} else null_file_cnt++;
			j += 16;
		}

		i++;
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
	for(i = 0; i < 12; i++) file_info.direct_pointers[i] = 0;
	for(i = 0; i < 2; i++) file_info.indirect_pointers[i] = 0;

	i = 1;
	while(null_file_cnt < 100) {
		read_sectors_ATA_PIO(bread, superblock_block + i, 1);

		j = 0;
		while(j <= 128) {
			if(bread[j] == 1) {
				null_file_cnt = 0;
				read_sectors_ATA_PIO(dbread, bread[j + 2], 1);
				memcpy(name_buffer, &dbread, sizeof(dbread));

				int set_to_zero = 0;
				for(int k = 0; k < 512; k++) {
					if(name_buffer[k] == 0) set_to_zero = 1;
					if(set_to_zero > 0) name_buffer[k] = 0;
				}

				if(strcmp(name_buffer, name) == 0) {
					file_info.valid = 1;
					file_info.size = bread[j + 1];
					for(int a = 2; a < 14; a++) file_info.direct_pointers[a - 2] = bread[j + a];
					for(int a = 14; a < 16; a++) file_info.indirect_pointers[a - 14] = bread[j + a];

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
				memcpy(name_buffer, &dbread, sizeof(dbread));

				int set_to_zero = 0;
				for(int k = 0; k < 512; k++) {
					if(name_buffer[k] == 0) set_to_zero = 1;
					if(set_to_zero > 0) name_buffer[k] = 0;
				}

				if(strcmp(name_buffer, name) == 0) {
					bread[j] = file_info.valid;
					bread[j + 1] = file_info.size;
					for(int a = 2; a < 14; a++) bread[j + a] = file_info.direct_pointers[a - 2];
					for(int a = 14; a < 16; a++) bread[j + a] = file_info.direct_pointers[a - 14];

					write_sectors_ATA_PIO(superblock_block + i, 1, bread);
				}
			} else null_file_cnt++;
			j += 16;
		}

		i++;
	}
}

int allocate_data_block() {
	int free_block;
	uint32_t bread[512];
	for(int i = (superblock_block) + superblock.inode_blocks; i <= superblock.blocks; i++) {
		read_sectors_ATA_PIO(bread, i, 1);
		int j = 0;
		for(j = 0; j < 128; j++) {
			if(bread[j] > 0) break;
		}

		if(j >= 127) {
			free_block = i;
			break;
		} 
	}

	return free_block;
}

void file_write(uint8_t* name, uint8_t* data) {
	uint32_t bread[128];
	uint8_t bread_bytes[512];
	uint8_t name_save[512];
	uint8_t bwrite[512];
	int data_i;
	int write_size = 0;
	int w_direct_ptr;

	for(data_i = 0; data_i < 512; data_i++) {
		bwrite[data_i] = 0;
		name_save[data_i] = 0;
	}

	data_i = 0;
	while(*name) {
		bwrite[data_i] = *name;
		name_save[data_i] = bwrite[data_i];
		*name++;
		data_i++;
	}
	data_i++;

	inode_t file_info = get_file_info(name_save);
	if(file_info.valid != 1) {
		print("File doesn't exist!"); print_newline();
		return;
	}

	while(1) {
		file_info = get_file_info(name_save);

		for(int i = 0; i < 512; i++) bwrite[i] = 0;

		for(w_direct_ptr = 0; w_direct_ptr < 12; w_direct_ptr++) {
			if(file_info.direct_pointers[w_direct_ptr] > 0) {
				read_sectors_ATA_PIO(bread, file_info.direct_pointers[w_direct_ptr], 1);
				if(bread[127] > 0) {
					continue;
				} else {
					memcpy(bread_bytes, &bread, sizeof(bread));
					int end_of_name = 0;
					data_i = 0;
					for(int k = 0; k < 512; k++) {
						if(bread_bytes[k] == 0 && w_direct_ptr == 0 && end_of_name == 0) {
							end_of_name = 1;
						} else if(bread_bytes[k] == 0 && w_direct_ptr > 0) {
							break;
						} else if(bread_bytes[k] == 0 && end_of_name == 1 && w_direct_ptr == 0) {
							break;
						}

						bwrite[data_i] = bread_bytes[k];
						data_i++;
					}
					break;
				}
			}
		}

		while(*data) {
			if(data_i >= 512) break;
			bwrite[data_i] = *data;
			*data++;
			data_i++;
			write_size++;
		}

		write_sectors_ATA_PIO(file_info.direct_pointers[w_direct_ptr], 1, bwrite);

		set_file_info(name_save, file_info);
		if(data_i >= 512) {
			data_i = 0;
			file_info.direct_pointers[w_direct_ptr + 1] = allocate_data_block();
			set_file_info(name_save, file_info);
			continue;
		} else {
			break;
		}
	}


	file_info.size += write_size;
	print("Wrote "); print_dec(write_size); print(" bytes to \""); print(name_save); print("\""); print_newline();
	set_file_info(name_save, file_info);
}

void file_read(uint8_t* name, char format) {
	inode_t file_info = get_file_info(name);
	int i = 0;
	uint32_t bread[128];
	uint8_t bread_bytes[512];
	int end_of_name = 0;

	if(file_info.valid == 1) {
		while(i < 12) {
			if(file_info.direct_pointers[i] != 0) {
				read_sectors_ATA_PIO(bread, file_info.direct_pointers[i], 1);
				memcpy(bread_bytes, &bread, sizeof(bread));
				int j = 0;

				while(j < 512) {
					if(bread_bytes[j] > 0 && end_of_name > 0) {
						if(format == 'b') {
							print_hex(bread_bytes[j]); print_char(' ');
						} else {
							print_char(bread_bytes[j]);
						}
					} else if(bread_bytes[j] == 0 && end_of_name == 0) {
						end_of_name = 1;
					}
					j++;
				}
			}
			i++;
		}
		print_newline();
	}
}

void file_delete(uint8_t* name) {
	inode_t file_info = get_file_info(name);
	inode_t file_info_new;
	int i = 0;
	static uint8_t bwrite[512];
	for(i = 0; i < 512; i++) bwrite[i] = 0;

	if(file_info.valid == 1) {
		file_info_new.valid = 0;
		file_info_new.size = 0;
		for(i = 0; i < 12; i++) file_info_new.direct_pointers[i] = 0;

		set_file_info(name, file_info_new);
		for(i = 0; i < 12; i++) {
			if(file_info.direct_pointers[i] > 0) write_sectors_ATA_PIO(file_info.direct_pointers[i], 1, bwrite);
		}
	}
}