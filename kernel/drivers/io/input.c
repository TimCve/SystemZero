#include "input.h"
#include "screen.h"
#include "usb_keyboard.h"
#include "../utils/mem.h"
#include "../utils/ports.h"
#include "../../cpu/timer.h"
#include "../../cpu/thread.h"
#include "../../../shared/desktop.h"

static env_vars_t *environment;
static input_runtime_t *runtime;
static input_event_t events[512];
static uint32_t head,tail;
static int polling,view_dirty;
static uint8_t packet[4],packet_at,packet_bytes=3;
static uint32_t packet_tick;
static int ps2,drawn,old_x,old_y;
static uint32_t under[12*18];
static uint16_t under_vga;
static uint8_t history_cursor_hidden,cursor_start;
static void console_mouse(const input_event_t *);
static uint32_t irq_save(void) {
    uint32_t flags=0;
#ifdef SYSTEMZERO_GUEST
    __asm__ volatile("pushfl; popl %0; cli":"=r"(flags)::"memory");
#endif
    return flags;
}
static void irq_restore(uint32_t flags) {
#ifdef SYSTEMZERO_GUEST
    __asm__ volatile("pushl %0; popfl"::"r"(flags):"memory");
#else
    (void)flags;
#endif
}
static uint32_t columns(void) { return environment->terminal?TERM_COLS:80; }
static uint32_t rows(void) { return environment->terminal?TERM_ROWS:24; }
static int top(void) { return environment->desktop && environment->desktop->enabled && !environment->desktop->position; }
static int console_enabled(void) {
    terminal_t *t=environment->terminal;
    return t?t->scrolling:runtime->scrolling;
}
static void enqueue(input_event_t event) {
    if(event.kind==INPUT_MOUSE && !event.changed && !event.wheel && head!=tail) {
        uint32_t last=(tail+511)%512;
        if(events[last].kind==INPUT_MOUSE && !events[last].changed && !events[last].wheel && events[last].buttons==event.buttons) {
            events[last]=event; return;
        }
    }
    uint32_t next=(tail+1)%512;
    if(next==head) {
        /* Motion may be coalesced, but keep button edges and key transitions. */
        if(event.kind==INPUT_MOUSE && !event.changed) return;
        head=(head+1)%512;
    }
    events[tail]=event; tail=next;
}
void input_feed_key(uint8_t code) { if(runtime) enqueue((input_event_t){.kind=INPUT_KEY,.code=code}); }
void input_feed_mouse(int dx,int dy,int wheel,uint8_t buttons) {
    if(!runtime) return;
    terminal_t *t=environment->terminal;
    int width=t?(int)t->width:640,height=t?(int)t->height:400;
    int x=runtime->x+dx,y=runtime->y+dy;
    if(x<0) x=0;
    if(x>=width) x=width-1;
    if(y<0) y=0;
    if(y>=height) y=height-1;
    uint8_t changed=runtime->buttons^buttons;
    if(x==runtime->x && y==runtime->y && !changed && !wheel) return;
    runtime->x=x; runtime->y=y; runtime->buttons=buttons; runtime->available=1;
    input_event_t event={x,y,wheel,INPUT_MOUSE,0,buttons,changed};
    if(console_enabled()) console_mouse(&event);
    enqueue(event);
}
static terminal_cell_t raw_cell(uint32_t absolute,uint32_t column) {
    console_history_t *c=&runtime->console; uint32_t cols=columns();
    if(absolute>=c->total) {
        uint32_t row=absolute-c->total;
        if(row<rows()) {
            terminal_t *t=environment->terminal;
            return t?t->cells[row*cols+column]:c->vga[row*cols+column];
        }
    } else if(absolute>=c->total-c->count) {
        uint32_t index=(c->head+UI_HISTORY-(c->total-absolute))%UI_HISTORY;
        return c->lines[index][column];
    }
    return (terminal_cell_t){' ',environment->term_color};
}
static uint8_t row_wrap(uint32_t absolute) {
    console_history_t *c=&runtime->console;
    if(absolute>=c->total) {
        uint32_t row=absolute-c->total; terminal_t *t=environment->terminal;
        return row<rows()?(t?t->wraps[row]:c->vga_wraps[row]):0;
    }
    if(absolute<c->total-c->count) return 0;
    return c->wraps[(c->head+UI_HISTORY-(c->total-absolute))%UI_HISTORY];
}
static terminal_cell_t view_cell(void *context,uint32_t index) {
    (void)context; terminal_t *t=environment->terminal;
    uint32_t cols=columns(); console_history_t *c=&runtime->console;
    if(!console_enabled()) return t?t->cells[index]:c->vga[index];
    if(!c->offset && !c->selected) return t?t->cells[index]:c->vga[index];
    uint32_t row=c->total-c->offset+index/cols,absolute=row*cols+index%cols;
    terminal_cell_t cell=raw_cell(row,index%cols);
    uint32_t a=c->anchor<c->extent?c->anchor:c->extent,b=c->anchor<c->extent?c->extent:c->anchor;
    if(c->selected && absolute>=a && absolute<b) cell.color=0x70;
    return cell;
}
static uint32_t view_cursor(void *context,uint32_t cursor) {
    (void)context; return console_enabled() && runtime->console.offset?TERM_NO_CURSOR:cursor;
}
static void pointer_hide(void) {
    if(!drawn) return;
    terminal_t *t=environment->terminal;
    if(t) {
        for(int y=0;y<18 && old_y+y<(int)t->height;y++)
            for(int x=0;x<12 && old_x+x<(int)t->width;x++)
                t->pixels[(old_y+y)*t->pitch+old_x+x]=under[y*12+x];
    } else ((volatile uint16_t *)VIDEO_ADDRESS)[old_y/16*80+old_x/8]=under_vga;
    drawn=0;
}
static void pointer_show(void) {
    if(!runtime->available) return;
    terminal_t *t=environment->terminal; old_x=runtime->x; old_y=runtime->y;
    if(t) {
        for(int y=0;y<18 && old_y+y<(int)t->height;y++)
            for(int x=0;x<12 && old_x+x<(int)t->width;x++) {
                uint32_t index=(old_y+y)*t->pitch+old_x+x;
                under[y*12+x]=t->pixels[index];
                int shape=y<13?x<=y/2:(x>=3 && x<=5 && y<17);
                if(shape) t->pixels[index]=(x==0 || x==y/2 || y==0)?0:0xffffff;
            }
    } else {
        volatile uint16_t *cell=(volatile uint16_t *)VIDEO_ADDRESS+old_y/16*80+old_x/8;
        under_vga=*cell; *cell=(*cell&255)|0x7000;
    }
    drawn=1;
}
static void paint(int show) {
    uint32_t flags=irq_save(); pointer_hide();
    if(show && !runtime->canvas_updates) {
        if(!environment->terminal && !runtime->vga_updates) {
            int hide=console_enabled() && runtime->console.offset;
            if(hide && !history_cursor_hidden) {
                port_byte_out(REG_SCREEN_CTRL,10); cursor_start=port_byte_in(REG_SCREEN_DATA);
                port_byte_out(REG_SCREEN_DATA,0x20); history_cursor_hidden=1;
            } else if(!hide && history_cursor_hidden) {
                port_byte_out(REG_SCREEN_CTRL,10); port_byte_out(REG_SCREEN_DATA,cursor_start); history_cursor_hidden=0;
            }
            volatile uint16_t *video=(void *)VIDEO_ADDRESS;
            for(uint32_t i=0;i<rows()*80;i++) {
                terminal_cell_t cell=view_cell(0,i); video[top()*80+i]=cell.ch|((uint16_t)cell.color<<8);
            }
        }
        pointer_show();
    }
    irq_restore(flags);
}
static void before_present(void *context) {
    (void)context; thread_guard_enter();
    uint32_t flags=irq_save(); runtime->canvas_updates++; pointer_hide(); irq_restore(flags);
}
static void after_present(void *context) {
    (void)context; uint32_t flags=irq_save();
    runtime->canvas_updates--;
    if(!runtime->canvas_updates) pointer_show();
    irq_restore(flags); thread_guard_leave();
}
static void refresh(void) {
    view_dirty=1;
    /* Finish draining hardware before rasterizing selection. A repaint per
       packet can keep interrupts disabled for the entire drag. */
    terminal_t *t=environment->terminal;
    if(polling || (t && (t->updates || t->presenting)) || runtime->canvas_updates) return;
    view_dirty=0;
    if(t) terminal_present(t); else paint(1);
}
void input_save_row(const terminal_cell_t *line,uint8_t wrap) {
    if(!runtime || !console_enabled()) return;
    console_history_t *c=&runtime->console;
    if(c->total>UINT32_MAX/TERM_COLS-UI_HISTORY-TERM_ROWS) {
        c->head=c->count=c->total=c->offset=0; c->selected=c->selecting=0;
    }
    for(uint32_t i=0;i<columns();i++) c->lines[c->head][i]=line[i];
    c->wraps[c->head]=wrap; c->head=(c->head+1)%UI_HISTORY; c->total++;
    if(c->count<UI_HISTORY) c->count++;
    if(c->offset && c->offset<c->count) c->offset++;
    if(c->selected && (c->anchor/columns()<c->total-c->count || c->extent/columns()<c->total-c->count))
        c->selected=c->selecting=0;
}
static void save_row(void *context,const terminal_cell_t *line,uint8_t wrap) { (void)context; input_save_row(line,wrap); }
static void scroll_view(int amount) {
    console_history_t *c=&runtime->console;
    if(amount>0) { uint32_t add=amount; c->offset+=add>c->count-c->offset?c->count-c->offset:add; }
    else { uint32_t sub=0u-(uint32_t)amount; c->offset-=sub>c->offset?c->offset:sub; }
    refresh();
}
static void console_mouse(const input_event_t *event) {
    console_history_t *c=&runtime->console;
    if(event->wheel) scroll_view(event->wheel*3);
    int row,col;
    if(!ui_cell(environment,event->x,event->y,&row,&col)) { if(!(event->buttons&1)) c->selecting=0; return; }
    uint32_t position=(c->total-c->offset+row)*columns()+col;
    if((event->changed&1) && (event->buttons&1)) {
        c->anchor=c->extent=position; c->selecting=1; c->selected=0;
    } else if(c->selecting) {
        c->extent=position; c->selected=c->anchor!=c->extent;
        if(!(event->buttons&1)) c->selecting=0;
    } else return;
    refresh();
}
static int copy_selection(void) {
    console_history_t *c=&runtime->console; if(!c->selected) return 0;
    uint32_t a=c->anchor<c->extent?c->anchor:c->extent,b=c->anchor<c->extent?c->extent:c->anchor;
    uint32_t cols=columns(),count=0;
    for(uint32_t row=a/cols;row<=(b-1)/cols && count<UI_CLIPBOARD;row++) {
        uint32_t first=row==a/cols?a%cols:0,end=row==(b-1)/cols?(b-1)%cols+1:cols;
        uint32_t last=end;
        if(end==cols && !row_wrap(row)) while(last>first && (raw_cell(row,last-1).ch==' ' || !raw_cell(row,last-1).ch)) last--;
        for(uint32_t col=first;col<last && count<UI_CLIPBOARD;col++) {
            uint8_t ch=raw_cell(row,col).ch; runtime->clipboard[count++]=ch?ch:' ';
        }
        if(end==cols && !row_wrap(row) && count<UI_CLIPBOARD) runtime->clipboard[count++]='\n';
    }
    runtime->clipboard_length=count; return 1;
}
static int selection_range(int origin,int length,uint32_t *start,uint32_t *end) {
    console_history_t *c=&runtime->console; if(!c->selected || c->offset) return 0;
    uint32_t base=c->total*columns()+origin;
    uint32_t a=c->anchor<c->extent?c->anchor:c->extent,b=c->anchor<c->extent?c->extent:c->anchor;
    if(a<base || b>base+(uint32_t)length) return 0;
    *start=a-base; *end=b-base; return 1;
}
static void ps2_byte(uint8_t byte) {
    uint32_t now=get_tick();
    if(packet_at && now-packet_tick>100) packet_at=0;
    packet_tick=now;
    if(!packet_at && !(byte&8)) return;
    packet[packet_at++]=byte; if(packet_at<packet_bytes) return; packet_at=0;
    int dx=packet[1]-((packet[0]&16)?256:0),dy=packet[2]-((packet[0]&32)?256:0),wheel=0;
    if(packet_bytes==4) { wheel=packet[3]&15; if(wheel&8) wheel-=16; wheel=-wheel; }
    if(packet[0]&0xc0) dx=dy=0;
    input_feed_mouse(dx,-dy,wheel,packet[0]&7);
}
static int wait_write(void) {
    for(uint32_t i=0;i<100000;i++) { uint8_t status=port_byte_in(0x64); if(status==255) return 0; if(!(status&2)) return 1; }
    return 0;
}
static int controller(uint8_t value) { if(!wait_write()) return 0; port_byte_out(0x64,value); return 1; }
static int data(uint8_t value) { if(!wait_write()) return 0; port_byte_out(0x60,value); return 1; }
static int mouse_read(void) {
    for(uint32_t i=0;i<100000;i++) {
        uint8_t status=port_byte_in(0x64);
        if(status==255) return -1;
        if(status&1) { uint8_t byte=port_byte_in(0x60); if(status&32) return byte; input_feed_key(byte); }
    }
    return -1;
}
static int mouse_command(uint8_t value) {
    for(int i=0;i<3;i++) {
        if(!controller(0xd4) || !data(value)) return 0;
        int result=mouse_read(); if(result==0xfa) return 1; if(result!=0xfe) return 0;
    }
    return 0;
}
static void ps2_init(void) {
    if(port_byte_in(0x64)==255) return;
    for(int i=0;i<64 && (port_byte_in(0x64)&1);i++) port_byte_in(0x60);
    if(!controller(0xa8) || !controller(0x20)) return;
    int config=-1;
    for(uint32_t i=0;i<100000;i++) if(port_byte_in(0x64)&1) { config=port_byte_in(0x60); break; }
    if(config<0 || !controller(0x60) || !data((config&~0x33)|0x40)) return;
    if(!mouse_command(0xf6)) return;
    if(mouse_command(0xf3) && mouse_command(200) && mouse_command(0xf3) && mouse_command(100) &&
       mouse_command(0xf3) && mouse_command(80) && mouse_command(0xf2)) {
        int id=mouse_read(); if(id==3 || id==4) packet_bytes=4;
    }
    ps2=mouse_command(0xf4);
}
static void service(void) {
    uint32_t flags=irq_save(); polling=1;
    for(uint32_t i=0;i<512;i++) {
        uint8_t status=port_byte_in(0x64); if(status==255 || !(status&1)) break;
        uint8_t byte=port_byte_in(0x60);
        if(status&0xc0) { packet_at=0; continue; }
        if(status&32) { if(ps2) ps2_byte(byte); } else input_feed_key(byte);
    }
    usb_hid_poll(); polling=0; irq_restore(flags);
    if(view_dirty) refresh();
}
static int next_event(input_event_t *event) {
    service(); if(head==tail) return 0;
    uint32_t flags=irq_save(); *event=events[head]; head=(head+1)%512; irq_restore(flags); return 1;
}
static int legacy_key(void) {
    input_event_t event; while(next_event(&event)) if(event.kind==INPUT_KEY) return event.code; return 0;
}
void input_tick(void) { if(runtime) { service(); paint(1); } }
void input_init(env_vars_t *env) {
    environment=env; runtime=(void *)malloc(sizeof *runtime,0,0); if(!runtime) return;
    memset(runtime,0,sizeof *runtime); env->input=runtime;
    runtime->scrolling=1; runtime->x=320; runtime->y=200; runtime->next=next_event; runtime->paint=paint;
    runtime->scroll=scroll_view; runtime->mouse=console_mouse; runtime->copy=copy_selection; runtime->range=selection_range; runtime->save=input_save_row;
    terminal_t *t=env->terminal;
    if(t) {
        t->overlay=runtime; t->before_present=before_present; t->after_present=after_present;
        t->view_cell=view_cell; t->view_cursor=view_cursor; t->save_row=save_row;
    } else {
        volatile terminal_cell_t *video=(void *)(VIDEO_ADDRESS+top()*160);
        for(uint32_t i=0;i<rows()*80;i++) runtime->console.vga[i]=video[i];
    }
    ps2_init(); usb_keyboard_init(env); env->keyboard_poll=legacy_key;
    env->free_mem_ptr=get_free_ptr();
}
