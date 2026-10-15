#ifndef SYSTEMZERO_BLOCK_H
#define SYSTEMZERO_BLOCK_H
#include <stdint.h>
#include "../../env_vars.h"
#include "../pci.h"
enum { BLOCK_IDE, BLOCK_AHCI, BLOCK_NVME };
typedef struct {
    uint32_t sectors,kind;
    void *context;
    int (*transfer)(void *,uint32_t,uint8_t,void *,int);
    int (*sync)(void *);
} block_device_t;
typedef struct block_runtime { block_device_t devices[4]; } block_runtime_t;
void block_init(env_vars_t *);
int block_add(block_runtime_t *,block_device_t);
void ahci_probe(pci_device_t,block_runtime_t *);
void nvme_probe(pci_device_t,block_runtime_t *);
const char *block_drive_kind(env_vars_t *,uint8_t);
#endif
