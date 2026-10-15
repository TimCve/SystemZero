#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/kernel.h"
int main(env_vars_t *env,char *args) { (void)args; return execute("diskman","diskman",env); }
