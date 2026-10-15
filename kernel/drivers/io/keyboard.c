#include "keyboard.h"
#include <stdint.h>
#include "../../env_vars.h"
static env_vars_t *environment;
void keyboard_bind_env(env_vars_t *env) { environment=env; }
char get_printable_char(char keycode, int i, uint8_t shift) {
    const kbd_keycode *table = shift ? shft_keycode_table : std_keycode_table;
    for (; i >= 0 && table[i].keycode; i++) if ((uint8_t)keycode == (uint8_t)table[i].keycode) return table[i].chr;
    return 0;
}
char get_input_keycode(void) {
    if(environment && environment->keyboard_poll) {
        return environment->keyboard_poll();
    }
    return (port_byte_in(0x64) & 1) ? port_byte_in(0x60) : 0;
}
int kbd_decode(keyboard_state_t *state, uint8_t raw, keyboard_event_t *event) {
    if (!raw) return 0;
    if (state->pause) { state->pause--; return 0; }
    if (raw == 0xe1) { state->pause = 5; return 0; }
    if (raw == 0xe0) { state->extended = 1; return 0; }
    int extended = state->extended, release = raw & 0x80;
    uint8_t code = raw & 0x7f; state->extended = 0;
    if (!extended && (code == 0x2a || code == 0x36)) {
        uint8_t bit = code == 0x2a ? 1 : 2;
        if (release) state->shift &= ~bit; else state->shift |= bit;
        return 0;
    }
    if (code == CTRL) {
        uint8_t bit = extended ? 2 : 1;
        if (release) state->control &= ~bit; else state->control |= bit;
        return 0;
    }
    if (release || (extended && (code == 0x2a || code == 0x36))) return 0;
    *event = (keyboard_event_t){.code=code,.character=get_printable_char(code,0,state->shift),
                              .shift=!!state->shift,.control=!!state->control};
    if (code == TAB) event->character = '\t';
    return 1;
}
void kbd_wait_event(keyboard_state_t *state, keyboard_event_t *event) {
    input_runtime_t *u=ui(environment);
    for(;;) {
        if(u && u->next) {
            input_event_t raw;
            if(!u->next(&raw)) { screen_idle(); continue; }
            if(raw.kind==INPUT_MOUSE) {
                *event=(keyboard_event_t){.kind=INPUT_MOUSE,.mouse=raw,.shift=!!state->shift,.control=!!state->control};
                return;
            }
            if(!kbd_decode(state,raw.code,event)) continue;
            int scrolling=environment->terminal?((terminal_t *)environment->terminal)->scrolling:u->scrolling;
            if(scrolling && event->control && event->code==0x2e && u->copy()) continue;
            if(scrolling && event->shift && (event->code==PGUP || event->code==PGDN)) {
                u->scroll(event->code==PGUP?(int)ROWS-1:1-(int)ROWS); continue;
            }
            if(scrolling) ui_live(environment);
            return;
        }
        if(kbd_decode(state,(uint8_t)get_input_keycode(),event)) return;
        screen_idle();
    }
}
static void readline_draw(const char *buffer, int length, int cursor, int *origin,
                          int previous_length) {
    int columns=COLS, cells=columns*ROWS, old_origin=*origin;
    screen_begin_update();
    while(*origin+cursor>=cells) {
        set_cursor_position(cells*2); scroll_terminal();
        *origin-=columns; old_origin-=columns;
    }
    /* Very long input can scroll its beginning away. Moving back through it
       restores the visible prefix without changing the actual command. */
    while(*origin+cursor<0) *origin+=columns;
    int first=old_origin<*origin?old_origin:*origin;
    int last=old_origin+previous_length>*origin+length?old_origin+previous_length:*origin+length;
    if(first<0) first=0;
    if(last>=cells) last=cells-1;
    uint8_t color=get_term_color();
    for(int cell=first;cell<=last;cell++) {
        int index=cell-*origin;
        screen_put_cell(cell/columns,cell%columns,index>=0 && index<length?buffer[index]:' ',color);
    }
    for(int row=0;row<(int)ROWS;row++) {
        int edge=(row+1)*columns;
        if(row*columns<=last && edge>first) screen_line_wrapped(row,edge>*origin && edge<*origin+length);
    }
    set_cursor_position((*origin+cursor)*2); screen_end_update();
}
int kbd_readline_complete(char *buffer,int capacity,kbd_recall_fn recall,kbd_complete_fn complete,void *context) {
    if (!buffer || capacity < 1) return 0;
    int length = 0, cursor = 0, repeated=0, origin=get_cursor_position()/2;
    keyboard_state_t state = {0};
    buffer[0] = 0;
    for (;;) {
        keyboard_event_t event; kbd_wait_event(&state, &event);
        if(event.kind==INPUT_MOUSE) {
            repeated=0;
            input_runtime_t *u=ui(environment); int row,col;
            if(!event.mouse.wheel && (event.mouse.buttons&1) && u && !u->console.offset &&
               ui_cell(environment,event.mouse.x,event.mouse.y,&row,&col)) {
                int position=row*(int)COLS+col-origin;
                if(position>=0 && position<=length) { cursor=position; set_cursor_position((origin+cursor)*2); }
            }
            continue;
        }
        uint8_t key = event.code;
        if(key==TAB && !event.control && complete) {
            int previous_length=length;
            if(complete(context,buffer,capacity,&cursor,repeated,event.shift)) {
                length=0; while(buffer[length] && length<capacity-1) length++;
                buffer[length]=0; if(cursor<0) cursor=0; if(cursor>length) cursor=length;
                ui_selection_clear(environment); readline_draw(buffer,length,cursor,&origin,previous_length);
            }
            repeated=1; continue;
        }
        repeated=0;
        if (key == ESC) { readline_draw(buffer,length,length,&origin,length); return 0; }
        if (key == ENTER) { readline_draw(buffer,length,length,&origin,length); print_newline(); return 1; }
        int previous_length=length;
        input_runtime_t *u=ui(environment); uint32_t start,end;
        if(event.control && key==0x2f && (!u || !u->clipboard_length)) continue;
        int editing=key==BACKSPACE || key==DELETE || (!event.control && event.character) || (event.control && key==0x2f);
        if(editing && u && u->range(origin,length,&start,&end)) {
            for(int i=end;i<=length;i++) buffer[start+i-end]=buffer[i];
            length-=end-start; cursor=start;
            if(key==BACKSPACE || key==DELETE) { ui_selection_clear(environment); readline_draw(buffer,length,cursor,&origin,previous_length); continue; }
        }
        if(event.control && key==0x2f) {
            if(!u) continue;
            uint32_t count=u->clipboard_length;
            if(count>(uint32_t)(capacity-1-length)) count=capacity-1-length;
            for(int i=length;i>=cursor;i--) buffer[i+count]=buffer[i];
            for(uint32_t i=0;i<count;i++) {
                uint8_t ch=u->clipboard[i]; buffer[cursor++]=ch<32 || ch==127?' ':ch;
            }
            length+=count; ui_selection_clear(environment);
            readline_draw(buffer,length,cursor,&origin,previous_length); continue;
        }
        if ((key == U_ARROW || key == D_ARROW) && recall) {
            const char *line=recall(context,key==U_ARROW?-1:1,buffer);
            if(!line) continue;
            length=0; while(line[length] && length<capacity-1) { buffer[length]=line[length]; length++; }
            buffer[length]=0; cursor=length;
        } else if (key == L_ARROW || key == R_ARROW || key == HOME || key == END) {
            if(key==L_ARROW && cursor) cursor--;
            if(key==R_ARROW && cursor<length) cursor++;
            if(key==HOME) cursor=0;
            if(key==END) cursor=length;
        } else if(key==BACKSPACE || key==DELETE) {
            if(key==BACKSPACE) { if(!cursor) continue; cursor--; }
            else if(cursor==length) continue;
            length--;
            for(int i=cursor;i<=length;i++) buffer[i]=buffer[i+1];
        } else {
            char ch = event.character;
            if(!ch || ch=='\t' || event.control || length>=capacity-1) continue;
            for (int i = length + 1; i > cursor; i--) buffer[i] = buffer[i - 1];
            buffer[cursor++] = ch; length++;
        }
        ui_selection_clear(environment);
        readline_draw(buffer,length,cursor,&origin,previous_length);
    }
}
int kbd_readline_recall(char *buffer,int capacity,kbd_recall_fn recall,void *context) {
    return kbd_readline_complete(buffer,capacity,recall,0,context);
}
int kbd_readline(char *buffer, int calibration, int capacity) {
    (void)calibration; return kbd_readline_recall(buffer,capacity,0,0);
}
