#include "efi.h"
#include "../../shared/boot.h"
#include "../../shared/elf.h"
#include "font8x8.h"
extern const uint8_t _binary_build_kernel_kernel_elf_start[], _binary_build_kernel_kernel_elf_end[];
extern const uint8_t _binary_build_uefi_bridge_bin_start[], _binary_build_uefi_bridge_bin_end[];
typedef void (__attribute__((sysv_abi)) *bridge_t)(uint32_t,uint32_t,uint32_t);
/* The existing env ABI is i386, even though firmware is x64. */
typedef struct {
    uint32_t free; uint8_t system_drive,selected_drive,color,padding;
    uint32_t calibration,last,filesystem,terminal,api,desktop,run,boot,disks,keyboard_poll;
    uint8_t selected_partition,system_partition; uint16_t reserved;
    uint32_t variables,input;
} env32_t;
_Static_assert(sizeof(env32_t) == 60, "i386 environment ABI");
static void copy(void *dst,const void *src,uint32_t bytes) {
    uint8_t *d=dst; const uint8_t *s=src; while(bytes--) *d++=*s++;
}
static void debug(const char *text) {
    while(*text) { __asm__ volatile("outb %0,$0xe9" : : "a"(*text)); text++; }
}
static status_t fail(system_table_t *system, const char *message) {
    uint16_t text[160]; uint32_t i=0;
    while(message[i] && i<157) { text[i]=(uint8_t)message[i]; i++; }
    text[i++]='\r'; text[i++]='\n'; text[i]=0;
    if(system->output) system->output->output(system->output,text);
    return 0x8000000000000003ull;
}
static int compatible(const mode_info_t *info) {
    return info->width >= 800 && info->height >= 600 && info->pitch >= info->width &&
        info->pitch <= 4096 && info->height <= 2160 &&
        (info->format <= 1 || (info->format == 2 && info->green == 0xff00 &&
        ((info->red == 0xff0000 && info->blue == 0xff) || (info->red == 0xff && info->blue == 0xff0000))));
}
status_t EFIAPI efi_main(void *image, system_table_t *system) {
    boot_services_t *bs=system->boot;
    bs->watchdog(0,0,0,0);
    guid_t guid={0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    graphics_t *graphics=0;
    if(bs->locate_protocol(&guid,0,(void **)&graphics) || !graphics || !graphics->mode)
        return fail(system,"SystemZero: no Graphics Output Protocol.");
    uint32_t best=UINT32_MAX,area=UINT32_MAX;
    for(uint32_t i=0;i<graphics->mode->maximum;i++) {
        mode_info_t *info=0; uint64_t size=0;
        if(graphics->query(graphics,i,&size,&info)) continue;
        if(size >= sizeof(*info) && compatible(info) && info->width*info->height < area) {
            best=i; area=info->width*info->height;
        }
        bs->free_pool(info);
    }
    if(best==UINT32_MAX || graphics->set(graphics,best)) return fail(system,"SystemZero: no usable 32-bit graphics mode.");
    mode_t *mode=graphics->mode; mode_info_t *info=mode->info;
    uint64_t pixel_bytes=(uint64_t)info->pitch*info->height*4;
    if(!compatible(info) || !mode->framebuffer || mode->framebuffer > UINT32_MAX ||
        pixel_bytes > mode->framebuffer_size || pixel_bytes > UINT32_MAX-mode->framebuffer)
        return fail(system,"SystemZero: framebuffer must fit below 4 GiB.");
    /* Allocate a contiguous arena from firmware's usable memory, never guessed RAM. */
    uint64_t base=0xbfffffffu; uint32_t arena=32*1024*1024;
    if(bs->allocate_pages(1,1,arena/4096,&base) || base < 0x100000 || base > UINT32_MAX-arena)
        return fail(system,"SystemZero: cannot reserve 32 MiB below 3 GiB.");
    uint32_t kernel=(uint32_t)base+0x20000;
    env32_t *env=(void *)(uintptr_t)(base+0x2000);
    boot_info_t *boot=(void *)(uintptr_t)(base+0x2100);
    const uint8_t *source=_binary_build_kernel_kernel_elf_start;
    uint32_t bytes=_binary_build_kernel_kernel_elf_end-source;
    elf_plan_t plan;
    if(!elf_plan(source,bytes,&plan) || plan.size > arena-0x20000 ||
        !elf_load(source,bytes,&plan,(void *)(uintptr_t)kernel,plan.size))
        return fail(system,"SystemZero: invalid embedded kernel ELF.");
    uint32_t heap=(kernel+plan.size+4095)&~4095u;
    *boot=(boot_info_t){.magic=SZ_BOOT_UEFI,.size=sizeof(*boot),.framebuffer=mode->framebuffer,
        .width=info->width,.height=info->height,.pitch=info->pitch,
        .red_first=info->format==0 || (info->format==2 && info->red==255),.font=base+0x1000,
        .heap_start=heap,.heap_end=base+arena,.kernel_base=kernel};
    *env=(env32_t){.free=heap,.color=0x1f,.boot=(uint32_t)(uintptr_t)boot};
    copy((void *)(uintptr_t)base,_binary_build_uefi_bridge_bin_start,
         _binary_build_uefi_bridge_bin_end-_binary_build_uefi_bridge_bin_start);
    copy((void *)(uintptr_t)boot->font,boot_font,sizeof boot_font);
    boot->memory_map=base+0x3000;
    char trace[]="SZ UEFI env=00000000\n";
    uint32_t address=(uintptr_t)env;
    for(uint32_t i=0;i<8;i++) trace[12+i]="0123456789abcdef"[(address >> (28-i*4))&15];
    debug(trace);
    uint64_t map_bytes,key,stride; uint32_t version; status_t status=1;
    /* Nothing may allocate between obtaining the key and exiting boot services. */
    for(uint32_t retry=0;retry<8;retry++) {
        map_bytes=0xc000;
        if(bs->memory_map(&map_bytes,(void *)(uintptr_t)boot->memory_map,&key,&stride,&version))
            return fail(system,"SystemZero: cannot capture firmware memory map.");
        if(stride < 40 || map_bytes > UINT32_MAX) return fail(system,"SystemZero: invalid memory map.");
        boot->memory_map_bytes=map_bytes; boot->descriptor_bytes=stride;
        status=bs->exit_boot(image,key);
        if(!status) break;
    }
    if(status) return fail(system,"SystemZero: ExitBootServices failed.");
    debug("SZ UEFI ExitBootServices OK\n");
    ((bridge_t)(uintptr_t)base)(kernel+plan.entry,(uintptr_t)env,base+0x20000);
    for(;;) __asm__ volatile("cli; hlt");
}
/* Keep one native COFF relocation so firmware can relocate this image freely. */
__attribute__((used)) static const void *entry_reference=(void *)efi_main;
