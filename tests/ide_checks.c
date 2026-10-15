#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/ide.h"
static int loads;
static int include(void *ctx,const char *file,const char *parent,int local,
                   uint8_t *out,uint32_t cap,char *resolved,uint32_t pathcap) {
    (void)ctx; (void)parent; (void)local;
    loads++;
    const char *text=0;
    if(!strcmp(file,"io.h")) text="#include \"nested.h\"\nint sz_print(char* message);\n";
    if(!strcmp(file,"nested.h")) text="#include \"io.h\"\nint sdk_add(int left, int right);\nextern int imported_value;\n";
    if(!text) return -1;
    assert(strlen(text)<=cap); assert(strlen(file)+6<pathcap);
    snprintf(resolved,pathcap,"api:/%s",file); memcpy(out,text,strlen(text)); return strlen(text);
}
static const ide_symbol_t *find(ide_index_t *index,uint8_t *source,uint32_t cursor,uint8_t *styles,const char *name) {
    uint32_t matches[IDE_MATCHES],start;
    uint32_t count=ide_complete(index,source,strlen((char *)source),cursor,styles,1,matches,IDE_MATCHES,&start);
    for(uint32_t i=0;i<count;i++) { const ide_symbol_t *symbol=ide_symbol(index,matches[i]); if(!strcmp(symbol->name,name)) return symbol; }
    return 0;
}
int main(void) {
    ide_index_t *index=malloc(ide_workspace_size()); uint8_t *styles=malloc(IDE_SOURCE),*source=calloc(IDE_SOURCE+1,1);
    assert(index && styles && source);
    strcpy((char *)source,
        "#include \"io.h\"\n/*\n#include \"missing.h\"\nint ghost;\n*/\n"
        "int shared;\nint helper(int count, char* message) { int local_value = 12; return count; }\n"
        "int main() { int shared = 2, second = 3; { char shared = 'q'; shared; } shared;\n"
        "// ignored print and helper\nprint(\"text and \\\"quote\");\nreturn helper(1, \"x\"); }\n");
    uint32_t size=strlen((char *)source);
    ide_analyze(index,source,size,"main.sz",styles,include,0);
    assert(!*ide_notice(index)); assert(loads==3); /* Cycle is detected, commented include is ignored. */
    assert(styles[0]==IDE_DIRECTIVE);
    assert(styles[strstr((char *)source,"ghost")-(char *)source]==IDE_COMMENT);
    assert(styles[strstr((char *)source,"12")-(char *)source]==IDE_NUMBER);
    assert(styles[strstr((char *)source,"int shared;")-(char *)source]==IDE_KEYWORD);
    assert(styles[strstr((char *)source,"helper(")-(char *)source]==IDE_FUNCTION);
    assert(styles[strstr((char *)source,"text and")-(char *)source]==IDE_STRING);
    uint32_t cursor=strstr((char *)source,"return helper")-(char *)source;
    const ide_symbol_t *s=find(index,source,cursor,styles,"helper"); assert(s && s->function && !s->imported);
    assert(!strcmp(s->signature,"int helper(int count, char* message)")); assert(!strcmp(s->origin,"local function"));
    assert(!find(index,source,cursor,styles,"local_value")); assert(!find(index,source,cursor,styles,"count"));
    assert(!find(index,source,cursor,styles,"ghost"));
    s=find(index,source,cursor,styles,"shared"); assert(s && !strcmp(s->signature,"int shared"));
    s=find(index,source,cursor,styles,"second"); assert(s && !strcmp(s->signature,"int second"));
    cursor=strstr((char *)source,"shared; } shared")-(char *)source;
    s=find(index,source,cursor,styles,"shared"); assert(s && !strcmp(s->signature,"char shared"));
    cursor=strstr((char *)source,"return count")-(char *)source;
    s=find(index,source,cursor,styles,"count"); assert(s && !strcmp(s->origin,"local parameter"));
    s=find(index,source,cursor,styles,"sdk_add"); assert(s && !strcmp(s->signature,"int sdk_add(int left, int right)"));
    assert(!strcmp(s->origin,"api:/nested.h")); assert(s->imported && s->function);
    s=find(index,source,cursor,styles,"print"); assert(s && !strcmp(s->signature,"int print(char*)"));
    assert(!strcmp(s->origin,"built-in kernel API"));
    uint32_t matches[IDE_MATCHES],start;
    cursor=strstr((char *)source,"text and")-(char *)source+3;
    assert(!ide_complete(index,source,size,cursor,styles,1,matches,IDE_MATCHES,&start));
    cursor=strstr((char *)source,"ignored print")-(char *)source+7;
    assert(!ide_complete(index,source,size,cursor,styles,1,matches,IDE_MATCHES,&start));
    strcpy((char *)source,"int main(){ int variable; vari"); size=strlen((char *)source);
    ide_analyze(index,source,size,"new.sz",styles,0,0);
    assert(ide_complete(index,source,size,size,styles,0,matches,IDE_MATCHES,&start)==1);
    assert(!strcmp(ide_symbol(index,matches[0])->name,"variable")); assert(start==size-4);
    /* Incomplete declarations, unterminated strings/comments and resource limits. */
    const char *incomplete[]={"int f(int x", "int main(){ char *s=\"unfinished", "/*", "int main(){int a[4]; int b; b"};
    for(uint32_t i=0;i<sizeof incomplete/sizeof *incomplete;i++) {
        strcpy((char *)source,incomplete[i]); size=strlen((char *)source);
        ide_analyze(index,source,size,"new.sz",styles,0,0);
        ide_complete(index,source,size,size,styles,1,matches,1,&start);
    }
    memset(source,';',IDE_SOURCE); source[IDE_SOURCE]=0;
    ide_analyze(index,source,IDE_SOURCE,"large.sz",styles,0,0); assert(*ide_notice(index));
    /* Prefixes at both source boundaries and a bounded zero-capacity result. */
    assert(!ide_complete(index,source,IDE_SOURCE,0,styles,1,matches,0,&start));
    ide_complete(index,source,IDE_SOURCE,IDE_SOURCE,styles,1,matches,IDE_MATCHES,&start);
    free(source); free(styles); free(index); puts("IDE syntax, scope, completion and include checks passed."); return 0;
}
