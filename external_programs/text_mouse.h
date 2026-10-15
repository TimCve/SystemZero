#ifndef SZ_TEXT_MOUSE_H
#define SZ_TEXT_MOUSE_H
#include "../shared/text.h"
#include "../shared/input.h"
/* Returns true for mouse events even outside the viewport. Wheel scrolling
   deliberately leaves the insertion point alone. */
static inline int text_mouse_at(text_buffer_t *b,env_vars_t *env,const keyboard_event_t *event,uint32_t left) {
    if(event->kind!=INPUT_MOUSE) return 0;
    const input_event_t *m=&event->mouse;
    if(m->wheel) text_scroll(b,-m->wheel*3);
    int row,col;
    if(ui_cell(env,m->x,m->y,&row,&col) && row<(int)ROWS-1) {
        uint32_t position=text_hit(b,row,col>(int)left?col-left:0);
        if((m->changed&1) && (m->buttons&1)) {
            b->cursor=b->anchor=position; b->selecting=1; b->selection=0; b->goal_valid=0;
        } else if(b->selecting) { b->cursor=position; b->selection=b->cursor!=b->anchor; b->goal_valid=0; }
    }
    if(!(m->buttons&1)) b->selecting=0;
    return 1;
}
static inline int text_mouse(text_buffer_t *b,env_vars_t *env,const keyboard_event_t *event) {
    return text_mouse_at(b,env,event,0);
}
static inline int text_copy(const text_buffer_t *b,env_vars_t *env) {
    if(!b->selection || b->anchor==b->cursor) return 0;
    uint32_t a=b->anchor<b->cursor?b->anchor:b->cursor,z=b->anchor<b->cursor?b->cursor:b->anchor;
    ui_clipboard_set(env,b->data+a,z-a); return 1;
}
static inline int text_paste(text_buffer_t *b,env_vars_t *env) {
    input_runtime_t *u=ui(env);
    return u && u->clipboard_length && text_replace(b,u->clipboard,u->clipboard_length);
}
#endif
