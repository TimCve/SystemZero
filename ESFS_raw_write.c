#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "shared/esfs.h"
static int read_sector(void *arg, uint32_t lba, void *data) {
    FILE *f = arg;
    return fseeko(f, (off_t)lba * 512, SEEK_SET) || fread(data, 1, 512, f) != 512;
}
static int write_sector(void *arg, uint32_t lba, void *data) {
    FILE *f = arg;
    return fseeko(f, (off_t)lba * 512, SEEK_SET) || fwrite(data, 1, 512, f) != 512;
}
static int read_sectors(void *arg, uint32_t lba, uint32_t count, void *data) {
    FILE *f = arg;
    size_t bytes = (size_t)count * 512;
    return fseeko(f, (off_t)lba * 512, SEEK_SET) || fread(data, 1, bytes, f) != bytes;
}
static int sync_disk(void *arg) {
    FILE *f = arg;
    return fflush(f) || fsync(fileno(f));
}
static const char *error_name(int rc) {
    switch (rc) {
    case ESFS_IO: return "disk I/O failed";
    case ESFS_INVALID: return "invalid filesystem, metadata, or filename";
    case ESFS_NOT_FOUND: return "file not found";
    case ESFS_EXISTS: return "file already exists";
    case ESFS_FULL: return "disk full or file exceeds the ESFS limit";
    default: return "operation failed";
    }
}
int main(int argc, char **argv) {
    int format = 0, extract = 0, check = 0, replace = 0, base = 1, rc = ESFS_INVALID;
    if (argc > 1 && argv[1][0] == '-') {
        format = !strcmp(argv[1], "--format"); extract = !strcmp(argv[1], "--extract");
        check = !strcmp(argv[1], "--check"); replace = !strcmp(argv[1], "--replace");
        if (!format && !extract && !check && !replace && strcmp(argv[1], "--append")) goto usage;
        base++;
    }
    if (argc != base + ((format || check) ? 1 : 3)) goto usage;
    const char *image = argv[base], *name = (format || check) ? 0 : argv[base + 1];
    FILE *disk = 0, *source = 0, *output = 0; uint8_t *data = 0, *map = 0, *cache = 0;
    uint32_t size = 0;
    if (name && (!*name || strlen(name) > ESFS_MAX_NAME)) { fprintf(stderr, "Filename must contain 1..511 bytes.\n"); return 1; }
    if (!format && !extract && !check) {
        source = fopen(argv[base + 2], "rb");
        if (!source) { perror(argv[base + 2]); return 1; }
        struct stat input;
        if (fstat(fileno(source), &input) || !S_ISREG(input.st_mode) || input.st_size < 0 ||
            input.st_size > (off_t)ESFS_MAX_FILE) {
            fprintf(stderr, "Input must be a regular file within the ESFS file-size limit.\n"); fclose(source); return 1;
        }
        size = (uint32_t)input.st_size;
        data = malloc(size ? size : 1);
        if (!data || fread(data, 1, size, source) != size) { fprintf(stderr, "Cannot read input file.\n"); goto done; }
    }
    disk = fopen(image, (extract || check) ? "rb" : "r+b");
    if (!disk) { perror(image); goto done; }
    struct flock lock = {.l_type = (extract || check) ? F_RDLCK : F_WRLCK, .l_whence = SEEK_SET};
    if (fcntl(fileno(disk), F_SETLK, &lock)) { fprintf(stderr, "Image is in use; shut down its VM first.\n"); goto done; }
    struct stat st;
    if (fstat(fileno(disk), &st) || !S_ISREG(st.st_mode) || st.st_size % 512 ||
        st.st_size / 512 <= ESFS_SUPER_LBA + 4 || st.st_size / 512 > 0x7fffff) {
        fprintf(stderr, "Invalid raw disk size (must be sector-aligned and smaller than 4 GiB).\n"); goto done;
    }
    uint32_t sectors = st.st_size / 512;
    map = malloc(esfs_map_bytes(sectors));
    cache = malloc(ESFS_INODE_CACHE_SECTORS * ESFS_SECTOR);
    if (!map || !cache) { fprintf(stderr, "Out of memory.\n"); goto done; }
    esfs_t fs = {.io = disk, .read = read_sector, .write = write_sector, .sectors = sectors,
                 .used = map, .used_bytes = esfs_map_bytes(sectors), .inode_cache = cache,
                 .read_many = read_sectors, .sync = sync_disk};
    rc = format ? esfs_format(&fs) : esfs_mount(&fs);
    if (rc || format || check) goto done;
    if (extract) {
        inode_t in; rc = esfs_find(&fs, name, &in, 0); if (rc) goto done;
        data = malloc(in.size ? in.size : 1); if (!data) { rc = ESFS_IO; goto done; }
        uint32_t actual; rc = esfs_read(&fs, name, data, 0, in.size, &actual);
        if (rc) goto done;
        struct stat target;
        if (!stat(argv[base + 2], &target) && target.st_dev == st.st_dev && target.st_ino == st.st_ino) {
            fprintf(stderr, "Extraction output must differ from the disk image.\n"); rc = ESFS_INVALID; goto done;
        }
        output = fopen(argv[base + 2], "wb");
        if (!output || fwrite(data, 1, actual, output) != actual) rc = ESFS_IO;
        goto done;
    }
    if (replace) { rc = esfs_replace(&fs, name, data, size); goto done; }
    rc = esfs_find(&fs, name, 0, 0);
    if (rc == ESFS_NOT_FOUND) rc = esfs_replace(&fs, name, data, size);
    else if (!rc) rc = esfs_append(&fs, name, data, size);
 done:
    if (disk && !(extract || check) && (fflush(disk) || fsync(fileno(disk)))) rc = ESFS_IO;
    if (source && fclose(source)) rc = ESFS_IO;
    if (output && fclose(output)) rc = ESFS_IO;
    if (disk && fclose(disk)) rc = ESFS_IO;
    free(data); free(map); free(cache);
    if (rc) fprintf(stderr, "ESFS: %s.\n", error_name(rc));
    return rc ? 1 : 0;
 usage:
    fprintf(stderr, "Usage: %s [--append|--replace] IMAGE NAME SOURCE\n"
                    "       %s --extract IMAGE NAME OUTPUT\n"
                    "       %s --format IMAGE\n       %s --check IMAGE\n", argv[0], argv[0], argv[0], argv[0]);
    return 1;
}
