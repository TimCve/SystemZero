#include "toolchain.h"
int main(env_vars_t *env,char *args) {
    char token[560],output[560]; sz_link_input_t objects[SZ_LINK_OBJECTS]; uint32_t count=0;
    if(tc_word(&args,token,sizeof token)<1 || tc_word(&args,output,sizeof output)<1) goto usage;
    int next;
    while((next=tc_word(&args,token,sizeof token))>0) { if(count==SZ_LINK_OBJECTS || !tc_object(env,token,&objects[count++])) return 1; }
    if(!count || next<0) goto usage;
    return tc_link(objects,count,output);
usage: print("Usage: ld OUTPUT OBJECT.o [OBJECT.o or LIBRARY.a ...]"); print_newline(); return 1;
}
