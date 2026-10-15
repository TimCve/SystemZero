#ifndef SYSTEMZERO_VOLUME_H
#define SYSTEMZERO_VOLUME_H
#include "../../../shared/esfs.h"
#include "../../../shared/partition.h"
#include "../../env_vars.h"
#define SZ_VOLUMES (4u * (SZ_PARTITIONS + 1u))
typedef struct { uint32_t drive, partition, first, sectors; int available; } volume_info_t;
int volume_info(env_vars_t *,uint32_t,volume_info_t *);
int volume_select(env_vars_t *,const char *);
int volume_format(env_vars_t *,uint32_t,uint32_t);
int volume_table(env_vars_t *,uint32_t,partition_table_t *);
int volume_edit(env_vars_t *,uint32_t,uint32_t,uint32_t);
int fs_resolve(const char *,uint32_t *,char *);
int fs_chdir(const char *);
int fs_getcwd(char *,uint32_t);
int fs_mkdir(const char *);
int fs_list_path(const char *);
int fs_list_entries(const char *,esfs_visit_fn,void *);
int fs_executable(env_vars_t *,const char *,char *,uint32_t);
#endif
