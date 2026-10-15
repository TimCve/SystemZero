#ifndef SYSTEMZERO_KERNEL_DESKTOP_H
#define SYSTEMZERO_KERNEL_DESKTOP_H
#include "env_vars.h"
#include "../shared/desktop.h"
void desktop_init(env_vars_t *env);
void desktop_tick(void);
void desktop_refresh(void);
void desktop_command(env_vars_t *env, const char *command);
int desktop_settings(env_vars_t *env, uint32_t seconds, uint32_t position, uint32_t color, uint32_t secondary);
void desktop_apply(env_vars_t *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t desktop_rtc_seconds(void);
#endif
