#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env,char *args) {
    char *name=splice(args,1,' '); char path[16]; uint32_t n=strlen(name);
    if(!n || n>8) goto bad;
    for(uint32_t i=0;i<n;i++) path[i]=name[i];
    if(path[n-1]!=':') path[n++]=':';
    path[n]=0;
    if(!volume_select(env,path)) return 0;
bad: print("Usage: drive DISK or DISKpPARTITION (for example: drive 1p1)"); print_newline(); return 1;
}
