#ifndef SYSTEMZERO_FS_H
#define SYSTEMZERO_FS_H
#include "../../env_vars.h"
#include "../../../shared/esfs.h"
void fs_bind_env(env_vars_t *env);
int init_fs(env_vars_t *env);
int set_superblock(void);
int format_disk(void);
int check_disk_fs(void);
int file_create(const char *name, env_vars_t *env);
void file_list(void);
inode_t get_file_info(const char *name);
int file_write(const char *name, const void *data, int size, env_vars_t *env);
int file_replace(const char *name, const void *data, uint32_t size);
int file_read(const char *name, void *target, uint32_t sectors, uint32_t offset);
int file_delete(const char *name, env_vars_t *env);
#endif
