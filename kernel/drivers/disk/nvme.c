/* Polled NVMe NVM queues. 4 KiB pages, 512-byte namespaces, below-4-GiB DMA.
   Controller registers/queue layouts follow NVM Express and QEMU's nvme.h. */
#include "block.h"
#include "../utils/mem.h"
#include "../../cpu/thread.h"
typedef struct { uint32_t *submission; volatile uint32_t *completion; uint16_t tail,head,size,id; uint8_t phase; } queue_t;
typedef struct {
    volatile uint32_t *registers,*doorbells;
    queue_t admin,io;
    uint32_t *pages;
    uint8_t *data;
    uint32_t stride,namespace,limit;
    uint16_t cid;
    int failed;
} nvme_disk_t;
static int submit(nvme_disk_t *disk,queue_t *queue,uint32_t command[16]) {
    if(disk->failed) return 0;
    uint16_t cid=++disk->cid; command[0]|=(uint32_t)cid<<16;
    memcpy(queue->submission+queue->tail*16,command,64);
    queue->tail=(queue->tail+1)%queue->size;
    __asm__ volatile("" ::: "memory");
    disk->doorbells[queue->id*2*disk->stride]=queue->tail;
    volatile uint32_t *entry=queue->completion+queue->head*4;
    for(uint32_t i=0;i<10000000;i++) {
        uint32_t status=entry[3];
        if(((status>>16)&1)==queue->phase) {
            __asm__ volatile("" ::: "memory");
            int result=(uint16_t)status==cid && !(status>>17);
            if(++queue->head==queue->size) { queue->head=0; queue->phase^=1; }
            disk->doorbells[(queue->id*2+1)*disk->stride]=queue->head;
            return result;
        }
        if(disk->registers[7]&2) break;
        __asm__ volatile("pause");
    }
    disk->failed=1; disk->registers[5]=0; return 0;
}
static int queue_init(queue_t *queue,uint16_t id,uint16_t size) {
    queue->submission=dma_alloc(4096); queue->completion=dma_alloc(4096);
    queue->id=id; queue->size=size; queue->phase=1;
    return queue->submission && queue->completion;
}
static int transfer(void *context,uint32_t lba,uint8_t count,void *data,int writing) {
    nvme_disk_t *disk=context; uint8_t *bytes=data; int result=1;
    thread_guard_enter();
    while(count) {
        uint32_t batch=count>disk->limit ? disk->limit:count;
        uint32_t command[16]={writing ? 1:2,disk->namespace};
        command[6]=(uintptr_t)disk->data;
        if(batch*512>4096) {
            if(batch*512<=8192) command[8]=(uintptr_t)disk->data+4096;
            else {
                for(uint32_t i=0;i<(batch*512-1)/4096;i++) {
                    disk->pages[i*2]=(uintptr_t)disk->data+(i+1)*4096; disk->pages[i*2+1]=0;
                }
                command[8]=(uintptr_t)disk->pages;
            }
        }
        command[10]=lba; command[12]=batch-1;
        if(writing) memcpy(disk->data,bytes,batch*512);
        if(!submit(disk,&disk->io,command)) { result=0; break; }
        if(!writing) memcpy(bytes,disk->data,batch*512);
        bytes+=batch*512; lba+=batch; count-=batch;
    }
    thread_guard_leave(); return result;
}
static int sync(void *context) {
    nvme_disk_t *disk=context; uint32_t command[16]={0,disk->namespace};
    thread_guard_enter(); int result=submit(disk,&disk->io,command); thread_guard_leave(); return result;
}
void nvme_probe(pci_device_t device,block_runtime_t *runtime) {
    uint32_t base=pci_bar(device,0); if(!base || base>UINT32_MAX-0x21000) return;
    pci_enable(device); volatile uint32_t *registers=(void *)base;
    uint64_t cap=(uint64_t)registers[0] | ((uint64_t)registers[1]<<32);
    if(((cap>>48)&15) || !((cap>>37)&1) || (cap&65535)<1) return;
    registers[5]&=~1u;
    if(!mmio_wait(registers+7,1,0)) return;
    nvme_disk_t *disk=dma_alloc(sizeof(*disk)); if(!disk) return;
    disk->registers=registers; disk->doorbells=(void *)(base+0x1000);
    disk->stride=1u<<((cap>>32)&15); disk->limit=255;
    uint16_t size=(cap&65535)<31 ? (cap&65535)+1:32;
    if(!queue_init(&disk->admin,0,size) || !queue_init(&disk->io,1,size)) return;
    disk->data=dma_alloc(128*1024); disk->pages=dma_alloc(4096);
    if(!disk->data || !disk->pages) return;
    registers[3]=UINT32_MAX;
    registers[9]=(size-1) | ((uint32_t)(size-1)<<16);
    registers[10]=(uintptr_t)disk->admin.submission; registers[11]=0;
    registers[12]=(uintptr_t)disk->admin.completion; registers[13]=0;
    registers[5]=1 | (6u<<16) | (4u<<20);
    if(!mmio_wait(registers+7,1,1)) return;
    uint32_t identify[16]={6}; identify[6]=(uintptr_t)disk->data; identify[10]=1;
    if(!submit(disk,&disk->admin,identify)) return;
    uint8_t mdts=disk->data[77];
    if(mdts && mdts<5) disk->limit=8u<<mdts;
    uint32_t create_cq[16]={5}; create_cq[6]=(uintptr_t)disk->io.completion;
    create_cq[10]=1 | ((uint32_t)(size-1)<<16); create_cq[11]=1;
    if(!submit(disk,&disk->admin,create_cq)) return;
    uint32_t create_sq[16]={1}; create_sq[6]=(uintptr_t)disk->io.submission;
    create_sq[10]=1 | ((uint32_t)(size-1)<<16); create_sq[11]=1 | (1u<<16);
    if(!submit(disk,&disk->admin,create_sq)) return;
    /* Discover the first active namespace rather than assuming its ID is one. */
    uint32_t active[16]={6}; active[6]=(uintptr_t)disk->data; active[10]=2;
    if(!submit(disk,&disk->admin,active)) return;
    disk->namespace=*(uint32_t *)disk->data;
    if(!disk->namespace) return;
    uint32_t ns[16]={6,disk->namespace}; ns[6]=(uintptr_t)disk->data;
    if(!submit(disk,&disk->admin,ns)) return;
    uint64_t sectors=*(uint64_t *)disk->data;
    uint32_t format=((uint32_t *)(disk->data+128))[disk->data[26]&15];
    if((format&65535) || ((format>>16)&255)!=9 || !sectors) return;
    if(sectors>UINT32_MAX) sectors=UINT32_MAX;
    block_add(runtime,(block_device_t){sectors,BLOCK_NVME,disk,transfer,sync});
}
