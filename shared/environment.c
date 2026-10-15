#include "environment.h"
static int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int length(const char *s,uint32_t capacity) { if(!s) return -1; uint32_t n=0; while(n<capacity && s[n]) n++; return n<capacity ? (int)n:-1; }
static void copy(char *d,const char *s) { while((*d++=*s++)) {} }
int environment_readonly(const char *name) { return name && (equal(name,"VERSION") || equal(name,"PWD") || equal(name,"SYSTEM_VOLUME")); }
void environment_defaults(system_variables_t *t) {
    *t=(system_variables_t){0};
#define SZ_ENV(name,value) environment_put(t,name,value);
#include "environment.def"
#undef SZ_ENV
}
const char *environment_get(const system_variables_t *t,const char *name) { if(!t || !name || t->count>SZ_ENV_MAX) return 0; for(uint32_t i=0;i<t->count;i++) if(equal(t->entries[i].name,name)) return t->entries[i].value; return 0; }
int environment_number(const char *s,uint32_t *out) { if(!s || !*s) return 0; uint32_t n=0; while(*s) { if(*s<'0' || *s>'9' || n>429496729u || (n==429496729u && *s>'5')) return 0; n=n*10+*s++-'0'; } *out=n; return 1; }
int environment_put(system_variables_t *t,const char *name,const char *value) {
    int n=length(name,SZ_ENV_NAME),v=length(value,SZ_ENV_VALUE); if(!t || t->count>SZ_ENV_MAX || n<1 || v<0 || environment_readonly(name)) return -1;
    for(int i=0;i<n;i++) if(!((name[i]>='A' && name[i]<='Z') || name[i]=='_' || (i && name[i]>='0' && name[i]<='9'))) return -1;
    for(int i=0;i<v;i++) if((uint8_t)value[i]<32 || (uint8_t)value[i]>126) return -1;
    uint32_t i=0; while(i<t->count && !equal(t->entries[i].name,name)) i++;
    if(i==SZ_ENV_MAX) return -1;
    if(i==t->count) t->count++;
    copy(t->entries[i].name,name); copy(t->entries[i].value,value); return 0;
}
int environment_validate(const system_variables_t *t) {
    static const char *colors[]={"COLOR_FOREGROUND","COLOR_BACKGROUND","COLOR_SECONDARY"}; uint32_t c[3],offset;
    if(!t || !environment_get(t,"PATH") || !environment_get(t,"INCLUDE") || !environment_get(t,"LIBRARY_PATH")) return -1;
    const char *home=environment_get(t,"HOME");
    if(!home || !*home) return -1;
    const char *p=home;
    if(*home!='/') {
        if(p[0]=='s' && p[1]=='y' && p[2]=='s' && p[3]==':') p+=4;
        else {
            if(*p<'0' || *p>'3') return -1;
            p++; if(*p=='p') { uint32_t part=0; p++; if(*p<'1' || *p>'9') return -1; while(*p>='0' && *p<='9') { if(part>16) return -1; part=part*10+*p++-'0'; } if(part>16) return -1; }
            if(*p++!=':') return -1;
        }
        if(*p!='/') return -1;
    }
    for(;*p;p++) if(*p==';' || *p==':') return -1;
    for(uint32_t i=0;i<3;i++) if(!environment_number(environment_get(t,colors[i]),&c[i]) || c[i]>15) return -1;
    const char *position=environment_get(t,"STATUS_POSITION");
    if(c[0]==c[1] || c[2]==c[1] || !position || (!equal(position,"top") && !equal(position,"bottom")) ||
       !environment_number(environment_get(t,"CLOCK_OFFSET"),&offset) || offset>=86400) return -1;
    return 0;
}
int environment_decode(system_variables_t *t,const char *text,uint32_t size) {
    if(!t || !text || size>SZ_ENV_FILE) return -1;
    uint32_t at=0; char name[SZ_ENV_NAME],value[SZ_ENV_VALUE];
    while(at<size) {
        uint32_t n=0,v=0;
        while(at<size && text[at]!='=' && text[at]!='\n') { if(n+1>=SZ_ENV_NAME) return -1; name[n++]=text[at++]; }
        if(at==size || text[at++]!='=') return -1;
        while(at<size && text[at]!='\n') { if(v+1>=SZ_ENV_VALUE) return -1; value[v++]=text[at++]; }
        if(at==size) return -1;
        at++; name[n]=value[v]=0;
        if(environment_put(t,name,value)) return -1;
    }
    return environment_validate(t);
}
int environment_encode(const system_variables_t *t,char *out,uint32_t capacity) {
    if(environment_validate(t) || !out) return -1;
    uint32_t n=0;
    for(uint32_t i=0;i<t->count;i++) {
        const char *parts[]={t->entries[i].name,"=",t->entries[i].value,"\n"};
        for(uint32_t j=0;j<4;j++) for(const char *p=parts[j];*p;p++) { if(n+1>=capacity) return -1; out[n++]=*p; }
    }
    out[n]=0; return n;
}
