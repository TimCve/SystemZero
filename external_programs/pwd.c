#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
int main(env_vars_t *env,char *args) { (void)env; (void)args; char path[560]; if(fs_getcwd(path,sizeof path)<0) return 1; print(path); print_newline(); return 0; }
