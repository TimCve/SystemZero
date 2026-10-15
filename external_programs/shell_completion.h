#ifndef SZ_SHELL_COMPLETION_H
#define SZ_SHELL_COMPLETION_H
#include "../shared/completion.h"
#include "../kernel/variables.h"
typedef struct {
    shell_history_t history;
    sz_completion_t completion;
    env_vars_t *env;
} shell_state_t;
static const char *shell_recall(void *context,int direction,const char *current) {
    return history_recall(&((shell_state_t *)context)->history,direction,current);
}
static int completion_read(void *context,const char *path,char *out,uint32_t bytes) {
    (void)context; return file_read_bytes(path,out,bytes,0);
}
typedef struct {
    const char *directory; int executable;
    sz_completion_visit_fn visit; void *context;
} completion_listing_t;
static int completion_entry(void *context,const char *name,const inode_t *info) {
    completion_listing_t *l=context; int executable=0;
    if(l->executable && info->valid==ESFS_FILE) {
        char path[1120]; uint32_t n=strlen((char *)l->directory),m=strlen((char *)name);
        if(n+m+2>sizeof path) return 0;
        memcpy(path,l->directory,n); if(n && path[n-1]!='/') path[n++]='/'; memcpy(path+n,name,m+1);
        uint8_t header[4]; executable=file_read_bytes(path,header,4,0)==4 && header[0]==127 && header[1]=='E' && header[2]=='L' && header[3]=='F';
    }
    return l->visit(l->context,name,info->valid==ESFS_DIRECTORY,executable);
}
static int completion_list(void *context,const char *path,int executable,sz_completion_visit_fn visit,void *arg) {
    (void)context; completion_listing_t listing={path,executable,visit,arg};
    return fs_list_entries(path,completion_entry,&listing);
}
static const char *completion_path(void *context) { return variables_get(((shell_state_t *)context)->env,"PATH"); }
static int shell_complete(void *context,char *line,int capacity,int *cursor,int repeated,int reverse) {
    shell_state_t *state=context;
    sz_completion_io_t io={state,completion_read,completion_list,completion_path};
    return sz_complete(&state->completion,&io,line,capacity,cursor,repeated,reverse);
}
#endif
