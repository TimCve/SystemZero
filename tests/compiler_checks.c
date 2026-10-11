#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/compiler.h"
#include "../shared/elf.h"

static uint8_t image[SZC_MAX_IMAGE + 16], loaded[SZC_MAX_IMAGE];
static void *workspace;
static szc_result_t result;
static void good(const char *source) {
    memset(image, 0xa5, sizeof image);
    assert(szc_compile((const uint8_t *)source, strlen(source), image, SZC_MAX_IMAGE, workspace, &result));
    assert(result.size && !result.error);
    for (uint32_t i = SZC_MAX_IMAGE; i < sizeof image; i++) assert(image[i] == 0xa5);
    elf_plan_t plan; assert(elf_plan(image, result.size, &plan));
    assert(plan.first == 0 && plan.entry == 96 && plan.size == result.size);
    assert(elf_load(image, result.size, &plan, loaded, sizeof loaded));
    assert(!memcmp(loaded, image, result.size));
}
static void bad(const char *source, const char *message) {
    assert(!szc_compile((const uint8_t *)source, strlen(source), image, SZC_MAX_IMAGE, workspace, &result));
    assert(!result.size && result.error && result.line && result.column);
    assert(strstr(result.error, message));
}
int main(int argc, char **argv) {
    assert(argc == 2); workspace = malloc(szc_workspace_size()); assert(workspace);
    good("int main() { return 0; }");
    good("int add(int a, int b); int main() { return add(2,3); } int add(int a,int b) { return a+b; }");
    good("char s[]=\"hello\"; int main() { char *p=s; p++; return *p; }");
    good("int main(){ int i=0; while(i<5){ if(i==3) break; i++; continue; } return i; }");
    FILE *file = fopen(argv[1], "rb"); assert(file);
    char source[SZC_MAX_SOURCE + 1]; size_t n = fread(source, 1, SZC_MAX_SOURCE, file);
    assert(!ferror(file)); fclose(file); source[n] = 0; good(source);
    bad("int main() {\n  int x = ;\n}\n", "expression");
    assert(result.line == 2 && result.column == 11);
    bad("int main() { unknown(); }", "Unknown function");
    bad("int main() { return missing; }", "Unknown variable");
    bad("int main() { print(1); }", "argument type");
    bad("int main() { print(); }", "number");
    bad("int main() { clear(1); }", "many");
    bad("int f(int); int main() { return f(1); }", "no definition");
    bad("void main() {}", "main must");
    bad("int main(int n) {}", "main must");
    bad("int x;", "requires");
    bad("int main(){ return 4294967296; }", "32 bits");
    bad("int main(){ return 0x; }", "digits");
    bad("int main(){ print(\"\\q\"); }", "escape");
    bad("int main(){ print(\"oops); }", "Unterminated");
    bad("/* comment", "Unterminated comment");
    bad("int main(){ break; }", "requires a loop");
    bad("int main(){ int a=0; int a=1; }", "Duplicate");
    bad("int main(){ int a[5000]; }", "16 KiB");
    bad("char a[1]=\"too long\"; int main(){}", "does not fit");
    bad("int main(){ int *****p; }", "Pointer nesting");
    bad("int main(){ char *p=\"x\"; return 1==p; }", "pointer comparison");
    bad("int main(){ int a[2]={1,2}; }", "array initializers");
    bad("#include <stdio.h>\nint main(){}", "preprocessor");
    char nested[1024] = "int main(){return ";
    size_t at = strlen(nested);
    for (int i = 0; i < 100; i++) nested[at++] = '(';
    nested[at++] = '1';
    for (int i = 0; i < 100; i++) nested[at++] = ')';
    memcpy(nested + at, ";}", 3); bad(nested, "nesting limit");
    assert(!szc_compile((const uint8_t *)"int main(){return 0;}", 21, image, 100, workspace, &result));
    assert(result.error && !result.size);
    /* Random malformed token streams exercise bounds and parser termination. */
    static const char alphabet[] = "int char main (){}[];=+-*/%&|!0129\"'\n_\\";
    uint32_t random = 0x13579bdf;
    for (uint32_t test = 0; test < 2000; test++) {
        uint8_t input[128];
        for (uint32_t i = 0; i < sizeof input; i++) {
            random = random * 1664525u + 1013904223u;
            input[i] = alphabet[random % (sizeof alphabet - 1)];
        }
        szc_compile(input, sizeof input, image, SZC_MAX_IMAGE, workspace, &result);
    }
    good("int main(){return 7;}"); free(workspace);
    puts("Compiler parsing, diagnostics, resource bounds, and ELF checks passed.");
    return 0;
}
