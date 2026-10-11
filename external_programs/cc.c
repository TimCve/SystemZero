#include "../shared/compiler.h"
#include "../shared/sys_api.h"
#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"

static int word(char **args, char *out, uint32_t capacity) {
    while (**args == ' ') (*args)++;
    uint32_t n = 0;
    while (**args && **args != ' ') {
        if (n + 1 >= capacity) return -1;
        out[n++] = *(*args)++;
    }
    out[n] = 0; return n;
}
int main(env_vars_t *env, char *args) {
    char command[512], source_name[512], output_name[512], extra[2];
    if (word(&args, command, sizeof command) < 1 ||
        word(&args, source_name, sizeof source_name) < 1 ||
        word(&args, output_name, sizeof output_name) < 1 || word(&args, extra, sizeof extra)) {
        print("Usage: cc SOURCE OUTPUT"); print_newline();
        print("Compile a C-like source file into a native executable.");
        print_newline(); print("Example: cc hello.sz hello"); print_newline(); return 1;
    }
    if (!strcmp(source_name, output_name)) {
        print("Source and output must have different names."); print_newline(); return 1;
    }
    if (!env->api || env->api->magic != SZ_API_MAGIC || env->api->version != SZ_API_VERSION ||
        env->api->count < SZ_API_COUNT) {
        print("Compiler requires SystemZero kernel API version 1."); print_newline(); return 1;
    }
    select_drive(env->selected_drive); set_term_color(env->term_color);
    inode_t source_info = get_file_info(source_name);
    if (!source_info.valid || source_info.size > SZC_MAX_SOURCE) {
        print("Source file is missing or exceeds 64 KiB."); print_newline(); return 1;
    }
    uint32_t sectors = (source_info.size + 511) / 512;
    uint8_t *source = (void *)malloc(sectors ? sectors * 512 : 1, 1, 0);
    uint8_t *image = (void *)malloc(SZC_MAX_IMAGE, 1, 0);
    void *workspace = (void *)malloc(szc_workspace_size(), 1, 0);
    if (!source || !image || !workspace) { print("Not enough memory to compile."); print_newline(); return 1; }
    if (file_read(source_name, source, sectors, 0) == 2) { print("Cannot read source file."); print_newline(); return 1; }
    szc_result_t result;
    if (!szc_compile(source, source_info.size, image, SZC_MAX_IMAGE, workspace, &result)) {
        print(source_name); print(":"); print_dec(result.line); print(":"); print_dec(result.column);
        print(": "); print((char *)result.error); print_newline(); return 1;
    }
    if (!file_replace(output_name, image, result.size)) { print("Cannot save executable."); print_newline(); return 1; }
    print("Compiled "); print(output_name); print(" ("); print_dec(result.size);
    print(" bytes)."); print_newline(); return 0;
}
