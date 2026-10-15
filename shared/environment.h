#ifndef SYSTEMZERO_ENVIRONMENT_H
#define SYSTEMZERO_ENVIRONMENT_H
#include <stdint.h>
#define SZ_ENV_MAX 64u
#define SZ_ENV_NAME 32u
#define SZ_ENV_VALUE 512u
#define SZ_ENV_FILE 36864u
typedef struct { char name[SZ_ENV_NAME], value[SZ_ENV_VALUE]; } environment_entry_t;
typedef struct system_variables { uint32_t count; environment_entry_t entries[SZ_ENV_MAX]; } system_variables_t;
const char *environment_get(const system_variables_t *,const char *);
int environment_put(system_variables_t *,const char *,const char *);
int environment_validate(const system_variables_t *);
int environment_decode(system_variables_t *,const char *,uint32_t);
int environment_encode(const system_variables_t *,char *,uint32_t);
int environment_number(const char *,uint32_t *);
void environment_defaults(system_variables_t *);
int environment_readonly(const char *);
#endif
