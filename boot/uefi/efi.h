#ifndef SYSTEMZERO_EFI_H
#define SYSTEMZERO_EFI_H
/* Minimal UEFI 2.x declarations. Protocol layouts follow the UEFI specification
   and edk2 MdePkg/Include/{Uefi/UefiSpec,Protocol/GraphicsOutput}.h. */
#include <stdint.h>
#include <stddef.h>
#define EFIAPI __attribute__((ms_abi))
typedef uint64_t status_t;
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } guid_t;
typedef struct { uint64_t signature; uint32_t revision,size,crc,reserved; } table_header_t;
typedef struct {
    table_header_t header;
    void *tpl[2];
    status_t (EFIAPI *allocate_pages)(uint32_t,uint32_t,uint64_t,uint64_t *);
    void *free_pages;
    status_t (EFIAPI *memory_map)(uint64_t *,void *,uint64_t *,uint64_t *,uint32_t *);
    status_t (EFIAPI *allocate_pool)(uint32_t,uint64_t,void **);
    status_t (EFIAPI *free_pool)(void *);
    void *create_event,*set_timer;
    status_t (EFIAPI *wait_for_event)(uint64_t,void **,uint64_t *);
    void *signal_event,*close_event,*check_event;
    void *install_protocol,*reinstall_protocol,*uninstall_protocol;
    status_t (EFIAPI *handle_protocol)(void *,guid_t *,void **);
    void *reserved,*register_protocol,*locate_handle,*locate_device_path,*install_config;
    void *images[4];
    status_t (EFIAPI *exit_boot)(void *,uint64_t);
    void *monotonic, *stall;
    status_t (EFIAPI *watchdog)(uint64_t,uint64_t,uint64_t,uint16_t *);
    status_t (EFIAPI *connect_controller)(void *,void **,void *,uint8_t);
    void *disconnect_controller,*open_protocol,*close_protocol,*protocol_info,*protocols_per_handle;
    status_t (EFIAPI *locate_handles)(uint32_t,guid_t *,void *,uint64_t *,void ***);
    status_t (EFIAPI *locate_protocol)(guid_t *,void *,void **);
    void *remaining[6];
} boot_services_t;
typedef struct {
    void *reset;
    status_t (EFIAPI *output)(void *,uint16_t *);
    void *test_string,*query_mode,*set_mode;
    status_t (EFIAPI *set_attribute)(void *,uint64_t);
    status_t (EFIAPI *clear)(void *);
    void *set_cursor,*enable_cursor,*mode;
} text_output_t;
typedef struct {
    table_header_t header;
    uint16_t *vendor; uint32_t revision;
    void *input_handle, *input, *output_handle;
    text_output_t *output;
    void *error_handle,*error,*runtime;
    boot_services_t *boot;
    uint64_t tables; void *config;
} system_table_t;
typedef struct { uint32_t version,width,height,format,red,green,blue,reserved,pitch; } mode_info_t;
typedef struct { uint32_t maximum,current; mode_info_t *info; uint64_t info_size,framebuffer,framebuffer_size; } mode_t;
typedef struct graphics {
    status_t (EFIAPI *query)(struct graphics *,uint32_t,uint64_t *,mode_info_t **);
    status_t (EFIAPI *set)(struct graphics *,uint32_t);
    void *blit; mode_t *mode;
} graphics_t;
_Static_assert(offsetof(boot_services_t, exit_boot) == 24 + 26 * 8, "ExitBootServices ABI");
_Static_assert(offsetof(boot_services_t, locate_protocol) == 24 + 37 * 8, "LocateProtocol ABI");
_Static_assert(offsetof(boot_services_t, handle_protocol) == 24 + 16 * 8, "HandleProtocol ABI");
_Static_assert(offsetof(boot_services_t, locate_handles) == 24 + 36 * 8, "LocateHandleBuffer ABI");
_Static_assert(offsetof(system_table_t, boot) == 96, "EFI system table ABI");
#endif
