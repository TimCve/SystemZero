#ifndef SYSTEMZERO_ENV_VARS_H
#define SYSTEMZERO_ENV_VARS_H
#include <stdint.h>

typedef struct env_vars {
	uint32_t free_mem_ptr;
	uint8_t system_drive;
	uint8_t selected_drive;
	uint8_t term_color;
	uint32_t tty_calibration;
	uint32_t last_allocated_block;
	void *filesystem;
	void *terminal;
    const struct systemzero_api *api;
    struct desktop *desktop;
    int (*run)(char *, char *, struct env_vars *);
    const struct boot_info *boot;
    struct block_runtime *disks;
    int (*keyboard_poll)(void);
    uint8_t selected_partition, system_partition;
    struct system_variables *variables;
    struct input_runtime *input;
} env_vars_t;

#endif
