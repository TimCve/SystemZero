#include "source_includes.h"
#include "../shared/compiler.h"
#include "../shared/preprocess.h"
int main(env_vars_t *env,char *args) {
    char command[560],source_name[560],output[560],token[560]; int object=0;
    if(tc_word(&args,command,sizeof command)<1 || tc_word(&args,source_name,sizeof source_name)<1) goto usage;
    if(!strcmp(source_name,"-c")) { object=1; if(tc_word(&args,source_name,sizeof source_name)<1) goto usage; }
    if(tc_word(&args,output,sizeof output)<1) goto usage;
    if(!strcmp(source_name,output)) { print("Source and output must have different names."); print_newline(); return 1; }
    sz_link_input_t objects[SZ_LINK_OBJECTS]; uint32_t count=1; includes_t includes={.env=env};
    int next;
    while((next=tc_word(&args,token,sizeof token))>0) {
        if(!strcmp(token,"-I")) { if(tc_word(&args,includes.extra_include,sizeof includes.extra_include)<1) goto usage; }
        else { if(object || count==SZ_LINK_OBJECTS || !tc_object(env,token,&objects[count++])) return 1; }
    }
    if(next<0) goto usage;
    inode_t in=get_file_info(source_name); if(in.valid!=ESFS_FILE || in.size>SZC_MAX_SOURCE) { print("Source file is missing or exceeds 64 KiB."); print_newline(); return 1; }
    uint8_t *source=(void *)malloc(in.size ? in.size:1,0,0),*expanded=(void *)malloc(SZC_MAX_SOURCE,0,0),*image=(void *)malloc(SZC_MAX_IMAGE,0,0);
    void *preprocessor=(void *)malloc(sz_preprocess_workspace_size(),0,0),*workspace=(void *)malloc(szc_workspace_size(),0,0);
    if(!source || !expanded || !image || !preprocessor || !workspace) return 1;
    if(file_read_bytes(source_name,source,in.size,0)!=(int)in.size) return 1;
    sz_preprocess_result_t prep; int bytes=sz_preprocess(source,in.size,source_name,expanded,SZC_MAX_SOURCE,preprocessor,source_include,&includes,&prep);
    if(bytes<0) { print(source_name); print(":"); print_dec(prep.line); print(": "); print((char *)prep.error); print_newline(); return 1; }
    szc_result_t result;
    if(!szc_compile_object(expanded,bytes,image,SZC_MAX_IMAGE,workspace,&result)) {
        print(source_name); print(":"); print_dec(result.line); print(":"); print_dec(result.column); print(": "); print((char *)result.error); print_newline(); return 1;
    }
    if(object) {
        if(!file_replace(output,image,result.size)) return 1;
        print("Compiled object "); print(output); print_newline(); return 0;
    }
    objects[0]=(sz_link_input_t){image,result.size,source_name}; return tc_link(objects,count,output);
usage: print("Usage: cc [-c] SOURCE OUTPUT [OBJECT.o or LIBRARY.a ...] [-I DIRECTORY]"); print_newline(); return 1;
}
