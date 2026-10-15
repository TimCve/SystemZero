#include "linker.h"
#include <stddef.h>
#define SECTIONS 256u
#define SYMBOLS 4096u
#define RELOCATIONS 16384u
#define HEADER 160u
typedef struct { sz_link_input_t input; uint32_t shoff,sections,symtab,strtab,symbols; uint32_t addresses[SECTIONS]; } object_t;
typedef struct { const char *name; uint32_t object,symbol; int weak; } export_t;
typedef struct { uint32_t object,symbol,address; } got_t;
typedef struct { object_t objects[SZ_LINK_OBJECTS]; export_t exports[SYMBOLS]; got_t got[SYMBOLS]; uint32_t relocations[RELOCATIONS]; uint32_t count,nexports,ngot,nrel; } linker_t;
static uint32_t u32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t u16(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8; }
static void put(uint8_t *p,uint32_t n) { for(uint32_t i=0;i<4;i++) p[i]=n>>(i*8); }
static void zero(void *p,uint32_t n) { uint8_t *b=p; while(n--) *b++=0; }
static void copy(void *d,const void *s,uint32_t n) { uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; }
static int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int range(uint32_t at,uint32_t n,uint32_t size) { return at<=size && n<=size-at; }
static int failure(sz_link_result_t *r,const char *message,const char *symbol) { r->error=message; r->size=0; uint32_t i=0; if(symbol) while(symbol[i] && i+1<sizeof r->symbol) { r->symbol[i]=symbol[i]; i++; } r->symbol[i]=0; return 0; }
static const uint8_t *section(const object_t *o,uint32_t i) { return o->input.bytes+o->shoff+i*40; }
static const uint8_t *symbol(const object_t *o,uint32_t i) { return o->input.bytes+u32(section(o,o->symtab)+16)+i*16; }
static const char *name(const object_t *o,uint32_t i) {
    uint32_t at=u32(symbol(o,i)),n=u32(section(o,o->strtab)+20);
    if(at>=n) return 0;
    const char *p=(const char *)o->input.bytes+u32(section(o,o->strtab)+16)+at;
    for(uint32_t j=at;j<n;j++) if(!p[j-at]) return p;
    return 0;
}
static int add(linker_t *l,sz_link_input_t input,sz_link_result_t *r) {
    if(l->count==SZ_LINK_OBJECTS || !input.bytes || input.size<52) return failure(r,"Too many objects or truncated object",input.name);
    const uint8_t *b=input.bytes;
    if(u32(b)!=0x464c457f || b[4]!=1 || b[5]!=1 || b[6]!=1 || u16(b+16)!=1 || u16(b+18)!=3 ||
       u32(b+20)!=1 || u16(b+40)!=52 || u16(b+46)!=40 || !u16(b+48) || u16(b+48)>SECTIONS ||
       !range(u32(b+32),u16(b+48)*40,input.size)) return failure(r,"Expected a little-endian i386 ELF relocatable object",input.name);
    object_t *o=&l->objects[l->count]; o->input=input; o->shoff=u32(b+32); o->sections=u16(b+48);
    for(uint32_t i=0;i<o->sections;i++) {
        const uint8_t *s=section(o,i); uint32_t type=u32(s+4),alignment=u32(s+32);
        if((type!=8 && !range(u32(s+16),u32(s+20),input.size)) || (alignment && (alignment&(alignment-1))) || alignment>4096 ||
           (u32(s+8)&0x400) || type==4) return failure(r,"Unsupported or invalid ELF section",input.name);
        if(type==2) { if(o->symtab || u32(s+36)!=16 || u32(s+20)%16 || u32(s+24)>=o->sections) return failure(r,"Invalid symbol table",input.name); o->symtab=i; o->strtab=u32(s+24); o->symbols=u32(s+20)/16; }
    }
    if(!o->symtab || u32(section(o,o->strtab)+4)!=3 || o->symbols>SYMBOLS) return failure(r,"Missing or oversized symbol table",input.name);
    for(uint32_t i=0;i<o->symbols;i++) {
        const uint8_t *s=symbol(o,i); uint32_t sh=u16(s+14); const char *n=name(o,i);
        if(!n || (sh>=o->sections && sh!=0xfff1) || (sh && sh!=0xfff1 &&
           (u32(s+4)>u32(section(o,sh)+20) || u32(s+8)>u32(section(o,sh)+20)-u32(s+4)))) return failure(r,"Invalid symbol or unsupported COMMON symbol",input.name);
        uint32_t bind=s[12]>>4; if(!sh || !*n || !bind) continue;
        if(bind!=1 && bind!=2) return failure(r,"Unsupported symbol binding",n);
        uint32_t e=0; while(e<l->nexports && !equal(n,l->exports[e].name)) e++;
        if(e<l->nexports) { if(!l->exports[e].weak && bind!=2) return failure(r,"Duplicate symbol",n); if(bind==2) continue; }
        else if(l->nexports==SYMBOLS) return failure(r,"Export limit exceeded",n);
        else l->nexports++;
        l->exports[e]=(export_t){n,l->count,i,bind==2};
    }
    l->count++; return 1;
}
static int inputs(linker_t *l,const sz_link_input_t *in,uint32_t count,sz_link_result_t *r) {
    for(uint32_t i=0;i<count;i++) {
        if(!in[i].bytes) return failure(r,"Missing input object",in[i].name);
        if(in[i].size>=8 && u32(in[i].bytes)==0x72613c21 && u32(in[i].bytes+4)==0x0a3e6863) {
            uint32_t at=8;
            while(at<in[i].size) {
                if(!range(at,60,in[i].size) || in[i].bytes[at+58]!='`' || in[i].bytes[at+59]!='\n') return failure(r,"Invalid ar archive",in[i].name);
                uint32_t n=0,j=0; while(j<10 && in[i].bytes[at+48+j]!=' ') { uint8_t c=in[i].bytes[at+48+j++]; if(c<'0' || c>'9' || n>0x19999999u) return failure(r,"Invalid archive size",in[i].name); n=n*10+c-'0'; }
                if(!range(at+60,n,in[i].size)) return failure(r,"Truncated archive member",in[i].name);
                if(n>=4 && u32(in[i].bytes+at+60)==0x464c457f && !add(l,(sz_link_input_t){in[i].bytes+at+60,n,in[i].name},r)) return 0;
                if(n>0xffffffffu-at-61) return failure(r,"Archive size overflow",in[i].name);
                at+=60+n+(n&1);
            }
        } else if(!add(l,in[i],r)) return 0;
    }
    return l->count ? 1:failure(r,"No input objects",0);
}
static int address(linker_t *l,uint32_t oi,uint32_t si,uint32_t *out,int *absolute,sz_link_result_t *r) {
    object_t *o=&l->objects[oi]; if(si>=o->symbols) return failure(r,"Invalid relocation symbol",o->input.name);
    const uint8_t *s=symbol(o,si); uint32_t sh=u16(s+14);
    if(!sh) {
        const char *n=name(o,si); uint32_t e=0; while(e<l->nexports && !equal(n,l->exports[e].name)) e++;
        if(e==l->nexports) { if(s[12]>>4==2) { *out=0; *absolute=1; return 1; } return failure(r,"Unresolved symbol",n); }
        export_t *v=&l->exports[e]; o=&l->objects[v->object]; s=symbol(o,v->symbol); sh=u16(s+14);
    }
    *absolute=sh==0xfff1; *out=*absolute ? u32(s+4):o->addresses[sh]+u32(s+4); return 1;
}
static int allocated(uint32_t *at,uint32_t n,uint32_t alignment,uint32_t cap) {
    if(!alignment) alignment=1;
    if(*at>cap || alignment-1>cap-*at) return 0;
    *at=(*at+alignment-1)&~(alignment-1); return n<=cap-*at;
}
static int add_rel(linker_t *l,uint32_t offset,sz_link_result_t *r) { if(l->nrel==RELOCATIONS) return failure(r,"Relocation limit exceeded",0); l->relocations[l->nrel++]=offset; return 1; }
uint32_t sz_link_workspace_size(void) { return sizeof(linker_t); }
int sz_link(const sz_link_input_t *in,uint32_t count,uint8_t *out,uint32_t capacity,void *workspace,sz_link_result_t *r) {
    if(!r) return 0;
    *r=(sz_link_result_t){0};
    if(!in || !count || count>SZ_LINK_OBJECTS || !out || capacity<HEADER || !workspace || (uintptr_t)workspace%_Alignof(linker_t)) return failure(r,"Invalid linker buffers",0);
    linker_t *l=workspace; zero(l,sizeof *l); if(!inputs(l,in,count,r)) return 0;
    uint32_t pc=HEADER,absolute_count=0;
    for(uint32_t oi=0;oi<l->count;oi++) {
        object_t *o=&l->objects[oi];
        for(uint32_t si=1;si<o->sections;si++) {
            const uint8_t *s=section(o,si); uint32_t n=u32(s+20);
            if((u32(s+8)&2) && u32(s+4)!=8) {
                if(!allocated(&pc,n,u32(s+32),capacity)) return failure(r,"Linked executable exceeds capacity",o->input.name);
                o->addresses[si]=pc; copy(out+pc,o->input.bytes+u32(s+16),n); pc+=n;
            }
            if(u32(s+4)!=9) continue;
            if(u32(s+36)!=8 || n%8 || u32(s+24)!=o->symtab || u32(s+28)>=o->sections) return failure(r,"Invalid relocation section",o->input.name);
            if(!(u32(section(o,u32(s+28))+8)&2)) continue;
            const uint8_t *rels=o->input.bytes+u32(s+16);
            for(uint32_t j=0;j<n;j+=8) {
                uint32_t info=u32(rels+j+4),sym=info>>8,type=info&255;
                if(sym>=o->symbols || !range(u32(rels+j),4,u32(section(o,u32(s+28))+20))) return failure(r,"Relocation outside its section",o->input.name);
                if(type==1) absolute_count++;
                if(type==3 || type==43) {
                    uint32_t k=0; while(k<l->ngot && !(l->got[k].object==oi && l->got[k].symbol==sym)) k++;
                    if(k==l->ngot) { if(k==SYMBOLS) return failure(r,"GOT limit exceeded",0); l->got[l->ngot++]=(got_t){oi,sym,0}; }
                } else if(type!=0 && type!=1 && type!=2 && type!=4 && type!=9 && type!=10) return failure(r,"Unsupported i386 relocation",o->input.name);
            }
        }
    }
    if(absolute_count+l->ngot>RELOCATIONS || !allocated(&pc,0,4,capacity)) return failure(r,"Relocation limit exceeded",0);
    uint32_t got=pc; if((l->ngot+3)*4>capacity-pc) return failure(r,"GOT exceeds capacity",0);
    zero(out+pc,(l->ngot+3)*4); pc+=(l->ngot+3)*4;
    uint32_t rel=pc,rel_cap=(absolute_count+l->ngot)*8; if(rel_cap+32>capacity-pc) return failure(r,"Relocation metadata exceeds capacity",0);
    zero(out+pc,rel_cap+32); pc+=rel_cap; uint32_t dynamic=pc; pc+=32; uint32_t file_size=pc,mem_size=pc;
    for(uint32_t oi=0;oi<l->count;oi++) for(uint32_t si=1;si<l->objects[oi].sections;si++) {
        object_t *o=&l->objects[oi]; const uint8_t *s=section(o,si);
        if((u32(s+8)&2) && u32(s+4)==8) { if(!allocated(&mem_size,u32(s+20),u32(s+32),8u*1024*1024)) return failure(r,"BSS exceeds 8 MiB",0); o->addresses[si]=mem_size; mem_size+=u32(s+20); }
    }
    for(uint32_t i=0;i<l->ngot;i++) {
        uint32_t value; int absolute; got_t *g=&l->got[i];
        if(!address(l,g->object,g->symbol,&value,&absolute,r)) return 0;
        g->address=got+(i+3)*4; put(out+g->address,value); if(!absolute && !add_rel(l,g->address,r)) return 0;
    }
    for(uint32_t oi=0;oi<l->count;oi++) {
        object_t *o=&l->objects[oi];
        for(uint32_t si=1;si<o->sections;si++) {
            const uint8_t *s=section(o,si); if(u32(s+4)!=9) continue;
            uint32_t target=u32(s+28); if(!(u32(section(o,target)+8)&2)) continue;
            if(u32(section(o,target)+4)==8) return failure(r,"Relocations in BSS are unsupported",o->input.name);
            const uint8_t *rels=o->input.bytes+u32(s+16);
            for(uint32_t j=0;j<u32(s+20);j+=8) {
                uint32_t info=u32(rels+j+4),sym=info>>8,type=info&255,at=o->addresses[target]+u32(rels+j),value=0;
                int absolute=0; if(!type) continue;
                if(type==10) value=got-at;
                else if(type==3 || type==43) { uint32_t k=0; while(k<l->ngot && !(l->got[k].object==oi && l->got[k].symbol==sym)) k++; value=l->got[k].address-got; }
                else {
                    if(!address(l,oi,sym,&value,&absolute,r)) return 0;
                    if(type==2 || type==4) value-=at;
                    else if(type==9) value-=got;
                    else if(type==1 && !absolute && !add_rel(l,at,r)) return 0;
                }
                put(out+at,u32(out+at)+value);
            }
        }
    }
    uint32_t entry=0; for(uint32_t i=0;i<l->nexports;i++) if(equal(l->exports[i].name,"__sz_start")) {
        int absolute; if(!address(l,l->exports[i].object,l->exports[i].symbol,&entry,&absolute,r) || absolute) return failure(r,"Invalid program entry",0);
    }
    if(!entry) return failure(r,"Program requires a compiled main function (__sz_start)",0);
    for(uint32_t i=0;i<l->nrel;i++) { put(out+rel+i*8,l->relocations[i]); put(out+rel+i*8+4,8); }
    put(out+dynamic,17); put(out+dynamic+4,rel); put(out+dynamic+8,18); put(out+dynamic+12,l->nrel*8);
    put(out+dynamic+16,19); put(out+dynamic+20,8);
    zero(out,HEADER); out[0]=0x7f; out[1]='E'; out[2]='L'; out[3]='F'; out[4]=out[5]=out[6]=1;
    out[16]=3; out[18]=3; put(out+20,1); put(out+24,entry); put(out+28,52); out[40]=52; out[42]=32; out[44]=2;
    put(out+52,1); put(out+68,file_size); put(out+72,mem_size); put(out+76,7); put(out+80,4096);
    put(out+84,2); put(out+88,dynamic); put(out+92,dynamic); put(out+100,32); put(out+104,32); put(out+108,6); put(out+112,4);
    r->size=file_size; return 1;
}
