#ifndef SZ_ARGUMENTS_H
#define SZ_ARGUMENTS_H
#include <stdint.h>
static inline int sz_arg_space(char ch) { return ch==' ' || ch=='\t'; }
/* Decode one shell word. Quotes may enclose any part of a word; a backslash
   escapes the next character outside single quotes. A bounded prefix can be
   decoded with open quotes while completing at an insertion point. */
static inline int sz_arg_decode(const char *text,uint32_t length,char *out,uint32_t cap,int partial) {
    uint32_t n=0; char quote=0;
    if(!cap) return -1;
    for(uint32_t i=0;i<length;i++) {
        char ch=text[i];
        if(ch=='\\' && quote!='\'') {
            if(++i==length) { if(partial) break; return -1; }
            ch=text[i];
        } else if(ch=='\'' || ch=='"') {
            if(!quote) { quote=ch; continue; }
            if(quote==ch) { quote=0; continue; }
        }
        if(n+1>=cap) return -1;
        out[n++]=ch;
    }
    out[n]=0; return quote && !partial?-1:(int)n;
}
static inline uint32_t sz_arg_end(const char *text,uint32_t at) {
    char quote=0;
    while(text[at]) {
        char ch=text[at];
        if(!quote && sz_arg_space(ch)) break;
        if(ch=='\\' && quote!='\'' && text[at+1]) { at+=2; continue; }
        if(ch=='\'' || ch=='"') { if(!quote) quote=ch; else if(quote==ch) quote=0; }
        at++;
    }
    return at;
}
static inline int sz_arg_next(const char **text,char *out,uint32_t cap) {
    const char *p=*text; while(sz_arg_space(*p)) p++;
    uint32_t end=sz_arg_end(p,0);
    int result=sz_arg_decode(p,end,out,cap,0); *text=p+end; return result;
}
static inline int sz_arg_quote(const char *text,char *out,uint32_t cap) {
    uint32_t n=0; int quote=!*text;
    for(const char *p=text;*p;p++) if(sz_arg_space(*p) || *p=='"' || *p=='\'' || *p=='\\') quote=1;
    if(quote) { if(n+1>=cap) return -1; out[n++]='"'; }
    for(;*text;text++) {
        if(quote && (*text=='"' || *text=='\\')) { if(n+1>=cap) return -1; out[n++]='\\'; }
        if(n+1>=cap) return -1;
        out[n++]=*text;
    }
    if(quote) { if(n+1>=cap) return -1; out[n++]='"'; }
    out[n]=0; return (int)n;
}
#endif
