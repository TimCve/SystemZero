#include "kernel.h"
#include "drivers/io/screen.h"
#include "drivers/io/keyboard.h"
#include "cpu/isr.h"
#include "cpu/timer.h"
#include "drivers/disk/ata.h"
#include "libc/strings.h"
#include "drivers/disk/fs.h"

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

	char kbd_buffer[2000];

	print("Welcome to the built-in kernel shell.");
	print_newline();
	print("\"help\" to bring up command menu");
	print_newline();

	while(1) {
		for(int i = 0; i < 2000; i++) {
			kbd_buffer[i] = 0x0;
		}

		print("k_shell> ");
		kbd_readline(kbd_buffer);

		if(strcmp(splice(kbd_buffer, 0, 0x20), "cls") == 0) clear();
		if(strcmp(splice(kbd_buffer, 0, 0x20), "disk_read") == 0) disk_read(atoi(splice(kbd_buffer, 1, 0x20)), atoi(splice(kbd_buffer, 2, 0x20)));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "file_create") == 0) file_create(splice(kbd_buffer, 1, 0x20));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "file_list") == 0) file_list();
		if(strcmp(splice(kbd_buffer, 0, 0x20), "file_write") == 0) {
			static char filename[512];
			static char data[512];

			int i;
			for(i = 0; i < 512; i++) {
				filename[i] = 0;
				data[i] = 0;
			}

			for(i = 0; i < 512; i++) {
				if(splice(kbd_buffer, 1, 0x20)[i] > 0) {
					filename[i] = splice(kbd_buffer, 1, 0x20)[i];
				} else {
					break;
				}
			}

			i+= 12;

			int k = 0;
			while(i < 512) {
				if(kbd_buffer[i]) data[k] = kbd_buffer[i];
				k++;
				i++;
			}

			file_write(filename, data);
		}
		if(strcmp(splice(kbd_buffer, 0, 0x20), "file_read") == 0) file_read(splice(kbd_buffer, 1, 0x20));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "file_delete") == 0) file_delete(splice(kbd_buffer, 1, 0x20));
		if(strcmp(splice(kbd_buffer, 0, 0x20), "help") == 0) {
			print("cls: clear screen"); print_newline();
			print("disk_read <block> <num_of_blocks>: read bytes from disk"); print_newline();
			print("file_create <name>: create a file (no spaces in name)"); print_newline();
			print("file_list: list all files"); print_newline();
			print("file_write <name> <content>: write to file (content can have spaces)"); print_newline();
			print("file_read <name>: read ascii text from file"); print_newline();
			print("file_delete <name>: delete a file"); print_newline();
			print("help: bring up this menu"); print_newline();
		}
	}
}
