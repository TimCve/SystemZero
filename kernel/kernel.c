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
#include "vga_colors.h"

void sleep(int time) {
	int init_tick = get_tick();
	while(get_tick() < init_tick + time) {
		continue;
	}
}

void kmain(uint32_t free_mem_addr)
{
	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();
	set_free_ptr(free_mem_addr);

	print("Installing interrupt service routines...");
	isr_install();
	__asm__("sti");
	print(" [DONE]"); print_newline();

	if(check_disk_fs() == 0) {
		format_disk();
	}

	print("Initialising timer...");
	init_timer(1000);
	print(" [DONE]"); print_newline();

	print("Starting TTY calibration sequence...");
	int tick_start = get_tick();
	int index = 0;
	while(index < 20000) {
		print_char(' ');
		set_cursor_position(get_cursor_position() - 2);
		index++;
	}
	int tty_calibration = get_tick() - tick_start;	
	print(" [TICKS: "); print_dec(tty_calibration); print("]"); print_newline();

	print_newline();

	char kbd_buffer[2000];

	set_term_color(0x1e);
	print(" _____         _             _____             "); print_newline();
	print("|   __|_ _ ___| |_ ___ _____|__   |___ ___ ___ "); print_newline();
	print("|__   | | |_ -|  _| -_|     |   __| -_|  _| . |"); print_newline();
	print("|_____|_  |___|_| |___|_|_|_|_____|___|_| |___|"); print_newline();
	print("      |___|                                    "); print_newline();
	set_term_color(STD_COLOR);

	print_newline();
	print("This operating system is licensed under the GPLv3.");
	print_newline();
	print("All source code can be cloned from: https://github.com/z3r0flag/SystemZero.git");
	print_newline(); print_newline();
	print("You are now working from the built in kernel shell.");
	print_newline();
	print("Type \"list\" to list all files on disk, files marked with \'x\' are executable.");
	print_newline();
	
	uint32_t phy_addr;

	while(1) {
		print("> ");
		for(int i = 0; i < 2000; i++) {
			kbd_buffer[i] = 0x0;
		}		
		kbd_readline(kbd_buffer, tty_calibration);

		if(splice(kbd_buffer, 0, 0x20)) {
			uint8_t exe_check_buffer[512];
			
			if(file_read(splice(kbd_buffer, 0, 0x20), exe_check_buffer, 1, 0) != 2) {
				// check if the file is executable
				if(exe_check_buffer[0] == 0x7F && exe_check_buffer[1] == 0x45 && exe_check_buffer[2] == 0x4C && exe_check_buffer[3] == 0x46) {
					// allocate memory for program
					uint32_t program_memory = malloc(50000, 1, &phy_addr);

					// print("program memory: "); print_dec(program_memory); print_newline();

					// read program into memory
					int file_size = get_file_info(splice(kbd_buffer, 0, 0x20)).size;
					file_read(splice(kbd_buffer, 0, 0x20), program_memory, file_size / 512, 0);

					// create a pointer to the program
					uint32_t* program = program_memory;
					
					// create pointer to the program's main function (entry point)
				    void (*func)(uint32_t free_mem_addr, char* input_buffer) = program_memory + program[6];

				    // execute program main function
	    			func(get_free_ptr(), &kbd_buffer);

	    			// clear out memory after program has finished execution
	    			for(int i = 0; i < 50000; i++) program[i] = 0;

	    			// reset the free memory pointer
	    			set_free_ptr(get_free_ptr() - 54096);
				} else {
					print("File is not executable!");
					print_newline();
				}
			}
		}
	}
}
