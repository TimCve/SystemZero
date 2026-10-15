#ifndef SZ_HID_MOUSE_H
#define SZ_HID_MOUSE_H
#include <stdint.h>
typedef struct { uint16_t bit; uint8_t size,sign; } hid_field_t;
typedef struct { uint8_t id; uint16_t bits; hid_field_t x,y,wheel,buttons[8]; } hid_mouse_report_t;
typedef struct { hid_mouse_report_t reports[8]; uint8_t count,ids; } hid_mouse_t;
int hid_mouse_parse(hid_mouse_t *,const uint8_t *,uint32_t);
int hid_mouse_decode(const hid_mouse_t *,const uint8_t *,uint32_t,int *,int *,int *,uint8_t *);
#endif
