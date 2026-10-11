#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/esfs.h"
#include "../shared/elf.h"

static uint8_t disk[8192 * 512], bitmap[1024];
static int fail_read, fail_write;
static int read_block(void *arg, uint32_t b, void *data) {
    (void)arg; assert(b < 8192);
    if (fail_read) return -1;
    memcpy(data, disk + b * 512, 512); return 0;
}
static int write_block(void *arg, uint32_t b, void *data) {
    (void)arg; assert(b < 8192);
    if (fail_write) return -1;
    memcpy(disk + b * 512, data, 512); return 0;
}
static esfs_t filesystem(uint32_t sectors) {
    return (esfs_t){.read=read_block, .write=write_block, .sectors=sectors,
                   .used=bitmap, .used_bytes=sizeof bitmap};
}
static void fs_checks(void) {
    esfs_t fs = filesystem(8192);
    assert(esfs_mount(&fs) == ESFS_INVALID);
    fail_write = 1; assert(esfs_format(&fs) == ESFS_IO); fail_write = 0;
    assert(esfs_format(&fs) == 0);
    assert(esfs_create(&fs, "binary") == 0);
    uint8_t initial[] = {'A', 0, 'B'}, result[70000]; uint32_t n;
    assert(!esfs_append(&fs, "binary", initial, 3));
    assert(!esfs_append(&fs, "binary", "C", 1));
    assert(!esfs_read(&fs, "binary", result, 0, sizeof result, &n));
    assert(n == 4 && !memcmp(result, "A\0BC", 4));
    assert(!esfs_mount(&fs));
    assert(!esfs_read(&fs, "binary", result, 3, 2, &n)); assert(n == 1 && result[0] == 'C');
    assert(!esfs_read(&fs, "binary", result, 4, 2, &n)); assert(n == 0);
    uint8_t *big = malloc(sizeof result); assert(big);
    for (uint32_t i = 0; i < sizeof result; i++) big[i] = (uint8_t)i;
    assert(!esfs_replace(&fs, "large", big, sizeof result));
    assert(!esfs_mount(&fs));
    assert(!esfs_read(&fs, "large", result, 0, sizeof result, &n));
    assert(n == sizeof result && !memcmp(big, result, n));
    assert(!esfs_read(&fs, "large", result, 512, 512, &n));
    assert(n == 512 && !memcmp(big + 512, result, n));
    assert(!esfs_append(&fs, "large", initial, 3));
    assert(!esfs_read(&fs, "large", result, 70000, 3, &n)); assert(n == 3 && !memcmp(initial, result, 3));
    assert(!esfs_create(&fs, "empty")); assert(!esfs_mount(&fs));
    assert(!esfs_replace(&fs, "another", big, 1000));
    assert(!esfs_read(&fs, "empty", result, 0, 1, &n)); assert(n == 0);
    for (int i = 0; i < 140; i++) { char name[32]; snprintf(name, sizeof name, "file-%d", i); assert(!esfs_create(&fs, name)); }
    for (int i = 0; i < 130; i++) { char name[32]; snprintf(name, sizeof name, "file-%d", i); assert(!esfs_delete(&fs, name)); }
    inode_t info;
    assert(!esfs_find(&fs, "file-139", &info, 0)); assert(info.valid);
    assert(esfs_find(&fs, "absent", &info, 0) == ESFS_NOT_FOUND); assert(!info.valid && !info.size);
    fail_read = 1; assert(esfs_find(&fs, "large", &info, 0) == ESFS_IO); fail_read = 0;
    fail_write = 1; assert(esfs_replace(&fs, "binary", "bad", 3) != 0); fail_write = 0;
    assert(!esfs_mount(&fs)); assert(!esfs_read(&fs, "binary", result, 0, 4, &n)); assert(!memcmp(result, "A\0BC", 4));
    /* Mount the legacy host layout without modifying it, then migrate on write. */
    superblock_t legacy = fs.sb; legacy.magic = ESFS_LEGACY_MAGIC; legacy.inode_blocks++;
    legacy.inodes = legacy.inode_blocks * 8; legacy.blocks -= 200; legacy.disk_size -= 200 * 512;
    memcpy(disk + ESFS_SUPER_LBA * 512, &legacy, sizeof legacy);
    assert(!esfs_mount(&fs)); assert(!esfs_append(&fs, "binary", "D", 1));
    assert(!esfs_mount(&fs)); assert(fs.sb.magic == ESFS_MAGIC);
    assert(!esfs_read(&fs, "binary", result, 0, 5, &n)); assert(n == 5 && !memcmp(result, "A\0BCD", 5));
    /* Reject out-of-range metadata before any out-of-range I/O. */
    inode_t *first = (inode_t *)(disk + (ESFS_SUPER_LBA + 1) * 512);
    first[0].indirect_pointers[0] = fs.sectors;
    assert(esfs_mount(&fs) == ESFS_INVALID);
    memset(disk, 0, sizeof disk); fs = filesystem(220); assert(!esfs_format(&fs));
    assert(!esfs_replace(&fs, "kept", "data", 4));
    assert(esfs_append(&fs, "kept", big, 20000) == ESFS_FULL);
    assert(esfs_replace(&fs, "kept", big, 20000) == ESFS_FULL);
    assert(!esfs_read(&fs, "kept", result, 0, 4, &n)); assert(n == 4 && !memcmp(result, "data", 4));
    free(big);
}
static void put16(uint8_t *p, uint32_t x) { p[0] = x; p[1] = x >> 8; }
static void put32(uint8_t *p, uint32_t x) { for (int i = 0; i < 4; i++) p[i] = x >> (i * 8); }
static void phdr(uint8_t *p, uint32_t type, uint32_t off, uint32_t addr, uint32_t filesz, uint32_t memsz, uint32_t flags) {
    put32(p, type); put32(p+4, off); put32(p+8, addr); put32(p+16, filesz);
    put32(p+20, memsz); put32(p+24, flags); put32(p+28, 1);
}
static void elf_checks(void) {
    uint32_t storage[256], loaded[128]; uint8_t *file = (void *)storage;
    memset(file, 0, sizeof storage); memcpy(file, "\177ELF\1\1\1", 7);
    put16(file+16, 3); put16(file+18, 3); put32(file+20, 1); put32(file+24, 0x1010);
    put32(file+28, 52); put16(file+40, 52); put16(file+42, 32); put16(file+44, 3);
    phdr(file+52, 1, 256, 0x1000, 32, 64, 5);
    phdr(file+84, 1, 512, 0x1100, 128, 256, 6);
    phdr(file+116, 2, 512, 0x1100, 32, 32, 6);
    memset(file+256, 0xcc, 32);
    put32(file+512, 17); put32(file+516, 0x1140);
    put32(file+520, 18); put32(file+524, 8);
    put32(file+528, 19); put32(file+532, 8);
    put32(file+576, 0x1180); put32(file+580, 8);
    put32(file+640, 0x1010); /* BSS addend deliberately starts zero after loading */
    elf_plan_t plan; assert(elf_plan(file, sizeof storage, &plan));
    assert(plan.first == 0x1000 && plan.size == 512 && plan.entry == 16);
    memset(loaded, 0xab, sizeof loaded);
    assert(elf_load(file, sizeof storage, &plan, loaded, sizeof loaded));
    uint8_t *memory = (void *)loaded;
    assert(memory[16] == 0xcc && memory[32] == 0 && memory[511] == 0);
    assert(loaded[0x180 / 4] == (uint32_t)(uintptr_t)loaded - 0x1000);
    put32(file+580, 1); assert(!elf_load(file, sizeof storage, &plan, loaded, sizeof loaded));
    put32(file+580, 8);
    assert(!elf_load(file, sizeof storage, &plan, loaded, sizeof loaded - 1));
    put32(file+24, 0x1180); assert(!elf_plan(file, sizeof storage, &plan));
    put32(file+24, 0x1010); put32(file+56, 0xfffffff0); assert(!elf_plan(file, sizeof storage, &plan));
    put32(file+56, 256); put16(file+16, 2); assert(!elf_plan(file, sizeof storage, &plan));
    put16(file+16, 3); put32(file+116, 3); assert(!elf_plan(file, sizeof storage, &plan));
    assert(!elf_plan(file, 51, &plan));
}
int main(void) { fs_checks(); elf_checks(); puts("Filesystem and ELF checks passed."); return 0; }
