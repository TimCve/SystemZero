#include "env_vars.h"
#include "drivers/utils/mem.h"
#include "drivers/disk/fs.h"
extern int main(env_vars_t *, char *);
int program_start(env_vars_t *env, char *args) {
    set_free_ptr(env->free_mem_ptr);
    fs_bind_env(env);
    int status = main(env, args);
    env->free_mem_ptr = get_free_ptr();
    return status;
}
