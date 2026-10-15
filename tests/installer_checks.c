#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../boot/installer/disk.h"
#include "../shared/partition.h"
#define IMAGE (4096u*512u)
#define TARGET (6144u*512u)
typedef struct { uint8_t *image,*disk; unsigned writes; int fail_source,fail_read,fail_write,fail_flush,corrupt; } fixture_t;
static void put32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
static void put64(uint8_t *p,uint64_t v) { memcpy(p,&v,8); }
static uint32_t get32(const uint8_t *p) { uint32_t n; memcpy(&n,p,4); return n; }
static void make_header(uint8_t *h,uint32_t own,uint32_t other,uint32_t array,uint32_t crc) {
    memset(h,0,512); memcpy(h,"EFI PART",8); put32(h+8,0x10000); put32(h+12,92);
    put64(h+24,own); put64(h+32,other); put64(h+40,34); put64(h+48,IMAGE/512-34);
    put64(h+72,array); put32(h+80,128); put32(h+84,128); put32(h+88,crc);
    put32(h+16,~install_crc(UINT32_MAX,h,92));
}
static int source(void *context,uint32_t at,void *out,uint32_t n) {
    fixture_t *f=context; assert(at<=IMAGE && n<=IMAGE-at);
    if(f->fail_source) return -1;
    memcpy(out,f->image+at,n); return 0;
}
static int read_disk(void *context,uint32_t lba,void *out,uint32_t n) {
    fixture_t *f=context; assert(lba<TARGET/512 && n<=TARGET-lba*512);
    if(f->fail_read) return -1;
    memcpy(out,f->disk+lba*512,n); if(f->corrupt && lba==34) ((uint8_t *)out)[0]^=1; return 0;
}
static int write_disk(void *context,uint32_t lba,const void *in,uint32_t n) {
    fixture_t *f=context; assert(lba<TARGET/512 && n<=TARGET-lba*512); f->writes++;
    if(f->fail_write) return -1;
    memcpy(f->disk+lba*512,in,n); return 0;
}
static int flush(void *context) { return ((fixture_t *)context)->fail_flush; }
static int sector_read(void *context,uint32_t lba,void *out) { return read_disk(context,lba,out,512); }
static int sector_write(void *context,uint32_t lba,void *in) { return write_disk(context,lba,in,512); }
int main(void) {
    fixture_t f={.image=calloc(1,IMAGE),.disk=malloc(TARGET)};
    uint8_t *buffer=malloc(INSTALL_CHUNK),*verify=malloc(INSTALL_CHUNK),*metadata=malloc(INSTALL_METADATA);
    assert(f.image && f.disk && buffer && verify && metadata);
    uint8_t *entries=f.image+1024;
    entries[0]=1; put64(entries+32,2048); put64(entries+40,2559);
    entries[128]=2; put64(entries+160,2560); put64(entries+168,3999);
    uint32_t crc=~install_crc(UINT32_MAX,entries,16384);
    make_header(f.image+512,1,IMAGE/512-1,2,crc);
    memcpy(f.image+IMAGE-33*512,entries,16384);
    make_header(f.image+IMAGE-512,IMAGE/512-1,1,IMAGE/512-33,crc);
    f.image[450]=0xee; f.image[510]=0x55; f.image[511]=0xaa;
    for(unsigned i=INSTALL_METADATA;i<IMAGE-33*512;i++) f.image[i]=(i*31)>>8;
    install_disk_t d={.context=&f,.source=source,.read=read_disk,.write=write_disk,.flush=flush,
        .sectors=TARGET/512,.image_bytes=IMAGE,.image_crc=~install_crc(UINT32_MAX,f.image,IMAGE)};
    for(unsigned i=0;i<48;i++) d.identifiers[i]=i+11;
    uint32_t image_crc=d.image_crc;
    memset(f.disk,0xa5,TARGET); d.image_crc^=1;
    assert(install_disk(&d,buffer,verify,metadata)==INSTALL_IMAGE && !f.writes);
    for(unsigned i=0;i<TARGET;i++) assert(f.disk[i]==0xa5);
    d.image_crc=image_crc; d.sectors=IMAGE/512-1;
    assert(install_disk(&d,buffer,verify,metadata)==INSTALL_SIZE && !f.writes);
    d.sectors=(uint64_t)UINT32_MAX+1;
    assert(install_disk(&d,buffer,verify,metadata)==INSTALL_SIZE && !f.writes);
    d.sectors=TARGET/512;
    f.fail_source=1; assert(install_disk(&d,buffer,verify,metadata)==INSTALL_READ && !f.writes); f.fail_source=0;
    /* Correct CRC cannot excuse malformed GPT metadata. */
    f.image[512+24]=2; d.image_crc=~install_crc(UINT32_MAX,f.image,IMAGE);
    assert(install_disk(&d,buffer,verify,metadata)==INSTALL_IMAGE && !f.writes);
    f.image[512+24]=1; d.image_crc=image_crc;
    f.fail_write=1; assert(install_disk(&d,buffer,verify,metadata)==INSTALL_WRITE); f.fail_write=0;
    f.fail_flush=1; assert(install_disk(&d,buffer,verify,metadata)==INSTALL_FLUSH); f.fail_flush=0;
    f.fail_read=1; assert(install_disk(&d,buffer,verify,metadata)==INSTALL_VERIFY); f.fail_read=0;
    f.corrupt=1; assert(install_disk(&d,buffer,verify,metadata)==INSTALL_VERIFY); f.corrupt=0;
    for(unsigned extra=0;extra<=TARGET/512-IMAGE/512;extra=extra<2 ? extra+1:extra==2 ? 2048:2049) {
        d.sectors=IMAGE/512+extra; memset(f.disk,0xa5,TARGET);
        assert(install_disk(&d,buffer,verify,metadata)==INSTALL_OK);
        assert(!memcmp(f.disk+INSTALL_METADATA,f.image+INSTALL_METADATA,IMAGE-INSTALL_METADATA-33*512));
        assert(~install_crc(UINT32_MAX,f.image,IMAGE)==image_crc);
        partition_disk_t disk={&f,d.sectors,sector_read,sector_write,flush}; partition_table_t table;
        assert(!partition_scan(&disk,&table) && table.kind==SZ_TABLE_GPT && !table.readonly && table.count==2);
        assert(table.last==d.sectors-34 && table.entries[1].sectors==1440);
        assert(get32(f.disk+458)==d.sectors-1);
        assert(memcmp(f.disk+512+56,f.image+512+56,16));
        assert((f.disk[512+63]&0xf0)==0x40 && (f.disk[512+64]&0xc0)==0x80);
        /* Force recovery from the relocated backup. */
        f.disk[512]^=1;
        assert(!partition_scan(&disk,&table) && table.readonly && table.count==2);
    }
    const uint8_t parent[]={1,1,6,0,0,1,0x7f,0xff,4,0};
    const uint8_t child[]={1,1,6,0,0,1,4,1,6,0,2,3,0x7f,0xff,4,0};
    const uint8_t other[]={1,1,6,0,0,2,0x7f,0xff,4,0};
    const uint8_t malformed[]={1,1,0,0};
    assert(install_path_parent(parent,child)==1 && install_path_parent(parent,parent)==1);
    assert(install_path_parent(parent,other)==0 && install_path_parent(child,parent)==0);
    assert(install_path_parent(malformed,child)==-1 && install_path_parent(0,child)==-1);
    const uint8_t sata[]={3,18,10,0,0,0,0,0,0,0,0x7f,0xff,4,0};
    const uint8_t usb[]={3,5,6,0,0,0,0x7f,0xff,4,0};
    assert(install_path_storage(sata)==2 && !install_path_storage(usb) && !install_path_storage(parent));
    uint8_t option[256]; uint32_t bytes=install_load_option(parent,d.identifiers,option,sizeof option);
    assert(bytes && get32(option)==1 && !memcmp(option+6,u"SystemZero",22));
    assert(!memcmp(option+28,parent,6) && option[34]==4 && option[35]==1 && option[36]==42);
    assert(get32(option+38)==1 && get32(option+42)==2048 && get32(option+50)==131072);
    assert(!memcmp(option+58,d.identifiers,16) && option[74]==2 && option[75]==2);
    assert(option[76]==4 && option[77]==4 && !memcmp(option+80,u"\\EFI\\BOOT\\BOOTX64.EFI",44));
    assert(install_load_option(parent,d.identifiers,option,bytes-1)==0);
    free(f.image); free(f.disk); free(buffer); free(verify); free(metadata);
    puts("PASS installer validation, failures, GPT relocation, verification, and source paths");
}
