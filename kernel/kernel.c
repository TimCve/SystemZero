#include "kernel.h"
#include "sys_api.h"
#include "desktop.h"
#include "cpu/thread.h"
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
    return env && env->run ? env->run(cmd, args, env) : 2;
}

static int kernel_execute(char *cmd, char *args, env_vars_t *env) {
    uint8_t previous_drive = selected_ata_drive();
    /* User executables live on the working drive; bundled commands are a fallback. */
    select_drive(env->selected_drive);
    inode_t info = get_file_info(cmd);
    if (!info.valid && env->selected_drive != env->system_drive) {
        select_drive(env->system_drive); info = get_file_info(cmd);
    }
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
            char previous_command[STATUS_COMMAND];
            memcpy(previous_command, env->desktop->command, STATUS_COMMAND);
            desktop_command(env, args);
            uint32_t previous_owner = thread_scope_begin();
            env->free_mem_ptr = get_free_ptr();
            status = ((int (*)(env_vars_t *, char *))(memory + plan.entry))(env, args);
            thread_scope_end(previous_owner);
            desktop_command(env, previous_command);
            /* Native compiler output calls the kernel without program_start(). */
            screen_bind_env(env);
            if (env->terminal) ((terminal_t *)env->terminal)->updates = 0;
            set_scrolling(1); screen_cursor_visible(1); set_term_color(env->term_color);
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
	sys_api_init(env_vars_ptr);
    env_vars_ptr->run = kernel_execute;
	set_term_color(env_vars_ptr->term_color);

	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();

	// KERNEL INIT PROCESS

	// set pointer for manual memory allocation
	set_free_ptr(env_vars_ptr->free_mem_ptr);
    mem_bind_env(env_vars_ptr);

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
    desktop_init(env_vars_ptr);
    threads_init(env_vars_ptr);
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
        scroll_terminal();
        screen_begin_update();
        uint32_t prompt_row = get_cursor_position() / (COLS * 2);
        for (uint32_t col = 0; col < COLS; col++) screen_put_cell(prompt_row, col, ' ', env_vars_ptr->term_color);

		set_term_color(((env_vars_ptr->term_color >> 4) * 16) + 0x0e);
		print("IDE"); print_dec(env_vars_ptr->selected_drive); print("> ");
		set_term_color(env_vars_ptr->term_color);
        screen_end_update();

		for(int i = 0; i < 2000; i++) kbd_buffer[i] = 0;

		if (!kbd_readline(kbd_buffer, env_vars_ptr->tty_calibration, 2000)) { print_newline(); continue; }

		execute(splice(kbd_buffer, 0, 0x20), kbd_buffer, env_vars_ptr);
		if (get_cursor_position() % (COLS * 2)) print_newline();
	}	
}
