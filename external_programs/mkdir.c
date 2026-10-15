#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env,char *args) { (void)env; char *path=splice(args,1,' '); if(*path && !fs_mkdir(path)) return 0; print("Cannot create directory. Check the path and parent directory."); print_newline(); return 1; }
