#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../shared/partition.h"
#include "../shared/esfs.h"
#include "../shared/environment.h"
#define SECTORS 32768u
static uint8_t bytes[SECTORS*512],map[SECTORS/8],cache[16384];
static uint32_t offset,limit=SECTORS; static int fail_at=-1;
static int read_disk(void *arg,uint32_t lba,void *out) { (void)arg; if(lba>=limit) return -1; memcpy(out,bytes+(offset+lba)*512,512); return 0; }
static int write_disk(void *arg,uint32_t lba,void *in) { (void)arg; if(lba>=limit || (int)lba==fail_at) return -1; memcpy(bytes+(offset+lba)*512,in,512); return 0; }
static int visit(void *arg,const char *name,const inode_t *in) { unsigned *count=arg; assert(!strchr(name,'/')); assert(in->valid); (*count)++; return 0; }
int main(void) {
    partition_disk_t disk={0,SECTORS,read_disk,write_disk,0}; partition_table_t table,loaded;
    for(uint32_t kind=SZ_TABLE_MBR;kind<=SZ_TABLE_GPT;kind++) {
        offset=0; limit=SECTORS; memset(bytes,0,sizeof bytes);
        assert(!partition_initialize(&disk,kind,&table)); assert(!partition_add(&disk,&table,8192)); assert(!partition_add(&disk,&table,4096));
        assert(table.count==2 && table.entries[0].first==2048 && table.entries[1].first==10240);
        assert(!partition_scan(&disk,&loaded)); assert(loaded.kind==kind && loaded.count==2);
        uint32_t crc=partition_crc(bytes,sizeof bytes); assert(partition_add(&disk,&table,SECTORS)==-1); assert(partition_crc(bytes,sizeof bytes)==crc);
        offset=table.entries[0].first; limit=table.entries[0].sectors;
        esfs_t fs={.read=read_disk,.write=write_disk,.sectors=limit,.used=map,.used_bytes=sizeof map,.inode_cache=cache};
        uint8_t before[512]; memcpy(before,bytes+(offset-1)*512,512);
        assert(!esfs_format(&fs)); assert(!esfs_mkdir(&fs,"home")); assert(!esfs_mkdir(&fs,"home/source"));
        assert(!esfs_replace(&fs,"home/source/a","abc\0xyz",7)); assert(!esfs_mount(&fs));
        inode_t in; assert(!esfs_find(&fs,"home/source",&in,0) && in.valid==ESFS_DIRECTORY);
        assert(esfs_replace(&fs,"home/source",0,0)==ESFS_IS_DIR);
        assert(esfs_create(&fs,"missing/a")==ESFS_NOT_FOUND);
        assert(esfs_create(&fs,"home/source/a/b")==ESFS_NOT_DIR);
        assert(esfs_create(&fs,"home/../b")==ESFS_INVALID);
        assert(esfs_delete(&fs,"home")==ESFS_NOT_EMPTY);
        assert(esfs_read(&fs,"home",before,0,1,0)==ESFS_IS_DIR);
        unsigned count=0; assert(!esfs_list(&fs,"home",visit,&count) && count==1);
        char out[8]={0}; uint32_t actual; assert(!esfs_read(&fs,"home/source/a",out,0,7,&actual) && actual==7 && !memcmp(out,"abc\0xyz",7));
        assert(!esfs_delete(&fs,"home/source/a")); assert(!esfs_delete(&fs,"home/source")); assert(!esfs_delete(&fs,"home"));
        assert(!memcmp(before,bytes+(offset-1)*512,512));
        offset=0; limit=SECTORS;
        assert(!partition_remove(&disk,&table,1)); assert(!partition_scan(&disk,&loaded) && loaded.count==1 && loaded.entries[0].slot==2);
        if(kind==SZ_TABLE_GPT) {
            bytes[512+16]^=1; assert(!partition_scan(&disk,&loaded) && loaded.readonly);
            assert(partition_add(&disk,&loaded,2048)==-1);
            bytes[(SECTORS-1)*512+16]^=1; assert(partition_scan(&disk,&loaded)==-1);
        }
    }
    system_variables_t env={0},restored={0};
    environment_defaults(&env); environment_defaults(&restored);
    assert(!environment_put(&env,"PATH","0:/bin;1p2:/tools;.")); assert(!environment_put(&env,"INCLUDE","0:/sys/include")); assert(!environment_put(&env,"LIBRARY_PATH","0:/sys/lib"));
    assert(!environment_put(&env,"COLOR_FOREGROUND","15")); assert(!environment_put(&env,"COLOR_BACKGROUND","1")); assert(!environment_put(&env,"COLOR_SECONDARY","14"));
    assert(!environment_put(&env,"CLOCK_OFFSET","86399")); assert(!environment_put(&env,"STATUS_POSITION","top")); assert(!environment_validate(&env));
    char text[SZ_ENV_FILE]; int n=environment_encode(&env,text,sizeof text); assert(n>0); assert(!environment_decode(&restored,text,n));
    assert(!strcmp(environment_get(&restored,"PATH"),"0:/bin;1p2:/tools;."));
    assert(environment_put(&env,"bad-name","x")==-1); assert(environment_put(&env,"OK","a\nb")==-1);
    assert(environment_put(&env,"PWD","0:/fake")==-1);
    assert(environment_put(&env,"SYSTEM_VOLUME","1:")==-1);
    assert(environment_put(&env,"VERSION","fake")==-1);
    assert(!environment_put(&env,"HOME","relative")); assert(environment_validate(&env)==-1);
    assert(!environment_put(&env,"HOME","0p01:/home")); assert(environment_validate(&env)==-1);
    assert(!environment_put(&env,"HOME","sys:/bad:path")); assert(environment_validate(&env)==-1);
    assert(!environment_put(&env,"HOME","sys:/home")); assert(!environment_validate(&env));
    assert(!environment_put(&env,"COLOR_SECONDARY","1")); assert(environment_validate(&env)==-1);
    uint32_t number; assert(!environment_number("4294967296",&number)); assert(!environment_number("-1",&number));
    puts("Partition isolation, GPT recovery, directories, and environment checks passed."); return 0;
}
