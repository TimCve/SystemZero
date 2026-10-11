#include "../kernel/kernel.h"
#include "../shared/sys_api.h"
#include "../kernel/drivers/io/screen.h"

int main(env_vars_t *env, char *args) {
    (void)args;
    const systemzero_api_t *original = env->api;
    systemzero_api_t copy = *original; int failed = 0;
    env->api = &copy;
    copy.version++;
    if (execute("hello", "hello", env) != 2) failed = 1;
    copy.version = SZ_API_VERSION; copy.magic = 0;
    if (execute("hello", "hello", env) != 2) failed = 1;
    copy.magic = SZ_API_MAGIC; copy.count = SZ_API_COUNT - 1;
    if (execute("hello", "hello", env) != 2) failed = 1;
    env->api = 0;
    if (execute("hello", "hello", env) != 2) failed = 1;
    env->api = &copy; copy.count = SZ_API_COUNT + 1;
    if (execute("hello", "hello", env)) failed = 1;
    env->api = original;
    if (!failed) { print("api-compat-ok"); print_newline(); }
    return failed;
}
