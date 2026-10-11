#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/utils/mem.h"
static volatile uint8_t payload[100000] = {[0]=0x7f, [65536]=0x42, [99999]=0x99};
static volatile uint8_t bss[1024 * 1024];
static volatile uint8_t *volatile relocated = &payload[65536];
int main(env_vars_t *env, char *args) {
    (void)env; (void)args;
    if (payload[0] != 0x7f || payload[99999] != 0x99 || *relocated != 0x42) return 1;
    for (uint32_t i = 0; i < sizeof bss; i++) if (bss[i]) return 1;
    bss[sizeof bss - 1] = 0x55;
    uint8_t *memory = (void *)malloc(100000, 1, 0);
    if (!memory) return 1;
    memset(memory, 0xa5, 100000);
    print("large-elf-ok"); print_newline(); return 0;
}
