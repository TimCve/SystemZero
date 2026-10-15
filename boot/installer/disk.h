#ifndef SZ_INSTALLER_DISK_H
#define SZ_INSTALLER_DISK_H
#include <stdint.h>
#define INSTALL_CHUNK (256u * 1024u)
#define INSTALL_METADATA (34u * 512u)
enum { INSTALL_OK, INSTALL_IMAGE, INSTALL_SIZE, INSTALL_READ, INSTALL_WRITE, INSTALL_FLUSH, INSTALL_VERIFY };
enum { INSTALL_CHECKING, INSTALL_COPYING, INSTALL_VERIFYING };
typedef struct {
    void *context;
    int (*source)(void *,uint32_t,void *,uint32_t);
    int (*read)(void *,uint32_t,void *,uint32_t);
    int (*write)(void *,uint32_t,const void *,uint32_t);
    int (*flush)(void *);
    void (*progress)(void *,unsigned,uint32_t,uint32_t);
    uint64_t sectors;
    uint32_t image_bytes,image_crc;
    uint8_t identifiers[48];
} install_disk_t;
/* Buffers are caller-owned and must satisfy the destination's I/O alignment.
   No destination write is issued until the complete source passes validation. */
int install_disk(const install_disk_t *,void *buffer,void *verify,void *metadata);
uint32_t install_crc(uint32_t,const void *,uint32_t);
int install_path_parent(const uint8_t *,const uint8_t *);
int install_path_storage(const uint8_t *);
uint32_t install_load_option(const uint8_t *disk_path,const uint8_t *partition_guid,void *out,uint32_t capacity);
#endif
