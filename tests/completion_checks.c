#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shared/completion.h"
#include "../shared/arguments.h"
#include "../shared/text_gutter.h"
static sz_completion_t state;
static const char *config="edit=paths\nlist=paths\nzedit=paths\ncopy=paths\ncd=directories\n";
static int read_config(void *ctx,const char *name,char *out,uint32_t cap) {
    (void)ctx; assert(!strcmp(name,SZ_COMPLETE_FILE));
    uint32_t n=strlen(config); if(n>cap) n=cap; memcpy(out,config,n); return n;
}
static int listing(void *ctx,const char *path,int executables,sz_completion_visit_fn visit,void *arg) {
    (void)ctx; (void)executables;
    if(!strcmp(path,"." ) || !strcmp(path,"/home/")) {
        visit(arg,"alpha.txt",0,0); visit(arg,"alpine",1,0); visit(arg,"beta.c",0,0);
        visit(arg,"two words.txt",0,0); visit(arg,"twofold",1,0); return 0;
    }
    if(!strcmp(path,"alpine/")) { visit(arg,"nest.sz",0,0); return 0; }
    if(!strcmp(path,"sys:/")) { visit(arg,"bin",1,0); visit(arg,"docs",1,0); return 0; }
    if(!strcmp(path,"sys:/bin")) {
        visit(arg,"edit",0,1); visit(arg,"env",0,1); visit(arg,"example.txt",0,0); visit(arg,"folder",1,0); return 0;
    }
    if(!strcmp(path,"other:/bin")) { visit(arg,"extra",0,1); visit(arg,"edit",0,1); return 0; }
    return -1;
}
static const char *search(void *ctx) { (void)ctx; return "sys:/bin;other:/bin"; }
static const sz_completion_io_t io={0,read_config,listing,search};
static void complete(const char *input,const char *expected) {
    char line[2000]; strcpy(line,input); int cursor=strlen(line);
    sz_complete(&state,&io,line,sizeof line,&cursor,0,0);
    if(strcmp(line,expected)) fprintf(stderr,"%s => [%s], expected [%s]\n",input,line,expected);
    assert(!strcmp(line,expected)); assert(cursor==(int)strlen(expected));
}
int main(void) {
    assert(sz_completion_mode(" edit = paths # ok\nedit=directories\nedit=wat\n",sizeof(" edit = paths # ok\nedit=directories\nedit=wat\n")-1,"sys:/bin/edit")==2);
    assert(!sz_completion_mode("edit=paths\nedit=none",sizeof("edit=paths\nedit=none")-1,"edit"));
    complete("ed","edit "); complete("ex","extra "); complete("folder","folder");
    complete("edit al","edit alp"); complete("edit alpine/n","edit alpine/nest.sz ");
    complete("cd al","cd alpine/"); complete("cd be","cd be");
    complete("missing al","missing al"); complete("edit absent/fi","edit absent/fi");
    complete("edit sys:/do","edit sys:/docs/"); complete("edit /home/be","edit /home/beta.c ");
    complete("edit \"two w","edit \"two words.txt\" ");
    complete("edit two\\ w","edit \"two words.txt\" ");
    complete("","e"); complete("edit ","edit ");
    char line[2000]="e"; int cursor=1;
    sz_complete(&state,&io,line,sizeof line,&cursor,0,0); assert(!strcmp(line,"e"));
    sz_complete(&state,&io,line,sizeof line,&cursor,1,0); assert(!strcmp(line,"edit "));
    sz_complete(&state,&io,line,sizeof line,&cursor,1,0); assert(!strcmp(line,"env "));
    sz_complete(&state,&io,line,sizeof line,&cursor,1,0); assert(!strcmp(line,"extra "));
    sz_complete(&state,&io,line,sizeof line,&cursor,1,1); assert(!strcmp(line,"env "));
    strcpy(line,"copy beTA.c alpha"); cursor=7;
    sz_complete(&state,&io,line,sizeof line,&cursor,0,0); assert(!strcmp(line,"copy beta.c alpha") && cursor==11);
    strcpy(line,"edit al"); cursor=7;
    assert(!sz_complete(&state,&io,line,8,&cursor,0,0)); assert(!strcmp(line,"edit al") && cursor==7);
    config="edit=none\n"; complete("edit al","edit al");
    const char *args="  \"two words\" 'plain \\ stuff' a\\ b"; char word[64];
    assert(sz_arg_next(&args,word,sizeof word)==9 && !strcmp(word,"two words"));
    assert(sz_arg_next(&args,word,sizeof word)==13 && !strcmp(word,"plain \\ stuff"));
    assert(sz_arg_next(&args,word,sizeof word)==3 && !strcmp(word,"a b"));
    assert(sz_arg_next(&args,word,sizeof word)==0 && !*word);
    args="\"unfinished"; assert(sz_arg_next(&args,word,sizeof word)<0);
    char quoted[128]; assert(sz_arg_quote("a \"b\" \\ c",quoted,sizeof quoted)>0);
    args=quoted; assert(sz_arg_next(&args,word,sizeof word)>0 && !strcmp(word,"a \"b\" \\ c"));
    uint8_t data[1000]; memset(data,'\n',sizeof data);
    assert(text_gutter_width(data,0)==6 && text_source_line(data,0)==1);
    assert(text_gutter_width(data,999)==7 && text_source_line(data,999)==1000);
    return 0;
}
