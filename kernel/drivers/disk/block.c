#include "block.h"
#include "ata.h"
#include "../utils/mem.h"
#include "../../../shared/partition.h"
static int probe_read(void *context,uint32_t sector,void *out) {
    select_drive((uint8_t)(uintptr_t)context);
    return read_sectors_ATA_PIO(out,sector,1) ? 0:-1;
}
static int probe_write(void *context,uint32_t sector,void *in) { (void)context; (void)sector; (void)in; return -1; }
static block_runtime_t runtime;
int block_add(block_runtime_t *runtime,block_device_t device) {
    for(uint32_t i=0;i<4;i++) if(!runtime->devices[i].sectors) { runtime->devices[i]=device; return 1; }
    return 0;
}
static void visit(pci_device_t device,uint32_t class,void *context) {
    if(class==0x010601) ahci_probe(device,context);
    else if(class==0x010802) nvme_probe(device,context);
}
void block_init(env_vars_t *env) {
    uint8_t previous=selected_ata_drive();
    for(uint8_t i=0;i<4;i++) {
        select_drive(i);
        runtime.devices[i]=(block_device_t){.sectors=ata_sector_count(),.kind=BLOCK_IDE,.context=(void *)(uintptr_t)i};
    }
    pci_scan(visit,&runtime);
    env->disks=&runtime; ata_bind_env(env);
    {
        /* Both BIOS and UEFI locate the system volume by its kernel manifest. */
        uint32_t manifest[128];
        for(uint8_t i=0;i<4;i++) {
            select_drive(i);
            uint32_t partition=0;
            int found=read_sectors_ATA_PIO(manifest,63,1) && manifest[0]==0x535a4b31 && manifest[1]==64;
            if(!found && runtime.devices[i].sectors) {
                partition_disk_t disk={(void *)(uintptr_t)i,runtime.devices[i].sectors,probe_read,probe_write,0};
                partition_table_t table;
                if(!partition_scan(&disk,&table)) for(uint32_t p=0;p<table.count;p++) {
                    if(table.entries[p].slot>SZ_PARTITIONS || table.entries[p].sectors<=200) continue;
                    if(!probe_read(disk.context,table.entries[p].first+63,manifest) && manifest[0]==0x535a4b31 && manifest[1]==64) {
                        found=1; partition=table.entries[p].slot; break;
                    }
                }
            }
            if(found) {
                block_device_t swap=runtime.devices[0]; runtime.devices[0]=runtime.devices[i]; runtime.devices[i]=swap;
                env->system_drive=env->selected_drive=previous=0;
                env->system_partition=env->selected_partition=partition; break;
            }
        }
    }
    select_drive(previous);
}
const char *block_drive_kind(env_vars_t *env,uint8_t drive) {
    if(!env->disks || drive>=4 || !env->disks->devices[drive].sectors) return "Absent";
    uint32_t kind=env->disks->devices[drive].kind;
    return kind==BLOCK_NVME ? "NVMe" : kind==BLOCK_AHCI ? "SATA/AHCI" : "IDE";
}
