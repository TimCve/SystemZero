#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
#include "../shared/sys_api.h"
int main(env_vars_t *env,char *args) {
    char home[512],*path=splice(args,1,' ');
    if(!*path) {
        if(env->api->functions[SZ_API_env_get](env,(uintptr_t)"HOME",(uintptr_t)home,sizeof home,0,0)<0) return 1;
        path=home;
    }
    if(!fs_chdir(path)) return 0;
    print("Directory unavailable."); print_newline(); return 1;
}
