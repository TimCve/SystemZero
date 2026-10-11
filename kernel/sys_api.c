#include "sys_api.h"
#include "kernel.h"
#include "drivers/io/screen.h"
#include "drivers/io/graphics.h"
#include "drivers/io/video.h"
#include "drivers/io/keyboard.h"
#include "drivers/disk/fs.h"
#include "drivers/disk/ata.h"
#include "drivers/utils/mem.h"
#include "cpu/timer.h"
#include "libc/strings.h"
#include <stddef.h>
#include "cpu/thread.h"
#include "desktop.h"
_Static_assert(offsetof(env_vars_t, api) == SZ_ENV_API_OFFSET, "environment ABI");
_Static_assert(offsetof(systemzero_api_t, functions) == SZ_API_FUNCTIONS_OFFSET, "service ABI");

static void enter(env_vars_t *env) {
    fs_bind_env(env); screen_bind_env(env); select_drive(env->selected_drive);
    if (env->free_mem_ptr > get_free_ptr()) set_free_ptr(env->free_mem_ptr);
}
#define SERVICE(name) \
    static int32_t service_##name(env_vars_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); \
    static int32_t api_##name(env_vars_t *env, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { \
        thread_guard_enter(); int32_t result = service_##name(env, a, b, c, d, e); \
        thread_guard_leave(); return result; \
    } \
    static int32_t service_##name(env_vars_t *env, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
