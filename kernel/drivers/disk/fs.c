#include "fs.h"
#include "volume.h"
#include "ata.h"
#include "../utils/mem.h"
#include "../io/screen.h"
#include "../../libc/strings.h"
#include "../../variables.h"
typedef struct { esfs_t fs; volume_info_t info; char cwd[512]; uint8_t cache[ESFS_INODE_CACHE_SECTORS * ESFS_SECTOR]; } mounted_volume_t;
typedef struct { mounted_volume_t volumes[SZ_VOLUMES]; uint8_t *maps[4]; uint32_t map_bytes[4]; partition_table_t tables[4]; } fs_runtime_t;
static env_vars_t *environment;
static fs_runtime_t *runtime(void) { return environment ? environment->filesystem:0; }
static int physical_read(void *arg,uint32_t lba,void *data) { select_drive((uint8_t)(uintptr_t)arg); return read_sectors_ATA_PIO(data,lba,1) ? 0:-1; }
static int physical_write(void *arg,uint32_t lba,void *data) { select_drive((uint8_t)(uintptr_t)arg); return write_sectors_ATA_PIO(lba,1,data) ? 0:-1; }
static int physical_sync(void *arg) { select_drive((uint8_t)(uintptr_t)arg); return ata_flush() ? 0:-1; }
static partition_disk_t disk(uint32_t drive) { select_drive(drive); return (partition_disk_t){(void *)(uintptr_t)drive,ata_sector_count(),physical_read,physical_write,physical_sync}; }
static int disk_read(void *arg,uint32_t lba,void *data) {
    mounted_volume_t *v=arg; if(lba>=v->info.sectors) return -1;
    return physical_read((void *)(uintptr_t)v->info.drive,v->info.first+lba,data);
}
static int disk_write(void *arg,uint32_t lba,void *data) {
    mounted_volume_t *v=arg; if(lba>=v->info.sectors) return -1;
    return physical_write((void *)(uintptr_t)v->info.drive,v->info.first+lba,data);
}
static int disk_read_many(void *arg,uint32_t lba,uint32_t count,void *data) {
    mounted_volume_t *v=arg; if(lba>v->info.sectors || count>v->info.sectors-lba) return -1;
    select_drive(v->info.drive); uint8_t *target=data;
    while(count) { uint8_t n=count>255 ? 255:count; if(!read_sectors_ATA_PIO(target,v->info.first+lba,n)) return -1; target+=n*512; lba+=n; count-=n; }
    return 0;
}
static int disk_sync(void *arg) { mounted_volume_t *v=arg; return physical_sync((void *)(uintptr_t)v->info.drive); }
static void reload(uint32_t drive) {
    fs_runtime_t *r=runtime(); partition_disk_t d=disk(drive); if(!r || !d.sectors) return;
    uint32_t map_used=0;
    for(uint32_t p=0;p<=SZ_PARTITIONS;p++) r->volumes[drive*(SZ_PARTITIONS+1)+p].info.available=0;
    partition_table_t *t=&r->tables[drive]; int rc=partition_scan(&d,t);
    uint32_t raw[128]={0}; int is_raw=!physical_read(d.context,ESFS_SUPER_LBA,raw) &&
        (raw[0]==ESFS_MAGIC || raw[0]==ESFS_FLAT_MAGIC || raw[0]==ESFS_LEGACY_MAGIC);
    for(uint32_t i=0;i<(is_raw || !t->kind ? 1:t->count);i++) {
        if(rc && !is_raw) break;
        uint32_t p=is_raw || !t->kind ? 0:t->entries[i].slot;
        if(p>SZ_PARTITIONS) continue;
        if(p && t->kind==SZ_TABLE_MBR && (t->entries[i].type==5 || t->entries[i].type==15 || t->entries[i].type==0x85)) continue;
        mounted_volume_t *v=&r->volumes[drive*(SZ_PARTITIONS+1)+p];
        uint32_t first=p ? t->entries[i].first:0, length=p ? t->entries[i].sectors:d.sectors;
        if(v->info.first!=first || v->info.sectors!=length) v->cwd[0]=0;
        v->info=(volume_info_t){drive,p,p ? t->entries[i].first:0,p ? t->entries[i].sectors:d.sectors,1};
        uint32_t header[128], fs_sectors=v->info.sectors;
        if(!disk_read(v,ESFS_SUPER_LBA,header) &&
           (header[0]==ESFS_MAGIC || header[0]==ESFS_FLAT_MAGIC || header[0]==ESFS_LEGACY_MAGIC) &&
           header[1]>ESFS_SUPER_LBA+4 && header[1]<=v->info.sectors) fs_sectors=header[1];
        uint32_t bytes=esfs_map_bytes(v->info.sectors);
        if(v->info.sectors>0x7fffffu || bytes>r->map_bytes[drive]-map_used) { v->info.available=0; continue; }
        v->fs=(esfs_t){.io=v,.read=disk_read,.write=disk_write,.sectors=fs_sectors,.used=r->maps[drive]+map_used,
            .used_bytes=bytes,.inode_cache=v->cache,.read_many=disk_read_many,.sync=disk_sync};
        map_used+=bytes; esfs_mount(&v->fs);
    }
    for(uint32_t p=0;p<=SZ_PARTITIONS;p++) {
        mounted_volume_t *v=&r->volumes[drive*(SZ_PARTITIONS+1)+p];
        if(!v->info.available) v->cwd[0]=0;
    }
}
void fs_bind_env(env_vars_t *env) { environment=env; }
static uint32_t current_id(void) {
    uint32_t drive=environment->selected_drive, part=environment->selected_partition;
    return drive*(SZ_PARTITIONS+1)+part;
}
static esfs_t *mounted(uint32_t id) {
    fs_runtime_t *r=runtime(); if(!r || id>=SZ_VOLUMES || !r->volumes[id].info.available) return 0;
    esfs_t *fs=&r->volumes[id].fs; return fs->used && (fs->mounted || !esfs_mount(fs)) ? fs:0;
}
int set_superblock(void) { return environment && !!mounted(current_id()); }
int check_disk_fs(void) { return set_superblock(); }
int init_fs(env_vars_t *env) {
    static fs_runtime_t storage; fs_bind_env(env);
    if(!env->filesystem) {
        env->filesystem=&storage; uint8_t previous=selected_ata_drive();
        for(uint32_t i=0;i<4;i++) {
            partition_disk_t d=disk(i); if(!d.sectors) continue;
            uint32_t sectors=d.sectors>0x7fffffu ? 0x7fffffu:d.sectors;
            storage.map_bytes[i]=esfs_map_bytes(sectors)+SZ_PARTITIONS;
            storage.maps[i]=(void *)malloc(storage.map_bytes[i],0,0);
            if(storage.maps[i]) reload(i); else storage.map_bytes[i]=0;
        }
        /* An existing whole-disk boot image takes precedence; otherwise use an ESFS partition. */
        if(!mounted(env->system_drive*(SZ_PARTITIONS+1)+env->system_partition)) {
            for(uint32_t i=0;i<SZ_VOLUMES;i++) if(mounted(i)) {
                env->system_drive=env->selected_drive=i/(SZ_PARTITIONS+1);
                env->system_partition=env->selected_partition=i%(SZ_PARTITIONS+1); previous=env->selected_drive; break;
            }
        }
        env->free_mem_ptr=get_free_ptr(); select_drive(previous);
    }
    return set_superblock();
}
int fs_resolve(const char *path,uint32_t *id,char *out) {
    if(!environment || !path || !id || !out) return 0;
    uint32_t drive=environment->selected_drive,part=environment->selected_partition;
    int qualified=0;
    const char *delimiter=path;
    while(*delimiter && *delimiter!='/' && *delimiter!=':') delimiter++;
    if(*delimiter==':') {
        if(delimiter-path==3 && path[0]=='s' && path[1]=='y' && path[2]=='s') {
            drive=environment->system_drive; part=environment->system_partition; path=delimiter+1; qualified=1;
        } else {
            if(path[0]<'0' || path[0]>'3') return 0;
            const char *at=path+1; uint32_t p=0;
            if(*at=='p') { at++; if(*at<'1' || *at>'9') return 0; while(*at>='0' && *at<='9') { p=p*10+*at++-'0'; if(p>SZ_PARTITIONS) return 0; } }
            if(*at!=':') return 0;
            drive=path[0]-'0'; part=p; path=at+1; qualified=1;
        }
    }
    *id=drive*(SZ_PARTITIONS+1)+part; fs_runtime_t *r=runtime();
    if(!r || *id>=SZ_VOLUMES || !r->volumes[*id].info.available) return 0;
    uint32_t n=0;
    if(!qualified && *path!='/') { while(r->volumes[*id].cwd[n]) { out[n]=r->volumes[*id].cwd[n]; n++; } }
    while(*path) {
        while(*path=='/') path++;
        if(!*path) break;
        const char *begin=path; uint32_t len=0;
        while(*path && *path!='/') { if(*path==':' || (uint8_t)*path<32 || len>=511) return 0; path++; len++; }
        if(len==1 && begin[0]=='.') continue;
        if(len==2 && begin[0]=='.' && begin[1]=='.') { while(n && out[n-1]!='/') n--; if(n) n--; continue; }
        if(n) { if(n>=511) return 0; out[n++]='/'; }
        if(len>511-n) return 0;
        memcpy(out+n,begin,len); n+=len;
    }
    out[n]=0; return 1;
}
static esfs_t *resolve(const char *name,char *out) { uint32_t id; return fs_resolve(name,&id,out) ? mounted(id):0; }
inode_t get_file_info(const char *name) { inode_t in={0}; char path[512]; esfs_t *fs=resolve(name,path); if(fs) esfs_find(fs,path,&in,0); return in; }
int file_create(const char *name,env_vars_t *env) { (void)env; char p[512]; esfs_t *fs=resolve(name,p); return !fs || esfs_create(fs,p) ? 1:0; }
int file_delete(const char *name,env_vars_t *env) { (void)env; char p[512]; esfs_t *fs=resolve(name,p); return !fs || esfs_delete(fs,p) ? 1:0; }
int file_write(const char *name,const void *data,int bytes,env_vars_t *env) { (void)env; char p[512]; esfs_t *fs=resolve(name,p); return bytes<0 || !fs || esfs_append(fs,p,data,bytes) ? 0:bytes; }
int file_replace(const char *name,const void *data,uint32_t bytes) { char p[512]; esfs_t *fs=resolve(name,p); return fs && !esfs_replace(fs,p,data,bytes); }
int file_read_bytes(const char *name,void *target,uint32_t bytes,uint32_t offset) { char p[512]; esfs_t *fs=resolve(name,p); uint32_t actual=0; return (!target && bytes) || !fs || esfs_read(fs,p,target,offset,bytes,&actual) ? -1:(int)actual; }
int file_read(const char *name,void *target,uint32_t sectors,uint32_t offset) {
    if(sectors>ESFS_MAX_FILE/512+1 || offset>ESFS_MAX_FILE/512+1 || (!target && sectors)) return 2;
    uint32_t bytes=sectors*512; if(bytes) memset(target,0,bytes);
    int actual=file_read_bytes(name,target,bytes,offset*512); if(actual<0) return 2;
    inode_t in=get_file_info(name); return in.size>offset*512+(uint32_t)actual ? 1:0;
}
int fs_mkdir(const char *name) { char p[512]; esfs_t *fs=resolve(name,p); return fs ? esfs_mkdir(fs,p):ESFS_INVALID; }
int fs_chdir(const char *name) {
    char p[512]; uint32_t id; if(!fs_resolve(name,&id,p)) return -1;
    esfs_t *fs=mounted(id); inode_t in; if(!fs || esfs_find(fs,p,&in,0) || in.valid!=ESFS_DIRECTORY) return -1;
    memcpy(runtime()->volumes[id].cwd,p,strlen(p)+1);
    environment->selected_drive=id/(SZ_PARTITIONS+1); environment->selected_partition=id%(SZ_PARTITIONS+1);
    select_drive(environment->selected_drive); return 0;
}
int fs_getcwd(char *out,uint32_t capacity) {
    if(!environment || !runtime() || !out || capacity<8) return -1;
    uint32_t id=current_id(),drive=id/(SZ_PARTITIONS+1),part=id%(SZ_PARTITIONS+1),n=0;
    out[n++]='0'+drive; if(part) { out[n++]='p'; if(part>=10) out[n++]='1'; out[n++]='0'+part%10; }
    out[n++]=':'; out[n++]='/';
    const char *cwd=runtime()->volumes[id].cwd; while(*cwd) { if(n+1>=capacity) return -1; out[n++]=*cwd++; } out[n]=0; return n;
}
int volume_info(env_vars_t *env,uint32_t id,volume_info_t *out) { fs_bind_env(env); if(!runtime() || id>=SZ_VOLUMES || !out) return -1; *out=runtime()->volumes[id].info; return out->available ? 0:-1; }
int volume_select(env_vars_t *env,const char *name) {
    fs_bind_env(env); char p[512]; uint32_t id; if(!fs_resolve(name,&id,p)) return -1;
    env->selected_drive=id/(SZ_PARTITIONS+1); env->selected_partition=id%(SZ_PARTITIONS+1); select_drive(env->selected_drive); return 0;
}
int volume_format(env_vars_t *env,uint32_t drive,uint32_t part) {
    fs_bind_env(env); if(drive>=4 || part>SZ_PARTITIONS || (drive==env->system_drive && part==env->system_partition)) return -1;
    if(drive==env->system_drive) {
        static const uint8_t efi[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
        partition_table_t *table=&runtime()->tables[drive];
        for(uint32_t i=0;i<table->count;i++) if(table->entries[i].slot==part) {
            int same=table->kind==SZ_TABLE_GPT;
            for(uint32_t j=0;j<16;j++) if(table->entries[i].guid[j]!=efi[j]) same=0;
            if(same) return -1;
        }
    }
    mounted_volume_t *v=&runtime()->volumes[drive*(SZ_PARTITIONS+1)+part];
    if(!v->info.available) return -1;
    v->fs.sectors=v->info.sectors;
    if(esfs_format(&v->fs)) return -1;
    v->cwd[0]=0; return 0;
}
int format_disk(void) { return environment && !volume_format(environment,environment->selected_drive,environment->selected_partition); }
int volume_table(env_vars_t *env,uint32_t drive,partition_table_t *table) {
    fs_bind_env(env); if(drive>=4 || !table) return -1; partition_disk_t d=disk(drive); return partition_scan(&d,table);
}
int volume_edit(env_vars_t *env,uint32_t drive,uint32_t action,uint32_t value) {
    fs_bind_env(env); if(drive>=4 || drive==env->system_drive || !runtime()->maps[drive]) return -1;
    partition_disk_t d=disk(drive); partition_table_t *t=&runtime()->tables[drive]; int rc=-1;
    if(action==0) rc=partition_initialize(&d,value,t);
    else if(!partition_scan(&d,t)) rc=action==1 ? partition_add(&d,t,value):action==2 ? partition_remove(&d,t,value):-1;
    reload(drive);
    if(env->selected_drive==drive && !runtime()->volumes[current_id()].info.available) {
        env->selected_partition=0;
        for(uint32_t p=0;p<=SZ_PARTITIONS;p++) if(runtime()->volumes[drive*(SZ_PARTITIONS+1)+p].info.available) { env->selected_partition=p; break; }
    }
    select_drive(env->selected_drive); return rc;
}
typedef struct { esfs_t *fs; char directory[512]; } listing_t;
static int list_entry(void *arg,const char *name,const inode_t *in) {
    listing_t *l=arg; uint8_t header[4]={0}; uint32_t actual=0; char path[512]; uint32_t n=strlen(l->directory), m=strlen((char *)name);
    if(n) { memcpy(path,l->directory,n); path[n++]='/'; } memcpy(path+n,name,m+1);
    if(in->valid==ESFS_FILE && esfs_read(l->fs,path,header,0,4,&actual)) return ESFS_IO;
    print(in->valid==ESFS_DIRECTORY ? "[d] ":actual==4 && header[0]==0x7f && header[1]=='E' && header[2]=='L' && header[3]=='F' ? "[x] ":"[-] ");
    print(" ["); print_dec(in->size); print(" bytes]  "); print((char *)name); if(in->valid==ESFS_DIRECTORY) print("/"); print_newline(); return 0;
}
int fs_list_path(const char *name) { listing_t l; l.fs=resolve(name,l.directory); return l.fs ? esfs_list(l.fs,l.directory,list_entry,&l):ESFS_INVALID; }
int fs_list_entries(const char *name,esfs_visit_fn visit,void *context) {
    char directory[512]; esfs_t *fs=resolve(name,directory);
    return fs ? esfs_list(fs,directory,visit,context):ESFS_INVALID;
}
void file_list(void) { if(fs_list_path(".")) { print("Filesystem unavailable or corrupt."); print_newline(); } }
static int executable(const char *path) { inode_t in=get_file_info(path); uint8_t h[4]; return in.valid==ESFS_FILE && in.size>=4 && file_read_bytes(path,h,4,0)==4 && h[0]==0x7f && h[1]=='E' && h[2]=='L' && h[3]=='F'; }
int fs_executable(env_vars_t *env,const char *name,char *out,uint32_t capacity) {
    fs_bind_env(env); uint32_t n=strlen((char *)name); if(!n || n+1>capacity) return 0;
    for(uint32_t i=0;i<n;i++) if(name[i]=='/' || name[i]==':') { memcpy(out,name,n+1); return executable(out); }
    /* Bare command names are searched only in the configured PATH. */
    const char *path=variables_get(env,"PATH"); if(!path) return 0;
    while(*path) {
        uint32_t at=0; while(*path && *path!=';') { if(at+1>=capacity) return 0; out[at++]=*path++; }
        if(*path==';') path++;
        if(!at) continue;
        if(at+n+2>capacity) continue;
        if(out[at-1]!='/') out[at++]='/';
        memcpy(out+at,name,n+1);
        if(executable(out)) return 1;
    }
    return 0;
}
