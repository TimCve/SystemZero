#include "keyboard.h"
#include <stdint.h>
char get_printable_char(char keycode, int i, uint8_t shift) {
    const kbd_keycode *table = shift ? shft_keycode_table : std_keycode_table;
    for (; i >= 0 && table[i].keycode; i++) if ((uint8_t)keycode == (uint8_t)table[i].keycode) return table[i].chr;
    return 0;
}
char get_input_keycode(void) {
    return (port_byte_in(0x64) & 1) ? port_byte_in(0x60) : 0;
}
void kbd_readline(char *buffer, int calibration, int capacity) {
    (void)calibration;
    if (!buffer || capacity < 1) return;
    int length = 0, cursor = 0, shift = 0;
    buffer[0] = 0;
    for (;;) {
        uint8_t key = get_input_keycode();
        if (!key) continue;
        if (key == 0x2a || key == 0x36) shift = 1;
        if (key == 0xaa || key == 0xb6) shift = 0;
        if (key & 0x80) continue;
        if (key == ENTER) { print_newline(); return; }
        if (key == L_ARROW && cursor) { cursor--; set_cursor_position(get_cursor_position() - 2); continue; }
        if (key == R_ARROW && cursor < length) { cursor++; set_cursor_position(get_cursor_position() + 2); continue; }
        int screen = get_cursor_position();
        if (key == BACKSPACE && cursor) {
            cursor--; length--; screen -= 2;
            for (int i = cursor; i <= length; i++) buffer[i] = buffer[i + 1];
        } else {
            char ch = get_printable_char(key, 0, shift);
            if (!ch || length >= capacity - 1) continue;
            for (int i = length + 1; i > cursor; i--) buffer[i] = buffer[i - 1];
            buffer[cursor++] = ch; length++;
            set_cursor_position(screen); print_char(ch); screen += 2;
        }
        set_cursor_position(screen);
        for (int i = cursor; i < length; i++) print_char(buffer[i]);
        print_char(' '); set_cursor_position(screen);
    }
}
