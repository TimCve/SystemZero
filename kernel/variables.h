#ifndef SYSTEMZERO_VARIABLES_H
#define SYSTEMZERO_VARIABLES_H
#include "env_vars.h"
#include "../shared/environment.h"
void variables_init(env_vars_t *,uint32_t);
const char *variables_get(env_vars_t *,const char *);
int variables_set(env_vars_t *,const char *,const char *);
int variables_settings(env_vars_t *,uint32_t,uint32_t,uint32_t,uint32_t);
void variables_apply(env_vars_t *);
#endif
