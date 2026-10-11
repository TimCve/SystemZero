#include "esfs.h"

static void zero(void *p, uint32_t n) { uint8_t *b = p; while (n--) *b++ = 0; }
static void copy(void *d, const void *s, uint32_t n) {
    uint8_t *dst = d; const uint8_t *src = s; while (n--) *dst++ = *src++;
}
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static uint32_t name_length(const char *name) {
    uint32_t n = 0;
    if (!name) return 0;
    while (n <= ESFS_MAX_NAME && name[n]) n++;
    return n && n <= ESFS_MAX_NAME ? n : 0;
}
uint32_t esfs_map_bytes(uint32_t sectors) { return sectors / 8 + !!(sectors % 8); }
static uint32_t data_start(esfs_t *fs) { return ESFS_SUPER_LBA + 1 + fs->sb.inode_blocks; }
static int taken(esfs_t *fs, uint32_t b) { return fs->used[b / 8] & (1u << (b % 8)); }
static void mark(esfs_t *fs, uint32_t b, int value) {
    if (value) fs->used[b / 8] |= (1u << (b % 8));
    else fs->used[b / 8] &= ~(1u << (b % 8));
}
static int claim(esfs_t *fs, uint32_t b) {
    if (b < data_start(fs) || b >= fs->sectors || taken(fs, b)) return ESFS_INVALID;
    mark(fs, b, 1); return 0;
}
static int io_failure(esfs_t *fs) {
    fs->mounted = 0; fs->cache_count = 0; fs->pointer_lba = 0;
    return ESFS_IO;
}
static int read_block(esfs_t *fs, uint32_t lba, void *data) {
    return fs->read(fs->io, lba, data) ? io_failure(fs) : 0;
}
static int read_blocks(esfs_t *fs, uint32_t lba, uint32_t count, void *data) {
    if (fs->read_many) return fs->read_many(fs->io, lba, count, data) ? io_failure(fs) : 0;
    for (uint32_t i = 0; i < count; i++)
        if (read_block(fs, lba + i, (uint8_t *)data + i * 512)) return ESFS_IO;
    return 0;
}
static int read_inode_block(esfs_t *fs, uint32_t block, void *data) {
    if (!fs->inode_cache) return read_block(fs, ESFS_SUPER_LBA + 1 + block, data);
    if (!fs->cache_count || block < fs->cache_first || block - fs->cache_first >= fs->cache_count) {
        uint32_t count = fs->sb.inode_blocks - block;
        if (count > ESFS_INODE_CACHE_SECTORS) count = ESFS_INODE_CACHE_SECTORS;
        fs->cache_count = 0;
        if (read_blocks(fs, ESFS_SUPER_LBA + 1 + block, count, fs->inode_cache)) return ESFS_IO;
        fs->cache_first = block; fs->cache_count = count;
    }
    copy(data, fs->inode_cache + (block - fs->cache_first) * 512, 512); return 0;
}
static int read_pointer(esfs_t *fs, uint32_t lba, uint32_t *data) {
    if (fs->pointer_lba != lba) {
        fs->pointer_lba = 0;
        if (read_block(fs, lba, fs->pointers)) return ESFS_IO;
        fs->pointer_lba = lba;
    }
    copy(data, fs->pointers, 512); return 0;
}
static int write_block(esfs_t *fs, uint32_t lba, void *data) {
    if (fs->write(fs->io, lba, data)) return io_failure(fs);
    if (fs->pointer_lba == lba) copy(fs->pointers, data, 512);
    uint32_t first = ESFS_SUPER_LBA + 1 + fs->cache_first;
    if (fs->cache_count && lba >= first && lba - first < fs->cache_count)
        copy(fs->inode_cache + (lba - first) * 512, data, 512);
    return 0;
}
static int sync_blocks(esfs_t *fs) {
    return fs->sync && fs->sync(fs->io) ? io_failure(fs) : 0;
}
static int write_inode(esfs_t *fs, uint32_t index, const inode_t *in) {
    inode_t block[8];
    if (index >= fs->sb.inodes) return ESFS_INVALID;
    uint32_t lba = ESFS_SUPER_LBA + 1 + index / 8;
    if (read_inode_block(fs, index / 8, block)) return ESFS_IO;
    block[index % 8] = *in;
    int rc = write_block(fs, lba, block);
    if (!rc && in->valid && index / 8 >= fs->inode_end) fs->inode_end = index / 8 + 1;
    return rc;
}
static int file_name(esfs_t *fs, const inode_t *in, char *name) {
    uint32_t ptr[128];
    if (!in->indirect_pointers[0] || read_pointer(fs, in->indirect_pointers[0], ptr)) return ESFS_IO;
    if (!ptr[0] || read_block(fs, ptr[0], name)) return ESFS_IO;
    return name_length(name) ? 0 : ESFS_INVALID;
}
int esfs_format(esfs_t *fs) {
    uint8_t blank[512]; zero(blank, sizeof blank);
    fs->mounted = 0;
    fs->cache_count = 0; fs->pointer_lba = 0;
    if (fs->sectors <= ESFS_SUPER_LBA + 4 || fs->sectors > 0x7fffffu) return ESFS_INVALID;
    /* Keep metadata modest; each sector holds eight files. */
    uint32_t inode_blocks = (fs->sectors - ESFS_SUPER_LBA - 1) / 100;
    if (!inode_blocks) inode_blocks = 1;
    if (inode_blocks > 4096) inode_blocks = 4096;
    fs->sb = (superblock_t){ESFS_MAGIC, fs->sectors, inode_blocks, inode_blocks * 8,
                           fs->sectors * 512, ESFS_SUPER_LBA + 1 + inode_blocks};
    /* Publish the magic only after all inode sectors were initialized. */
    if (write_block(fs, ESFS_SUPER_LBA, blank) || sync_blocks(fs)) return ESFS_IO;
    for (uint32_t i = 1; i <= inode_blocks; i++)
        if (write_block(fs, ESFS_SUPER_LBA + i, blank)) return ESFS_IO;
    if (sync_blocks(fs)) return ESFS_IO;
    copy(blank, &fs->sb, sizeof fs->sb);
    if (write_block(fs, ESFS_SUPER_LBA, blank) || sync_blocks(fs)) return ESFS_IO;
    return esfs_mount(fs);
}
int esfs_mount(esfs_t *fs) {
    uint8_t raw[512];
    fs->mounted = 0;
    fs->cache_count = 0; fs->pointer_lba = 0; fs->inode_end = 0;
    if (!fs->read || !fs->write || !fs->used || fs->used_bytes < esfs_map_bytes(fs->sectors) ||
        fs->sectors <= ESFS_SUPER_LBA + 4 || fs->sectors > 0x7fffffu) return ESFS_INVALID;
    if (read_block(fs, ESFS_SUPER_LBA, raw)) return ESFS_IO;
    copy(&fs->sb, raw, sizeof fs->sb);
    if (fs->sb.magic == ESFS_LEGACY_MAGIC) {
        /* Old writers allocated the final inode sector as their first data sector.
           Reserve that sector for data when mounting those images. */
        if (!fs->sb.inode_blocks || fs->sb.inode_blocks > fs->sectors - ESFS_SUPER_LBA - 2)
            return ESFS_INVALID;
        fs->sb.inode_blocks--;
        fs->sb.inodes = fs->sb.inode_blocks * 8;
        fs->sb.blocks = fs->sectors;
        fs->sb.disk_size = fs->sectors * 512;
    } else if (fs->sb.magic != ESFS_MAGIC) return ESFS_INVALID;
    if (!fs->sb.inode_blocks || fs->sb.blocks != fs->sectors ||
        fs->sb.inode_blocks >= fs->sectors - ESFS_SUPER_LBA - 1 ||
        fs->sb.inodes != fs->sb.inode_blocks * 8 || fs->sb.disk_size != fs->sectors * 512)
        return ESFS_INVALID;
    zero(fs->used, fs->used_bytes);
    for (uint32_t i = 0; i < data_start(fs); i++) mark(fs, i, 1);
    inode_t inodes[8]; uint32_t ptr[128]; char name[512];
    for (uint32_t b = 0; b < fs->sb.inode_blocks; b++) {
        if (read_inode_block(fs, b, inodes)) return ESFS_IO;
        for (uint32_t i = 0; i < 8; i++) {
            inode_t *in = &inodes[i];
            if (!in->valid) continue;
            fs->inode_end = b + 1;
            if (in->valid != 1 || in->size > ESFS_MAX_FILE) return ESFS_INVALID;
            uint32_t data_count = 0; int ended = 0;
            for (uint32_t p = 0; p < 14; p++) {
                if (!in->indirect_pointers[p]) { ended = 1; continue; }
                if (ended || claim(fs, in->indirect_pointers[p])) return ESFS_INVALID;
                if (read_pointer(fs, in->indirect_pointers[p], ptr)) return ESFS_IO;
                for (uint32_t j = 0; j < 128; j++) {
                    if (!ptr[j]) { ended = 1; continue; }
                    if (ended || claim(fs, ptr[j])) return ESFS_INVALID;
                    data_count++;
                }
            }
            int rc = file_name(fs, in, name);
            if (rc) return rc;
            if (in->size + name_length(name) + 1 > data_count * 512) return ESFS_INVALID;
        }
    }
    fs->next = data_start(fs); fs->mounted = 1; return 0;
}
int esfs_find(esfs_t *fs, const char *name, inode_t *out, uint32_t *index) {
    if (out) zero(out, sizeof *out);
    if (!fs->mounted || !name_length(name)) return ESFS_INVALID;
    inode_t block[8]; char found[512];
    for (uint32_t b = 0; b < fs->inode_end; b++) {
        if (read_inode_block(fs, b, block)) return ESFS_IO;
        for (uint32_t i = 0; i < 8; i++) if (block[i].valid) {
            int rc = file_name(fs, &block[i], found); if (rc) return rc;
            if (equal(name, found)) {
                if (out) *out = block[i];
                if (index) *index = b * 8 + i;
                return 0;
            }
        }
    }
    return ESFS_NOT_FOUND;
}
int esfs_visit(esfs_t *fs, esfs_visit_fn visit, void *arg) {
    if (!fs->mounted) return ESFS_INVALID;
    inode_t block[8]; char name[512];
    for (uint32_t b = 0; b < fs->inode_end; b++) {
        if (read_inode_block(fs, b, block)) return ESFS_IO;
        for (uint32_t i = 0; i < 8; i++) if (block[i].valid) {
            int rc = file_name(fs, &block[i], name); if (rc) return rc;
            rc = visit(arg, name, &block[i]); if (rc) return rc;
        }
    }
    return 0;
}
static uint32_t allocate(esfs_t *fs) {
    uint32_t start = data_start(fs), total = fs->sectors - start;
    for (uint32_t i = 0; i < total; i++) {
        uint32_t b = fs->next++;
        if (fs->next >= fs->sectors) fs->next = start;
        if (!taken(fs, b)) {
            uint8_t blank[512]; zero(blank, sizeof blank);
            if (write_block(fs, b, blank)) return 0;
            mark(fs, b, 1); return b;
        }
    }
    return 0;
}
static int publish_geometry(esfs_t *fs) {
    if (fs->sb.magic == ESFS_MAGIC) return 0;
    uint8_t raw[512]; zero(raw, sizeof raw);
    fs->sb.magic = ESFS_MAGIC; copy(raw, &fs->sb, sizeof fs->sb);
    return write_block(fs, ESFS_SUPER_LBA, raw);
}
int esfs_create(esfs_t *fs, const char *name) {
    uint32_t length = name_length(name);
    if (!fs->mounted || !length) return ESFS_INVALID;
    int rc = esfs_find(fs, name, 0, 0);
    if (!rc) return ESFS_EXISTS;
    if (rc != ESFS_NOT_FOUND) return rc;
    uint32_t index = fs->sb.inodes; inode_t block[8];
    for (uint32_t b = 0; b < fs->sb.inode_blocks && b <= fs->inode_end && index == fs->sb.inodes; b++) {
        if (read_inode_block(fs, b, block)) return ESFS_IO;
        for (uint32_t i = 0; i < 8; i++) if (!block[i].valid) { index = b * 8 + i; break; }
    }
    if (index == fs->sb.inodes) return ESFS_FULL;
    uint32_t pointer = allocate(fs), data = pointer ? allocate(fs) : 0;
    if (!data) { if (pointer) mark(fs, pointer, 0); return ESFS_FULL; }
    uint32_t ptr[128]; uint8_t bytes[512]; zero(ptr, sizeof ptr); zero(bytes, sizeof bytes);
    ptr[0] = data; copy(bytes, name, length);
    inode_t in; zero(&in, sizeof in); in.valid = 1; in.indirect_pointers[0] = pointer;
    rc = publish_geometry(fs);
    if (!rc && (write_block(fs, data, bytes) || write_block(fs, pointer, ptr))) rc = ESFS_IO;
    /* Persist data and pointers before exposing an inode, then persist publication. */
    if (!rc) rc = sync_blocks(fs);
    if (!rc) rc = write_inode(fs, index, &in);
    if (!rc) rc = sync_blocks(fs);
    if (rc) { fs->mounted = 0; return rc; }
    return 0;
}
/* Resolve a logical sector. Pointer slots are indexed explicitly, including at 64 KiB. */
static int sector(esfs_t *fs, inode_t *in, uint32_t logical, int create, uint32_t *out) {
    uint32_t p = logical / 128, j = logical % 128, ptr[128];
    if (p >= 14) return ESFS_FULL;
    if (!in->indirect_pointers[p]) {
        if (!create) return ESFS_INVALID;
        in->indirect_pointers[p] = allocate(fs);
        if (!in->indirect_pointers[p]) return ESFS_FULL;
    }
    if (read_pointer(fs, in->indirect_pointers[p], ptr)) return ESFS_IO;
    if (!ptr[j] && create) {
        ptr[j] = allocate(fs);
        if (!ptr[j]) return ESFS_FULL;
        if (write_block(fs, in->indirect_pointers[p], ptr)) return ESFS_IO;
    }
    if (!ptr[j]) return ESFS_INVALID;
    *out = ptr[j]; return 0;
}
int esfs_read(esfs_t *fs, const char *name, void *dst, uint32_t offset, uint32_t count, uint32_t *actual) {
    if (actual) *actual = 0;
    inode_t in; int rc = esfs_find(fs, name, &in, 0); if (rc) return rc;
    if (!dst && count) return ESFS_INVALID;
    if (offset >= in.size) return 0;
    if (count > in.size - offset) count = in.size - offset;
    char filename[512]; rc = file_name(fs, &in, filename); if (rc) return rc;
    uint32_t position = name_length(filename) + 1 + offset, done = 0;
    uint8_t data[512], *target = dst;
    while (done < count) {
        uint32_t lba, n = 512 - position % 512;
        if (n > count - done) n = count - done;
        rc = sector(fs, &in, position / 512, 0, &lba); if (rc) return rc;
        if (!(position % 512) && n == 512 && fs->read_many) {
            uint32_t slot = (position / 512) % 128, batch = 1;
            /* sector() has cached this pointer block; stop at fragmentation or its end. */
            while (slot + batch < 128 && batch < (count - done) / 512 &&
                   fs->pointers[slot + batch] == lba + batch) batch++;
            if (read_blocks(fs, lba, batch, target + done)) return ESFS_IO;
            done += batch * 512; position += batch * 512; continue;
        }
        if (read_block(fs, lba, data)) return ESFS_IO;
        copy(target + done, data + position % 512, n); done += n; position += n;
    }
    if (actual) *actual = done;
    return 0;
}
int esfs_append(esfs_t *fs, const char *name, const void *src, uint32_t count) {
    inode_t in; uint32_t index; int rc = esfs_find(fs, name, &in, &index); if (rc) return rc;
    char filename[512]; rc = file_name(fs, &in, filename); if (rc) return rc;
    uint32_t prefix = name_length(filename) + 1;
    if ((!src && count) || count > ESFS_MAX_FILE - in.size) return ESFS_FULL;
    if (!count) return 0;
    /* Check capacity before changing any existing contents. */
    uint32_t old_sectors = (prefix + in.size + 511) / 512;
    uint32_t new_sectors = (prefix + in.size + count + 511) / 512;
    uint32_t needed = new_sectors - old_sectors + (new_sectors + 127) / 128 - (old_sectors + 127) / 128;
    uint32_t available = 0;
    for (uint32_t b = data_start(fs); b < fs->sectors && available < needed; b++) if (!taken(fs, b)) available++;
    if (available < needed) return ESFS_FULL;
    rc = publish_geometry(fs); if (rc) return rc;
    uint32_t position = prefix + in.size, done = 0;
    uint8_t data[512]; const uint8_t *source = src;
    while (done < count) {
        uint32_t lba, n = 512 - position % 512;
        if (n > count - done) n = count - done;
        rc = sector(fs, &in, position / 512, 1, &lba); if (rc) { fs->mounted = 0; return rc; }
        if (read_block(fs, lba, data)) return ESFS_IO;
        copy(data + position % 512, source + done, n);
        if (write_block(fs, lba, data)) return ESFS_IO;
        done += n; position += n;
    }
    rc = sync_blocks(fs);
    if (!rc) { in.size += done; rc = write_inode(fs, index, &in); }
    if (!rc) rc = sync_blocks(fs);
    if (rc) fs->mounted = 0;
    return rc;
}
int esfs_delete(esfs_t *fs, const char *name) {
    inode_t in, empty; uint32_t index; int rc = esfs_find(fs, name, &in, &index); if (rc) return rc;
    rc = publish_geometry(fs); if (rc) return rc;
    zero(&empty, sizeof empty); rc = write_inode(fs, index, &empty); if (rc) return rc;
    /* Persist removal before any of the old blocks become available for reuse. */
    rc = sync_blocks(fs); if (rc) return rc;
    uint32_t ptr[128];
    for (uint32_t p = 0; p < 14; p++) if (in.indirect_pointers[p]) {
        if (read_pointer(fs, in.indirect_pointers[p], ptr)) return ESFS_IO;
        for (uint32_t j = 0; j < 128; j++) if (ptr[j]) mark(fs, ptr[j], 0);
        mark(fs, in.indirect_pointers[p], 0);
    }
    fs->next = data_start(fs); return 0;
}
int esfs_replace(esfs_t *fs, const char *name, const void *source, uint32_t count) {
    uint32_t prefix = name_length(name) + 1, index;
    if (!fs->mounted || prefix == 1 || (!source && count) || count > ESFS_MAX_FILE) return ESFS_INVALID;
    inode_t previous, replacement = {0};
    int rc = esfs_find(fs, name, &previous, &index);
    if (rc == ESFS_NOT_FOUND) {
        inode_t block[8]; index = fs->sb.inodes;
        for (uint32_t b = 0; b < fs->sb.inode_blocks && b <= fs->inode_end && index == fs->sb.inodes; b++) {
            if (read_inode_block(fs, b, block)) return ESFS_IO;
            for (uint32_t j = 0; j < 8; j++) if (!block[j].valid) { index = b * 8 + j; break; }
        }
        if (index == fs->sb.inodes) return ESFS_FULL;
    } else if (rc) return rc;
    uint32_t sectors = (prefix + count + 511) / 512;
    uint32_t needed = sectors + (sectors + 127) / 128, available = 0;
    for (uint32_t b = data_start(fs); b < fs->sectors && available < needed; b++) if (!taken(fs, b)) available++;
    if (available < needed) return ESFS_FULL;
    replacement.valid = 1; replacement.size = count;
    const uint8_t *bytes = source; uint8_t data[512];
    for (uint32_t logical = 0; logical < sectors; logical++) {
        uint32_t lba;
        rc = sector(fs, &replacement, logical, 1, &lba); if (rc) { fs->mounted = 0; return rc; }
        zero(data, sizeof data);
        for (uint32_t i = 0; i < 512; i++) {
            uint32_t pos = logical * 512 + i;
            if (pos < prefix - 1) data[i] = name[pos];
            else if (pos >= prefix && pos - prefix < count) data[i] = bytes[pos - prefix];
        }
        if (write_block(fs, lba, data)) return ESFS_IO;
    }
    rc = publish_geometry(fs);
    if (!rc) rc = sync_blocks(fs);
    if (!rc) rc = write_inode(fs, index, &replacement);
    if (!rc) rc = sync_blocks(fs);
    if (rc) { fs->mounted = 0; return rc; }
    /* The old inode remains intact until the complete replacement is published. */
    uint32_t ptr[128];
    for (uint32_t p = 0; p < 14; p++) if (previous.indirect_pointers[p]) {
        if (read_pointer(fs, previous.indirect_pointers[p], ptr)) return ESFS_IO;
        for (uint32_t j = 0; j < 128; j++) if (ptr[j]) mark(fs, ptr[j], 0);
        mark(fs, previous.indirect_pointers[p], 0);
    }
    fs->next = data_start(fs); return 0;
}
