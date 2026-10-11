#ifndef SYSTEMZERO_GRAPHICS_H
#define SYSTEMZERO_GRAPHICS_H
#include "screen.h"
#include "../../vga_colors.h"

int draw_pixel(uint16_t x, uint16_t y, uint8_t color);
void draw_line(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint8_t color);
#endif
