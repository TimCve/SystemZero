#include "disk.h"
static void copy(void *to,const void *from,uint32_t n) { uint8_t *d=to; const uint8_t *s=from; while(n--) *d++=*s++; }
static void zero(void *to,uint32_t n) { uint8_t *d=to; while(n--) *d++=0; }
static int equal(const void *a,const void *b,uint32_t n) { const uint8_t *x=a,*y=b; while(n--) if(*x++!=*y++) return 0; return 1; }
static uint32_t get32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t get64(const uint8_t *p) { return get32(p)|(uint64_t)get32(p+4)<<32; }
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=n>>(i*8); }
static void put64(uint8_t *p,uint64_t n) { put32(p,n); put32(p+4,n>>32); }
uint32_t install_crc(uint32_t crc,const void *data,uint32_t n) {
    static uint32_t table[256]; static int ready;
    if(!ready) {
        for(unsigned i=0;i<256;i++) {
            uint32_t value=i;
            for(unsigned bit=0;bit<8;bit++) value=(value>>1)^(0xedb88320u & (0u-(value&1)));
            table[i]=value;
        }
        ready=1;
    }
    const uint8_t *p=data;
    while(n--) crc=(crc>>8)^table[(crc^*p++)&255];
    return crc;
}
/* Compare complete nodes, ignoring the parent's end node. Fail closed for
   malformed/empty paths; callers exclude these from the destination list. */
