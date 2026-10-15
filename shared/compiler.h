#ifndef SYSTEMZERO_COMPILER_H
#define SYSTEMZERO_COMPILER_H
#include <stdint.h>
#define SZC_MAX_SOURCE 65536u
#define SZC_MAX_IMAGE 262144u
typedef struct {
    uint32_t size, line, column;
    const char *error;
} szc_result_t;
uint32_t szc_workspace_size(void);
/* Source need not be NUL terminated. On failure, size is zero and error identifies
   the first diagnostic; the caller must not publish the incomplete image. */
int szc_compile(const uint8_t *source, uint32_t size, uint8_t *image, uint32_t capacity,
                void *workspace, szc_result_t *result);
int szc_compile_object(const uint8_t *, uint32_t, uint8_t *, uint32_t, void *, szc_result_t *);
#endif
