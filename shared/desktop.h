#ifndef SYSTEMZERO_DESKTOP_H
#define SYSTEMZERO_DESKTOP_H
#include <stdint.h>
#define STATUS_HEIGHT 12u
#define STATUS_COMMAND 2000u
#define SETTINGS_FILE "systemzero.cfg"
/* Shared display state; the status bar owns the framebuffer margin. */
typedef struct desktop {
    volatile uint32_t seconds, milliseconds, dirty;
    uint8_t position, color, enabled, reserved;
    char command[STATUS_COMMAND];
    void (*refresh)(void);
    char bar[101];
} desktop_t;
static inline int desktop_pixel_reserved(const desktop_t *d, uint32_t height, uint32_t y) {
    return d && d->enabled && (d->position ? y >= height - STATUS_HEIGHT : y < STATUS_HEIGHT);
}
#endif
