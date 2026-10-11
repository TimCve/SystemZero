#ifndef SYSTEMZERO_ENV_VARS_H
#define SYSTEMZERO_ENV_VARS_H
#include <stdint.h>

typedef struct {
	uint32_t free_mem_ptr;
	uint8_t system_drive;
	uint8_t selected_drive;
	uint8_t term_color;
	uint32_t tty_calibration;
	uint32_t last_allocated_block;
	void *filesystem;
} env_vars_t;

#endif
