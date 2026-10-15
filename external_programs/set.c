#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
#include "../shared/sys_api.h"
int main(env_vars_t *env,char *args) {
    while(*args && *args!=' ') args++;
    while(*args==' ') args++;
    char name[32]; uint32_t n=0; while(*args && *args!=' ') { if(n+1>=sizeof name) return 1; name[n++]=*args++; } name[n]=0;
    while(*args==' ') args++;
    if(!n) { print("Usage: set NAME VALUE (or set NAME to display it)"); print_newline(); return 1; }
    if(!*args) {
        char value[560]; int rc=env->api->functions[SZ_API_env_get](env,(uintptr_t)name,(uintptr_t)value,sizeof value,0,0);
        if(rc<0) return 1;
        print(name); print("="); print(value); print_newline(); return 0;
    }
    int rc=env->api->functions[SZ_API_env_set](env,(uintptr_t)name,(uintptr_t)args,0,0,0);
    if(rc) { print("Invalid environment setting or unable to save."); print_newline(); return 1; }
    print("Saved "); print(name); print_newline(); return 0;
}
