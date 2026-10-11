#include "fs.h"
#include "ata.h"
#include "../utils/mem.h"
#include "../io/screen.h"
typedef struct {
    esfs_t drives[4];
    uint8_t inode_cache[4][ESFS_INODE_CACHE_SECTORS * ESFS_SECTOR];
} fs_runtime_t;
static env_vars_t *environment;
static int disk_read(void *arg, uint32_t lba, void *data) {
    select_drive((uint8_t)(uintptr_t)arg);
    return read_sectors_ATA_PIO(data, lba, 1) ? 0 : -1;
}
static int disk_write(void *arg, uint32_t lba, void *data) {
    select_drive((uint8_t)(uintptr_t)arg);
    return write_sectors_ATA_PIO(lba, 1, data) ? 0 : -1;
}
static int disk_read_many(void *arg, uint32_t lba, uint32_t count, void *data) {
    select_drive((uint8_t)(uintptr_t)arg);
    uint8_t *target = data;
    while (count) {
        uint8_t batch = count > 255 ? 255 : count;
        if (!read_sectors_ATA_PIO(target, lba, batch)) return -1;
        target += batch * ESFS_SECTOR; lba += batch; count -= batch;
    }
    return 0;
}
static int disk_sync(void *arg) {
    select_drive((uint8_t)(uintptr_t)arg);
    return ata_flush() ? 0 : -1;
}
static esfs_t *current_fs(void) {
    uint8_t drive = selected_ata_drive();
    if (!environment || !environment->filesystem || drive >= 4) return 0;
    return &((fs_runtime_t *)environment->filesystem)->drives[drive];
}
void fs_bind_env(env_vars_t *env) { environment = env; }
int set_superblock(void) {
    esfs_t *fs = current_fs();
    return fs && fs->used && (fs->mounted || !esfs_mount(fs));
}
int check_disk_fs(void) { return set_superblock(); }
int init_fs(env_vars_t *env) {
    static fs_runtime_t runtime;
    fs_bind_env(env);
    if (!env->filesystem) {
        /* Reserve persistent maps before execute() starts allocating temporary memory.
           Callbacks live in the kernel, so programs may safely share these mounts. */
        uint8_t previous = selected_ata_drive();
        for (uint8_t drive = 0; drive < 4; drive++) {
            select_drive(drive);
            uint32_t sectors = ata_sector_count();
            if (!sectors || sectors > 0x7fffffu) continue;
            uint32_t bytes = esfs_map_bytes(sectors), map = malloc(bytes, 0, 0);
            if (!map) continue;
            runtime.drives[drive] = (esfs_t){.io = (void *)(uintptr_t)drive,
                .read = disk_read, .write = disk_write, .read_many = disk_read_many,
                .sync = disk_sync, .sectors = sectors, .used = (void *)map,
                .used_bytes = bytes, .inode_cache = runtime.inode_cache[drive]};
        }
        env->filesystem = &runtime; env->free_mem_ptr = get_free_ptr();
        select_drive(previous);
    }
    return set_superblock();
}
int format_disk(void) {
    esfs_t *fs = current_fs();
    return fs && fs->used && !esfs_format(fs);
}
inode_t get_file_info(const char *name) {
    inode_t in = {0}; if (set_superblock()) esfs_find(current_fs(), name, &in, 0); return in;
}
int file_create(const char *name, env_vars_t *env) {
    (void)env; return !set_superblock() || esfs_create(current_fs(), name) ? 1 : 0;
}
int file_delete(const char *name, env_vars_t *env) {
    (void)env; return !set_superblock() || esfs_delete(current_fs(), name) ? 1 : 0;
}
int file_write(const char *name, const void *data, int bytes, env_vars_t *env) {
    (void)env;
    if (bytes < 0 || !set_superblock() || esfs_append(current_fs(), name, data, bytes)) return 0;
    return bytes;
}
int file_read(const char *name, void *target, uint32_t sectors, uint32_t offset) {
    if (sectors > ESFS_MAX_FILE / 512 + 1 || offset > ESFS_MAX_FILE / 512 + 1 || !set_superblock()) return 2;
    uint32_t bytes = sectors * 512, actual;
    if (!target && bytes) return 2;
    memset(target, 0, bytes);
    if (esfs_read(current_fs(), name, target, offset * 512, bytes, &actual)) return 2;
    inode_t in = get_file_info(name);
    return in.size > offset * 512 + actual ? 1 : 0;
}
int file_replace(const char *name, const void *data, uint32_t bytes) {
    return set_superblock() && !esfs_replace(current_fs(), name, data, bytes);
}
int file_read_bytes(const char *name, void *target, uint32_t bytes, uint32_t offset) {
    uint32_t actual;
    if (bytes > ESFS_MAX_FILE || !set_superblock() ||
        esfs_read(current_fs(), name, target, offset, bytes, &actual)) return -1;
    return actual;
}
static int list_entry(void *arg, const char *name, const inode_t *in) {
    esfs_t *fs = arg; uint8_t header[4] = {0}; uint32_t actual;
    if (esfs_read(fs, name, header, 0, 4, &actual)) return ESFS_IO;
    print(actual == 4 && header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F' ? "[x] " : "[-] ");
    print(" ["); print_dec(in->size); print(" bytes]  "); print((char *)name); print_newline(); return 0;
}
void file_list(void) {
    if (!set_superblock() || esfs_visit(current_fs(), list_entry, current_fs())) { print("Filesystem unavailable or corrupt."); print_newline(); }
}
