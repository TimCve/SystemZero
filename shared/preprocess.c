#include "preprocess.h"
#include <stddef.h>
#define MACROS 64u
#define DEPTH 8u
typedef struct { char name[32],value[128]; } macro_t;
typedef struct { macro_t macros[MACROS]; uint32_t count,used; uint8_t files[DEPTH][65536]; char paths[DEPTH][560]; uint8_t *output; uint32_t capacity; sz_include_fn load; void *context; sz_preprocess_result_t *result; } preprocess_t;
static int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int alpha(uint8_t c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_'; }
static int identifier(uint8_t c) { return alpha(c) || (c>='0' && c<='9'); }
static int error(preprocess_t *p,const char *message,uint32_t line) { if(!p->result->error) { p->result->error=message; p->result->line=line; } return 0; }
static int emit(preprocess_t *p,uint8_t c,uint32_t line) { if(p->used==p->capacity) return error(p,"Expanded source exceeds 64 KiB",line); p->output[p->used++]=c; return 1; }
static int macro(preprocess_t *p,const char *name) { for(uint32_t i=0;i<p->count;i++) if(equal(p->macros[i].name,name)) return i; return -1; }
static int process(preprocess_t *p,const uint8_t *text,uint32_t size,const char *filename,uint32_t depth) {
    if(depth>=DEPTH) return error(p,"Include nesting limit exceeded",1);
    uint32_t at=0,line=1,level=0; int active[16]={1},else_seen[16]={0},quote=0,block=0,line_comment=0;
    while(at<size) {
        uint32_t start=at; while(at<size && text[at]!='\n') at++; uint32_t end=at; if(at<size) at++;
        uint32_t first=start; while(first<end && (text[first]==' ' || text[first]=='\t')) first++;
        if(!block && !quote && first<end && text[first]=='#') {
            uint32_t i=first+1; while(i<end && (text[i]==' ' || text[i]=='\t')) i++;
            char directive[16]; uint32_t n=0; while(i<end && alpha(text[i]) && n+1<sizeof directive) directive[n++]=text[i++]; directive[n]=0;
            while(i<end && (text[i]==' ' || text[i]=='\t')) i++;
            char key[32]; n=0; uint32_t begin=i;
            while(i<end && identifier(text[i]) && n+1<sizeof key) key[n++]=text[i++];
            key[n]=0;
            if(equal(directive,"ifdef") || equal(directive,"ifndef")) {
                if(level==15 || !n) return error(p,"Invalid conditional include guard",line);
                int defined=macro(p,key)>=0; level++; active[level]=active[level-1] && (equal(directive,"ifdef") ? defined:!defined); else_seen[level]=0;
            } else if(equal(directive,"else")) {
                if(!level || else_seen[level]) return error(p,"Unexpected #else",line);
                active[level]=active[level-1] && !active[level]; else_seen[level]=1;
            } else if(equal(directive,"endif")) { if(!level) return error(p,"Unexpected #endif",line); level--; }
            else if(active[level]) {
                if(equal(directive,"include")) {
                    i=begin; if(i==end || (text[i]!='<' && text[i]!='"')) return error(p,"Expected a header path",line);
                    int local=text[i]=='"'; uint8_t close=local ? '"':'>'; i++;
                    char path[512]; n=0; while(i<end && text[i]!=close) { if(n+1>=sizeof path) return error(p,"Header path too long",line); path[n++]=text[i++]; } path[n]=0;
                    if(i==end || !n || !p->load) return error(p,"Invalid include directive",line);
                    p->paths[depth][0]=0;
                    int bytes=p->load(p->context,path,filename,local,p->files[depth],65536,p->paths[depth],560);
                    if(bytes<0 || bytes>65536) return error(p,"Included header not found or too large",line);
                    if(!process(p,p->files[depth],bytes,p->paths[depth][0] ? p->paths[depth]:path,depth+1)) return 0;
                } else if(equal(directive,"define")) {
                    if(!n || (i<end && text[i]=='(')) return error(p,"Only object-like #define macros are supported",line);
                    int index=macro(p,key); if(index<0) { if(p->count==MACROS) return error(p,"Macro limit exceeded",line); index=p->count++; }
                    for(uint32_t j=0;j<=n;j++) p->macros[index].name[j]=key[j];
                    while(i<end && (text[i]==' ' || text[i]=='\t')) i++;
                    n=0; while(i<end && text[i]!='\r') { if(n+1>=128) return error(p,"Macro value too long",line); p->macros[index].value[n++]=text[i++]; } p->macros[index].value[n]=0;
                } else return error(p,"Unsupported preprocessor directive",line);
            }
            if(!emit(p,'\n',line)) return 0;
        } else if(active[level]) {
            for(uint32_t i=start;i<at;i++) {
                uint8_t c=text[i],next=i+1<at ? text[i+1]:0;
                if(c=='\n') line_comment=0;
                if(!quote && !line_comment && c=='/' && next=='*') block=1;
                if(!block && !quote && c=='/' && next=='/') line_comment=1;
                if(!block && !line_comment && !quote && alpha(c)) {
                    char key[32]; uint32_t n=0,j=i; while(j<end && identifier(text[j])) { if(n+1<sizeof key) key[n++]=text[j]; j++; } key[n]=0;
                    int index=j-i<32 ? macro(p,key):-1;
                    if(index>=0) { const char *v=p->macros[index].value; while(*v) if(!emit(p,*v++,line)) return 0; }
                    else for(uint32_t k=i;k<j;k++) if(!emit(p,text[k],line)) return 0;
                    i=j-1; continue;
                }
                if(!emit(p,c,line)) return 0;
                if(block && c=='*' && next=='/') { if(!emit(p,next,line)) return 0; i++; block=0; }
                else if(!block && !line_comment) {
                    if(quote && c=='\\' && next) { if(!emit(p,next,line)) return 0; i++; }
                    else if(c==quote) quote=0;
                    else if(!quote && (c=='"' || c=='\'')) quote=c;
                }
            }
        } else if(!emit(p,'\n',line)) return 0;
        line++;
    }
    if(level) return error(p,"Unterminated include guard",line);
    return 1;
}
uint32_t sz_preprocess_workspace_size(void) { return sizeof(preprocess_t); }
int sz_preprocess(const uint8_t *text,uint32_t size,const char *filename,uint8_t *output,uint32_t capacity,void *workspace,sz_include_fn load,void *context,sz_preprocess_result_t *result) {
    if(!result) return -1;
    *result=(sz_preprocess_result_t){0};
    if(!text || size>65536 || !filename || !output || !workspace || (uintptr_t)workspace%_Alignof(preprocess_t)) { result->error="Invalid preprocessor buffers"; return -1; }
    preprocess_t *p=workspace; p->count=p->used=0; p->output=output; p->capacity=capacity; p->load=load; p->context=context; p->result=result;
    return process(p,text,size,filename,0) ? (int)p->used:-1;
}
