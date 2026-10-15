#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/compiler.h"
#include "../shared/linker.h"
#include "../shared/elf.h"
#include "../shared/preprocess.h"
static uint8_t object[262144],library[262144],output[1048576],loaded[1048576];
static int include(void *ctx,const char *path,const char *parent,int local,uint8_t *out,uint32_t capacity,char *resolved,uint32_t path_capacity) {
    (void)ctx; (void)parent; (void)local; const char *text="#ifndef MY_H\n#define MY_H\n#define VALUE 7\nint add(int,int);\n#endif\n";
    if(strcmp(path,"math.h") || strlen(text)>capacity || strlen(path)>=path_capacity) return -1;
    strcpy(resolved,path);
    memcpy(out,text,strlen(text)); return strlen(text);
}
int main(int argc,char **argv) {
    void *compiler=malloc(szc_workspace_size()),*linker=malloc(sz_link_workspace_size()),*prep=malloc(sz_preprocess_workspace_size()); assert(compiler && linker && prep);
    szc_result_t result; const char *source="int add(int,int); char text[]=\"hello\"; int main(){print(text);return add(4,7);}";
    assert(szc_compile_object((const uint8_t *)source,strlen(source),object,sizeof object,compiler,&result)); uint32_t n=result.size;
    const char *lib="int add(int a,int b){return a+b;}";
    assert(szc_compile_object((const uint8_t *)lib,strlen(lib),library,sizeof library,compiler,&result));
    sz_link_input_t inputs[]={{object,n,"main.o"},{library,result.size,"add.o"}}; sz_link_result_t linked;
    assert(sz_link(inputs,2,output,sizeof output,linker,&linked)); elf_plan_t plan; assert(elf_plan(output,linked.size,&plan)); assert(elf_load(output,linked.size,&plan,loaded,sizeof loaded));
    assert(!sz_link(inputs,1,output,sizeof output,linker,&linked)); assert(!strcmp(linked.symbol,"add"));
    inputs[0]=inputs[1]; assert(!sz_link(inputs,2,output,sizeof output,linker,&linked)); assert(strstr(linked.error,"Duplicate"));
    const char *program="int main(){return 7;}";
    assert(szc_compile_object((const uint8_t *)program,strlen(program),object,sizeof object,compiler,&result)); inputs[0]=(sz_link_input_t){object,result.size,"main.o"};
    uint32_t seed=7; for(uint32_t i=0;i<3000;i++) {
        uint8_t bad[1024]; for(uint32_t j=0;j<sizeof bad;j++) { seed=seed*1664525u+1013904223u; bad[j]=seed>>24; }
        sz_link_input_t input={bad,sizeof bad,"fuzz.o"}; sz_link(&input,1,output,sizeof output,linker,&linked);
    }
    for(uint32_t i=0;i<result.size;i+=3) { uint8_t old=object[i]; object[i]^=0x80; sz_link(inputs,1,output,sizeof output,linker,&linked); object[i]=old; }
    if(argc>1) {
        FILE *file=fopen(argv[1],"rb"); assert(file); size_t bytes=fread(library,1,sizeof library,file); assert(!ferror(file)); fclose(file);
        source="int sz_print(char*); int main(){return sz_print(\"linked\");}";
        assert(szc_compile_object((const uint8_t *)source,strlen(source),object,sizeof object,compiler,&result));
        inputs[0]=(sz_link_input_t){object,result.size,"main.o"}; inputs[1]=(sz_link_input_t){library,bytes,"system.a"};
        assert(sz_link(inputs,2,output,sizeof output,linker,&linked)); assert(elf_plan(output,linked.size,&plan)); assert(elf_load(output,linked.size,&plan,loaded,sizeof loaded));
    }
    const char *text="#include <math.h>\n#include <math.h>\nchar x[]=\"VALUE\"; int MY_VALUE=3; int main(){return add(VALUE,MY_VALUE);}\n";
    sz_preprocess_result_t preprocessing; int bytes=sz_preprocess((const uint8_t *)text,strlen(text),"main.sz",output,sizeof output,prep,include,0,&preprocessing);
    assert(bytes>0); output[bytes]=0; assert(strstr((char *)output,"add(7,MY_VALUE)") && strstr((char *)output,"\"VALUE\""));
    assert(szc_compile_object(output,bytes,object,sizeof object,compiler,&result));
    text="#include <missing.h>\n"; assert(sz_preprocess((const uint8_t *)text,strlen(text),"x",output,sizeof output,prep,include,0,&preprocessing)==-1);
    free(prep); free(linker); free(compiler); puts("Compiler objects, symbols, archives, relocations, headers, and malformed input checks passed."); return 0;
}
