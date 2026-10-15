#include <assert.h>
#include <string.h>
#include "../shared/hid_mouse.h"
#include "../shared/text.h"
static const uint8_t descriptor[]={
  0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,3,
  0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,
  0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0};
int main(void) {
    hid_mouse_t mouse; int x,y,wheel; uint8_t buttons;
    assert(hid_mouse_parse(&mouse,descriptor,sizeof descriptor));
    uint8_t report[]={5,127,0x81,255};
    assert(hid_mouse_decode(&mouse,report,4,&x,&y,&wheel,&buttons));
    assert(x==127 && y==-127 && wheel==-1 && buttons==5);
    assert(!hid_mouse_decode(&mouse,report,3,&x,&y,&wheel,&buttons));
    uint8_t with_id[sizeof descriptor+2]; with_id[0]=0x85; with_id[1]=7;
    memcpy(with_id+2,descriptor,sizeof descriptor);
    assert(hid_mouse_parse(&mouse,with_id,sizeof with_id));
    uint8_t identified[]={7,1,255,2,3};
    assert(hid_mouse_decode(&mouse,identified,5,&x,&y,&wheel,&buttons));
    assert(x==-1 && y==2 && wheel==3 && buttons==1);
    identified[0]=8; assert(!hid_mouse_decode(&mouse,identified,5,&x,&y,&wheel,&buttons));
    const uint8_t wide[]={0x05,1,0x09,0x30,0x09,0x31,0x16,0,0x80,0x75,16,0x95,2,0x81,6};
    assert(hid_mouse_parse(&mouse,wide,sizeof wide));
    uint8_t values[]={0,0x80,0xff,0x7f};
    assert(hid_mouse_decode(&mouse,values,4,&x,&y,&wheel,&buttons) && x==-32768 && y==32767);
    for(unsigned i=0;i<sizeof descriptor;i++) { hid_mouse_parse(&mouse,descriptor,i); }
    uint8_t bad[]={0x75,32,0x96,255,255,0x81,6}; assert(!hid_mouse_parse(&mouse,bad,sizeof bad));
    uint8_t data[64]="a\tb\ncde\nlast";
    text_buffer_t b={.data=data,.size=12,.capacity=sizeof data}; text_set_geometry(8,4);
    assert(text_hit(&b,0,2)==1 && text_hit(&b,0,4)==2 && text_hit(&b,1,2)==6);
    b.anchor=1; b.cursor=6; b.selection=1;
    assert(text_selected(&b,1) && text_selected(&b,5) && !text_selected(&b,6));
    assert(text_replace(&b,(const uint8_t *)"XYZ",3));
    assert(b.cursor==4 && b.size==10 && !memcmp(b.data,"aXYZe\nlast",10));
    b.anchor=9; b.cursor=1; b.selection=1; assert(text_delete(&b,0));
    assert(b.size==2 && b.cursor==1 && !memcmp(b.data,"at",2));
    b.capacity=3; assert(!text_replace(&b,(const uint8_t *)"long",4));
    assert(b.size==2 && b.cursor==1);
    text_scroll(&b,3); assert(b.top<=b.size);
    text_scroll(&b,-3); assert(b.top==0);
    return 0;
}
