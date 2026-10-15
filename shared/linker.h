#ifndef SYSTEMZERO_LINKER_H
#define SYSTEMZERO_LINKER_H
#include <stdint.h>
#define SZ_LINK_OBJECTS 64u
#define SZ_LINK_IMAGE 1048576u
typedef struct { const uint8_t *bytes; uint32_t size; const char *name; } sz_link_input_t;
typedef struct { uint32_t size; const char *error; char symbol[128]; } sz_link_result_t;
uint32_t sz_link_workspace_size(void);
int sz_link(const sz_link_input_t *,uint32_t,uint8_t *,uint32_t,void *,sz_link_result_t *);
#endif
