#ifndef SYSTEMZERO_SOURCE_INCLUDES_H
#define SYSTEMZERO_SOURCE_INCLUDES_H
#include "toolchain.h"
typedef struct { env_vars_t *env; char extra_include[560]; } includes_t;
static int source_include(void *arg,const char *file,const char *parent,int local,
                          uint8_t *out,uint32_t cap,char *resolved,uint32_t path_capacity) {
    includes_t *ctx=arg; char name[560]; int found=0;
    if(local) {
        uint32_t last=0; for(uint32_t i=0;parent[i];i++) if(parent[i]=='/') last=i+1;
        uint32_t n=strlen((char *)file);
        if(last+n+1<sizeof name) { memcpy(name,parent,last); memcpy(name+last,file,n+1); found=get_file_info(name).valid==ESFS_FILE; }
    }
    if(!found && *ctx->extra_include) {
        uint32_t n=strlen(ctx->extra_include),m=strlen((char *)file);
        if(n+m+2<sizeof name) { memcpy(name,ctx->extra_include,n); name[n++]='/'; memcpy(name+n,file,m+1); found=get_file_info(name).valid==ESFS_FILE; }
    }
    if(!found && !tc_search(ctx->env,file,"INCLUDE",name,sizeof name)) return -1;
    uint32_t length=strlen(name); if(length>=path_capacity) return -1; memcpy(resolved,name,length+1);
    inode_t in=get_file_info(name); return in.size<=cap ? file_read_bytes(name,out,in.size,0):-1;
}
#endif
