#include "kernel.h"
#include "drivers/screen/screen.h"
#include "drivers/io/keyboard.h"

void kmain()
{
	// actual kernel starts here
	print("Kernel successfully initialized!"); print_newline();

	// requests input from keyboard
	kbd_input();
}