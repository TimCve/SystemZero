#include "protocols.h"
#include "disk.h"
#include "payload.h"
#define LIMIT 32
static guid_t loaded_guid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
static guid_t fs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static guid_t block_guid={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static guid_t path_guid={0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static guid_t text_guid={0x8b843e20,0x8132,0x4852,{0x90,0xcc,0x55,0x1a,0x4e,0x4a,0x7f,0x1c}};
static guid_t info_guid={0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static guid_t rng_guid={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};
static system_table_t *system;
static file_io_t *payload;
static unsigned last_phase=~0u,last_percent=~0u;
typedef struct { block_io_t *io; uint8_t *path; uint16_t *name; uint32_t id; uint64_t last; } disk_t;
static disk_t disks[LIMIT];
static void trace(const char *s) { while(*s) { __asm__ volatile("outb %0,$0xe9"::"a"(*s)); s++; } }
static void print(const char *s) {
    trace(s);
    uint16_t wide[160]; unsigned n=0;
    while(*s) { if(n==159) { wide[n]=0; system->output->output(system->output,wide); n=0; }
        wide[n++]=(uint8_t)*s++; }
    wide[n]=0; system->output->output(system->output,wide);
}
static void number(uint64_t n) { char b[24]; unsigned at=23; b[at]=0; do { b[--at]='0'+n%10; n/=10; } while(n); print(b+at); }
static input_key_t key(void) {
    text_input_t *input=system->input; input_key_t k;
    while(input->read(input,&k)) { uint64_t index; system->boot->wait_for_event(1,&input->wait,&index); }
    return k;
}
static void line(char *text,unsigned capacity) {
    unsigned n=0;
    for(;;) {
        input_key_t k=key();
        if(k.scan==23) { text[0]=0; print("\r\n"); return; }
        if(k.character=='\r') { text[n]=0; print("\r\n"); return; }
        if(k.character==8 && n) { n--; print("\b \b"); }
        else if(k.character>=32 && k.character<127 && n+1<capacity) {
            text[n++]=k.character; char echo[2]={(char)k.character,0}; print(echo);
        }
    }
}
static int same(const char *a,const char *b) { while(*a && *a==*b) a++,b++; return *a==*b; }
static void heading(void) {
    system->output->set_attribute(system->output,0x1f);
    system->output->clear(system->output);
    print("SystemZero UEFI disk installer\r\n\r\n");
}
static void describe(unsigned i) {
    disk_t *d=&disks[i];
    print("Disk "); number(i+1); print("  "); number((d->last+1)/2048); print(" MiB  ");
    print(d->io->media->removable ? "removable":"fixed"); print("\r\n");
    if(d->name) system->output->output(system->output,d->name);
    else {
        /* Even without DevicePathToText, show the complete binary path. */
        print("Device path: "); const uint8_t *p=d->path; unsigned at=0;
        while(at<=4092) {
            unsigned length=p[at+2]|(unsigned)p[at+3]<<8;
            if(length<4 || length>4096-at || p[at]==0x7f) break;
            for(unsigned j=0;j<length;j++) {
                char b[3]={"0123456789ABCDEF"[p[at+j]>>4],"0123456789ABCDEF"[p[at+j]&15],0}; print(b);
            }
            print("/"); at+=length;
        }
    }
    print("\r\n");
}
static int source(void *context,uint32_t offset,void *buffer,uint32_t bytes) {
    (void)context; uint64_t size=bytes;
    return payload->seek(payload,offset) || payload->read(payload,&size,buffer) || size!=bytes;
}
static int read_disk(void *context,uint32_t lba,void *buffer,uint32_t bytes) {
    disk_t *d=context; return !!d->io->read(d->io,d->id,lba,bytes,buffer);
}
static int write_disk(void *context,uint32_t lba,const void *buffer,uint32_t bytes) {
    disk_t *d=context; return !!d->io->write(d->io,d->id,lba,bytes,(void *)buffer);
}
static int flush_disk(void *context) { disk_t *d=context; return !!d->io->flush(d->io); }
static void progress(void *context,unsigned phase,uint32_t done,uint32_t total) {
    (void)context; unsigned percent=(uint64_t)done*100/total;
    if(phase==last_phase && percent==last_percent) return;
    if(phase!=last_phase) { print("\r\n"); trace(phase==INSTALL_CHECKING ? "SZ INSTALL checking\n":phase==INSTALL_COPYING ? "SZ INSTALL copying\n":"SZ INSTALL verifying\n"); }
    print("\r"); print(phase==INSTALL_CHECKING ? "Checking installer image: ":phase==INSTALL_COPYING ? "Writing destination disk: ":"Verifying destination disk: ");
    number(percent); print("%   "); last_phase=phase; last_percent=percent;
}
static void *aligned(uint32_t bytes,uint32_t alignment) {
    uint64_t address=0; if(alignment<4096) alignment=4096;
    if(system->boot->allocate_pages(0,2,(bytes+alignment+4095)/4096,&address)) return 0;
    return (void *)(uintptr_t)((address+alignment-1)&~(uint64_t)(alignment-1));
}
static status_t stop(const char *message) {
    print("\r\n"); print(message); print("\r\nPress any key to return to firmware.\r\n");
    trace("SZ INSTALL stopped\n"); key(); return 0x8000000000000003ull;
}
static int register_boot(disk_t *disk,const uint8_t *partition_guid) {
    guid_t global={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,0x03,0x2b,0x8c}};
    runtime_services_t *rt=system->runtime; boot_services_t *bs=system->boot;
    uint16_t name[]=u"Boot0000",order_name[]=u"BootOrder"; uint64_t bytes=0;
    status_t status=rt->get_variable(order_name,&global,0,&bytes,0);
    if(status==0x800000000000000eull) bytes=0;
    else if(status!=0x8000000000000005ull || bytes>65534 || bytes%2) return 0;
    uint64_t allocated=bytes; uint16_t *order=0; void *option=0;
    if(bs->allocate_pool(2,bytes+2,(void **)&order)) return 0;
    if(bytes && (rt->get_variable(order_name,&global,0,&bytes,order) || bytes>allocated || bytes%2)) {
        bs->free_pool(order); return 0;
    }
    unsigned number;
    for(number=0;number<256;number++) {
        for(unsigned digit=0;digit<4;digit++) name[7-digit]="0123456789ABCDEF"[(number>>(digit*4))&15];
        uint64_t size=0;
        if(rt->get_variable(name,&global,0,&size,0)==0x800000000000000eull) break;
    }
    uint32_t size=0; int ok=0;
    if(number<256 && !bs->allocate_pool(2,8192,&option)) size=install_load_option(disk->path,partition_guid,option,8192);
    if(size && !rt->set_variable(name,&global,7,size,option)) {
        unsigned count=bytes/2;
        for(unsigned i=count;i>0;i--) order[i]=order[i-1];
        order[0]=number;
        ok=!rt->set_variable(order_name,&global,7,bytes+2,order);
        if(!ok) rt->set_variable(name,&global,0,0,0);
    }
    if(option) bs->free_pool(option);
    bs->free_pool(order);
    return ok;
}
status_t EFIAPI efi_main(void *image,system_table_t *st) {
    system=st; boot_services_t *bs=st->boot; bs->watchdog(0,0,0,0); heading();
    loaded_image_t *loaded=0; filesystem_t *fs=0; file_io_t *root=0; uint8_t *source_path=0;
    if(bs->handle_protocol(image,&loaded_guid,(void **)&loaded) || !loaded ||
       bs->handle_protocol(loaded->device,&path_guid,(void **)&source_path) ||
       install_path_parent(source_path,source_path)!=1 ||
       bs->handle_protocol(loaded->device,&fs_guid,(void **)&fs) || fs->open(fs,&root) ||
       root->open(root,&payload,(uint16_t *)u"\\SYSTEM.IMG",1,0))
        return stop("Cannot locate installer files or identify the installer device. No disk was written.");
    root->close(root);
    uint8_t info[512]; uint64_t info_bytes=sizeof info;
    if(payload->info(payload,&info_guid,&info_bytes,info) || info_bytes<24 || *(uint64_t *)(info+8)!=INSTALL_IMAGE_BYTES)
        return stop("Installer image has the wrong size. No disk was written.");
    /* Firmware may connect only boot devices. Bind its remaining drivers before
       listing destinations, including disks with no current boot entry. */
    for(unsigned pass=0;pass<3;pass++) {
        uint64_t count=0; void **handles=0;
        if(bs->locate_handles(0,0,0,&count,&handles)) break;
        for(uint64_t i=0;i<count;i++) bs->connect_controller(handles[i],0,0,1);
        bs->free_pool(handles);
    }
    path_text_t *text=0; bs->locate_protocol(&text_guid,0,(void **)&text);
    uint64_t count=0; void **handles=0; unsigned found=0;
    if(bs->locate_handles(2,&block_guid,0,&count,&handles)) return stop("No firmware block devices found.");
    for(uint64_t i=0;i<count;i++) {
        block_io_t *io=0; uint8_t *path=0;
        if(bs->handle_protocol(handles[i],&block_guid,(void **)&io) || !io || !io->media ||
           !io->media->present || io->media->partition) continue;
        if(bs->handle_protocol(handles[i],&path_guid,(void **)&path) || install_path_parent(path,path)!=1) continue;
        int parent=install_path_parent(path,source_path);
        if(parent!=0) continue;
        block_media_t *m=io->media;
        if(!install_path_storage(path) || m->readonly || m->block_size!=512 || m->last==UINT64_MAX || m->last+1>UINT32_MAX ||
           m->last+1<INSTALL_IMAGE_BYTES/512 || m->alignment>1024*1024 ||
           (m->alignment && (m->alignment&(m->alignment-1)))) continue;
        if(found==LIMIT) return stop("Too many destination disks. Disconnect unused disks and restart.");
        disks[found++]=(disk_t){io,path,text ? text->path(path,0,0):0,m->id,m->last};
    }
    bs->free_pool(handles);
    trace("SZ INSTALL ready\n");
    if(!found) return stop("No eligible destination. Need writable SATA/NVMe/IDE storage with\r\n512-byte sectors, 166 MiB to below 2 TiB. USB and 4Kn targets are unsupported.\r\nThe installer disk is protected.");
    unsigned selected=0;
    for(;;) {
        heading();
        print("Replaces ALL partitions on the selected disk. Existing files will be lost.\r\n");
        print("Creates a 64 MiB EFI partition and a 100 MiB ESFS system volume.\r\n");
        print("Remaining disk space is left unallocated. This is not a secure erase.\r\n\r\n");
        print("Installer source: protected. Eligible destination disks: "); number(found); print("\r\n\r\n");
        describe(selected);
        print("\r\nLeft/Right: choose disk   Enter: review installation   Escape: cancel\r\n");
        input_key_t k=key();
        if(k.scan==23) { trace("SZ INSTALL cancelled\n"); return 0; }
        if(k.scan==4 || k.scan==2) selected=(selected+1)%found;
        else if(k.scan==3 || k.scan==1) selected=(selected+found-1)%found;
        else if(k.character=='\r') break;
    }
    heading(); describe(selected);
    print("\r\nALL PARTITIONS AND FILES ON THIS DISK WILL BE REPLACED.\r\n");
    print("SystemZero is experimental; successful installation does not prove that\r\n");
    print("its kernel supports this computer. Use a spare disk and keep backups.\r\n\r\n");
    print("Type ERASE DISK "); number(selected+1); print(" to install, or press Escape to cancel:\r\n> ");
    char confirmation[40],expected[]="ERASE DISK 00"; line(confirmation,sizeof confirmation);
    unsigned digits=selected+1;
    if(digits<10) { expected[11]='0'+digits; expected[12]=0; }
    else { expected[11]='0'+digits/10; expected[12]='0'+digits%10; expected[13]=0; }
    if(!same(confirmation,expected)) { print("Cancelled. No disk was written.\r\n"); trace("SZ INSTALL cancelled\n"); return 0; }
    disk_t *disk=&disks[selected]; block_media_t *media=disk->io->media;
    if(!media->present || media->readonly || media->partition || media->id!=disk->id || media->last!=disk->last || media->block_size!=512)
        return stop("Destination media changed. No disk was written.");
    uint8_t *buffer=aligned(INSTALL_CHUNK,media->alignment),*verify=aligned(INSTALL_CHUNK,media->alignment),*metadata=aligned(INSTALL_METADATA,media->alignment);
    if(!buffer || !verify || !metadata) return stop("Insufficient firmware memory. No disk was written.");
    install_disk_t install={.context=disk,.source=source,.read=read_disk,.write=write_disk,.flush=flush_disk,.progress=progress,
        .sectors=disk->last+1,.image_bytes=INSTALL_IMAGE_BYTES,.image_crc=INSTALL_IMAGE_CRC};
    rng_t *rng=0;
    if(bs->locate_protocol(&rng_guid,0,(void **)&rng) || !rng || rng->get(rng,0,sizeof install.identifiers,install.identifiers)) {
        /* GUID uniqueness only, not cryptographic key material. */
        uint32_t low,high; __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
        uint64_t seed=(uint64_t)high<<32|low; seed^=(uintptr_t)disk; if(!seed) seed=1;
        for(unsigned i=0;i<sizeof install.identifiers;i++) { seed^=seed<<13; seed^=seed>>7; seed^=seed<<17; install.identifiers[i]=seed; }
    }
    int result=install_disk(&install,buffer,verify,metadata);
    if(result) {
        static const char *errors[]={"", "Installer image failed validation.", "Unsupported destination size.", "A disk read failed.",
            "A disk write failed.", "A disk flush failed.", "Written data did not verify."};
        print("\r\nINSTALLATION FAILED: "); print(errors[result]);
        return stop("Do not boot the destination disk. Correct the problem and reinstall.");
    }
    int boot_registered=register_boot(disk,metadata+1024+16);
    print("\r\n\r\nInstallation verified.\r\n");
    if(boot_registered) { print("SystemZero added to the firmware boot menu.\r\n"); trace("SZ INSTALL boot entry saved\n"); }
    else print("Firmware boot entry could not be saved. Use your firmware's Boot from File\r\noption to launch \\EFI\\BOOT\\BOOTX64.EFI from the installed EFI partition.\r\n");
    trace("SZ INSTALL success\n");
    print("Remove the installer USB/DVD, then press Enter to restart.\r\n");
    while(key().character!='\r') {}
    ((runtime_services_t *)st->runtime)->reset(0,0,0,0);
    return 0;
}
__attribute__((used)) static const void *entry_reference=(void *)efi_main;
