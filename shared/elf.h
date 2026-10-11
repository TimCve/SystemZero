#ifndef SYSTEMZERO_ELF_H
#define SYSTEMZERO_ELF_H
#include <stdint.h>
typedef struct { uint32_t first, size, entry; } elf_plan_t;
int elf_plan(const void *file, uint32_t bytes, elf_plan_t *plan);
int elf_load(const void *file, uint32_t bytes, const elf_plan_t *plan,
             void *memory, uint32_t capacity);
#endif
