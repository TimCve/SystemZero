#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "shared/esfs.h"
#include "shared/partition.h"
typedef struct { FILE *file; uint32_t first; } disk_t;
static int read_sector(void *arg, uint32_t lba, void *data) {
    disk_t *d = arg; FILE *f = d->file;
    return fseeko(f, ((off_t)d->first + lba) * 512, SEEK_SET) || fread(data, 1, 512, f) != 512;
}
static int write_sector(void *arg, uint32_t lba, void *data) {
    disk_t *d = arg; FILE *f = d->file;
    return fseeko(f, ((off_t)d->first + lba) * 512, SEEK_SET) || fwrite(data, 1, 512, f) != 512;
}
static int read_sectors(void *arg, uint32_t lba, uint32_t count, void *data) {
    disk_t *d = arg; FILE *f = d->file;
    size_t bytes = (size_t)count * 512;
    return fseeko(f, ((off_t)d->first + lba) * 512, SEEK_SET) || fread(data, 1, bytes, f) != bytes;
}
static int sync_disk(void *arg) {
    FILE *f = ((disk_t *)arg)->file;
    return fflush(f) || fsync(fileno(f));
}
static const char *error_name(int rc) {
    switch (rc) {
    case ESFS_IO: return "disk I/O failed";
    case ESFS_INVALID: return "invalid filesystem, metadata, or filename";
    case ESFS_NOT_FOUND: return "file not found";
    case ESFS_EXISTS: return "file already exists";
    case ESFS_FULL: return "disk full or file exceeds the ESFS limit";
    case ESFS_NOT_DIR: return "parent is not a directory";
    case ESFS_IS_DIR: return "path is a directory";
    case ESFS_NOT_EMPTY: return "directory is not empty";
    default: return "operation failed";
    }
}
static int list_entry(void *arg,const char *name,const inode_t *in) {
    (void)arg; printf("%s\t%u\t%s\n",in->valid==ESFS_DIRECTORY ? "dir":"file",in->size,name); return 0;
}
static int system_partition(disk_t *context,const partition_table_t *table,uint32_t *slot) {
    static const uint8_t guid[16]={0x53,0x5a,0x46,0x53,0x0e,0x13,0x42,0x4d,0x94,0x7a,0x73,0x79,0x73,0x7a,0x65,0x72};
    uint32_t candidates=0,selected=0;
    for(uint32_t i=0;i<table->count;i++) {
        const partition_t *p=&table->entries[i]; uint32_t data[128];
        if(p->sectors<=ESFS_SUPER_LBA+4) continue;
        if(!read_sector(context,p->first+63,data) && data[0]==0x535a4b31 && data[1]==64) { *slot=p->slot; return 0; }
        int esfs=table->kind==SZ_TABLE_GPT && !memcmp(p->guid,guid,16);
        if(!esfs && !read_sector(context,p->first+ESFS_SUPER_LBA,data)) esfs=data[0]==ESFS_MAGIC || data[0]==ESFS_FLAT_MAGIC || data[0]==ESFS_LEGACY_MAGIC;
        if(esfs) { selected=p->slot; candidates++; }
    }
    if(candidates!=1) { fprintf(stderr,"Cannot identify the system volume; specify --volume PARTITION.\n"); return -1; }
    *slot=selected; return 0;
}
int main(int argc, char **argv) {
    int format = 0, extract = 0, check = 0, replace = 0, delete = 0, mkdir = 0, locate = 0, list = 0, geometry = 0, base = 1, rc = ESFS_INVALID;
    uint32_t volume = 0;
    if (argc > 3 && !strcmp(argv[base], "--volume")) {
        char *end; unsigned long n = strtoul(argv[base + 1], &end, 10);
        if (*end || !n || n > 128) goto usage;
        volume = n; base += 2;
    }
    if (argc > base && argv[base][0] == '-') {
        format = !strcmp(argv[base], "--format"); extract = !strcmp(argv[base], "--extract");
        check = !strcmp(argv[base], "--check"); replace = !strcmp(argv[base], "--replace");
        delete = !strcmp(argv[base], "--delete"); mkdir = !strcmp(argv[base], "--mkdir"); locate = !strcmp(argv[base], "--locate");
        list = !strcmp(argv[base], "--list");
        geometry = !strcmp(argv[base], "--geometry");
        if (!format && !extract && !check && !replace && !delete && !mkdir && !locate && !list && !geometry && strcmp(argv[base], "--append")) goto usage;
        base++;
    }
    if (list ? (argc!=base+1 && argc!=base+2) : argc != base + ((format || check || geometry) ? 1 : (delete || mkdir || locate) ? 2 : 3)) goto usage;
    const char *image = argv[base], *name = (format || check || geometry) ? 0 : list && argc==base+1 ? "" : argv[base + 1];
    FILE *disk = 0, *source = 0, *output = 0; uint8_t *data = 0, *map = 0, *cache = 0;
    uint32_t size = 0;
    if (name && ((!*name && !list) || strlen(name) > ESFS_MAX_NAME)) { fprintf(stderr, "Filename must contain 1..511 bytes.\n"); return 1; }
    if (!format && !extract && !check && !delete && !mkdir && !locate && !list && !geometry) {
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
    disk = fopen(image, (extract || check || locate || list || geometry) ? "rb" : "r+b");
    if (!disk) { perror(image); goto done; }
    struct flock lock = {.l_type = (extract || check || locate || list || geometry) ? F_RDLCK : F_WRLCK, .l_whence = SEEK_SET};
    if (fcntl(fileno(disk), F_SETLK, &lock)) { fprintf(stderr, "Image is in use; shut down its VM first.\n"); goto done; }
    struct stat st;
    if (fstat(fileno(disk), &st) || !S_ISREG(st.st_mode) || st.st_size % 512 ||
        st.st_size / 512 <= ESFS_SUPER_LBA + 4 || st.st_size / 512 > 0xffffffffu) {
        fprintf(stderr, "Invalid raw disk size (must be sector-aligned and smaller than 4 GiB).\n"); goto done;
    }
    uint32_t sectors = st.st_size / 512;
    disk_t context = {disk, 0};
    partition_disk_t physical = {&context, sectors, read_sector, write_sector, sync_disk};
    partition_table_t table={0}; uint32_t raw[128];
    int whole=!read_sector(&context,ESFS_SUPER_LBA,raw) && (raw[0]==ESFS_MAGIC || raw[0]==ESFS_FLAT_MAGIC || raw[0]==ESFS_LEGACY_MAGIC);
    if(volume || !whole) {
        if(partition_scan(&physical,&table)) goto done;
        if(!volume && table.kind && system_partition(&context,&table,&volume)) goto done;
    }
    if (volume) {
        uint32_t i = 0; while (i < table.count && table.entries[i].slot != volume) i++;
        if (i == table.count) goto done;
        context.first = table.entries[i].first; sectors = table.entries[i].sectors;
    }
    if(geometry) {
        printf("{\"table\":%u,\"readonly\":%d,\"partition\":%u,\"first\":%u,\"sectors\":%u,\"disk_sectors\":%u,\"partitions\":[",table.kind,table.readonly,volume,context.first,sectors,physical.sectors);
        for(uint32_t i=0;i<table.count;i++) {
            const partition_t *p=&table.entries[i]; printf("%s{\"slot\":%u,\"first\":%u,\"sectors\":%u,\"guid\":\"",i ? ",":"",p->slot,p->first,p->sectors);
            for(uint32_t j=0;j<16;j++) printf("%02x",p->guid[j]);
            printf("\"}");
        }
        printf("]}\n"); rc=ESFS_OK; goto done;
    }
    if (!format) {
        uint32_t header[128];
        if (!read_sector(&context, ESFS_SUPER_LBA, header) &&
            (header[0] == ESFS_MAGIC || header[0] == ESFS_FLAT_MAGIC || header[0] == ESFS_LEGACY_MAGIC) &&
            header[1] > ESFS_SUPER_LBA + 4 && header[1] <= sectors) sectors = header[1];
    }
    if (sectors > 0x7fffffu) goto done;
    map = malloc(esfs_map_bytes(sectors));
    cache = malloc(ESFS_INODE_CACHE_SECTORS * ESFS_SECTOR);
    if (!map || !cache) { fprintf(stderr, "Out of memory.\n"); goto done; }
    esfs_t fs = {.io = &context, .read = read_sector, .write = write_sector, .sectors = sectors,
                 .used = map, .used_bytes = esfs_map_bytes(sectors), .inode_cache = cache,
                 .read_many = read_sectors, .sync = sync_disk};
    rc = format ? esfs_format(&fs) : esfs_mount(&fs);
    if (rc || format || check) goto done;
    if (list) { rc=esfs_list(&fs,name,list_entry,0); goto done; }
    if (mkdir) { rc = esfs_mkdir(&fs, name); if (rc == ESFS_EXISTS) { inode_t in; rc = esfs_find(&fs, name, &in, 0); if (!rc && in.valid != ESFS_DIRECTORY) rc = ESFS_NOT_DIR; } goto done; }
    if (locate) { uint32_t index; rc = esfs_find(&fs, name, 0, &index); if (!rc) printf("%u\n", index); goto done; }
    if (delete) {
        rc = esfs_delete(&fs, name);
        if (rc == ESFS_NOT_FOUND) rc = ESFS_OK; /* Cleanup can safely run on every build. */
        goto done;
    }
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
    if (disk && !(extract || check || locate || list || geometry) && (fflush(disk) || fsync(fileno(disk)))) rc = ESFS_IO;
    if (source && fclose(source)) rc = ESFS_IO;
    if (output && fclose(output)) rc = ESFS_IO;
    if (disk && fclose(disk)) rc = ESFS_IO;
    free(data); free(map); free(cache);
    if (rc) fprintf(stderr, "ESFS: %s.\n", error_name(rc));
    return rc ? 1 : 0;
 usage:
    fprintf(stderr, "Usage: %s [--append|--replace] IMAGE NAME SOURCE\n"
                    "       %s --extract IMAGE NAME OUTPUT\n"
                    "       %s --delete IMAGE NAME\n"
                    "       %s --format IMAGE\n       %s --check IMAGE\n"
                    "       %s --mkdir IMAGE DIRECTORY\n       %s --locate IMAGE NAME\n"
                    "       %s --list IMAGE [DIRECTORY]\n"
                    "       %s --geometry IMAGE\n"
                    "The system ESFS volume is selected automatically. Use --volume PARTITION to override.\n", argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 1;
}