#define PTR(value) ((void *)(uintptr_t)(value))
SERVICE(print) { enter(env); if (!a) return -1; print(PTR(a)); return 0; }
SERVICE(print_dec) {
    enter(env); int32_t value = (int32_t)a;
    if (value < 0) print_char('-');
    /* Handle INT_MIN without signed overflow. */
    uint32_t n = value < 0 ? 0u - a : a;
    char digits[10]; uint32_t used = 0;
    do { digits[used++] = '0' + n % 10; n /= 10; } while (n);
    screen_begin_update(); while (used) print_char(digits[--used]); screen_end_update(); return 0;
}
SERVICE(print_hex) {
    enter(env); char digits[8]; uint32_t used = 0;
    do { digits[used++] = "0123456789ABCDEF"[a & 15]; a >>= 4; } while (a);
    screen_begin_update(); while (used) print_char(digits[--used]); screen_end_update(); return 0;
}
SERVICE(putchar) { enter(env); print_char(a); return a & 255; }
SERVICE(newline) { enter(env); print_newline(); return 0; }
SERVICE(clear) { enter(env); clear(); return 0; }
SERVICE(set_color) { enter(env); if (a > 255) return -1; env->term_color = a; set_term_color(a); return 0; }
SERVICE(get_color) { enter(env); return get_term_color(); }
SERVICE(screen_rows) { enter(env); return screen_rows(); }
SERVICE(screen_columns) { enter(env); return screen_columns(); }
SERVICE(put_text) {
    enter(env); if (a >= ROWS || b >= COLS || !c || d > 255) return -1;
    screen_put_text(a, b, PTR(c), d); return 0;
}
SERVICE(put_cell) {
    enter(env); if (a >= ROWS || b >= COLS || c > 255 || d > 255) return -1;
    screen_put_cell(a, b, c, d); return 0;
}
SERVICE(begin_update) { enter(env); screen_begin_update(); return 0; }
SERVICE(end_update) { enter(env); screen_end_update(); return 0; }
SERVICE(draw_pixel) { enter(env); if (c > 15) return -1; return screen_draw_pixel(a, b, c) ? -1 : 0; }
SERVICE(draw_line) {
    enter(env);
    if (!env->terminal || a >= VIDEO_WIDTH || c >= VIDEO_WIDTH ||
        b >= VIDEO_HEIGHT || d >= VIDEO_HEIGHT || e > 15) return -1;
    terminal_t *t = env->terminal;
    if (b < t->reserved_top || d < t->reserved_top ||
        b >= t->height - t->reserved_bottom || d >= t->height - t->reserved_bottom) return -1;
    draw_line(a, b, c, d, e); return 0;
}
SERVICE(alloc) {
    enter(env); uint32_t address = malloc(a, 0, 0);
    env->free_mem_ptr = get_free_ptr(); return address;
}
SERVICE(memcpy) {
    enter(env); if (((!a || !b) && c) || c > HEAP_END - HEAP_START) return 0;
    return (uint32_t)(uintptr_t)memcpy(PTR(a), PTR(b), c);
}
SERVICE(memset) {
    enter(env); if ((!a && c) || c > HEAP_END - HEAP_START) return 0;
    return (uint32_t)(uintptr_t)memset(PTR(a), b, c);
}
SERVICE(strlen) { enter(env); return a ? strlen(PTR(a)) : -1; }
SERVICE(readline) {
    if (thread_id() != 0) return -1;
    enter(env); if (!a || b < 2 || b > 65536) return -1;
    if (!kbd_readline(PTR(a), 0, b)) return -1;
    return strlen(PTR(a));
}
SERVICE(getchar) {
    if (thread_id() != 0) return -1;
    enter(env); keyboard_state_t state = {0}; keyboard_event_t event;
    for (;;) {
        kbd_wait_event(&state, &event);
        if (event.code == ESC) return 27;
        if (event.code == ENTER) return '\n';
        if (event.character) return event.character;
        thread_sleep(1);
    }
}
SERVICE(file_size) { enter(env); if (!a) return -1; inode_t in = get_file_info(PTR(a)); return in.valid ? (int32_t)in.size : -1; }
SERVICE(file_read) { enter(env); if (!a || c > ESFS_MAX_FILE) return -1; return file_read_bytes(PTR(a), PTR(b), c, d); }
SERVICE(file_write) {
    enter(env); if (!a || (!b && c) || c > ESFS_MAX_FILE) return -1;
    return file_replace(PTR(a), PTR(b), c) ? 0 : -1;
}
SERVICE(file_delete) { enter(env); return a && !file_delete(PTR(a), env) ? 0 : -1; }
SERVICE(file_create) { enter(env); return a && !file_create(PTR(a), env) ? 0 : -1; }
SERVICE(sleep) {
    enter(env); if (a > 60000) return -1;
    return thread_sleep(a);
}
SERVICE(execute) {
    if (thread_id() != 0) return 2;
    enter(env); if (!a) return 2;
    char name[512], *args = PTR(a); uint32_t length = 0;
    while (args[length] && args[length] != ' ') {
        if (length == sizeof name - 1) return 2;
        name[length] = args[length]; length++;
    }
    name[length] = 0; if (!length) return 2;
    return execute(name, args, env);
}
SERVICE(thread_start) { enter(env); return thread_start(env, (thread_entry_t)(uintptr_t)a, b); }
SERVICE(thread_join) { enter(env); return thread_join((int)a); }
SERVICE(thread_yield) { thread_yield(); return 0; }
SERVICE(thread_sleep) { return thread_sleep(a); }
SERVICE(thread_id) { return thread_id(); }
SERVICE(thread_count) { return thread_count(); }
SERVICE(clock_time) { return env->desktop ? (int32_t)env->desktop->seconds : -1; }
SERVICE(status_position) { return env->desktop ? env->desktop->position : -1; }
SERVICE(settings_apply) { enter(env); return thread_id() == 0 ? desktop_settings(env, a, b, c) : -1; }
SERVICE(mutex_create) { return thread_mutex_create(); }
SERVICE(mutex_lock) { return thread_mutex_lock((int)a); }
SERVICE(mutex_unlock) { return thread_mutex_unlock((int)a); }
void sys_api_init(env_vars_t *env) {
    static const systemzero_api_t api = {
        .magic = SZ_API_MAGIC, .version = SZ_API_VERSION, .count = SZ_API_COUNT,
        .functions = {
#define SZ_SERVICE(id, name, result, argc, a, b, c, d, e) [id] = api_##name,
#include "../shared/sys_api.def"
#undef SZ_SERVICE
        }
    };
    env->api = &api;
}
