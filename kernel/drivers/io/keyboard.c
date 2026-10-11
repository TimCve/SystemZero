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
int kbd_decode(keyboard_state_t *state, uint8_t raw, keyboard_event_t *event) {
    if (!raw) return 0;
    if (state->pause) { state->pause--; return 0; }
    if (raw == 0xe1) { state->pause = 5; return 0; }
    if (raw == 0xe0) { state->extended = 1; return 0; }
    int extended = state->extended, release = raw & 0x80;
    uint8_t code = raw & 0x7f; state->extended = 0;
    if (!extended && (code == 0x2a || code == 0x36)) {
        uint8_t bit = code == 0x2a ? 1 : 2;
        if (release) state->shift &= ~bit; else state->shift |= bit;
        return 0;
    }
    if (code == CTRL) {
        uint8_t bit = extended ? 2 : 1;
        if (release) state->control &= ~bit; else state->control |= bit;
        return 0;
    }
    if (release || (extended && (code == 0x2a || code == 0x36))) return 0;
    *event = (keyboard_event_t){code, get_printable_char(code, 0, state->shift),
                              !!state->shift, !!state->control};
    if (code == TAB) event->character = '\t';
    return 1;
}
void kbd_wait_event(keyboard_state_t *state, keyboard_event_t *event) {
    while (!kbd_decode(state, (uint8_t)get_input_keycode(), event)) {}
}
int kbd_readline(char *buffer, int calibration, int capacity) {
    (void)calibration;
    if (!buffer || capacity < 1) return 0;
    int length = 0, cursor = 0;
    keyboard_state_t state = {0};
    buffer[0] = 0;
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&state, &event);
        uint8_t key = event.code;
        if (key == ESC) return 0;
        if (key == ENTER) { print_newline(); return 1; }
        if (key == L_ARROW && cursor) { cursor--; set_cursor_position(get_cursor_position() - 2); continue; }
        if (key == R_ARROW && cursor < length) { cursor++; set_cursor_position(get_cursor_position() + 2); continue; }
        int screen = get_cursor_position();
        if (key == BACKSPACE && !cursor) continue;
        char ch = event.character;
        if (key != BACKSPACE && (!ch || ch == '\t' || event.control || length >= capacity - 1)) continue;
        screen_begin_update();
        if (key == BACKSPACE) {
            cursor--; length--; screen -= 2;
            for (int i = cursor; i <= length; i++) buffer[i] = buffer[i + 1];
        } else {
            for (int i = length + 1; i > cursor; i--) buffer[i] = buffer[i - 1];
            buffer[cursor++] = ch; length++;
            set_cursor_position(screen); print_char(ch); screen += 2;
        }
        set_cursor_position(screen);
        for (int i = cursor; i < length; i++) print_char(buffer[i]);
        print_char(' '); set_cursor_position(screen);
        screen_end_update();
    }
}
