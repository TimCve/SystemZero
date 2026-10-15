#ifndef SYSTEMZERO_PREPROCESS_H
#define SYSTEMZERO_PREPROCESS_H
#include <stdint.h>
typedef int (*sz_include_fn)(void *,const char *,const char *,int,uint8_t *,uint32_t,char *,uint32_t);
typedef struct { const char *error; uint32_t line; } sz_preprocess_result_t;
uint32_t sz_preprocess_workspace_size(void);
int sz_preprocess(const uint8_t *,uint32_t,const char *,uint8_t *,uint32_t,void *,sz_include_fn,void *,sz_preprocess_result_t *);
#endif
