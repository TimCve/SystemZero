#include "completion.h"
#include "arguments.h"
static uint32_t length(const char *p) { uint32_t n=0; while(p[n]) n++; return n; }
static void copy(char *out,const char *in) { while((*out++=*in++)); }
static int compare(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return (uint8_t)*a-(uint8_t)*b; }
static int word_is(const char *p,uint32_t n,const char *word) {
    if(length(word)!=n) return 0;
    for(uint32_t i=0;i<n;i++) if(p[i]!=word[i]) return 0;
    return 1;
}
int sz_completion_mode(const char *data,uint32_t bytes,const char *command) {
    const char *name=command;
    for(const char *p=command;*p;p++) if(*p=='/' || *p==':') name=p+1;
    int result=0;
    for(uint32_t at=0;at<bytes;) {
        uint32_t end=at; while(end<bytes && data[end]!='\n') end++;
        uint32_t next=end<bytes?end+1:end;
        while(at<end && sz_arg_space(data[at])) at++;
        for(uint32_t i=at;i<end;i++) if(data[i]=='#' || data[i]=='\r') { end=i; break; }
        while(end>at && sz_arg_space(data[end-1])) end--;
        uint32_t equal=at; while(equal<end && data[equal]!='=') equal++;
        uint32_t key_end=equal; while(key_end>at && sz_arg_space(data[key_end-1])) key_end--;
        uint32_t value=equal<end?equal+1:end; while(value<end && sz_arg_space(data[value])) value++;
        if(equal<end && word_is(data+at,key_end-at,name)) {
            if(word_is(data+value,end-value,"paths")) result=1;
            else if(word_is(data+value,end-value,"directories")) result=2;
            else if(word_is(data+value,end-value,"none")) result=0;
        }
        at=next;
    }
    return result;
}
static int collect(void *context,const char *name,int directory,int executable) {
    sz_completion_t *s=context;
    if((s->directories && !directory) || (s->commands && ((!directory && !executable) || (s->bare && directory)))) return 0;
    const char *prefix=s->prefix+s->base; uint32_t n=length(prefix),m=length(name);
    for(uint32_t i=0;i<n;i++) if(!name[i] || name[i]!=prefix[i]) return 0;
    uint32_t base=s->bare?0:s->base;
    if(base+m+!!directory+1>SZ_COMPLETE_PATH) return 0;
    char candidate[SZ_COMPLETE_PATH];
    for(uint32_t i=0;i<base;i++) candidate[i]=s->prefix[i];
    copy(candidate+base,name); if(directory) { candidate[base+m++]='/'; candidate[base+m]=0; }
    if(!s->total++) copy(s->common,candidate);
    else { uint32_t i=0; while(s->common[i] && s->common[i]==candidate[i]) i++; s->common[i]=0; }
    uint32_t at=0; while(at<s->count && compare(s->matches[at],candidate)<0) at++;
    if(at<s->count && !compare(s->matches[at],candidate)) return 0;
    if(at==SZ_COMPLETE_MATCHES) return 0;
    uint32_t end=s->count<SZ_COMPLETE_MATCHES?s->count++:SZ_COMPLETE_MATCHES-1;
    for(uint32_t i=end;i>at;i--) copy(s->matches[i],s->matches[i-1]);
    copy(s->matches[at],candidate); return 0;
}
static int begin(sz_completion_t *s,const sz_completion_io_t *io,const char *line,uint32_t cursor) {
    char command[SZ_COMPLETE_PATH]; const char *scan=line;
    if(sz_arg_next(&scan,command,sizeof command)<0) return 0;
    uint32_t at=0,index=0,start=cursor,end=cursor;
    while(line[at]) {
        while(sz_arg_space(line[at])) at++;
        if(at>cursor || !line[at]) break;
        uint32_t stop=sz_arg_end(line,at);
        if(cursor<=stop) { start=at; end=stop; break; }
        index++; at=stop;
    }
    if(sz_arg_decode(line+start,cursor-start,s->prefix,sizeof s->prefix,1)<0) return 0;
    if(index) {
        int bytes=io->read(io->context,SZ_COMPLETE_FILE,s->config,sizeof s->config);
        if(bytes<0 || bytes>(int)SZ_COMPLETE_CONFIG) return 0;
        int mode=sz_completion_mode(s->config,bytes,command); if(!mode) return 0;
        s->directories=mode==2;
    } else s->commands=1;
    s->start=start; s->end=end; s->cursor=cursor; copy(s->original,line);
    uint32_t n=length(s->prefix);
    for(uint32_t i=0;i<n;i++) if(s->prefix[i]=='/' || s->prefix[i]==':') s->base=i+1;
    if(s->commands && !s->base) {
        s->bare=1; const char *path=io->path(io->context);
        while(path && *path) {
            uint32_t at=0; int overflow=0;
            while(*path && *path!=';') { if(at+1<sizeof s->directory) s->directory[at++]=*path; else overflow=1; path++; }
            if(*path==';') path++;
            s->directory[at]=0;
            if(at && !overflow) io->list(io->context,s->directory,1,collect,s);
        }
    } else {
        for(uint32_t i=0;i<s->base;i++) s->directory[i]=s->prefix[i];
        s->directory[s->base]=0;
        if(!s->base) copy(s->directory,".");
        if(io->list(io->context,s->directory,s->commands,collect,s)) return 0;
    }
    s->active=s->count>0; s->selected=SZ_COMPLETE_MATCHES; return s->active;
}
int sz_complete(sz_completion_t *s,const sz_completion_io_t *io,char *line,uint32_t cap,int *cursor,int repeated,int reverse) {
    uint32_t size=length(line);
    if(!cap || size>=cap || size>=SZ_COMPLETE_LINE || *cursor<0 || (uint32_t)*cursor>size) return 0;
    int fresh=!repeated || !s->active;
    if(fresh) {
        s->active=s->directories=s->commands=s->bare=0; s->base=s->count=s->total=0;
        if(!begin(s,io,line,*cursor)) return 0;
    }
    const char *choice=s->common; int unique=s->count==1;
    if(!fresh || unique) {
        if(s->selected==SZ_COMPLETE_MATCHES) s->selected=reverse?s->count-1:0;
        else s->selected=reverse?(s->selected+s->count-1)%s->count:(s->selected+1)%s->count;
        choice=s->matches[s->selected];
    }
    if(!*choice) return 0;
    char quoted[SZ_COMPLETE_PATH*2+2]; int written=sz_arg_quote(choice,quoted,sizeof quoted);
    if(written<0) return 0;
    uint32_t original_size=length(s->original),n=length(choice);
    int space=(unique || !fresh) && n && choice[n-1]!='/' && !s->original[s->end];
    if(s->start+(uint32_t)written+space+original_size-s->end+1>cap) return 0;
    uint32_t at=s->start;
    for(uint32_t i=0;i<s->start;i++) line[i]=s->original[i];
    for(int i=0;i<written;i++) line[at++]=quoted[i];
    if(space) line[at++]=' ';
    *cursor=at; copy(line+at,s->original+s->end); return 1;
}
