#include "pci.h"
#include "utils/ports.h"
#include "utils/mem.h"
/* Keep DMA/kernel RAM below 3 GiB. One unused PCI aperture provides virtual
   MMIO addresses for 64-bit BARs; PAE preserves identity mappings elsewhere. */
static uint64_t *directories,*pdpt;
static uint32_t aperture,mappings;
static uint32_t occupied;
static void inventory(pci_device_t device,uint32_t class,void *context) {
    (void)class; (void)context;
    if((pci_read(device,12)>>16&0x7f)!=0) return;
    for(uint8_t index=0;index<6;index++) {
        uint32_t bar=pci_read(device,16+index*4);
        if(bar&1) continue;
        uint32_t high=(bar&6)==4 && index<5 ? pci_read(device,20+index*4):0;
        if(!high && bar) occupied|=1u<<(bar>>28);
        if((bar&6)==4) index++;
    }
}
static uint32_t map_high(uint64_t physical) {
    if(!directories) {
        uint32_t a,b,c,d; __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(1));
        if(!(d&(1u<<6))) return 0;
        pci_scan(inventory,0);
        for(uint32_t nibble=12;nibble<=14;nibble++) if(!(occupied&(1u<<nibble))) { aperture=nibble<<28; break; }
        if(!aperture) return 0;
        directories=dma_alloc(4*4096); pdpt=dma_alloc(4096);
        if(!directories || !pdpt) { directories=0; return 0; }
        for(uint32_t i=0;i<2048;i++) directories[i]=((uint64_t)i<<21)|0x83;
        for(uint32_t i=0;i<4;i++) pdpt[i]=((uintptr_t)directories+i*4096)|1;
        uint32_t flags,control;
        __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
        __asm__ volatile("mov %%cr4,%0" : "=r"(control)); control|=1u<<5;
        __asm__ volatile("mov %0,%%cr4; mov %1,%%cr3" : : "r"(control),"r"((uintptr_t)pdpt) : "memory");
        __asm__ volatile("mov %%cr0,%0" : "=r"(control)); control|=1u<<31;
        __asm__ volatile("mov %0,%%cr0; pushl %1; popfl" : : "r"(control),"r"(flags) : "memory");
    }
    if(!aperture || mappings==64) return 0;
    uint32_t virtual=aperture+mappings++*0x400000;
    uint64_t page=physical&~0x1fffffull;
    directories[virtual>>21]=page|0x9b;
    directories[(virtual>>21)+1]=(page+0x200000)|0x9b;
    __asm__ volatile("invlpg (%0); invlpg (%1)" : : "r"(virtual),"r"(virtual+0x200000) : "memory");
    return virtual+(physical&0x1fffff);
}
static void address(pci_device_t d,uint8_t offset) {
    port_long_out(0xcf8,0x80000000u | ((uint32_t)d.bus<<16) | ((uint32_t)d.device<<11) | ((uint32_t)d.function<<8) | (offset&0xfc));
}
uint32_t pci_read(pci_device_t d,uint8_t offset) { address(d,offset); return port_long_in(0xcfc); }
void pci_write(pci_device_t d,uint8_t offset,uint32_t value) { address(d,offset); port_long_out(0xcfc,value); }
void pci_enable(pci_device_t d) {
    uint16_t command=pci_read(d,4); address(d,4); port_word_out(0xcfc,command|6);
}
uint32_t pci_bar(pci_device_t d,uint8_t number) {
    uint32_t bar=pci_read(d,16+number*4);
    if((bar&1) || ((bar&6)==4 && number==5)) return 0;
    if((bar&6)==4) {
        uint32_t high=pci_read(d,20+number*4);
        if(high) return map_high(((uint64_t)high<<32)|(bar&~15u));
    }
    return bar&~15u;
}
void pci_scan(void (*visit)(pci_device_t,uint32_t,void *),void *context) {
    for(uint32_t bus=0;bus<256;bus++) for(uint8_t device=0;device<32;device++) {
        pci_device_t d={bus,device,0};
        if(pci_read(d,0)==UINT32_MAX) continue;
        uint8_t functions=pci_read(d,12)&0x800000u ? 8:1;
        for(d.function=0;d.function<functions;d.function++)
            if(pci_read(d,0)!=UINT32_MAX) visit(d,pci_read(d,8)>>8,context);
    }
}
int mmio_wait(volatile uint32_t *reg,uint32_t mask,uint32_t value) {
    for(uint32_t i=0;i<10000000;i++) { if((*reg&mask)==value) return 1; __asm__ volatile("pause"); }
    return 0;
}
void *dma_alloc(uint32_t bytes) {
    void *memory=(void *)malloc(bytes,1,0);
    if(memory) memset(memory,0,bytes);
    return memory;
}
