#include "kernel.h"
#include "env_vars.h"
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

void kmain(env_vars_t* env_vars_ptr) {
	set_term_color(env_vars_ptr->term_color);

	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();

	// KERNEL INIT PROCESS

	// set pointer for manual memory allocation
	set_free_ptr(env_vars_ptr->free_mem_ptr);

	// install interrupt service routines
	print("Installing interrupt service routines...");
	isr_install();
	__asm__("sti");
	print(" [DONE]"); print_newline();
	
	// select initial drive (master)
	select_drive(env_vars_ptr->selected_drive);

	uint8_t system_drive = env_vars_ptr->selected_drive;

	// determine size of system drive
	print("Determining size of system drive...");	

	uint8_t ATA_err;
	uint8_t read_target[512];
	int lba = 0;
	int bytes = 0;

	while(!ATA_err) {
		read_sectors_ATA_PIO(read_target, lba, 1);
		ATA_err = ATA_get_ERROR();
		lba += 10;
		bytes += 5120;
	}

	print(" ["); print_dec(bytes); print(" bytes]");
	print_newline();

	// intitialize superblock in memory for system drive 
	init_fs(bytes);

	// format system drive if necessary
	if(check_disk_fs() == 0) {
		format_disk();
	}

	// initialize timer
	print("Initialising timer...");
	init_timer(1000);
	print(" [DONE]"); print_newline();

	// calibrate TTY for speed
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
	env_vars_ptr->tty_calibration = tty_calibration;

	print_newline();

	char kbd_buffer[2000];

	// startup logo
	set_term_color(((env_vars_ptr->term_color >> 4) * 16) + 0x0e);
	print(" _____         _             _____             "); print_newline();
	print("|   __|_ _ ___| |_ ___ _____|__   |___ ___ ___ "); print_newline();
	print("|__   | | |_ -|  _| -_|     |   __| -_|  _| . |"); print_newline();
	print("|_____|_  |___|_| |___|_|_|_|_____|___|_| |___|"); print_newline();
	print("      |___|                                    "); print_newline();
	set_term_color(env_vars_ptr->term_color);
	print_newline();

	// startup message
	print("This operating system is licensed under the GPLv3.");
	print_newline();
	print("All source code can be cloned from: https://github.com/z3r0flag/SystemZero.git");
	print_newline(); print_newline();
	print("You are now working from the built in kernel shell.");
	print_newline();
	print("Type \"list\" to list all files on disk, files marked with \'x\' are executable.");
	print_newline(); print_newline();

	uint32_t phy_addr;	

	while(1) {
		select_drive(env_vars_ptr->selected_drive);

		set_term_color(((env_vars_ptr->term_color >> 4) * 16) + 0x0e);
		print("IDE"); print_dec(env_vars_ptr->selected_drive); print("> ");
		set_term_color(env_vars_ptr->term_color);
		
		for(int i = 0; i < 2000; i++) {
			kbd_buffer[i] = 0x0;
		}		
		kbd_readline(kbd_buffer, env_vars_ptr->tty_calibration);	
			
		if(splice(kbd_buffer, 0, 0x20)) {
			select_drive(system_drive);
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
					void (*func)(env_vars_t* env_vars_ptr, char* input_buffer) = program_memory + program[6];
				
					// set free memory pointer environment variable
					env_vars_ptr->free_mem_ptr = get_free_ptr();

					// execute program main function
					func(env_vars_ptr, &kbd_buffer);

					// clear out memory after program has finished execution
					for(int i = 0; i < 50000; i++) program[i] = 0;

					// reset the free memory pointer
					set_free_ptr(get_free_ptr() - 54096);
					env_vars_ptr->free_mem_ptr = get_free_ptr();
				} else {
					print("File is not executable!");
					print_newline();
				}
			}
		}
	}	
}
