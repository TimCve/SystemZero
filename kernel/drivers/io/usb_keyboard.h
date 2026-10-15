#ifndef SYSTEMZERO_USB_KEYBOARD_H
#define SYSTEMZERO_USB_KEYBOARD_H
#include "../../env_vars.h"
void usb_hid_poll(void);
void usb_keyboard_init(env_vars_t *);
#endif
