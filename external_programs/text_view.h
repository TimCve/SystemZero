#ifndef SYSTEMZERO_TEXT_VIEW_H
#define SYSTEMZERO_TEXT_VIEW_H
#include "../kernel/drivers/io/screen.h"
static uint32_t text_start(const uint8_t *data, uint32_t size, uint32_t line) {
    uint32_t pos = 0;
    while (line-- && pos < size) {
        uint32_t col = 0;
        while (pos < size && col++ < 80) if (data[pos++] == '\n') break;
    }
    return pos;
}
static void text_render(const uint8_t *data, uint32_t size, uint32_t start,
                        uint32_t cursor, uint8_t color) {
    clear(); uint32_t pos = start;
    for (uint32_t row = 0; row < 24; row++) {
        for (uint32_t col = 0; col < 80; col++) {
            set_term_color(pos == cursor ? (color << 4) | (color >> 4) : color);
            if (pos >= size) { print_char(' '); pos++; }
            else if (data[pos] == '\n') { pos++; break; }
            else print_char(data[pos++]);
        }
        set_term_color(color); set_cursor_position((row + 1) * 160);
    }
    set_term_color(color);
}
#endif
