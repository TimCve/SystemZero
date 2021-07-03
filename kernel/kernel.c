#include "kernel.h"
#include "drivers/io/screen.h"
#include "drivers/io/keyboard.h"
#include "cpu/isr.h"
#include "cpu/timer.h"

void sleep(int time) {
	int init_tick = get_tick();
	while(get_tick() < init_tick + time) {
		continue;
	}
}

void kmain()
{
	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();

	print("Installing interrupt service routines..."); print_newline();
	isr_install();
	__asm__("sti");

	init_timer(1000);

	print_newline();
	char kbd_buffer[512];

	while(1) {
		for(int i = 0; i < 512; i++) {
			kbd_buffer[i] = 0x0;
		}

		print("kbd> ");
		kbd_readline(kbd_buffer);
		print("Entered: ");
		print(kbd_buffer);
		print_newline();
		print("[timer tick]: ");
		print_dec(get_tick());
		print_newline();
	}
}