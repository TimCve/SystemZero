#include "graphics.h"
#include "video.h"
int draw_pixel(uint16_t x, uint16_t y, uint8_t color) { return screen_draw_pixel(x, y, color); }
void draw_line(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint8_t color) {
    if (x_start >= VIDEO_WIDTH || x_end >= VIDEO_WIDTH || y_start >= VIDEO_HEIGHT || y_end >= VIDEO_HEIGHT) return;
    screen_begin_update();
    int x = x_start, y = y_start;
    int dx = x_end > x_start ? x_end - x_start : x_start - x_end;
    int dy = y_end > y_start ? y_start - y_end : y_end - y_start;
    int step_x = x_start < x_end ? 1 : -1, step_y = y_start < y_end ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        draw_pixel(x, y, color);
        if (x == x_end && y == y_end) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x += step_x; }
        if (twice <= dx) { error += dx; y += step_y; }
    }
    screen_end_update();
}



