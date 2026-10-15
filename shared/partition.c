#include "partition.h"
static void zero(void *p,uint32_t n) { uint8_t *b=p; while(n--) *b++=0; }
static void copy(void *d,const void *s,uint32_t n) { uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; }
static uint32_t u32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(uint8_t *p,uint32_t n) { for(uint32_t i=0;i<4;i++) p[i]=n>>(i*8); }
static uint32_t crc_more(uint32_t c,const uint8_t *p,uint32_t n) { while(n--) { c^=*p++; for(uint32_t i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1))); } return c; }
uint32_t partition_crc(const void *p,uint32_t n) { return ~crc_more(0xffffffffu,p,n); }
static int valid(const partition_disk_t *d,const partition_table_t *t) {
    if(!d || !d->read || !d->write || !t || t->count>SZ_PARTITIONS || t->last>=d->sectors || t->first>t->last) return 0;
    for(uint32_t i=0;i<t->count;i++) {
        const partition_t *p=&t->entries[i];
        if(!p->sectors || p->first<t->first || p->first>t->last || p->sectors>t->last-p->first+1 ||
           !p->slot || p->slot>(t->kind==SZ_TABLE_MBR ? 4u:t->entries_count)) return 0;
        for(uint32_t j=0;j<i;j++) {
            const partition_t *q=&t->entries[j];
            if(p->slot==q->slot || (p->first<q->first+q->sectors && q->first<p->first+p->sectors)) return 0;
        }
    }
    return 1;
}
static int gpt(const partition_disk_t *d,partition_table_t *t,uint32_t header) {
    uint8_t b[512], h[512];
    if(d->read(d->context,header,h) || u32(h)!=0x20494645 || u32(h+4)!=0x54524150 ||
       u32(h+8)!=0x10000 || u32(h+12)<92 || u32(h+12)>512 || u32(h+24)!=header || u32(h+28) ||
       u32(h+36) || u32(h+44) || u32(h+52) || u32(h+76) || u32(h+84)!=128) return -1;
    uint32_t check=u32(h+16); put(h+16,0);
    if(partition_crc(h,u32(h+12))!=check) return -1;
    t->kind=SZ_TABLE_GPT; t->first=u32(h+40); t->last=u32(h+48);
    t->entries_lba=u32(h+72); t->entries_count=u32(h+80); t->entry_bytes=128;
    copy(t->guid,h+56,16);
    if(!t->entries_count || t->entries_count>128 || t->entries_lba>=d->sectors ||
       (t->entries_count+3)/4>d->sectors-t->entries_lba) return -1;
    uint32_t metadata=(t->entries_count+3)/4;
    if(t->first<2+metadata || t->last>=d->sectors-1-metadata ||
       u32(h+32)!=(header==1 ? d->sectors-1:1) ||
       (header==1 ? (t->entries_lba<2 || t->entries_lba>t->first || metadata>t->first-t->entries_lba):
        (t->entries_lba<=t->last || t->entries_lba+metadata>header))) return -1;
    uint32_t crc=0xffffffffu;
    for(uint32_t i=0;i<t->entries_count;i++) {
        if(!(i%4) && d->read(d->context,t->entries_lba+i/4,b)) return -1;
        uint8_t *e=b+(i%4)*128;
        crc=crc_more(crc,e,128);
        int used=0; for(uint32_t j=0;j<16;j++) used|=e[j];
        if(!used) continue;
        if(t->count==SZ_PARTITIONS || u32(e+36) || u32(e+44) || u32(e+32)>u32(e+40)) return -1;
        partition_t *p=&t->entries[t->count++];
        p->slot=i+1; p->first=u32(e+32); p->sectors=u32(e+40)-p->first+1; p->type=0x83;
        copy(p->guid,e,16); copy(p->unique,e+16,16);
        for(uint32_t j=0;j<36;j++) {
            if(e[56+j*2+1]) t->readonly=1;
            p->label[j]=e[56+j*2+1] ? '?' : e[56+j*2];
        }
        /* Preserve unsupported GPT attributes by refusing table edits. */
        for(uint32_t j=48;j<56;j++) if(e[j]) t->readonly=1;
    }
    return ~crc==u32(h+88) && valid(d,t) ? 0:-1;
}
int partition_scan(const partition_disk_t *d,partition_table_t *t) {
    zero(t,sizeof *t); if(!d || !d->read || d->sectors<2) return -1;
    uint8_t b[512]; if(d->read(d->context,0,b)) return -1;
    t->first=2048; t->last=d->sectors-1;
    if(b[510]!=0x55 || b[511]!=0xaa) return 0;
    int protective=0; for(uint32_t i=0;i<4;i++) if(b[446+i*16+4]==0xee) protective=1;
    if(protective) {
        if(!gpt(d,t,1)) return 0;
        zero(t,sizeof *t);
        if(!gpt(d,t,d->sectors-1)) { t->readonly=1; return 0; }
        zero(t,sizeof *t); return -1;
    }
    for(uint32_t i=0;i<4;i++) {
        uint8_t *e=b+446+i*16; if(!e[4]) continue;
        t->kind=SZ_TABLE_MBR;
        partition_t *p=&t->entries[t->count++]; p->slot=i+1; p->type=e[4];
        p->first=u32(e+8); p->sectors=u32(e+12);
        if(p->type==5 || p->type==15 || p->type==0x85) t->readonly=1;
    }
    if(t->kind) { t->first=1; return valid(d,t) ? 0:-1; }
    return 0;
}
static const uint8_t esfs_guid[16]={0x53,0x5a,0x46,0x53,0x0e,0x13,0x42,0x4d,0x94,0x7a,0x73,0x79,0x73,0x7a,0x65,0x72};
static void header(uint8_t *b,const partition_table_t *t,uint32_t own,uint32_t other,uint32_t entries,uint32_t crc) {
    zero(b,512); put(b,0x20494645); put(b+4,0x54524150); put(b+8,0x10000); put(b+12,92);
    put(b+24,own); put(b+32,other); put(b+40,t->first); put(b+48,t->last); copy(b+56,t->guid,16);
    put(b+72,entries); put(b+80,t->entries_count); put(b+84,128); put(b+88,crc); put(b+16,partition_crc(b,92));
}
static void encode_entries(uint8_t *b,const partition_table_t *t,uint32_t block) {
    zero(b,512);
    for(uint32_t i=0;i<t->count;i++) {
        const partition_t *p=&t->entries[i]; if((p->slot-1)/4!=block) continue;
        uint8_t *e=b+((p->slot-1)%4)*128; copy(e,p->guid,16); copy(e+16,p->unique,16);
        put(e+32,p->first); put(e+40,p->first+p->sectors-1);
        for(uint32_t j=0;j<36;j++) e[56+j*2]=p->label[j];
    }
}
static int commit(const partition_disk_t *d,const partition_table_t *t) {
    if(t->readonly || !valid(d,t)) return -1;
    uint8_t b[512]; if(d->read(d->context,0,b)) return -1;
    if(t->kind==SZ_TABLE_MBR) {
        zero(b+446,64);
        for(uint32_t i=0;i<t->count;i++) {
            const partition_t *p=&t->entries[i]; uint8_t *e=b+446+(p->slot-1)*16;
            e[1]=e[5]=0xfe; e[2]=e[3]=e[6]=e[7]=0xff; e[4]=p->type;
            put(e+8,p->first); put(e+12,p->sectors);
        }
    } else if(t->kind==SZ_TABLE_GPT) {
        uint32_t blocks=(t->entries_count+3)/4, crc=0xffffffffu;
        for(uint32_t i=0;i<blocks;i++) {
            encode_entries(b,t,i);
            uint32_t remaining=t->entries_count-i*4;
            crc=crc_more(crc,b,remaining<4 ? remaining*128:512);
        }
        crc=~crc;
        /* Write and flush the backup before replacing the primary table. */
        for(uint32_t i=0;i<blocks;i++) { encode_entries(b,t,i); if(d->write(d->context,d->sectors-1-blocks+i,b)) return -1; }
        header(b,t,d->sectors-1,1,d->sectors-1-blocks,crc);
        if(d->write(d->context,d->sectors-1,b) || (d->sync && d->sync(d->context))) return -1;
        for(uint32_t i=0;i<blocks;i++) { encode_entries(b,t,i); if(d->write(d->context,2+i,b)) return -1; }
        header(b,t,1,d->sectors-1,2,crc); if(d->write(d->context,1,b)) return -1;
        zero(b,512); b[446+4]=0xee; put(b+446+8,1); put(b+446+12,d->sectors-1);
    } else return -1;
    b[510]=0x55; b[511]=0xaa;
    return d->write(d->context,0,b) || (d->sync && d->sync(d->context)) ? -1:0;
}
int partition_initialize(const partition_disk_t *d,uint32_t kind,partition_table_t *t) {
    if(!d || d->sectors<4096 || (kind!=SZ_TABLE_MBR && kind!=SZ_TABLE_GPT)) return -1;
    partition_table_t next={.kind=kind,.first=2048,.last=d->sectors-(kind==SZ_TABLE_GPT ? 34:1),.entries_lba=2,.entries_count=128,.entry_bytes=128};
    copy(next.guid,esfs_guid,16); put(next.guid+12,d->sectors);
    if(commit(d,&next)) return -1;
    uint8_t b[512]; zero(b,512); /* Retire an earlier whole-disk ESFS superblock. */
    if(d->write(d->context,200,b) || (d->sync && d->sync(d->context))) return -1;
    *t=next; return 0;
}
int partition_add(const partition_disk_t *d,partition_table_t *t,uint32_t sectors) {
    if(t->readonly || !t->kind || t->count==SZ_PARTITIONS || !sectors) return -1;
    partition_table_t next=*t;
    uint32_t slot=1, limit=t->kind==SZ_TABLE_MBR ? 4:128;
    for(;slot<=limit;slot++) { int used=0; for(uint32_t i=0;i<t->count;i++) used|=t->entries[i].slot==slot; if(!used) break; }
    if(slot>limit) return -1;
    uint32_t first=(t->first+2047)&~2047u;
    for(uint32_t attempt=0;attempt<=t->count;attempt++) {
        if(first>t->last || sectors>t->last-first+1) return -1;
        int overlap=0;
        for(uint32_t i=0;i<t->count;i++) {
            const partition_t *p=&t->entries[i];
            if(first<p->first+p->sectors && p->first<first+sectors) {
                if(p->first+p->sectors>0xfffff800u) return -1;
                first=(p->first+p->sectors+2047)&~2047u; overlap=1; break;
            }
        }
        if(!overlap) break;
    }
    partition_t *p=&next.entries[next.count++]; *p=(partition_t){.first=first,.sectors=sectors,.slot=slot,.type=0x83};
    copy(p->guid,esfs_guid,16); copy(p->unique,t->guid,16); put(p->unique+12,first);
    copy(p->label,"SystemZero",10);
    if(commit(d,&next)) return -1;
    *t=next; return 0;
}
int partition_remove(const partition_disk_t *d,partition_table_t *t,uint32_t slot) {
    partition_table_t next=*t; uint32_t i=0; while(i<next.count && next.entries[i].slot!=slot) i++;
    if(i==next.count) return -1;
    for(;i+1<next.count;i++) next.entries[i]=next.entries[i+1];
    next.count--;
    if(commit(d,&next)) return -1;
    *t=next; return 0;
}
