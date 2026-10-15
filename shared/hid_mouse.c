#include "hid_mouse.h"
typedef struct { uint32_t page,size,count,id; int32_t minimum; } globals_t;
static int32_t signed_value(uint32_t value,uint32_t bits) {
    if(bits<32 && (value&(1u<<(bits-1)))) value|=~((1u<<bits)-1);
    return (int32_t)value;
}
int hid_mouse_parse(hid_mouse_t *out,const uint8_t *data,uint32_t length) {
    *out=(hid_mouse_t){0}; globals_t g={0},stack[4]; uint32_t depth=0;
    uint32_t usages[32],n=0,minimum=0,maximum=0; int range=0;
    for(uint32_t at=0;at<length;) {
        uint8_t prefix=data[at++]; if(prefix==0xfe) return 0;
        uint32_t size=prefix&3; if(size==3) size=4;
        if(size>length-at) return 0;
        uint32_t value=0; for(uint32_t i=0;i<size;i++) value|=(uint32_t)data[at++]<<(i*8);
        uint32_t type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1) {
            if(tag==0) g.page=value;
            else if(tag==1) g.minimum=size?signed_value(value,size*8):0;
            else if(tag==7) g.size=value;
            else if(tag==8) { if(!value || value>255) return 0; g.id=value; out->ids=1; }
            else if(tag==9) g.count=value;
            else if(tag==10) { if(depth==4) return 0; stack[depth++]=g; }
            else if(tag==11) { if(!depth) return 0; g=stack[--depth]; }
        } else if(type==2) {
            if(tag==0 && n<32) usages[n++]=value;
            if(tag==1) { minimum=value; range=1; }
            if(tag==2) maximum=value;
        } else if(type==0) {
            if(tag==8) {
                if(!g.size || g.size>32 || g.count>512 || g.size*g.count>512) return 0;
                uint32_t index=0;
                while(index<out->count && out->reports[index].id!=g.id) index++;
                if(index==out->count) { if(index==8) return 0; out->reports[index].id=g.id; out->count++; }
                hid_mouse_report_t *r=&out->reports[index];
                if(r->bits+g.size*g.count>512) return 0;
                for(uint32_t i=0;i<g.count;i++) {
                    uint32_t usage=i<n?usages[i]:range && minimum+i<=maximum?minimum+i:0;
                    uint32_t page=usage>>16?usage>>16:g.page; usage&=65535;
                    hid_field_t *field=0;
                    if(!(value&1) && (value&2)) {
                        if(page==9 && usage>=1 && usage<=8) field=&r->buttons[usage-1];
                        if(page==1 && (value&4)) {
                            if(usage==0x30) field=&r->x;
                            if(usage==0x31) field=&r->y;
                            if(usage==0x38) field=&r->wheel;
                        }
                    }
                    if(field) *field=(hid_field_t){r->bits+i*g.size,g.size,g.minimum<0};
                }
                r->bits+=g.size*g.count;
            }
            n=0; range=0; minimum=maximum=0;
        }
    }
    for(uint32_t i=0;i<out->count;i++) if(out->reports[i].x.size && out->reports[i].y.size) return 1;
    return 0;
}
static int32_t field_value(hid_field_t f,const uint8_t *data) {
    if(!f.size) return 0;
    uint32_t value=0; for(uint32_t i=0;i<f.size;i++) value|=(uint32_t)((data[(f.bit+i)/8]>>((f.bit+i)%8))&1)<<i;
    return f.sign?signed_value(value,f.size):(int32_t)value;
}
int hid_mouse_decode(const hid_mouse_t *layout,const uint8_t *data,uint32_t length,int *x,int *y,int *wheel,uint8_t *buttons) {
    uint8_t id=0; if(layout->ids) { if(!length) return 0; id=*data++; length--; }
    for(uint32_t i=0;i<layout->count;i++) {
        const hid_mouse_report_t *r=&layout->reports[i];
        if(r->id!=id || !r->x.size || !r->y.size || length<(r->bits+7u)/8) continue;
        *x=field_value(r->x,data); *y=field_value(r->y,data); *wheel=field_value(r->wheel,data); *buttons=0;
        for(uint32_t b=0;b<8;b++) if(field_value(r->buttons[b],data)) *buttons|=1u<<b;
        return 1;
    }
    return 0;
}
