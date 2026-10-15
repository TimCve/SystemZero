#ifndef SYSTEMZERO_INPUT_H
#define SYSTEMZERO_INPUT_H
#include <stdint.h>
#include "terminal.h"
#include "desktop.h"
#include "../kernel/env_vars.h"
#define UI_CLIPBOARD 65536u
#define UI_HISTORY 2048u
#define INPUT_KEY 0u
#define INPUT_MOUSE 1u
typedef struct {
    int32_t x,y,wheel;
    uint8_t kind,code,buttons,changed;
} input_event_t;
typedef struct {
    terminal_cell_t lines[UI_HISTORY][TERM_COLS];
    uint8_t wraps[UI_HISTORY];
    terminal_cell_t vga[80*25]; uint8_t vga_wraps[25];
    uint32_t head,count,total,offset,anchor,extent;
    uint8_t selecting,selected;
} console_history_t;
typedef struct input_runtime {
    int32_t x,y; uint8_t buttons,available; uint16_t reserved;
    uint32_t clipboard_length;
    int (*next)(input_event_t *);
    void (*paint)(int);
    void (*scroll)(int);
    void (*mouse)(const input_event_t *);
    int (*copy)(void);
    int (*range)(int,int,uint32_t *,uint32_t *);
    void (*save)(const terminal_cell_t *,uint8_t);
    uint8_t clipboard[UI_CLIPBOARD];
    console_history_t console;
    uint32_t vga_updates; int scrolling;
    /* Logical text batches leave the displayed canvas intact. Only direct
       canvas changes and snapshots suspend the pointer's saved backing pixels. */
    uint32_t canvas_updates;
} input_runtime_t;
static inline input_runtime_t *ui(env_vars_t *env) { return env?env->input:0; }
static inline void ui_paint(env_vars_t *env,int show) { input_runtime_t *u=ui(env); if(u && u->paint) u->paint(show); }
static inline void ui_live(env_vars_t *env) { input_runtime_t *u=ui(env); if(u && u->console.offset) u->scroll(-2147483647); }
static inline void ui_selection_clear(env_vars_t *env) {
    input_runtime_t *u=ui(env); if(u) { u->console.selected=u->console.selecting=0; }
}
static inline uint32_t ui_clipboard_set(env_vars_t *env,const uint8_t *bytes,uint32_t count) {
    input_runtime_t *u=ui(env); if(!u) return 0;
    if(count>UI_CLIPBOARD) count=UI_CLIPBOARD;
    for(uint32_t i=0;i<count;i++) u->clipboard[i]=bytes[i];
    u->clipboard_length=count; return count;
}
/* Pixel coordinates map to insertion positions, including the right edge of
   the final cell. Status-bar and footer filtering belongs to the caller. */
static inline int ui_cell(env_vars_t *env,int x,int y,int *row,int *column) {
    terminal_t *t=env?env->terminal:0;
    int ox=t?(int)t->origin_x:0,oy=t?(int)t->origin_y:0;
    int width=t?TERM_COLS:80,height=t?TERM_ROWS:24,cell_height=t?TERM_CELL_HEIGHT:16;
    if(!t && env && env->desktop) {
        /* VGA reserves one character row for the status bar. */
        oy=env->desktop->enabled && !env->desktop->position?16:0;
    }
    if(x<ox || y<oy || x>=ox+width*8 || y>=oy+height*cell_height) return 0;
    *row=(y-oy)/cell_height; *column=(x-ox+4)/8;
    if(*column>width) *column=width;
    return 1;
}
#endif
