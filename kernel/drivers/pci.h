#ifndef SYSTEMZERO_PCI_H
#define SYSTEMZERO_PCI_H
#include <stdint.h>
typedef struct { uint8_t bus,device,function; } pci_device_t;
uint32_t pci_read(pci_device_t,uint8_t);
void pci_write(pci_device_t,uint8_t,uint32_t);
void pci_enable(pci_device_t);
uint32_t pci_bar(pci_device_t,uint8_t);
void pci_scan(void (*visit)(pci_device_t,uint32_t,void *),void *);
int mmio_wait(volatile uint32_t *,uint32_t,uint32_t);
void *dma_alloc(uint32_t);
#endif
