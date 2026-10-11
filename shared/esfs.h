#ifndef SYSTEMZERO_ESFS_H
#define SYSTEMZERO_ESFS_H
#include <stdint.h>

#define ESFS_SECTOR 512u
#define ESFS_SUPER_LBA 200u
#define ESFS_MAGIC 0xf0f03411u
#define ESFS_LEGACY_MAGIC 0xf0f03410u
#define ESFS_MAX_NAME 511u
#define ESFS_MAX_FILE (14u * 128u * 512u - 512u)
#define ESFS_INODE_CACHE_SECTORS 32u
enum { ESFS_OK = 0, ESFS_IO = -1, ESFS_INVALID = -2,
       ESFS_NOT_FOUND = -3, ESFS_EXISTS = -4, ESFS_FULL = -5 };
typedef struct {
    uint32_t magic, blocks, inode_blocks, inodes, disk_size, last_allocated_block;
} superblock_t;
typedef struct { uint32_t valid, size, indirect_pointers[14]; } inode_t;
typedef int (*esfs_io_fn)(void *, uint32_t, void *);
typedef int (*esfs_read_many_fn)(void *, uint32_t, uint32_t, void *);
typedef int (*esfs_sync_fn)(void *);
typedef struct {
    void *io;
    esfs_io_fn read, write;
    uint32_t sectors;
    superblock_t sb;
    uint8_t *used;
    uint32_t used_bytes, next;
    int mounted;
    esfs_read_many_fn read_many;
    esfs_sync_fn sync;
    /* Optional inode-sector window. Shared mounts retain this between commands. */
    uint8_t *inode_cache;
    /* inode_end is a validated upper bound, updated when publishing new files. */
    uint32_t cache_first, cache_count, inode_end;
    uint32_t pointer_lba, pointers[128];
} esfs_t;
typedef int (*esfs_visit_fn)(void *, const char *, const inode_t *);
uint32_t esfs_map_bytes(uint32_t sectors);
int esfs_format(esfs_t *fs);
int esfs_mount(esfs_t *fs);
int esfs_find(esfs_t *fs, const char *name, inode_t *out, uint32_t *index);
int esfs_visit(esfs_t *fs, esfs_visit_fn visit, void *arg);
int esfs_create(esfs_t *fs, const char *name);
int esfs_delete(esfs_t *fs, const char *name);
int esfs_read(esfs_t *fs, const char *name, void *dst, uint32_t offset,
              uint32_t count, uint32_t *actual);
int esfs_append(esfs_t *fs, const char *name, const void *src, uint32_t count);
int esfs_replace(esfs_t *fs, const char *name, const void *src, uint32_t count);
#endif
