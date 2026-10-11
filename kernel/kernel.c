#include "kernel.h"
#include "../shared/elf.h"
#include "drivers/utils/ports.h"
#include "drivers/io/screen.h"
#include "drivers/io/graphics.h"
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

int execute(char *cmd, char *args, env_vars_t *env) {
    uint8_t previous_drive = selected_ata_drive();
    select_drive(env->system_drive);
    inode_t info = get_file_info(cmd);
    if (!info.valid) { print("Command not found or filesystem unavailable."); print_newline(); select_drive(previous_drive); return 2; }
    uint32_t marker = get_free_ptr();
    uint32_t sectors = (info.size + 511) / 512;
    uint32_t source = malloc(sectors * 512, 1, 0);
    int status = 2;
    elf_plan_t plan;
    if (!source || file_read(cmd, (void *)source, sectors, 0) == 2 ||
        !elf_plan((void *)source, info.size, &plan)) {
        print("Invalid executable or insufficient memory."); print_newline();
    } else {
        uint32_t memory = malloc(plan.size, 1, 0);
        if (!memory || !elf_load((void *)source, info.size, &plan, (void *)memory, plan.size)) {
            print("Unsupported executable or insufficient memory."); print_newline();
        } else {
            env->free_mem_ptr = get_free_ptr();
            status = ((int (*)(env_vars_t *, char *))(memory + plan.entry))(env, args);
            /* Each program has its own allocator state; collect its final marker. */
            if (env->free_mem_ptr > get_free_ptr()) set_free_ptr(env->free_mem_ptr);
        }
    }
    heap_release(marker); env->free_mem_ptr = get_free_ptr();
    select_drive(previous_drive);
    return status;
}

void kmain(env_vars_t* env_vars_ptr) {
	screen_init(env_vars_ptr);
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

	env_vars_ptr->system_drive = env_vars_ptr->selected_drive;

	// intitialize filesystem
	if (!init_fs(env_vars_ptr)) {
        print("System filesystem unavailable or corrupt. Format it explicitly from the host.");
        print_newline();
    }

	// initialize timer
	print("Initialising timer...");
	init_timer(1000);
	print(" [DONE]"); print_newline();

	// Keyboard input is event-driven; it no longer needs display-speed calibration.
	env_vars_ptr->tty_calibration = 0;

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
	print("You are now working from the built-in kernel shell.");
	print_newline();
	print("Type \"list\" to list all files on disk, files marked with \'x\' are executable.");
	print_newline(); print_newline();

	// kernel shell
	while(1) {
		select_drive(env_vars_ptr->selected_drive);

		set_term_color(((env_vars_ptr->term_color >> 4) * 16) + 0x0e);
		print("IDE"); print_dec(env_vars_ptr->selected_drive); print("> ");
		set_term_color(env_vars_ptr->term_color);

		for(int i = 0; i < 2000; i++) kbd_buffer[i] = 0;

		if (!kbd_readline(kbd_buffer, env_vars_ptr->tty_calibration, 2000)) { print_newline(); continue; }

		print_dec(execute(splice(kbd_buffer, 0, 0x20), kbd_buffer, env_vars_ptr)); print(" -> exit status"); print_newline();
	}	
}
