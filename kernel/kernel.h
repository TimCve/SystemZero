#ifndef SYSTEMZERO_KERNEL_H
#define SYSTEMZERO_KERNEL_H
#include "env_vars.h"

int execute(char* cmd, char* args_str, env_vars_t* env_vars_ptr);
void sleep(int time);

#endif
