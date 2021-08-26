#include <stdint.h>

typedef struct {
	uint32_t free_mem_ptr;
	uint8_t selected_drive;
	uint8_t term_color;
	uint32_t tty_calibration;
} env_vars_t;
