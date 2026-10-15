#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env,char *args) { (void)env; char *path=splice(args,1,' '); if(!*path) path="."; if(!fs_list_path(path)) return 0; print("Directory unavailable."); print_newline(); return 1; }
