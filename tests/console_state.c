#include "../kernel/kernel.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/utils/mem.h"
#include "../shared/desktop.h"

static int equal(const void *left, const void *right, uint32_t size) {
    const uint8_t *a = left, *b = right;
    for (uint32_t i = 0; i < size; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int main(env_vars_t *env, char *args) {
    (void)args;
    terminal_t *terminal = env->terminal;
    uint32_t cells_size = ROWS * COLS * 2;
    uint8_t *vga = (void *)(VIDEO_ADDRESS + (env->desktop && !env->desktop->position ? 160 : 0));
    uint8_t *cells = (void *)malloc(cells_size, 0, 0);
    uint32_t pixels_size = terminal ? terminal->pitch * terminal->height * 4 : 0;
    uint8_t *pixels = pixels_size ? (void *)malloc(pixels_size, 0, 0) : 0;
    if (!cells || (pixels_size && !pixels)) return 1;
    screen_put_text(1, 0, "Preserve console text and colors", 0x2e);
    screen_draw_pixel(10, 30, 14); screen_draw_pixel(50, 50, 13);
    const char *programs[] = {"edit", "hexedit", "view"};
    const char *commands[] = {"edit editing", "hexedit editing", "view multiline"};
    for (uint32_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++) {
        set_cursor_position((COLS * 3 + 12) * 2);
        int cursor = get_cursor_position();
        uint8_t color = get_term_color();
        memcpy(cells, terminal ? (void *)terminal->cells : vga, cells_size);
        if (terminal) memcpy(pixels, (void *)terminal->pixels, pixels_size);
        if (execute((char *)programs[i], (char *)commands[i], env) ||
            get_cursor_position() != cursor || get_term_color() != color ||
            !equal(cells, terminal ? (void *)terminal->cells : vga, cells_size) ||
            (terminal && (!equal(pixels + terminal->reserved_top * terminal->pitch * 4,
                          (uint8_t *)terminal->pixels + terminal->reserved_top * terminal->pitch * 4,
                          (terminal->height - terminal->reserved_top - terminal->reserved_bottom) * terminal->pitch * 4) ||
                          terminal->updates || !terminal->scrolling || !terminal->cursor_visible))) {
            print("console-restore-failed"); print_newline(); return 1;
        }
    }
    print("console-restore-ok"); print_newline(); return 0;
}
