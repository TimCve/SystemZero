#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
#include "../shared/arguments.h"

typedef struct entry { struct entry *next; uint8_t directory; char name[]; } entry_t;
typedef struct { entry_t *next; uint32_t length; } frame_t;
/* A path is limited to 511 bytes, so even one-character directory components
   cannot exceed this many levels. Traversal frames live on the command heap. */
#define TREE_LEVELS (ESFS_MAX_NAME/2+2)
static int collect(void *context,const char *name,const inode_t *info) {
    entry_t **list=context;
    entry_t *entry=(void *)malloc(sizeof *entry+strlen((char *)name)+1,0,0);
    if(!entry) return ESFS_FULL;
    entry->directory=info->valid==ESFS_DIRECTORY;
    memcpy(entry->name,name,strlen((char *)name)+1);
    while(*list && strcmp((*list)->name,entry->name)<0) list=&(*list)->next;
    entry->next=*list; *list=entry; return 0;
}
static int unavailable(const char *path) {
    print("Path unavailable: "); print((char *)path); print_newline(); return 1;
}
int main(env_vars_t *env,char *args) {
    (void)env;
    char command[560]; const char *scan=args;
    if(sz_arg_next(&scan,command,sizeof command)<0) return 1;
    args=(char *)scan; while(sz_arg_space(*args)) args++;
    uint32_t bytes=strlen(args); while(bytes && sz_arg_space(args[bytes-1])) bytes--;
    char requested[512]; int decoded=sz_arg_decode(args,bytes,requested,sizeof requested,0);
    if(decoded<0) { print("Invalid or too long path."); print_newline(); return 1; }
    uint32_t n=decoded;
    const char *root=n?requested:".";
    uint32_t volume; char relative[512],path[560];
    if(!fs_resolve(root,&volume,relative)) return unavailable(root);
    n=0; path[n++]='0'+volume/(SZ_PARTITIONS+1);
    uint32_t part=volume%(SZ_PARTITIONS+1);
    if(part) { path[n++]='p'; if(part>=10) path[n++]='1'; path[n++]='0'+part%10; }
    path[n++]=':'; path[n++]='/'; memcpy(path+n,relative,strlen(relative)+1);
    inode_t info=get_file_info(path);
    if(!info.valid) return unavailable(root);
    print((char *)root); print_newline();
    uint32_t directories=0,files=0;
    if(info.valid==ESFS_FILE) files=1;
    else {
        frame_t *stack=(void *)malloc(sizeof *stack*TREE_LEVELS,0,0);
        if(!stack) { print("Not enough memory to list tree."); print_newline(); return 1; }
        stack[0]=(frame_t){.length=strlen(path)};
        if(fs_list_entries(path,collect,&stack[0].next)) return unavailable(root);
        uint32_t depth=0;
        for(;;) {
            frame_t *frame=&stack[depth]; entry_t *entry=frame->next;
            if(!entry) { if(!depth) break; path[stack[--depth].length]=0; continue; }
            frame->next=entry->next;
            screen_begin_update();
            for(uint32_t i=0;i<depth;i++) print(stack[i].next?"|   ":"    ");
            print(entry->next?"|-- ":"`-- "); print(entry->name);
            if(entry->directory) { directories++; print("/"); } else files++;
            print_newline();
            screen_end_update();
            if(!entry->directory) continue;
            n=frame->length; uint32_t length=strlen(entry->name);
            if(n+length+2>sizeof path || depth+1==TREE_LEVELS) return unavailable(root);
            if(path[n-1]!='/') path[n++]='/';
            memcpy(path+n,entry->name,length+1);
            stack[++depth]=(frame_t){.length=n+length};
            if(fs_list_entries(path,collect,&stack[depth].next)) return unavailable(path);
        }
    }
    print_newline(); print_dec(directories); print(directories==1?" directory, ":" directories, ");
    print_dec(files); print(files==1?" file":" files"); print_newline(); return 0;
}
