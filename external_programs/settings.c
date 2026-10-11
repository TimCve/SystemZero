#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../shared/sys_api.h"

static int service(env_vars_t *env, uint32_t id, uint32_t a, uint32_t b, uint32_t c) {
    return env->api->functions[id](env, a, b, c, 0, 0);
}
static void number(uint32_t row, uint32_t col, int value, uint8_t color) {
    char text[] = {'0' + value / 10, '0' + value % 10, 0}; screen_put_text(row, col, text, color);
}
static void palette(uint32_t row, uint32_t col, int selected, int focus) {
    for (uint32_t index = 0; index < 16; index++) {
        uint32_t y = row + index / 4 * 2, x = col + index % 4 * 6;
        uint8_t ink = index == 0 || index == 1 || index == 4 || index == 5 || index == 8 ? 15 : 0;
        uint8_t attr = (index << 4) | ink;
        for (uint32_t dy = 0; dy < 2; dy++) for (uint32_t dx = 0; dx < 5; dx++)
            screen_put_cell(y + dy, x + dx, ' ', attr);
        screen_put_cell(y, x + 2, "0123456789ABCDEF"[index], attr);
        if ((int)index == selected) {
            screen_put_cell(y, x, focus ? '[' : '>', attr);
            screen_put_cell(y, x + 4, focus ? ']' : '<', attr);
        }
    }
}
static void render(int *values, int focus, const char *notice) {
    uint32_t top = ROWS > 30 ? 6 : 0;
    screen_begin_update(); set_term_color(0x17); screen_cursor_visible(0);
    for (uint32_t row = 0; row < ROWS; row++)
        for (uint32_t col = 0; col < COLS; col++) screen_put_cell(row, col, ' ', 0x17);
    screen_put_text(top + 1, 6, "SystemZero Settings", 0x1f);
    screen_put_text(top + 4, 6, "Clock", 0x17);
    for (uint32_t i = 0; i < 3; i++) {
        number(top + 4, 20 + i * 3, values[i], focus == (int)i ? 0x71 : 0x1f);
        if (i != 2) screen_put_cell(top + 4, 22 + i * 3, ':', 0x1f);
    }
    screen_put_text(top + 6, 6, "Status bar", 0x17);
    screen_put_text(top + 6, 20, values[3] ? "Bottom" : "Top   ", focus == 3 ? 0x71 : 0x1f);
    screen_put_text(top + 8, 6, "Foreground", focus == 4 ? 0x71 : 0x1f);
    screen_put_text(top + 8, 44, "Background", focus == 5 ? 0x71 : 0x1f);
    palette(top + 10, 6, values[4], focus == 4); palette(top + 10, 44, values[5], focus == 5);
    screen_put_text(top + 20, 6, "Sample terminal text", (values[5] << 4) | values[4]);
    screen_put_text(ROWS - 3, 6, notice, 0x1e);
    screen_put_text(ROWS - 1, 0, "Tab: next | Arrows: adjust | Enter: save | Esc: cancel", 0x1f);
    screen_end_update();
}
int main(env_vars_t *env, char *args) {
    if (!env->api || env->api->version != SZ_API_VERSION || env->api->count <= SZ_API_settings_apply) return 1;
    int seconds = service(env, SZ_API_clock_time, 0, 0, 0), color = env->term_color;
    int values[] = {seconds / 3600, seconds / 60 % 60, seconds % 60,
                   service(env, SZ_API_status_position, 0, 0, 0), color & 15, color >> 4};
    screen_state_t *console = screen_save_state();
    if (!console) { print("Not enough memory to save the console."); print_newline(); return 1; }
    keyboard_state_t keyboard = {0}; int focus = 0, digits = 0;
    const int limits[] = {24, 60, 60, 2, 16, 16}; const char *notice = "";
    set_scrolling(0); render(values, focus, notice);
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard, &event);
        if (event.code == ESC) { screen_restore_state(console); return 0; }
        if (event.code == ENTER) {
            if (values[4] == values[5]) { notice = "Choose different foreground and background colors."; }
            else {
                screen_restore_state(console);
                if (!service(env, SZ_API_settings_apply, values[0] * 3600 + values[1] * 60 + values[2],
                             values[3], (values[5] << 4) | values[4])) return 0;
                notice = "Could not save settings to the system disk. Try again or cancel.";
                set_scrolling(0);
            }
        } else if (event.code == TAB) { focus = (focus + (event.shift ? 5 : 1)) % 6; digits = 0; }
        else if (event.code == L_ARROW || event.code == R_ARROW || event.code == U_ARROW || event.code == D_ARROW) {
            int delta = event.code == L_ARROW || event.code == D_ARROW ? -1 : 1;
            if (focus >= 4 && (event.code == U_ARROW || event.code == D_ARROW))
                delta = event.code == U_ARROW ? -4 : 4;
            values[focus] = (values[focus] + limits[focus] + delta) % limits[focus]; digits = 0; notice = "";
        } else if (focus < 3 && event.character >= '0' && event.character <= '9') {
            int value = (digits == 1 ? values[focus] * 10 : 0) + event.character - '0';
            if (value < limits[focus]) { values[focus] = value; digits = (digits + 1) % 2; notice = ""; }
            else { digits = 0; notice = "Invalid time value."; }
        } else continue;
        render(values, focus, notice);
    }
}
