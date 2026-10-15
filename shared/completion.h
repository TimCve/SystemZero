#ifndef SZ_COMPLETION_H
#define SZ_COMPLETION_H
#include <stdint.h>
#define SZ_COMPLETE_LINE 2000u
#define SZ_COMPLETE_PATH 560u
#define SZ_COMPLETE_MATCHES 128u
#define SZ_COMPLETE_CONFIG 8192u
#define SZ_COMPLETE_FILE "sys:/sys/config/completion"
typedef int (*sz_completion_visit_fn)(void *,const char *,int directory,int executable);
typedef struct {
    void *context;
    int (*read)(void *,const char *,char *,uint32_t);
    int (*list)(void *,const char *,int executables,sz_completion_visit_fn,void *);
    const char *(*path)(void *);
} sz_completion_io_t;
typedef struct {
    char original[SZ_COMPLETE_LINE],prefix[SZ_COMPLETE_PATH],directory[SZ_COMPLETE_PATH];
    char common[SZ_COMPLETE_PATH],matches[SZ_COMPLETE_MATCHES][SZ_COMPLETE_PATH];
    char config[SZ_COMPLETE_CONFIG+1];
    uint32_t start,end,cursor,base,count,total,selected;
    int active,directories,commands,bare;
} sz_completion_t;
/* Returns 0=disabled, 1=paths, 2=directories. Last valid command rule wins. */
int sz_completion_mode(const char *,uint32_t,const char *command);
int sz_complete(sz_completion_t *,const sz_completion_io_t *,char *,uint32_t,int *cursor,int repeated,int reverse);
#endif
