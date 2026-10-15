/* Polled AHCI 1.x, one command slot, 512-byte ATA disks. DMA uses a persistent
   bounce buffer so caller buffers need neither alignment nor physical lists. */
#include "block.h"
#include "../utils/mem.h"
#include "../../cpu/thread.h"
typedef struct {
    volatile uint32_t *port;
    uint32_t *header;
    uint8_t *table,*data;
    int failed;
} ahci_disk_t;
static int command(ahci_disk_t *disk,uint8_t opcode,uint32_t lba,uint8_t count,int writing) {
    volatile uint32_t *p=disk->port;
    if(disk->failed || !mmio_wait(p+8,0x88,0)) return 0;
    memset(disk->table,0,256);
    uint8_t *fis=disk->table;
    fis[0]=0x27; fis[1]=0x80; fis[2]=opcode;
    fis[4]=lba; fis[5]=lba>>8; fis[6]=lba>>16; fis[7]=0x40;
    fis[8]=lba>>24; fis[12]=count;
    disk->header[0]=5 | (writing ? 1u<<6:0) | (count ? 1u<<16:0);
    disk->header[1]=0;
    disk->header[2]=(uintptr_t)disk->table; disk->header[3]=0;
    if(count) {
        uint32_t *prdt=(void *)(disk->table+128);
        prdt[0]=(uintptr_t)disk->data; prdt[3]=count*512u-1;
    }
    p[4]=UINT32_MAX; p[12]=UINT32_MAX;
    __asm__ volatile("" ::: "memory"); p[14]=1;
    for(uint32_t i=0;i<10000000;i++) {
        if(p[4] & (1u<<30)) return 0;
        if(!(p[14]&1)) { __asm__ volatile("" ::: "memory"); return !(p[8]&1); }
        __asm__ volatile("pause");
    }
    disk->failed=1; p[6]&=~1u; return 0;
}
static int transfer(void *context,uint32_t lba,uint8_t count,void *data,int writing) {
    ahci_disk_t *disk=context; thread_guard_enter();
    if(writing) memcpy(disk->data,data,count*512u);
    int result=command(disk,writing ? 0x35:0x25,lba,count,writing);
    if(result && !writing) memcpy(data,disk->data,count*512u);
    thread_guard_leave(); return result;
}
static int sync(void *context) {
    thread_guard_enter(); int result=command(context,0xea,0,0,0); thread_guard_leave(); return result;
}
void ahci_probe(pci_device_t device,block_runtime_t *runtime) {
    uint32_t base=pci_bar(device,5); if(!base || base>UINT32_MAX-0x1100) return;
    pci_enable(device); volatile uint32_t *hba=(void *)base;
    if(hba[9]&1) {
        hba[10]|=2;
        if(!mmio_wait(hba+10,0x11,0)) return;
    }
    hba[1]=(hba[1]|0x80000000u)&~2u;
    uint32_t implemented=hba[3];
    for(uint32_t port=0;port<32;port++) {
        if(!(implemented&(1u<<port))) continue;
        volatile uint32_t *p=(void *)(base+0x100+port*0x80);
        if((p[10]&0xf)!=3 || ((p[10]>>8)&0xf)!=1) continue;
        p[6]&=~1u;
        if(!mmio_wait(p+6,1u<<15,0)) continue;
        p[6]&=~16u;
        if(!mmio_wait(p+6,1u<<14,0)) continue;
        ahci_disk_t *disk=dma_alloc(sizeof(*disk));
        void *fis=dma_alloc(256);
        if(!disk || !fis) return;
        disk->port=p; disk->header=dma_alloc(1024); disk->table=dma_alloc(256); disk->data=dma_alloc(128*1024);
        if(!disk->header || !disk->table || !disk->data) return;
        p[0]=(uintptr_t)disk->header; p[1]=0; p[2]=(uintptr_t)fis; p[3]=0;
        p[5]=0; p[4]=UINT32_MAX; p[12]=UINT32_MAX;
        /* The initial D2H FIS publishes PxSIG only after receiving is enabled.
           NVMe boot can leave otherwise connected SATA ports untouched by UEFI. */
        p[6]|=16;
        if(!mmio_wait(p+9,UINT32_MAX,0x101)) { p[6]&=~16u; mmio_wait(p+6,1u<<14,0); continue; }
        p[6]|=1;
        if(!command(disk,0xec,0,1,0)) continue;
        const uint16_t *id=(void *)disk->data;
        if(!(id[83]&(1u<<10)) || (id[106]&0xd000)==0x5000) continue;
        uint64_t sectors=(uint64_t)id[100] | ((uint64_t)id[101]<<16) | ((uint64_t)id[102]<<32) | ((uint64_t)id[103]<<48);
        if(!sectors) continue;
        if(sectors>UINT32_MAX) sectors=UINT32_MAX;
        if(!block_add(runtime,(block_device_t){sectors,BLOCK_AHCI,disk,transfer,sync})) return;
    }
}
