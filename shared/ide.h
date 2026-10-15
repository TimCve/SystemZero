#ifndef SYSTEMZERO_IDE_H
#define SYSTEMZERO_IDE_H
#include <stdint.h>
#define IDE_SYMBOLS 512u
#define IDE_MATCHES IDE_SYMBOLS
#define IDE_NAME 64u
#define IDE_SIGNATURE 192u
#define IDE_ORIGIN 560u
#define IDE_SOURCE 65536u
enum { IDE_PLAIN, IDE_KEYWORD, IDE_STRING, IDE_COMMENT, IDE_NUMBER,
       IDE_FUNCTION, IDE_VARIABLE, IDE_DIRECTIVE };
typedef struct {
    char name[IDE_NAME], signature[IDE_SIGNATURE], origin[IDE_ORIGIN];
    uint32_t position, start, end;
    uint8_t function, imported;
} ide_symbol_t;
typedef struct ide_index ide_index_t;
/* Resolve an include using the same local/INCLUDE search as cc. Return bytes
   read or -1; the resolved path is used both as provenance and a cycle guard. */
typedef int (*ide_include_t)(void *, const char *, const char *, int,
                            uint8_t *, uint32_t, char *, uint32_t);
uint32_t ide_workspace_size(void);
void ide_analyze(ide_index_t *, const uint8_t *, uint32_t, const char *,
                 uint8_t *styles, ide_include_t, void *);
const char *ide_notice(const ide_index_t *);
const ide_symbol_t *ide_symbol(const ide_index_t *, uint32_t);
uint32_t ide_complete(const ide_index_t *, const uint8_t *, uint32_t,
                      uint32_t cursor, const uint8_t *, int force,
                      uint32_t *matches, uint32_t capacity, uint32_t *start);
#endif
