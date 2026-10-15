#ifndef SZ_INSTALLER_PROTOCOLS_H
#define SZ_INSTALLER_PROTOCOLS_H
#include "../uefi/efi.h"
typedef struct { uint16_t scan,character; } input_key_t;
typedef struct {
    status_t (EFIAPI *reset)(void *,uint8_t);
    status_t (EFIAPI *read)(void *,input_key_t *);
    void *wait;
} text_input_t;
typedef struct {
    uint32_t id; uint8_t removable,present,partition,readonly,caching;
    uint32_t block_size,alignment;
    uint64_t last;
} block_media_t;
typedef struct block_io {
    uint64_t revision; block_media_t *media;
    status_t (EFIAPI *reset)(struct block_io *,uint8_t);
    status_t (EFIAPI *read)(struct block_io *,uint32_t,uint64_t,uint64_t,void *);
    status_t (EFIAPI *write)(struct block_io *,uint32_t,uint64_t,uint64_t,void *);
    status_t (EFIAPI *flush)(struct block_io *);
} block_io_t;
typedef struct file_io {
    uint64_t revision;
    status_t (EFIAPI *open)(struct file_io *,struct file_io **,uint16_t *,uint64_t,uint64_t);
    status_t (EFIAPI *close)(struct file_io *);
    void *remove;
    status_t (EFIAPI *read)(struct file_io *,uint64_t *,void *);
    void *write,*position;
    status_t (EFIAPI *seek)(struct file_io *,uint64_t);
    status_t (EFIAPI *info)(struct file_io *,guid_t *,uint64_t *,void *);
} file_io_t;
typedef struct { uint64_t revision; status_t (EFIAPI *open)(void *,file_io_t **); } filesystem_t;
typedef struct {
    uint32_t revision;
    void *parent,*system,*device,*path,*reserved;
    uint32_t options_size; void *options,*base;
    uint64_t size; uint32_t code_type,data_type; void *unload;
} loaded_image_t;
typedef struct {
    void *node;
    uint16_t *(EFIAPI *path)(void *,uint8_t,uint8_t);
} path_text_t;
typedef struct {
    table_header_t header; void *functions[6];
    status_t (EFIAPI *get_variable)(uint16_t *,guid_t *,uint32_t *,uint64_t *,void *);
    void *next_variable;
    status_t (EFIAPI *set_variable)(uint16_t *,guid_t *,uint32_t,uint64_t,void *);
    void *monotonic;
    void (EFIAPI *reset)(uint32_t,status_t,uint64_t,void *);
} runtime_services_t;
typedef struct {
    void *info;
    status_t (EFIAPI *get)(void *,guid_t *,uint64_t,uint8_t *);
} rng_t;
_Static_assert(offsetof(block_media_t,last)==24,"EFI block media ABI");
_Static_assert(offsetof(loaded_image_t,device)==24,"EFI loaded image ABI");
_Static_assert(offsetof(file_io_t,seek)==56,"EFI file ABI");
_Static_assert(offsetof(runtime_services_t,reset)==24+10*8,"EFI runtime ABI");
#endif
