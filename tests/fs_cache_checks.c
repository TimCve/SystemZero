#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../shared/esfs.h"

static uint8_t disk[8192 * 512], durable[8192 * 512], bitmap[1024];
static uint8_t cache[ESFS_INODE_CACHE_SECTORS * ESFS_SECTOR];
static uint32_t reads, batches, largest_batch, writes, flushes, fail_flush;
static int read_sector(void *arg, uint32_t lba, void *data) {
    (void)arg; assert(lba < 8192); reads++;
    memcpy(data, disk + lba * 512, 512); return 0;
}
static int read_many(void *arg, uint32_t lba, uint32_t count, void *data) {
    (void)arg; assert(lba < 8192 && count <= 8192 - lba); batches++;
    if (count > largest_batch) largest_batch = count;
    memcpy(data, disk + lba * 512, count * 512); return 0;
}
static int write_sector(void *arg, uint32_t lba, void *data) {
    (void)arg; assert(lba < 8192); writes++;
    memcpy(disk + lba * 512, data, 512); return 0;
}
static int sync_disk(void *arg) {
    (void)arg; flushes++;
    if (flushes == fail_flush) return -1;
    memcpy(durable, disk, sizeof disk); return 0;
}
static int count_file(void *arg, const char *name, const inode_t *in) {
    (void)name; assert(in->valid); (*(uint32_t *)arg)++; return 0;
}
int main(void) {
    esfs_t fs = {.read = read_sector, .read_many = read_many, .write = write_sector,
        .sync = sync_disk, .sectors = 8192, .used = bitmap, .used_bytes = sizeof bitmap,
        .inode_cache = cache};
    assert(!esfs_format(&fs) && flushes == 3);
    assert(batches == 3 && largest_batch == 32); /* Full validation is batched. */
    uint8_t payload[70000], output[70000];
    for (uint32_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)i;
    uint32_t before = flushes;
    assert(!esfs_replace(&fs, "large", payload, sizeof payload));
    assert(flushes == before + 2); /* Flush at publication boundaries, not per sector. */
    assert(!esfs_mount(&fs) && fs.inode_end == 1);
    inode_t info; assert(!esfs_find(&fs, "large", &info, 0));
    before = batches;
    for (int i = 0; i < 10; i++) {
        assert(!esfs_find(&fs, "large", &info, 0));
        assert(esfs_find(&fs, "absent", 0, 0) == ESFS_NOT_FOUND);
        uint32_t files = 0; assert(!esfs_visit(&fs, count_file, &files) && files == 1);
    }
    assert(batches == before); /* Repeated lookups/listing reuse inode sectors. */
    reads = batches = largest_batch = 0;
    uint32_t actual;
    assert(!esfs_read(&fs, "large", output, 0, sizeof output, &actual));
    assert(actual == sizeof output && !memcmp(payload, output, actual));
    assert(batches == 2 && largest_batch == 127 && reads < 10);

    /* A failed data flush must not publish a replacement inode. */
    assert(!esfs_replace(&fs, "kept", "original", 8));
    fail_flush = flushes + 1;
    assert(esfs_replace(&fs, "kept", "changed", 7) == ESFS_IO && !fs.mounted);
    memcpy(disk, durable, sizeof disk); fail_flush = 0;
    assert(!esfs_mount(&fs));
    assert(!esfs_read(&fs, "kept", output, 0, 8, &actual));
    assert(actual == 8 && !memcmp(output, "original", 8));
    /* A failed publication flush also invalidates caches and preserves the durable old file. */
    fail_flush = flushes + 2;
    assert(esfs_replace(&fs, "kept", "changed", 7) == ESFS_IO && !fs.mounted);
    memcpy(disk, durable, sizeof disk); fail_flush = 0;
    assert(!esfs_mount(&fs));
    assert(!esfs_read(&fs, "kept", output, 0, 8, &actual));
    assert(actual == 8 && !memcmp(output, "original", 8));
    fail_flush = flushes + 1;
    assert(esfs_delete(&fs, "kept") == ESFS_IO && !fs.mounted);
    memcpy(disk, durable, sizeof disk); fail_flush = 0;
    assert(!esfs_mount(&fs) && !esfs_find(&fs, "kept", 0, 0));
    before = flushes; assert(!esfs_delete(&fs, "kept") && flushes == before + 1);
    assert(esfs_find(&fs, "kept", 0, 0) == ESFS_NOT_FOUND);
    assert(!esfs_create(&fs, "kept"));
    assert(!esfs_find(&fs, "kept", &info, 0) && info.size == 0);
    before = flushes; assert(!esfs_append(&fs, "kept", payload, sizeof payload));
    assert(flushes == before + 2);
    assert(!esfs_read(&fs, "kept", output, 0, sizeof output, &actual));
    assert(actual == sizeof output && !memcmp(output, payload, actual));

    /* The validated extent includes files beyond long holes and cache windows. */
    assert(!esfs_delete(&fs, "large"));
    inode_t *first = (inode_t *)(disk + (ESFS_SUPER_LBA + 1) * 512);
    inode_t *late = (inode_t *)(disk + (ESFS_SUPER_LBA + 61) * 512);
    *late = first[1]; memset(&first[1], 0, sizeof first[1]);
    assert(!esfs_mount(&fs) && fs.inode_end == 61);
    assert(!esfs_find(&fs, "kept", &info, 0));
    uint32_t files = 0; assert(!esfs_visit(&fs, count_file, &files) && files == 1);
    assert(!esfs_format(&fs) && fs.inode_end == 0);
    reads = batches = writes = 0;
    assert(esfs_find(&fs, "kept", 0, 0) == ESFS_NOT_FOUND);
    assert(!reads && !batches && !writes);
    puts("Filesystem cache, batching, and flush checks passed.");
    return 0;
}
