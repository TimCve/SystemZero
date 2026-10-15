#ifndef SYSTEMZERO_PARTITION_H
#define SYSTEMZERO_PARTITION_H
#include <stdint.h>
#define SZ_PARTITIONS 16u
enum { SZ_TABLE_NONE, SZ_TABLE_MBR, SZ_TABLE_GPT };
typedef struct { uint32_t first, sectors, slot, type; uint8_t guid[16], unique[16]; char label[37]; } partition_t;
typedef struct { uint32_t kind, count, first, last, entries_lba, entries_count, entry_bytes; int readonly; uint8_t guid[16]; partition_t entries[SZ_PARTITIONS]; } partition_table_t;
typedef struct { void *context; uint32_t sectors; int (*read)(void *,uint32_t,void *); int (*write)(void *,uint32_t,void *); int (*sync)(void *); } partition_disk_t;
int partition_scan(const partition_disk_t *,partition_table_t *);
int partition_initialize(const partition_disk_t *,uint32_t,partition_table_t *);
int partition_add(const partition_disk_t *,partition_table_t *,uint32_t);
int partition_remove(const partition_disk_t *,partition_table_t *,uint32_t);
uint32_t partition_crc(const void *,uint32_t);
#endif
