#ifndef SYSTEMZERO_TOOLCHAIN_H
#define SYSTEMZERO_TOOLCHAIN_H
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
#include "../kernel/variables.h"
#include "../shared/linker.h"
#include "../shared/arguments.h"
static inline int tc_word(char **args,char *out,uint32_t cap) {
    const char *scan=*args;
    int result=sz_arg_next(&scan,out,cap); *args=(char *)scan; return result;
}
static inline int tc_search(env_vars_t *env,const char *file,const char *variable,char *out,uint32_t cap) {
    uint32_t n=strlen((char *)file); if(n+1>cap) return 0;
    memcpy(out,file,n+1); if(get_file_info(out).valid==ESFS_FILE) return 1;
    const char *path=variables_get(env,variable); if(!path) return 0;
    while(*path) {
        uint32_t at=0; while(*path && *path!=';') { if(at+1>=cap) return 0; out[at++]=*path++; } if(*path==';') path++;
        if(!at || at+n+2>cap) continue;
        if(out[at-1]!='/') out[at++]='/';
        memcpy(out+at,file,n+1);
        if(get_file_info(out).valid==ESFS_FILE) return 1;
    }
    return 0;
}
static inline int tc_object(env_vars_t *env,const char *file,sz_link_input_t *out) {
    char name[560]; if(!tc_search(env,file,"LIBRARY_PATH",name,sizeof name)) { print("Object or archive not found: "); print((char *)file); print_newline(); return 0; }
    inode_t in=get_file_info(name); uint8_t *bytes=(void *)malloc(in.size ? in.size:1,0,0); char *saved=(void *)malloc(strlen(name)+1,0,0);
    if(!bytes || !saved || file_read_bytes(name,bytes,in.size,0)!=(int)in.size) return 0;
    memcpy(saved,name,strlen(name)+1); *out=(sz_link_input_t){bytes,in.size,saved}; return 1;
}
static inline int tc_link(sz_link_input_t *objects,uint32_t count,const char *output) {
    uint8_t *image=(void *)malloc(SZ_LINK_IMAGE,0,0); void *workspace=(void *)malloc(sz_link_workspace_size(),0,0);
    if(!image || !workspace) { print("Not enough memory to link."); print_newline(); return 1; }
    sz_link_result_t result;
    if(!sz_link(objects,count,image,SZ_LINK_IMAGE,workspace,&result)) {
        print("Link error: "); print((char *)result.error); if(*result.symbol) { print(": "); print(result.symbol); } print_newline(); return 1;
    }
    if(!file_replace(output,image,result.size)) { print("Cannot save executable."); print_newline(); return 1; }
    print("Compiled "); print((char *)output); print(" ("); print_dec(result.size); print(" bytes)."); print_newline(); return 0;
}
#endif
