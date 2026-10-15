#include "ide.h"
#include "sys_api.h"
#define TOKENS 16384u
#define SCOPES 512u
#define HEADERS 32u
typedef struct { uint32_t at, end; uint16_t scope; uint8_t kind; } token_t;
typedef struct { uint32_t start, end; } scope_t;
struct ide_index {
    ide_symbol_t symbols[IDE_SYMBOLS]; uint32_t count;
    token_t tokens[TOKENS]; uint32_t token_count;
    scope_t scopes[SCOPES]; uint32_t scope_count;
    char visited[HEADERS][560]; uint32_t visits;
    uint8_t headers[8][IDE_SOURCE];
    const char *notice;
};
static int alpha(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int ident(uint8_t c) { return alpha(c) || digit(c); }
static uint32_t length(const char *s) { uint32_t n=0; while(s[n]) n++; return n; }
static int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int compare(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return (uint8_t)*a-(uint8_t)*b; }
static void copy(char *out,uint32_t cap,const char *in) { uint32_t n=0; while(in[n] && n+1<cap) { out[n]=in[n]; n++; } out[n]=0; }
static int is(const uint8_t *s,token_t t,const char *word) {
    uint32_t n=length(word); if(t.end-t.at!=n) return 0;
    for(uint32_t i=0;i<n;i++) if(s[t.at+i]!=(uint8_t)word[i]) return 0;
    return 1;
}
static int keyword(const uint8_t *s,token_t t) {
    const char *words[]={"int","char","void","if","else","while","break","continue","return","sizeof",
        "const","unsigned","signed","short","long","static","extern","typedef","struct","enum","volatile"};
    for(uint32_t i=0;i<sizeof words/sizeof *words;i++) if(is(s,t,words[i])) return 1;
    return 0;
}
static int type(const uint8_t *s,token_t t) {
    return is(s,t,"int") || is(s,t,"char") || is(s,t,"void") ||
        (t.kind==1 && t.end-t.at>2 && s[t.end-2]=='_' && s[t.end-1]=='t');
}
static void paint(uint8_t *styles,uint32_t start,uint32_t end,uint8_t color) {
    if(styles) for(uint32_t i=start;i<end;i++) styles[i]=color;
}
static void lex(ide_index_t *index,const uint8_t *s,uint32_t size,uint8_t *styles) {
    index->token_count=0; index->scope_count=1;
    index->scopes[0]=(scope_t){0,size};
    uint16_t stack[SCOPES]={0}; uint32_t depth=0; int line_start=1;
    paint(styles,0,size,IDE_PLAIN);
    for(uint32_t at=0;at<size;) {
        uint32_t start=at; uint8_t c=s[at++];
        if(c=='\n') { line_start=1; continue; }
        if(c==' ' || c=='\t' || c=='\r') continue;
        if(c=='#' && line_start) {
            while(at<size && s[at]!='\n') at++;
            paint(styles,start,at,IDE_DIRECTIVE); continue;
        }
        line_start=0;
        if(c=='/' && at<size && (s[at]=='/' || s[at]=='*')) {
            int block=s[at++]=='*';
            if(block) {
                while(at<size && !(s[at-1]=='*' && s[at]=='/')) at++;
                if(at<size) at++;
            } else while(at<size && s[at]!='\n') at++;
            paint(styles,start,at,IDE_COMMENT); continue;
        }
        if(c=='"' || c=='\'') {
            while(at<size) { uint8_t next=s[at++]; if(next=='\\' && at<size) at++; else if(next==c || next=='\n') break; }
            paint(styles,start,at,IDE_STRING); continue;
        }
        uint8_t kind=0;
        if(alpha(c)) { kind=1; while(at<size && ident(s[at])) at++; }
        else if(digit(c)) { kind=2; while(at<size && (ident(s[at]) || s[at]=='.')) at++; paint(styles,start,at,IDE_NUMBER); }
        if(index->token_count==TOKENS) { index->notice="Source index token limit reached."; break; }
        token_t t={start,at,stack[depth],kind};
        index->tokens[index->token_count++]=t;
        if(kind==1 && keyword(s,t)) paint(styles,start,at,IDE_KEYWORD);
        if(c=='{') {
            if(index->scope_count==SCOPES || depth+1==SCOPES) { index->notice="Source index scope limit reached."; break; }
            uint16_t id=index->scope_count++; index->scopes[id]=(scope_t){at,size}; stack[++depth]=id;
        } else if(c=='}' && depth) index->scopes[stack[depth--]].end=start;
    }
}
static void normalized(char *out,uint32_t cap,const uint8_t *s,uint32_t start,uint32_t end) {
    uint32_t n=0; int space=0;
    for(uint32_t i=start;i<end && n+1<cap;i++) {
        if(s[i]=='\n' || s[i]=='\r' || s[i]=='\t' || s[i]==' ') { space=n>0; continue; }
        if(space && n+2<cap) out[n++]=' ';
        space=0; out[n++]=s[i];
    }
    out[n]=0;
}
static void add(ide_index_t *index,const uint8_t *s,token_t name,uint32_t from,uint32_t to,
                scope_t scope,int function,const char *origin,int imported) {
    if(name.end-name.at>=IDE_NAME) { index->notice="A symbol name exceeds the index limit."; return; }
    char text[IDE_NAME]; normalized(text,sizeof text,s,name.at,name.end);
    for(uint32_t i=0;i<index->count;i++) {
        ide_symbol_t *old=&index->symbols[i];
        if(equal(old->name,text) && old->start==scope.start && old->end==scope.end &&
           old->imported==imported && equal(old->origin,origin)) {
            if(function) normalized(old->signature,sizeof old->signature,s,from,to);
            return;
        }
    }
    if(index->count==IDE_SYMBOLS) { index->notice="Source index symbol limit reached."; return; }
    ide_symbol_t *symbol=&index->symbols[index->count++];
    copy(symbol->name,sizeof symbol->name,text);
    normalized(symbol->signature,sizeof symbol->signature,s,from,to);
    copy(symbol->origin,sizeof symbol->origin,origin);
    symbol->position=name.at; symbol->start=scope.start; symbol->end=scope.end;
    symbol->function=function; symbol->imported=imported;
}
static void declarations(ide_index_t *index,const uint8_t *s,uint32_t size,const char *origin,int imported,uint8_t *styles) {
    token_t *t=index->tokens; uint32_t count=index->token_count;
    for(uint32_t i=0;i<count;i++) {
        if(!type(s,t[i])) continue;
        uint32_t name=i+1;
        while(name<count && (is(s,t[name],"*") || is(s,t[name],"const"))) name++;
        if(name>=count || t[name].kind!=1 || keyword(s,t[name])) continue;
        if(name+1<count && is(s,t[name+1],"(")) {
            uint32_t close=name+2, nesting=1;
            while(close<count) {
                if(is(s,t[close],"(")) nesting++;
                if(is(s,t[close],")") && !--nesting) break;
                close++;
            }
            if(close==count) continue;
            if(!t[i].scope) add(index,s,t[name],t[i].at,t[close].end,(scope_t){0,size},1,origin,imported);
            paint(styles,t[name].at,t[name].end,IDE_FUNCTION);
            if(!imported && close+1<count && is(s,t[close+1],"{") && close+2<count) {
                scope_t body=index->scopes[t[close+2].scope];
                for(uint32_t p=name+2;p<close;p++) if(type(s,t[p])) {
                    uint32_t v=p+1; while(v<close && is(s,t[v],"*")) v++;
                    if(v<close && t[v].kind==1) {
                        add(index,s,t[v],t[p].at,t[v].end,body,0,"local parameter",0);
                        paint(styles,t[v].at,t[v].end,IDE_VARIABLE);
                    }
                }
            }
            i=close; continue;
        }
        if(imported && t[i].scope) continue;
        scope_t scope=index->scopes[t[i].scope];
        add(index,s,t[name],t[i].at,t[name].end,scope,0,imported?origin:t[i].scope?"local variable":"local global",imported);
        paint(styles,t[name].at,t[name].end,IDE_VARIABLE);
        /* Additional declarators in int a=..., b=...; skip commas inside calls. */
        uint32_t nesting=0;
        for(uint32_t p=name+1;p<count;p++) {
            if(is(s,t[p],";") || is(s,t[p],"{") || is(s,t[p],"}")) break;
            if(is(s,t[p],"(") || is(s,t[p],"[")) nesting++;
            if((is(s,t[p],")") || is(s,t[p],"]")) && nesting) nesting--;
            if(!nesting && is(s,t[p],",") && p+1<count) {
                uint32_t v=p+1; while(v<count && is(s,t[v],"*")) v++;
                if(v<count && t[v].kind==1) {
                    uint32_t before=index->count;
                    add(index,s,t[v],t[i].at,t[v].end,scope,0,imported?origin:t[i].scope?"local variable":"local global",imported);
                    if(index->count>before) {
                        char *signature=index->symbols[before].signature;
                        normalized(signature,IDE_SIGNATURE,s,t[i].at,t[i].end);
                        uint32_t n=length(signature); signature[n++]=' ';
                        normalized(signature+n,IDE_SIGNATURE-n,s,t[p+1].at,t[v].end);
                    }
                    paint(styles,t[v].at,t[v].end,IDE_VARIABLE);
                }
            }
        }
    }
    if(styles) for(uint32_t i=0;i<count;i++) if(t[i].kind==1 && !keyword(s,t[i])) {
        for(uint32_t j=0;j<index->count;j++) if(is(s,t[i],index->symbols[j].name)) {
            paint(styles,t[i].at,t[i].end,index->symbols[j].function?IDE_FUNCTION:IDE_VARIABLE); break;
        }
        if(i+1<count && is(s,t[i+1],"(")) paint(styles,t[i].at,t[i].end,IDE_FUNCTION);
    }
}
static const char *type_name(uint32_t type) {
    switch(type) { case SZ_CHAR:return "char"; case SZ_VOID:return "void";
        case SZ_INT_PTR:return "int*"; case SZ_CHAR_PTR:return "char*";
        case SZ_VOID_PTR:return "void*"; default:return "int"; }
}
static void append(char *out,const char *part) { copy(out+length(out),IDE_SIGNATURE-length(out),part); }
static void builtin(ide_index_t *index,const char *name,uint32_t result,uint32_t argc,const uint32_t *args) {
    ide_symbol_t *symbol=&index->symbols[index->count++];
    copy(symbol->name,sizeof symbol->name,name); copy(symbol->origin,sizeof symbol->origin,"built-in kernel API");
    symbol->signature[0]=0; append(symbol->signature,type_name(result)); append(symbol->signature," ");
    append(symbol->signature,name); append(symbol->signature,"(");
    for(uint32_t i=0;i<argc;i++) { if(i) append(symbol->signature,", "); append(symbol->signature,type_name(args[i])); }
    append(symbol->signature,")"); symbol->function=1; symbol->imported=1;
    symbol->position=0; symbol->start=0; symbol->end=0xffffffffu;
}
static void imports(ide_index_t *index,const uint8_t *s,uint32_t size,const char *parent,uint32_t depth,ide_include_t include,void *ctx) {
    if(!include) return;
    int line_start=1;
    for(uint32_t at=0;at<size;) {
        uint8_t c=s[at++];
        if(c=='\n') { line_start=1; continue; }
        if(c==' ' || c=='\t' || c=='\r') continue;
        if(c=='/' && at<size && s[at]=='/') { while(at<size && s[at]!='\n') at++; continue; }
        if(c=='/' && at<size && s[at]=='*') {
            at++;
            while(at<size && !(s[at-1]=='*' && s[at]=='/')) { if(s[at]=='\n') line_start=1; at++; }
            if(at<size) at++;
            continue;
        }
        if(c=='"' || c=='\'') {
            while(at<size) { uint8_t next=s[at++]; if(next=='\\' && at<size) at++; else if(next==c || next=='\n') break; }
            line_start=0; continue;
        }
        if(c!='#' || !line_start) { line_start=0; continue; }
        uint32_t start=at; while(at<size && s[at]!='\n') at++; uint32_t end=at;
        line_start=0;
        while(start<end && (s[start]==' ' || s[start]=='\t')) start++;
        const char *directive="include"; uint32_t p=0;
        while(p<7 && start<end && s[start]==(uint8_t)directive[p]) { start++; p++; }
        if(p!=7) continue;
        while(start<end && (s[start]==' ' || s[start]=='\t')) start++;
        if(start==end || (s[start]!='"' && s[start]!='<')) continue;
        int local=s[start++]=='"'; uint8_t delimiter=local?'"':'>'; char file[560],resolved[560]; uint32_t n=0;
        while(start<end && s[start]!=delimiter && n+1<sizeof file) file[n++]=s[start++];
        file[n]=0;
        if(start==end || s[start]!=delimiter) continue;
        if(depth==8) { index->notice="Include nesting exceeds the index limit."; continue; }
        int bytes=include(ctx,file,parent,local,index->headers[depth],IDE_SOURCE,resolved,sizeof resolved);
        if(bytes<0 || bytes>(int)IDE_SOURCE) { index->notice="An include could not be indexed."; continue; }
        int visited=0; for(uint32_t i=0;i<index->visits;i++) if(equal(index->visited[i],resolved)) visited=1;
        if(visited) continue;
        if(index->visits==HEADERS) { index->notice="Source index include limit reached."; continue; }
        copy(index->visited[index->visits++],560,resolved);
        lex(index,index->headers[depth],bytes,0);
        declarations(index,index->headers[depth],bytes,resolved,1,0);
        imports(index,index->headers[depth],bytes,resolved,depth+1,include,ctx);
    }
}
uint32_t ide_workspace_size(void) { return sizeof(ide_index_t); }
void ide_analyze(ide_index_t *index,const uint8_t *s,uint32_t size,const char *file,uint8_t *styles,ide_include_t include,void *ctx) {
    index->count=0; index->visits=0; index->notice="";
    if(file && *file) copy(index->visited[index->visits++],560,file);
    if(size>IDE_SOURCE) { size=IDE_SOURCE; index->notice="Source exceeds 64 KiB."; }
#define SZ_SERVICE(id,name,result,argc,a,b,c,d,e) builtin(index,#name,result,argc,(uint32_t[]){a,b,c,d,e});
#include "sys_api.def"
#undef SZ_SERVICE
    lex(index,s,size,styles); declarations(index,s,size,"local function",0,styles);
    imports(index,s,size,file,0,include,ctx);
}
const char *ide_notice(const ide_index_t *index) { return index->notice; }
const ide_symbol_t *ide_symbol(const ide_index_t *index,uint32_t id) { return id<index->count?&index->symbols[id]:0; }
uint32_t ide_complete(const ide_index_t *index,const uint8_t *s,uint32_t size,uint32_t cursor,
                      const uint8_t *styles,int force,uint32_t *matches,uint32_t capacity,uint32_t *start) {
    if(cursor>size) cursor=size;
    *start=cursor; while(*start && ident(s[*start-1])) (*start)--;
    uint32_t prefix=cursor-*start;
    if((!prefix && !force) || (prefix && !alpha(s[*start]))) return 0;
    if(cursor && styles && (styles[cursor-1]==IDE_STRING || styles[cursor-1]==IDE_COMMENT || styles[cursor-1]==IDE_DIRECTIVE)) return 0;
    uint32_t count=0;
    for(uint32_t i=0;i<index->count;i++) {
        const ide_symbol_t *symbol=&index->symbols[i];
        if(!symbol->function && !symbol->imported && (cursor<symbol->start || cursor>symbol->end || cursor<symbol->position)) continue;
        uint32_t n=0; while(n<prefix && symbol->name[n] && symbol->name[n]==(char)s[*start+n]) n++;
        if(n!=prefix) continue;
        /* Prefer the innermost visible declaration when a name is shadowed. */
        uint32_t duplicate=count;
        for(uint32_t j=0;j<count;j++) if(equal(ide_symbol(index,matches[j])->name,symbol->name)) { duplicate=j; break; }
        if(duplicate<count) {
            const ide_symbol_t *old=ide_symbol(index,matches[duplicate]);
            if(symbol->imported || (!old->imported && symbol->start<=old->start)) continue;
            matches[duplicate]=i;
        } else if(count<capacity) matches[count++]=i;
    }
    for(uint32_t i=1;i<count;i++) {
        uint32_t id=matches[i],j=i;
        while(j && compare(index->symbols[id].name,index->symbols[matches[j-1]].name)<0) { matches[j]=matches[j-1]; j--; }
        matches[j]=id;
    }
    return count;
}
