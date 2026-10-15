/* Polled xHCI root-port HID keyboards and relative mice. DMA is persistent. */
#include "usb_keyboard.h"
#include "input.h"
#include "../../../shared/hid_mouse.h"
#include "../pci.h"
#include "../utils/mem.h"
#include "../../cpu/timer.h"
typedef struct { uint32_t parameter,high,status,control; } trb_t;
typedef struct { trb_t *entries; uint32_t at,cycle; } ring_t;
typedef struct keyboard {
    struct keyboard *host;
    volatile uint32_t *operation,*interrupter,*doorbells;
    volatile trb_t *events;
    ring_t commands,control,input;
    uint32_t *device_context,*input_context,*dcbaa;
    uint8_t *buffer,*report;
    uint32_t context_words,event_at,event_cycle,slot,endpoint;
    uint32_t pending,repeat_at;
    uint8_t previous[8],repeat;
    uint8_t mouse,active; uint16_t report_bytes;
    hid_mouse_t layout;
    struct keyboard *devices[8]; uint32_t device_count;
} keyboard_t;
static keyboard_t *keyboard;
static int ring_init(ring_t *ring) {
    ring->entries=dma_alloc(4096); ring->cycle=1; return ring->entries!=0;
}
static uint32_t put(ring_t *ring,trb_t trb) {
    trb_t *entry=&ring->entries[ring->at];
    entry->parameter=trb.parameter; entry->high=trb.high; entry->status=trb.status;
    __asm__ volatile("" ::: "memory"); entry->control=trb.control|ring->cycle;
    if(++ring->at==255) {
        ring->entries[255]=(trb_t){(uintptr_t)ring->entries,0,0,(6u<<10)|2|ring->cycle};
        ring->at=0; ring->cycle^=1;
    }
    return (uintptr_t)entry;
}
static int event(keyboard_t *k,trb_t *result) {
    if(k->host) k=k->host;
    volatile trb_t *entry=&k->events[k->event_at];
    if((entry->control&1)!=k->event_cycle) return 0;
    __asm__ volatile("" ::: "memory"); *result=*entry;
    if(++k->event_at==256) { k->event_at=0; k->event_cycle^=1; }
    k->interrupter[6]=((uintptr_t)&k->events[k->event_at])|8;
    k->interrupter[7]=0; return 1;
}
static int wait_event(keyboard_t *k,uint32_t type,uint32_t pointer,trb_t *result) {
    for(uint32_t i=0;i<10000000;i++) {
        trb_t got;
        if(event(k,&got) && (got.control>>10&63)==type && got.parameter==pointer) {
            *result=got; return (got.status>>24)==1 || (got.status>>24)==13;
        }
        __asm__ volatile("pause");
    }
    return 0;
}
static int command(keyboard_t *k,uint32_t type,uint32_t pointer,uint32_t slot,trb_t *result) {
    if(k->host) k=k->host;
    uint32_t address=put(&k->commands,(trb_t){pointer,0,0,(type<<10)|(slot<<24)});
    __asm__ volatile("" ::: "memory"); k->doorbells[0]=0;
    return wait_event(k,33,address,result);
}
static int control(keyboard_t *k,uint8_t request_type,uint8_t request,uint16_t value,uint16_t index,uint16_t length) {
    uint32_t direction=length ? ((request_type&0x80) ? 3:2):0;
    put(&k->control,(trb_t){request_type|((uint32_t)request<<8)|((uint32_t)value<<16),
        index|((uint32_t)length<<16),8,(2u<<10)|(1u<<6)|(direction<<16)});
    if(length) put(&k->control,(trb_t){(uintptr_t)k->buffer,0,length,
        (3u<<10)|((request_type&0x80) ? 1u<<16:0)});
    uint32_t address=put(&k->control,(trb_t){0,0,0,(4u<<10)|(1u<<5)|
        ((!length || !(request_type&0x80)) ? 1u<<16:0)});
    __asm__ volatile("" ::: "memory"); k->doorbells[k->slot]=1;
    trb_t result; return wait_event(k,32,address,&result);
}
static uint16_t little16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static uint32_t *input_context(keyboard_t *k,uint32_t index) { return k->input_context+(index+1)*k->context_words; }
static uint32_t *output_context(keyboard_t *k,uint32_t index) { return k->device_context+index*k->context_words; }
static void arm(keyboard_t *k) {
    k->pending=put(&k->input,(trb_t){(uintptr_t)k->report,0,k->report_bytes,(1u<<10)|(1u<<5)|(1u<<2)});
    __asm__ volatile("" ::: "memory"); k->doorbells[k->slot]=k->endpoint;
}
static int enumerate(keyboard_t *k,uint32_t port,uint32_t speed) {
    trb_t result;
    if(!command(k,9,0,0,&result)) return 0;
    k->slot=result.control>>24;
    if(!k->slot || k->slot>8) return 0;
    k->device_context=dma_alloc(4096); k->input_context=dma_alloc(4096);
    k->buffer=dma_alloc(4096); k->report=dma_alloc(64);
    if(!k->device_context || !k->input_context || !k->buffer || !k->report ||
        !ring_init(&k->control) || !ring_init(&k->input)) return 0;
    k->dcbaa[k->slot*2]=(uintptr_t)k->device_context;
    k->input_context[1]=3;
    uint32_t *slot=input_context(k,0),*ep=input_context(k,1);
    slot[0]=(speed<<20)|(1u<<27); slot[1]=(port+1)<<16;
    uint32_t packet=speed==4 ? 512:speed==3 ? 64:8;
    ep[1]=(packet<<16)|(4u<<3)|(3u<<1);
    ep[2]=(uintptr_t)k->control.entries|1; ep[4]=8;
    if(!command(k,11,(uintptr_t)k->input_context,k->slot,&result) ||
        !control(k,0x80,6,0x100,0,8)) return 0;
    uint32_t actual=k->buffer[7];
    if(speed==4) { if(actual!=9) return 0; actual=512; }
    if(actual!=packet) {
        if(actual!=8 && actual!=16 && actual!=32 && actual!=64 && actual!=512) return 0;
        memset(k->input_context,0,4096); k->input_context[1]=2;
        ep=input_context(k,1); memcpy(ep,output_context(k,1),k->context_words*4);
        ep[0]&=~7u; ep[1]=(ep[1]&65535)|(actual<<16);
        if(!command(k,13,(uintptr_t)k->input_context,k->slot,&result)) return 0;
    }
    if(!control(k,0x80,6,0x200,0,9)) return 0;
    uint32_t length=little16(k->buffer+2);
    if(length<9 || length>4096 || !control(k,0x80,6,0x200,0,length)) return 0;
    uint8_t config=k->buffer[5],interface=0; int selected=0;
    uint32_t endpoint=0,interval=0,maximum=0,report_length=0;
    for(uint32_t at=0;at+2<=length;) {
        const uint8_t *d=k->buffer+at;
        if(d[0]<2 || d[0]>length-at) return 0;
        if(d[1]==4 && d[0]>=9) {
            selected=d[3]==0 && d[5]==3 && d[6]==1 && (d[7]==1 || d[7]==2);
            if(selected) k->mouse=d[7]==2;
            report_length=0;
            interface=d[2];
        } else if(selected && d[1]==0x21 && d[0]>=9 && d[6]==0x22) {
            report_length=little16(d+7);
        } else if(selected && d[1]==5 && d[0]>=7 && (d[2]&0x80) && (d[3]&3)==3) {
            endpoint=(d[2]&15)*2+1; maximum=little16(d+4)&0x7ff; interval=d[6]; break;
        }
        at+=d[0];
    }
    if(endpoint<3 || endpoint>31 || maximum<(k->mouse?3u:8u) || maximum>64 || !interval) return 0;
    if(!control(k,0,9,config,0,0)) return 0;
    if(k->mouse) {
        if(!control(k,0x21,11,1,interface,0) || !report_length || report_length>4096 ||
            !control(k,0x81,6,0x2200,interface,report_length) ||
            !hid_mouse_parse(&k->layout,k->buffer,report_length)) return 0;
    } else if(!control(k,0x21,11,0,interface,0)) return 0;
    k->report_bytes=k->mouse?maximum:8;
    memset(k->input_context,0,4096); k->input_context[1]=1u|(1u<<endpoint);
    slot=input_context(k,0); memcpy(slot,output_context(k,0),k->context_words*4);
    slot[0]=(slot[0]&0x7ffffff)|(endpoint<<27);
    ep=input_context(k,endpoint);
    uint32_t exponent;
    if(speed>=3) exponent=interval-1;
    else { exponent=3; while(interval>1) { exponent++; interval>>=1; } }
    if(exponent>15) return 0;
    ep[0]=exponent<<16; ep[1]=(maximum<<16)|(7u<<3)|(3u<<1);
    ep[2]=(uintptr_t)k->input.entries|1; ep[4]=k->report_bytes|((uint32_t)k->report_bytes<<16);
    if(!command(k,12,(uintptr_t)k->input_context,k->slot,&result)) return 0;
    k->endpoint=endpoint; k->active=1; return 1;
}
static uint16_t keycode(uint8_t usage) {
    static const uint8_t letters[26]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    static const uint16_t special[45]={0x1c,1,0x0e,0x0f,0x39,0x0c,0x0d,0x1a,0x1b,0x2b,0x2b,0x27,0x28,0x29,0x33,0x34,0x35,0x3a,
        0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x57,0x58,0,0,0,0x152,0x147,0x149,0x153,0x14f,0x151,0x14d,0x14b,0x150,0x148,0,0};
    if(usage>=4 && usage<=29) return letters[usage-4];
    if(usage>=0x1e && usage<=0x27) return usage==0x27 ? 0x0b:usage-0x1c;
    if(usage>=0x28 && usage<0x28+45) return special[usage-0x28];
    if(usage==0x64) return 0x56;
    return 0;
}
static void emit(keyboard_t *k,uint16_t code,int release) {
    (void)k; if(!code) return;
    if(code&256) input_feed_key(0xe0);
    input_feed_key((uint8_t)code|(release ? 0x80:0));
}
static int contains(const uint8_t *report,uint8_t usage) {
    for(uint32_t i=2;i<8;i++) if(report[i]==usage) return 1;
    return 0;
}
static void report(keyboard_t *k) {
    for(uint32_t i=2;i<8;i++) if(k->report[i] && k->report[i]<4) return;
    static const uint16_t modifiers[8]={0x1d,0x2a,0x38,0x15b,0x11d,0x36,0x138,0x15c};
    for(uint32_t i=0;i<8;i++) if((k->previous[0]^k->report[0])&(1u<<i))
        emit(k,modifiers[i],!(k->report[0]&(1u<<i)));
    for(uint32_t i=2;i<8;i++) if(k->previous[i] && !contains(k->report,k->previous[i])) emit(k,keycode(k->previous[i]),1);
    if(k->repeat && !contains(k->report,k->repeat)) k->repeat=0;
    for(uint32_t i=2;i<8;i++) if(k->report[i] && !contains(k->previous,k->report[i])) {
        emit(k,keycode(k->report[i]),0); k->repeat=k->report[i]; k->repeat_at=(uint32_t)get_tick()+500;
    }
    memcpy(k->previous,k->report,8);
}
void usb_hid_poll(void) {
    keyboard_t *host=keyboard; if(!host) return;
    trb_t got;
    /* Give rendering and the scheduler a turn even under continuous input. */
    for(uint32_t budget=64;budget && event(host,&got);budget--) if((got.control>>10&63)==32) {
        for(uint32_t i=0;i<host->device_count;i++) {
            keyboard_t *k=host->devices[i];
            if(!k->active || got.parameter!=k->pending || (got.control>>24)!=k->slot) continue;
            uint32_t completion=got.status>>24;
            if(completion!=1 && completion!=13) { k->active=0; continue; }
            uint32_t residue=got.status&0xffffff,bytes=residue>k->report_bytes?0:k->report_bytes-residue;
            if(k->mouse) {
                int x,y,wheel; uint8_t buttons;
                if(hid_mouse_decode(&k->layout,k->report,bytes,&x,&y,&wheel,&buttons)) input_feed_mouse(x,y,wheel,buttons);
            } else if(bytes>=8) report(k);
            arm(k);
        }
    }
    for(uint32_t i=0;i<host->device_count;i++) {
        keyboard_t *k=host->devices[i];
        if(k->active && !k->mouse && k->repeat && (int32_t)((uint32_t)get_tick()-k->repeat_at)>=0) {
            emit(k,keycode(k->repeat),0); k->repeat_at=(uint32_t)get_tick()+33;
        }
    }
}
static void probe(pci_device_t device,uint32_t class,void *context) {
    (void)context; if(keyboard || class!=0x0c0330) return;
    uint32_t base=pci_bar(device,0); if(!base || base>UINT32_MAX-0x100000) return;
    pci_enable(device); volatile uint32_t *cap=(void *)base;
    if((cap[0]&255)<0x20 || (cap[5]&~3u)>0xf0000 || (cap[6]&~31u)>0xf0000) return;
    uint32_t offset=cap[4]>>16;
    for(uint32_t i=0;offset && i<64;i++) {
        if(offset>0x4000) return;
        volatile uint32_t *extension=cap+offset;
        uint32_t value=*extension;
        if((value&255)==1) {
            *extension=value|(1u<<24);
            if(!mmio_wait(extension,1u<<16,0)) return;
            extension[1]=0;
        }
        uint32_t next=(value>>8)&255; if(!next) break; offset+=next;
    }
    keyboard_t *k=dma_alloc(sizeof(*k)); if(!k) return;
    k->operation=(void *)(base+(cap[0]&255));
    k->doorbells=(void *)(base+(cap[5]&~3u));
    k->interrupter=(void *)(base+(cap[6]&~31u)+0x20);
    k->context_words=(cap[4]&4) ? 16:8;
    k->operation[0]&=~1u;
    if(!mmio_wait(k->operation+1,1,1)) return;
    k->operation[0]=2;
    if(!mmio_wait(k->operation,2,0) || !mmio_wait(k->operation+1,1u<<11,0) || !(k->operation[2]&1)) return;
    if(!ring_init(&k->commands)) return;
    k->events=dma_alloc(4096); k->dcbaa=dma_alloc(2048);
    uint32_t *erst=dma_alloc(64);
    if(!k->events || !k->dcbaa || !erst) return;
    uint32_t scratch=((cap[2]>>27)&31)|(((cap[2]>>21)&31)<<5);
    if(scratch) {
        uint32_t *array=dma_alloc(scratch*8); if(!array) return;
        for(uint32_t i=0;i<scratch;i++) { array[i*2]=(uintptr_t)dma_alloc(4096); if(!array[i*2]) return; }
        k->dcbaa[0]=(uintptr_t)array;
    }
    uint32_t slots=cap[1]&255; if(slots>8) slots=8; if(!slots) return;
    k->operation[14]=slots;
    k->operation[12]=(uintptr_t)k->dcbaa; k->operation[13]=0;
    k->operation[6]=(uintptr_t)k->commands.entries|1; k->operation[7]=0;
    erst[0]=(uintptr_t)k->events; erst[2]=256; k->event_cycle=1;
    k->interrupter[0]=0; k->interrupter[2]=1;
    k->interrupter[4]=(uintptr_t)erst; k->interrupter[5]=0;
    k->interrupter[6]=(uintptr_t)k->events; k->interrupter[7]=0;
    k->operation[0]=1; if(!mmio_wait(k->operation+1,1,0)) return;
    uint32_t ports=cap[1]>>24;
    for(uint32_t port=0;port<ports;port++) {
        volatile uint32_t *status=k->operation+0x100+port*4;
        if(!(*status&1)) continue;
        *status=(*status&0xe00)|16;
        if(!mmio_wait(status,16,0) || !(*status&2)) continue;
        uint32_t speed=(*status>>10)&15;
        if(k->device_count==8) break;
        keyboard_t *d=dma_alloc(sizeof *d); if(!d) break;
        d->host=k; d->operation=k->operation; d->doorbells=k->doorbells;
        d->context_words=k->context_words; d->dcbaa=k->dcbaa;
        if(enumerate(d,port,speed)) k->devices[k->device_count++]=d;
        else if(d->slot) { trb_t result; command(k,10,0,d->slot,&result); }
    }
    if(k->device_count) {
        keyboard=k;
        for(uint32_t i=0;i<k->device_count;i++) arm(k->devices[i]);
    } else k->operation[0]=0;
}
void usb_keyboard_init(env_vars_t *env) {
    (void)env; pci_scan(probe,0);
}
