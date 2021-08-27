#include <stdint.h>
#include "../utils/ports.h"
#include "screen.h"

#define KEYBOARD_PORT 0x60

// special keys
#define ESC 0x1
#define PGUP 0x49
#define PGDN 0x51
#define BACKSPACE 0xE
#define TAB	0xF
#define ENTER 0x1C
#define CAPS_LOCK 0x3A
#define SHIFT 0x36
#define CTRL 0x1D
#define ALT 0x38
#define SUPER 0xB9
#define R_ARROW 0x4D
#define L_ARROW 0x4B
#define U_ARROW 0x48
#define D_ARROW 0x50

char get_input_keycode();
void kbd_readline();
