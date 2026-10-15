#include <assert.h>
#include <stdint.h>
/* Exercise the real polling, selection and pointer code with simulated HID
   packets. Only privileged I/O and text rasterization are replaced. */
#include "../kernel/drivers/io/input.c"
static uint32_t pixels[800*600],presentations;
static input_runtime_t test_runtime;
static terminal_t test_terminal;
static env_vars_t test_environment;
static int packets,guards;
void thread_guard_enter(void) { guards++; }
void thread_guard_leave(void) { assert(guards>0); guards--; }
uint8_t port_byte_in(uint16_t port) { (void)port; return 0; }
void port_byte_out(uint16_t port,uint8_t value) { (void)port; (void)value; }
int get_tick(void) { return 1; }
void usb_hid_poll(void) {
    if(!packets) return;
    input_feed_mouse(0,0,0,1);
    for(int i=0;i<64;i++) input_feed_mouse(2,1,0,1);
    input_feed_mouse(0,0,0,0); packets=0;
}
void terminal_present(terminal_t *t) {
    assert(t==&test_terminal);
    assert(!polling); /* Rasterization must never run in the hardware drain. */
    presentations++;
}
int main(void) {
    environment=&test_environment; runtime=&test_runtime;
    environment->terminal=&test_terminal;
    test_terminal.pixels=pixels; test_terminal.pitch=test_terminal.width=800; test_terminal.height=600;
    test_terminal.origin_x=16; test_terminal.origin_y=12; test_terminal.scrolling=1;
    runtime->x=32; runtime->y=24;
    packets=1; service();
    assert(presentations==1 && runtime->x==160 && runtime->y==88);
    assert(!runtime->buttons && !runtime->console.selecting && runtime->console.selected);
    /* Logical cell writes do not touch the displayed framebuffer. The cursor
       must remain drawable while a slow editor is preparing its next frame. */
    for(uint32_t i=0;i<800*600;i++) pixels[i]=0x0000aa;
    test_terminal.updates=1; paint(1);
    assert(drawn && pixels[88*800+160]==0 && pixels[92*800+161]==0xffffff);
    runtime->x=400; runtime->y=200; paint(1);
    assert(pixels[88*800+160]==0x0000aa && pixels[200*800+400]==0);
    before_present(0); assert(guards==1 && runtime->canvas_updates==1 && !drawn);
    runtime->x=410; runtime->y=210; paint(1); assert(!drawn);
    after_present(0); assert(!guards && !runtime->canvas_updates && drawn);
    assert(pixels[210*800+410]==0 && pixels[200*800+400]==0x0000aa);
    runtime->x=400; runtime->y=200; paint(1);
    /* Direct canvas updates protect backing pixels; ending the operation
       redraws the latest pointer rather than restoring stale coordinates. */
    runtime->canvas_updates=1; paint(1);
    assert(!drawn && pixels[200*800+400]==0x0000aa);
    runtime->x=420; runtime->y=220; runtime->canvas_updates=0; paint(1);
    assert(drawn && pixels[220*800+420]==0 && pixels[224*800+421]==0xffffff);
    packets=1; presentations=0; service();
    assert(!presentations && view_dirty);
    test_terminal.updates=0; service(); assert(presentations==1 && !view_dirty);
    /* Releases on the status bar also terminate a drag. */
    input_feed_mouse(0,0,0,1); assert(runtime->console.selecting);
    input_feed_mouse(0,-1000,0,0); assert(!runtime->console.selecting);
    return 0;
}
