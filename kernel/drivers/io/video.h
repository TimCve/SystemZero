#ifndef SYSTEMZERO_VIDEO_H
#define SYSTEMZERO_VIDEO_H
#include <stdint.h>
#define VIDEO_WIDTH 640u
#define VIDEO_HEIGHT 480u
#define VIDEO_FONT_ADDRESS 0x80000u
volatile uint32_t *video_init(void);
#endif
