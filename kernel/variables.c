#include "variables.h"
#include "desktop.h"
#include "drivers/disk/fs.h"
#include "drivers/disk/volume.h"
#include "drivers/disk/ata.h"
#include "drivers/utils/mem.h"
#include "libc/strings.h"
#include "../shared/desktop.h"
#include "../shared/version.h"
static void decimal(char *out,uint32_t n) { char reversed[10]; uint32_t at=0; do { reversed[at++]='0'+n%10; n/=10; } while(n); uint32_t i=0; while(at) out[i++]=reversed[--at]; out[i]=0; }
static void system_path(env_vars_t *env,char *out,const char *suffix) {
    uint32_t n=0; out[n++]='0'+env->system_drive;
    if(env->system_partition) { out[n++]='p'; if(env->system_partition>=10) out[n++]='1'; out[n++]='0'+env->system_partition%10; }
    out[n++]=':'; while(*suffix) out[n++]=*suffix++; out[n]=0;
}
static void numeric(system_variables_t *t,const char *key,uint32_t value) { char text[11]; decimal(text,value); environment_put(t,key,text); }
const char *variables_get(env_vars_t *env,const char *name) {
    static char derived[560];
    if(!env || !name) return 0;
    if(!strcmp((char *)name,"VERSION")) return SYSTEMZERO_VERSION;
    if(!strcmp((char *)name,"SYSTEM_VOLUME")) { system_path(env,derived,""); return derived; }
    if(!strcmp((char *)name,"PWD")) return fs_getcwd(derived,sizeof derived)<0 ? 0:derived;
    return environment_get(env->variables,name);
}
void variables_apply(env_vars_t *env) {
    static uint32_t applied_offset = 0xffffffffu;
    if(!env || !env->variables || !env->desktop || environment_validate(env->variables)) return;
    uint32_t fg,bg,secondary,offset;
    environment_number(variables_get(env,"COLOR_FOREGROUND"),&fg); environment_number(variables_get(env,"COLOR_BACKGROUND"),&bg);
    environment_number(variables_get(env,"COLOR_SECONDARY"),&secondary); environment_number(variables_get(env,"CLOCK_OFFSET"),&offset);
    uint32_t milliseconds = env->desktop->milliseconds;
    int clock_changed = offset != applied_offset;
    desktop_apply(env,clock_changed ? (desktop_rtc_seconds()+offset)%86400 : env->desktop->seconds,
        !strcmp((char *)variables_get(env,"STATUS_POSITION"),"bottom"),(bg<<4)|fg,secondary);
    if (!clock_changed) env->desktop->milliseconds = milliseconds;
    applied_offset = offset;
}
static int save(env_vars_t *env,system_variables_t *candidate) {
    if(environment_validate(candidate)) return -1;
    char *text=(void *)malloc(SZ_ENV_FILE,0,0); if(!text) return -1;
    int length=environment_encode(candidate,text,SZ_ENV_FILE); char path[560]; system_path(env,path,"/sys/config/environment");
    if(length<0 || !file_replace(path,text,length)) return -1;
    const char *keys[] = {"COLOR_FOREGROUND", "COLOR_BACKGROUND", "COLOR_SECONDARY", "CLOCK_OFFSET", "STATUS_POSITION"};
    int display_changed = 0;
    for (uint32_t i = 0; i < 5; i++)
        if (strcmp((char *)environment_get(env->variables, keys[i]), (char *)environment_get(candidate, keys[i]))) display_changed = 1;
    *env->variables=*candidate;
    if (display_changed) variables_apply(env);
    return 0;
}
void variables_init(env_vars_t *env,uint32_t offset) {
    static system_variables_t table; env->variables=&table;
    char path[560]; environment_defaults(&table);
    numeric(&table,"COLOR_FOREGROUND",env->term_color&15); numeric(&table,"COLOR_BACKGROUND",env->term_color>>4);
    numeric(&table,"COLOR_SECONDARY",env->desktop->secondary_color); numeric(&table,"CLOCK_OFFSET",offset);
    environment_put(&table,"STATUS_POSITION",env->desktop->position ? "bottom":"top");
    uint32_t marker=get_free_ptr(); system_path(env,path,"/sys/config/environment"); inode_t in=get_file_info(path);
    if(in.valid==ESFS_FILE && in.size<=SZ_ENV_FILE) {
        char *text=(void *)malloc(SZ_ENV_FILE,0,0); system_variables_t *candidate=(void *)malloc(sizeof *candidate,0,0);
        if(text && candidate) { *candidate=table; if(file_read_bytes(path,text,in.size,0)==(int)in.size && !environment_decode(candidate,text,in.size)) table=*candidate; }
    }
    heap_release(marker); env->free_mem_ptr=get_free_ptr(); variables_apply(env);
}
int variables_set(env_vars_t *env,const char *key,const char *value) {
    if(!env || !env->variables || !key || environment_readonly(key)) return -1;
    uint32_t marker = get_free_ptr();
    system_variables_t *candidate=(void *)malloc(sizeof *candidate,0,0); if(!candidate) return -1;
    *candidate=*env->variables;
    int result = environment_put(candidate,key,value) ? -1:save(env,candidate);
    heap_release(marker); env->free_mem_ptr = get_free_ptr(); return result;
}
int variables_settings(env_vars_t *env,uint32_t seconds,uint32_t position,uint32_t color,uint32_t secondary) {
    if(!env || !env->variables || seconds>=86400 || position>1 || color>255 || secondary>15) return -1;
    uint32_t marker = get_free_ptr();
    system_variables_t *candidate=(void *)malloc(sizeof *candidate,0,0); if(!candidate) return -1;
    *candidate=*env->variables;
    numeric(candidate,"CLOCK_OFFSET",(seconds+86400-desktop_rtc_seconds())%86400);
    numeric(candidate,"COLOR_FOREGROUND",color&15); numeric(candidate,"COLOR_BACKGROUND",color>>4); numeric(candidate,"COLOR_SECONDARY",secondary);
    environment_put(candidate,"STATUS_POSITION",position ? "bottom":"top");
    int result = save(env,candidate);
    heap_release(marker); env->free_mem_ptr = get_free_ptr(); return result;
}
