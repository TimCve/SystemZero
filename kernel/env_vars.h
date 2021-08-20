#include <stdint.h>

typedef struct {
	uint32_t free_mem_ptr;
	uint8_t selected_drive;
	uint8_t master_exists;
	uint8_t slave_exists;
	uint8_t term_color;
} env_vars_t;
