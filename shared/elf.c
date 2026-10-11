#include "elf.h"
#include <stddef.h>
typedef struct {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ehdr_t;
typedef struct { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; } phdr_t;
typedef struct { uint32_t tag, value; } dynamic_t;
typedef struct { uint32_t offset, info; } rel_t;
static int range(uint32_t start, uint32_t n, uint32_t size) { return start <= size && n <= size - start; }
static const phdr_t *ph(const uint8_t *file, const ehdr_t *h, uint32_t i) {
    return (const phdr_t *)(file + h->phoff + i * sizeof(phdr_t));
}
static int mapped(const uint8_t *file, const ehdr_t *h, uint32_t address, uint32_t n, int writable) {
    for (uint32_t i = 0; i < h->phnum; i++) {
        const phdr_t *p = ph(file, h, i);
        if (p->type == 1 && (!writable || (p->flags & 2)) && address >= p->vaddr &&
            range(address - p->vaddr, n, p->memsz)) return 1;
    }
    return 0;
}
int elf_plan(const void *source, uint32_t bytes, elf_plan_t *plan) {
    if (!source || !plan || bytes < sizeof(ehdr_t)) return 0;
    const uint8_t *file = source; const ehdr_t *h = source;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' || h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 1 || h->ident[5] != 1 || h->ident[6] != 1 || h->type != 3 ||
        h->machine != 3 || h->version != 1 || h->ehsize != sizeof(ehdr_t) ||
        h->phentsize != sizeof(phdr_t) || !h->phnum || h->phnum > 64 ||
        h->phoff % 4 || !range(h->phoff, h->phnum * sizeof(phdr_t), bytes)) return 0;
    uint32_t first = 0xffffffffu, end = 0; int entry = 0, dynamic = 0;
    for (uint32_t i = 0; i < h->phnum; i++) {
        const phdr_t *p = ph(file, h, i);
        if (p->type == 3 || p->type == 7) return 0; /* no interpreter or TLS runtime */
        if (p->type == 2 && ++dynamic > 1) return 0;
        if (p->type != 1) continue;
        if (p->filesz > p->memsz || !range(p->offset, p->filesz, bytes) ||
            p->vaddr > 0xffffffffu - p->memsz ||
            (p->align > 1 && ((p->align & (p->align - 1)) || (p->vaddr % p->align != p->offset % p->align)))) return 0;
        for (uint32_t j = 0; j < i; j++) {
            const phdr_t *q = ph(file, h, j);
            if (q->type == 1 && p->memsz && q->memsz && p->vaddr < q->vaddr + q->memsz && q->vaddr < p->vaddr + p->memsz) return 0;
        }
        if (p->memsz && p->vaddr < first) first = p->vaddr;
        if (p->vaddr + p->memsz > end) end = p->vaddr + p->memsz;
        if ((p->flags & 1) && h->entry >= p->vaddr && h->entry - p->vaddr < p->memsz) entry = 1;
    }
    if (!entry || first == 0xffffffffu || end <= first || end - first > 8u * 1024u * 1024u) return 0;
    plan->first = first; plan->size = end - first; plan->entry = h->entry - first;
    return 1;
}
int elf_load(const void *source, uint32_t bytes, const elf_plan_t *expected, void *target, uint32_t capacity) {
    elf_plan_t plan;
    if (!expected || !target || !elf_plan(source, bytes, &plan) || capacity < plan.size ||
        plan.first != expected->first || plan.size != expected->size || plan.entry != expected->entry) return 0;
    const uint8_t *file = source; uint8_t *memory = target; const ehdr_t *h = source;
    for (uint32_t i = 0; i < plan.size; i++) memory[i] = 0;
    const phdr_t *dyn = 0;
    for (uint32_t i = 0; i < h->phnum; i++) {
        const phdr_t *p = ph(file, h, i);
        if (p->type == 1) for (uint32_t j = 0; j < p->filesz; j++) memory[p->vaddr - plan.first + j] = file[p->offset + j];
        if (p->type == 2) dyn = p;
    }
    if (!dyn) return 1;
    if (dyn->vaddr % 4 || !mapped(file, h, dyn->vaddr, dyn->memsz, 0) || dyn->memsz % sizeof(dynamic_t)) return 0;
    const dynamic_t *d = (const dynamic_t *)(memory + dyn->vaddr - plan.first);
    uint32_t rel = 0, relsz = 0, relent = sizeof(rel_t); int terminated = 0;
    for (uint32_t i = 0; i < dyn->memsz / sizeof(dynamic_t); i++) {
        if (!d[i].tag) { terminated = 1; break; }
        if (d[i].tag == 1 || d[i].tag == 7 || d[i].tag == 8 || d[i].tag == 9 || d[i].tag == 23 ||
            d[i].tag == 22 || d[i].tag == 35 || d[i].tag == 36 || d[i].tag == 37) return 0;
        if (d[i].tag == 17) rel = d[i].value;
        if (d[i].tag == 18) relsz = d[i].value;
        if (d[i].tag == 19) relent = d[i].value;
    }
    if (!terminated || relent != sizeof(rel_t) || rel % 4 || relsz % sizeof(rel_t) ||
        (relsz && !mapped(file, h, rel, relsz, 0))) return 0;
    const rel_t *r = (const rel_t *)(memory + (relsz ? rel - plan.first : 0));
    /* Validate the complete relocation table before applying any entry. */
    for (uint32_t i = 0; i < relsz / sizeof(rel_t); i++) {
        if (r[i].info != 8 || r[i].offset % 4 || !mapped(file, h, r[i].offset, 4, 1)) return 0;
        if (r[i].offset < rel + relsz && rel < r[i].offset + 4) return 0;
    }
    uint32_t bias = (uint32_t)(uintptr_t)memory - plan.first;
    for (uint32_t i = 0; i < relsz / sizeof(rel_t); i++) {
        uint32_t *value = (uint32_t *)(memory + r[i].offset - plan.first); *value += bias;
    }
    return 1;
}
