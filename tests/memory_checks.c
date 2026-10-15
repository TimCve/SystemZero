#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/env_vars.h"
#include "../shared/boot.h"
int main(void) {
    void *region = mmap((void *)HEAP_START, HEAP_END - HEAP_START, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(region == (void *)HEAP_START);
    set_free_ptr(HEAP_START);
    uint32_t physical;
    assert(malloc(4096, 1, &physical) == HEAP_START && physical == HEAP_START);
    assert(malloc(1, 1, 0) == HEAP_START + 4096);
    uint32_t marker = get_free_ptr(), allocation = malloc(100000, 1, 0);
    assert(allocation && !(allocation % 4096));
    memset((void *)(uintptr_t)allocation, 0x55, 100000);
    ((uint8_t *)region)[0] = 0x7a;
    heap_release(marker); assert(get_free_ptr() == marker && ((uint8_t *)region)[0] == 0x7a);
    assert(!*(uint8_t *)(uintptr_t)allocation);
    set_free_ptr(HEAP_END - 4); assert(!malloc(5, 0, &physical) && !physical);
    assert(get_free_ptr() == HEAP_END - 4);
    assert(!malloc(0xffffffffu, 1, 0));
    set_free_ptr(0); assert(get_free_ptr() == HEAP_END - 4);
    /* UEFI must allocate only within the arena firmware actually reserved. */
    boot_info_t boot = {.heap_start = HEAP_START + 0x200000, .heap_end = HEAP_START + 0x300000};
    env_vars_t env = {.boot = &boot, .free_mem_ptr = boot.heap_start};
    mem_bind_env(&env); set_free_ptr(boot.heap_start);
    assert(malloc(4096, 1, 0) == boot.heap_start);
    assert(env.free_mem_ptr == boot.heap_start + 4096);
    set_free_ptr(boot.heap_end - 4); assert(!malloc(5, 0, 0));
    env.free_mem_ptr = boot.heap_end - 4;
    set_free_ptr(HEAP_START); assert(get_free_ptr() == boot.heap_end - 4);
    heap_release(boot.heap_start); assert(env.free_mem_ptr == boot.heap_start);
    mem_bind_env(0);
    munmap(region, HEAP_END - HEAP_START);
    puts("Memory checks passed."); return 0;
}
