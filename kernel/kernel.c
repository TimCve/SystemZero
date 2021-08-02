#include "kernel.h"
#include "drivers/utils/ports.h"
#include "drivers/io/screen.h"
#include "drivers/io/keyboard.h"
#include "cpu/isr.h"
#include "cpu/timer.h"
#include "drivers/disk/ata.h"
#include "libc/strings.h"
#include "drivers/disk/fs.h"
#include "drivers/utils/mem.h"

void sleep(int time) {
	int init_tick = get_tick();
	while(get_tick() < init_tick + time) {
		continue;
	}
}

int atoi(char* txt)
{   
    int sum, digit, i;
    sum = 0;
    i = 0;
    char cur_char;
    cur_char = 0;

    do {
    	cur_char = txt[i + 1];
        digit = txt[i] - 0x30;
        sum = (sum * 10) + digit;
        i++;
    } while(cur_char != 0);

    return sum;
}

void disk_read(uint32_t LBA, uint8_t sector_count) {
	uint32_t disk_read_buffer[128];

	uint8_t current_block = 0;
	int i;

	while(current_block < sector_count) {
		print("BLOCK: ");
		print_dec(LBA);
		print_newline();

		read_sectors_ATA_PIO(disk_read_buffer, LBA, 1);

		i = 0;
		while(i < 128) {
			print_hex(disk_read_buffer[i] & 0xFF); print_char(' ');
			print_hex((disk_read_buffer[i] >> 8) & 0xFF); print_char(' ');
			print_hex((disk_read_buffer[i] >> 16) & 0xFF); print_char(' ');
			print_hex((disk_read_buffer[i] >> 24) & 0xFF); print_char(' ');
			i++;
		}

		LBA++;
		current_block++;
		print_newline();
		// sleep(1000);
	}
	print_newline();
}

void kmain()
{
	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();


	print("Installing interrupt service routines..."); print_newline();
	isr_install();
	__asm__("sti");

	if(check_disk_fs() == 0) {
		format_disk();
	}

	init_timer(1000);

	print_newline();

	uint32_t phy_addr;
	malloc(50000, 1, &phy_addr);

	// how to allocate memory for a program
	uint32_t* mem = malloc(1000, 1, &phy_addr);

	char last_kbd_buffer[2000];
	char kbd_buffer[2000];

	print("This operating system is licensed under the GPLv3.");
	print_newline(); print_newline();
	print("Welcome to the built-in kernel shell.");
	print_newline();
	print("\"help\" to bring up command menu");
	print_newline();
	
	int skip_prompt = 0;

	while(1) {
		memcpy(last_kbd_buffer, &kbd_buffer, sizeof(kbd_buffer));

		if(skip_prompt == 0) {
			print("k_shell> ");
			for(int i = 0; i < 2000; i++) {
				kbd_buffer[i] = 0x0;
			}		
			kbd_readline(kbd_buffer);
		}

		if(strcmp(splice(kbd_buffer, 0, 0x20), "cls") == 0) clear();
		if(strcmp(splice(kbd_buffer, 0, 0x20), "disk_read") == 0) {
			disk_read(atoi(splice(kbd_buffer, 1, 0x20)), 1);
		}
		if(strcmp(splice(kbd_buffer, 0, 0x20), "touch") == 0) file_create(splice(kbd_buffer, 1, 0x20));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "list") == 0) file_list();
		if(strcmp(splice(kbd_buffer, 0, 0x20), "write") == 0) {
			static char filename[512];
			static char data[1024];

			char chr;
			int input_i = 6;
			int buffer_i = 0;

			do {
				chr = kbd_buffer[input_i];
				filename[buffer_i] = chr;
				input_i++;
				buffer_i++;
			} while(chr != 0x20);
			filename[buffer_i - 1] = 0;

			buffer_i = 0;
			do {
				chr = kbd_buffer[input_i];
				data[buffer_i] = chr;
				input_i++;
				buffer_i++;				
			} while(chr || input_i >= 1024);
			
			file_write(filename, data);
		}
		if(strcmp(splice(kbd_buffer, 0, 0x20), "read") == 0) {
			static uint8_t read_target[512];
			for(int i = 0; i < 512; i++) read_target[i] = 0;

			if(atoi(splice(kbd_buffer, 2, 0x20)) >= 0) {
				if(atoi(splice(kbd_buffer, 3, 0x20)) > 0) {
					int read_offset = atoi(splice(kbd_buffer, 2, 0x20));

					int i = 0;
					do {
						file_read(splice(kbd_buffer, 1, 0x20), read_target, 1, read_offset);
						print(read_target);
						read_offset++;
						i++;
					} while(i <= atoi(splice(kbd_buffer, 3, 0x20)));

					print_newline();
				} else {
					file_read(splice(kbd_buffer, 1, 0x20), read_target, 1, atoi(splice(kbd_buffer, 2, 0x20)));
					print(read_target);
					print_newline();
				}
			} else {
				int file_size = get_file_info(splice(kbd_buffer, 1, 0x20)).size;
				int sectors_size = file_size / 512;
				int read_offset = 0;

				while(sectors_size > -1) {
					for(int i = 0; i < 512; i++) read_target[i] = 0;
					file_read(splice(kbd_buffer, 1, 0x20), read_target, 1, read_offset);
					print(read_target);
					read_offset++;
					sectors_size--;
				}

				print_newline();
			}
		} 
		if(strcmp(splice(kbd_buffer, 0, 0x20), "del") == 0) file_delete(splice(kbd_buffer, 1, 0x20)); 
		if(strcmp(splice(kbd_buffer, 0, 0x20), "color") == 0) set_term_color((atoi(splice(kbd_buffer, 2, 0x20)) * 16) + atoi(splice(kbd_buffer, 1, 0x20)));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "rep") == 0) {
			skip_prompt = 1;
			memcpy(kbd_buffer, &last_kbd_buffer, sizeof(last_kbd_buffer));
		} else skip_prompt = 0; 
		if(strcmp(splice(kbd_buffer, 0, 0x20), "help") == 0) {
			print("cls: clear screen"); print_newline();
			print("disk_read <block>: read bytes from disk"); print_newline();
			print("touch <name>: create a file (no spaces in name)"); print_newline();
			print("list: list all files"); print_newline();
			print("write <name> <content>: write to file (content can have spaces)"); print_newline();
			print("read <name> <offset (blocks)> <size (blocks)>: read ascii text from file"); print_newline();
			print("del <name>: delete a file"); print_newline();
			print("color <foreground> <background>"); print_newline();
			print("--> color values are 0 - 15 (VGA 16-color mode)"); print_newline();
			print("rep: repeat previous command"); print_newline();
			print("help: bring up this menu"); print_newline();
		}
	}
}