int install_path_parent(const uint8_t *parent,const uint8_t *child) {
    unsigned at=0;
    if(!parent || !child || parent[0]==0x7f) return -1;
    while(at<=4092) {
        unsigned n=parent[at+2]|(unsigned)parent[at+3]<<8;
        if(n<4 || n>4096-at) return -1;
        if(parent[at]==0x7f) return parent[at+1]==0xff && n==4 ? 1:-1;
        unsigned m=child[at+2]|(unsigned)child[at+3]<<8;
        if(m<4 || m>4096-at) return -1;
        if(n!=m || !equal(parent+at,child+at,n)) return 0;
        at+=n;
    }
    return -1;
}
int install_path_storage(const uint8_t *path) {
    if(install_path_parent(path,path)!=1) return 0;
    int kind=0;
    for(unsigned at=0;path[at]!=0x7f;at+=path[at+2]|(unsigned)path[at+3]<<8) {
        if(path[at]!=3) continue;
        if(path[at+1]==5 || path[at+1]==15 || path[at+1]==16) return 0; /* USB */
        if(path[at+1]==1) kind=1; /* ATAPI/ATA */
        if(path[at+1]==18) kind=2; /* SATA */
        if(path[at+1]==23) kind=3; /* NVMe namespace */
    }
    return kind;
}
uint32_t install_load_option(const uint8_t *path,const uint8_t *guid,void *out,uint32_t capacity) {
    static const uint16_t description[]=u"SystemZero",file[]=u"\\EFI\\BOOT\\BOOTX64.EFI";
    if(install_path_parent(path,path)!=1 || !guid || !out) return 0;
    uint32_t prefix=0;
    while(path[prefix]!=0x7f) prefix+=path[prefix+2]|(uint32_t)path[prefix+3]<<8;
    uint32_t path_bytes=prefix+42+4+sizeof file+4,bytes=6+sizeof description+path_bytes;
    if(bytes>capacity) return 0;
    uint8_t *b=out; zero(b,bytes); put32(b,1); b[4]=path_bytes; b[5]=path_bytes>>8;
    copy(b+6,description,sizeof description); b+=6+sizeof description;
    copy(b,path,prefix); b+=prefix;
    b[0]=4; b[1]=1; b[2]=42; put32(b+4,1); put64(b+8,2048); put64(b+16,131072);
    copy(b+24,guid,16); b[40]=2; b[41]=2; b+=42;
    b[0]=4; b[1]=4; b[2]=4+sizeof file; copy(b+4,file,sizeof file); b+=4+sizeof file;
    b[0]=0x7f; b[1]=0xff; b[2]=4;
    return bytes;
}
static int header_valid(uint8_t *h,uint32_t own,uint32_t other,uint32_t array,uint32_t sectors) {
    if(!equal(h,"EFI PART",8) || get32(h+8)!=0x10000 || get32(h+12)!=92 ||
       get64(h+24)!=own || get64(h+32)!=other || get64(h+40)!=34 ||
       get64(h+48)!=sectors-34 || get64(h+72)!=array || get32(h+80)!=128 || get32(h+84)!=128) return 0;
    uint32_t expected=get32(h+16); put32(h+16,0);
    uint32_t actual=~install_crc(UINT32_MAX,h,92); put32(h+16,expected);
    return expected==actual;
}
static void checksum(uint8_t *h) { put32(h+16,0); put32(h+16,~install_crc(UINT32_MAX,h,92)); }
static void progress(const install_disk_t *d,unsigned phase,uint32_t done,uint32_t total) {
    if(d->progress) d->progress(d->context,phase,done,total);
}
int install_disk(const install_disk_t *d,void *buffer,void *verify,void *metadata) {
    uint8_t *b=buffer,*v=verify,*m=metadata;
    if(!d || !d->source || !d->read || !d->write || !d->flush || !b || !v || !m ||
       d->image_bytes%512 || d->image_bytes<4096u*512) return INSTALL_IMAGE;
    uint32_t image_sectors=d->image_bytes/512;
    if(d->sectors<image_sectors || d->sectors>UINT32_MAX) return INSTALL_SIZE;
    uint32_t sectors=d->sectors,crc=UINT32_MAX;
    /* Read the entire payload before touching the destination. */
    for(uint32_t at=0;at<d->image_bytes;) {
        uint32_t n=d->image_bytes-at; if(n>INSTALL_CHUNK) n=INSTALL_CHUNK;
        if(d->source(d->context,at,b,n)) return INSTALL_READ;
        crc=install_crc(crc,b,n); at+=n;
        progress(d,INSTALL_CHECKING,at,d->image_bytes);
    }
    if(~crc!=d->image_crc || d->source(d->context,0,m,INSTALL_METADATA) ||
       d->source(d->context,(image_sectors-33)*512,b,33*512)) return INSTALL_IMAGE;
    uint8_t *primary=m+512,*entries=m+1024;
    if(m[510]!=0x55 || m[511]!=0xaa || m[450]!=0xee ||
       !header_valid(primary,1,image_sectors-1,2,image_sectors) ||
       !header_valid(b+32*512,image_sectors-1,1,image_sectors-33,image_sectors) ||
       !equal(entries,b,32*512) || get32(primary+88)!=~install_crc(UINT32_MAX,entries,32*512)) return INSTALL_IMAGE;
    unsigned partitions=0;
    for(unsigned i=0;i<128;i++) {
        uint8_t *e=entries+i*128; int used=0;
        for(unsigned j=0;j<16;j++) used|=e[j];
        if(!used) continue;
        if(++partitions>2 || get64(e+32)<34 || get64(e+40)>=image_sectors-33 || get64(e+32)>get64(e+40)) return INSTALL_IMAGE;
        copy(e+16,d->identifiers+partitions*16,16);
        e[23]=(e[23]&15)|0x40; e[24]=(e[24]&63)|0x80;
    }
    if(partitions!=2) return INSTALL_IMAGE;
    copy(primary+56,d->identifiers,16);
    primary[63]=(primary[63]&15)|0x40; primary[64]=(primary[64]&63)|0x80;
    put64(primary+32,sectors-1); put64(primary+48,sectors-34);
    put32(primary+88,~install_crc(UINT32_MAX,entries,32*512)); checksum(primary);
    put32(m+458,sectors-1);
    /* Payload first, backup GPT second, primary GPT/MBR last. */
    uint32_t end=(image_sectors-33)*512;
    progress(d,INSTALL_COPYING,0,end);
    for(uint32_t at=INSTALL_METADATA;at<end;) {
        uint32_t n=end-at; if(n>INSTALL_CHUNK) n=INSTALL_CHUNK;
        if(d->source(d->context,at,b,n)) return INSTALL_READ;
        if(d->write(d->context,at/512,b,n)) return INSTALL_WRITE;
        at+=n; progress(d,INSTALL_COPYING,at,end);
    }
    /* Retire the payload's old backup location when the target is larger. */
    uint32_t retired=sectors-image_sectors; if(retired>33) retired=33;
    zero(b,33*512);
    if(retired && d->write(d->context,image_sectors-33,b,retired*512)) return INSTALL_WRITE;
    copy(b,entries,32*512); copy(b+32*512,primary,512);
    uint8_t *backup=b+32*512;
    put64(backup+24,sectors-1); put64(backup+32,1); put64(backup+72,sectors-33); checksum(backup);
    if(d->write(d->context,sectors-33,b,33*512)) return INSTALL_WRITE;
    if(d->flush(d->context)) return INSTALL_FLUSH;
    if(d->write(d->context,0,m,INSTALL_METADATA)) return INSTALL_WRITE;
    if(d->flush(d->context)) return INSTALL_FLUSH;
    progress(d,INSTALL_VERIFYING,0,end);
    if(d->read(d->context,0,v,INSTALL_METADATA) || !equal(m,v,INSTALL_METADATA) ||
       d->read(d->context,sectors-33,v,33*512) || !equal(b,v,33*512)) return INSTALL_VERIFY;
    zero(b,retired*512);
    if(retired && (d->read(d->context,image_sectors-33,v,retired*512) || !equal(b,v,retired*512))) return INSTALL_VERIFY;
    for(uint32_t at=INSTALL_METADATA;at<end;) {
        uint32_t n=end-at; if(n>INSTALL_CHUNK) n=INSTALL_CHUNK;
        if(d->source(d->context,at,b,n) || d->read(d->context,at/512,v,n)) return INSTALL_READ;
        if(!equal(b,v,n)) return INSTALL_VERIFY;
        at+=n; progress(d,INSTALL_VERIFYING,at,end);
    }
    return INSTALL_OK;
}
