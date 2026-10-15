#ifndef SYSTEMZERO_INPUT_DRIVER_H
#define SYSTEMZERO_INPUT_DRIVER_H
#include "../../../shared/input.h"
void input_init(env_vars_t *);
void input_tick(void);
void input_feed_key(uint8_t);
void input_feed_mouse(int,int,int,uint8_t);
void input_save_row(const terminal_cell_t *,uint8_t);
#endif
