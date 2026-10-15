#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
int main(env_vars_t *env,char *args) {
    char source[560],dest[560],a[512],b[512]; uint32_t av,bv;
    char *token=splice(args,1,' '); if(strlen(token)>=560) return 1; memcpy(source,token,strlen(token)+1);
    token=splice(args,2,' '); if(strlen(token)>=560) return 1; memcpy(dest,token,strlen(token)+1);
    if(!*source || !*dest || !fs_resolve(source,&av,a) || !fs_resolve(dest,&bv,b) || (av==bv && !strcmp(a,b))) {
        print("Usage: copy SOURCE DESTINATION (paths may include a volume, e.g. 1p1:/file)"); print_newline(); return 1;
    }
    inode_t in=get_file_info(source); if(in.valid!=ESFS_FILE) { print("Source file unavailable."); print_newline(); return 1; }
    uint8_t *data=(void *)malloc(in.size ? in.size:1,0,0);
    if(!data || file_read_bytes(source,data,in.size,0)!=(int)in.size) return 1;
    if(get_file_info(dest).valid) { print("Destination already exists."); print_newline(); return 1; }
    if(!file_replace(dest,data,in.size)) { print("Copy failed."); print_newline(); return 1; }
    print("Copied "); print_dec(in.size); print(" bytes."); print_newline(); (void)env; return 0;
}
