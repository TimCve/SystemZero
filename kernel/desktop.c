#include "desktop.h"
#include "../shared/input.h"
#include "../shared/version.h"
#include "drivers/io/screen.h"
#include "drivers/io/video.h"
#include "drivers/utils/mem.h"
#include "drivers/utils/ports.h"
#include "drivers/disk/fs.h"
#include "drivers/disk/ata.h"
#include "cpu/thread.h"
#include "variables.h"
static desktop_t desktop;
static env_vars_t *environment;
typedef struct { uint32_t magic, offset; uint8_t position, color, secondary, reserved; uint32_t check; } config_t;
static uint8_t rtc_read(uint8_t index) {
    port_byte_out(0x70, index); return port_byte_in(0x71);
}
static uint32_t bcd(uint8_t n) { return (n >> 4) * 10 + (n & 15); }
uint32_t desktop_rtc_seconds(void) {
    for (uint32_t attempt = 0; attempt < 10000; attempt++) {
        if (rtc_read(10) & 0x80) continue;
        uint8_t s = rtc_read(0), m = rtc_read(2), h = rtc_read(4), mode = rtc_read(11);
        if ((rtc_read(10) & 0x80) || rtc_read(0) != s) continue;
        int pm = h & 0x80; h &= 0x7f;
        uint32_t second = mode & 4 ? s : bcd(s), minute = mode & 4 ? m : bcd(m);
        uint32_t hour = mode & 4 ? h : bcd(h);
        if (!(mode & 2)) hour = hour % 12 + (pm ? 12 : 0);
        if (second < 60 && minute < 60 && hour < 24) return hour * 3600 + minute * 60 + second;
    }
    return 0;
}
static uint32_t checksum(const config_t *c) {
    return c->magic ^ c->offset ^ c->position ^ ((uint32_t)c->color << 8) ^
        ((uint32_t)c->secondary << 16) ^ 0x18b52c4du;
}
static void viewport(desktop_t *d) {
    terminal_t *t = environment->terminal;
    if (t) { t->reserved_top = d->position ? 0 : STATUS_HEIGHT; t->reserved_bottom = d->position ? STATUS_HEIGHT : 0; }
}
static void recolor(env_vars_t *env, uint8_t color) {
    terminal_t *t = env->terminal; set_term_color(color);
    uint8_t secondary = (color & 0xf0) | env->desktop->secondary_color;
    if (t) {
        for (uint32_t i = 0; i < TERM_CELLS; i++) {
            t->cells[i].color = t->secondary[i] ? secondary : color; t->shown[i].ch = 0xff;
        }
        t->clear_canvas = 1; terminal_present(t);
    } else {
        volatile uint8_t *video = ui(env)?(void *)ui(env)->console.vga:(void *)VIDEO_ADDRESS;
        uint32_t offset = ui(env) || env->desktop->position ? 0 : 80 * 2;
        for (uint32_t i = 0; i < 24 * 80; i++)
            video[offset + i * 2 + 1] = env->desktop->vga_secondary[i] ? secondary : color;
    }
}
void desktop_init(env_vars_t *env) {
    int cursor = get_cursor_position();
    environment = env;
    desktop = (desktop_t){.seconds = desktop_rtc_seconds(), .dirty = 1, .color = env->term_color,
        .enabled = 1, .secondary_color = 14, .refresh = desktop_refresh};
    env->desktop = &desktop;
    uint8_t previous = selected_ata_drive(); select_drive(env->system_drive);
    config_t config; uint32_t offset = 0;
    int config_bytes=file_read_bytes("sys:/sys/config/legacy.cfg", &config, sizeof config, 0);
    if(config_bytes!=sizeof config) config_bytes=file_read_bytes("sys:/systemzero.cfg", &config, sizeof config, 0);
    if (config_bytes == sizeof config &&
        (config.magic == 0x535a4331u || config.magic == 0x535a4332u) && config.offset < 86400 && config.position < 2 &&
        (config.color & 15) != (config.color >> 4) && !config.reserved && config.check == checksum(&config)) {
        int legacy = config.magic == 0x535a4331u && config.secondary == 0;
        int current = config.magic == 0x535a4332u && config.secondary < 16 && config.secondary != (config.color >> 4);
        if (legacy || current) {
            desktop.seconds = (desktop.seconds + config.offset) % 86400;
            offset = config.offset;
            desktop.position = config.position; desktop.color = env->term_color = config.color;
            desktop.secondary_color = legacy ? ((config.color >> 4) == 14 ? 15 : 14) : config.secondary;
        }
    }
    select_drive(previous); viewport(&desktop);
    if (!env->terminal && !desktop.position) {
        volatile uint16_t *video = (void *)VIDEO_ADDRESS;
        for (uint32_t i = 24 * 80; i; i--) video[i - 1 + 80] = video[i - 1];
    }
    screen_bind_env(env); set_term_color(env->term_color);
    if (!env->terminal) set_cursor_position(cursor);
    recolor(env, env->term_color);
    desktop_command(env, "shell");
    variables_init(env, offset);
}
void desktop_tick(void) {
    if (!desktop.enabled) return;
    if (++desktop.milliseconds == 1000) {
        desktop.milliseconds = 0; desktop.seconds = (desktop.seconds + 1) % 86400; desktop.dirty = 1;
    }
}
void desktop_command(env_vars_t *env, const char *command) {
    desktop_t *d = env->desktop;
    if (!d) return;
    thread_guard_enter();
    uint32_t i = 0; while (command && command[i] && i + 1 < STATUS_COMMAND) { d->command[i] = command[i]; i++; }
    d->command[i] = 0; d->dirty = 1; if (d->refresh) d->refresh(); thread_guard_leave();
}
void desktop_refresh(void) {
    if (!environment || !desktop.enabled || !desktop.dirty) return;
    thread_guard_enter(); ui_paint(environment,0); desktop.dirty = 0;
    terminal_t *t = environment->terminal;
    uint32_t columns = t ? t->width / TERM_FONT_WIDTH : 80;
    if (columns > 100) columns = 100;
    for (uint32_t i = 0; i < columns; i++) desktop.bar[i] = ' ';
    desktop.bar[columns] = 0;
    const char *prefix = "SystemZero  v" SYSTEMZERO_VERSION "  ";
    uint32_t at = 0; while (*prefix && at < columns) desktop.bar[at++] = *prefix++;
    uint32_t end = columns - 10, n = 0;
    while (at < end && desktop.command[n]) desktop.bar[at++] = desktop.command[n++];
    if (desktop.command[n] && end >= 3) for (uint32_t i = end - 3; i < end; i++) desktop.bar[i] = '.';
    uint32_t seconds = desktop.seconds, values[] = {seconds / 3600, seconds / 60 % 60, seconds % 60};
    at = columns - 9;
    for (uint32_t i = 0; i < 3; i++) {
        desktop.bar[at++] = '0' + values[i] / 10; desktop.bar[at++] = '0' + values[i] % 10;
        if (i != 2) desktop.bar[at++] = ':';
    }
    if (t) {
        uint32_t y0 = desktop.position ? t->height - STATUS_HEIGHT : 0;
        for (uint32_t y = 0; y < STATUS_HEIGHT; y++) for (uint32_t x = 0; x < t->width; x++) {
            uint8_t bits = y >= TERM_FONT_TOP && y < TERM_FONT_TOP + TERM_FONT_HEIGHT && x / 8 < columns ?
                t->font[(uint8_t)desktop.bar[x / 8] * TERM_FONT_HEIGHT + y - TERM_FONT_TOP] : 0;
            t->pixels[(y0 + y) * t->pitch + x] = bits & (0x80u >> (x % 8)) ? 0xffffff : 0x333333;
        }
    } else {
        volatile uint16_t *video = (void *)VIDEO_ADDRESS;
        uint32_t row = desktop.position ? 24 : 0;
        for (uint32_t col = 0; col < columns; col++) video[row * 80 + col] = 0x7000 | (uint8_t)desktop.bar[col];
    }
    ui_paint(environment,1); thread_guard_leave();
}
int desktop_settings(env_vars_t *env, uint32_t seconds, uint32_t position, uint32_t color, uint32_t secondary) {
    return variables_settings(env, seconds, position, color, secondary);
}
void desktop_apply(env_vars_t *env, uint32_t seconds, uint32_t position, uint32_t color, uint32_t secondary) {
    desktop_t *d = env->desktop; terminal_t *t = env->terminal;
    if (!d || seconds >= 86400 || position > 1 || color > 255 || secondary > 15 ||
        (color & 15) == (color >> 4) || secondary == (color >> 4)) return;
    ui_paint(env,0);
    if (t) {
        uint32_t old_y = d->position ? t->height - STATUS_HEIGHT : 0;
        for (uint32_t y = old_y; y < old_y + STATUS_HEIGHT; y++)
            for (uint32_t x = 0; x < t->width; x++) t->pixels[y * t->pitch + x] = terminal_color(t, color >> 4);
    } else if (d->position != position) {
        /* Preserve the 24-row text viewport while moving its reserved row. */
        volatile uint16_t *video = (void *)VIDEO_ADDRESS;
        int cursor = get_cursor_position();
        if (position) for (uint32_t i = 0; i < 24 * 80; i++) video[i] = video[i + 80];
        else for (uint32_t i = 24 * 80; i; i--) video[i - 1 + 80] = video[i - 1];
        d->position = position; set_cursor_position(cursor);
    }
    d->position = position; d->seconds = seconds; d->milliseconds = 0; d->color = env->term_color = color;
    d->secondary_color = secondary;
    viewport(d); recolor(env, color);
    d->dirty = 1; desktop_refresh();
}
