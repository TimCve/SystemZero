#include "../kernel/kernel.h"
#include "../kernel/drivers/io/screen.h"

/* Nested command execution remains a kernel API, independent of bundled commands. */
int main(env_vars_t *env, char *args) {
    (void)args;
    if (execute("touch", "touch nested.txt", env)) return 1;
    if (execute("del", "del nested.txt", env)) return 1;
    print("nested-ok"); print_newline(); return 0;
}
