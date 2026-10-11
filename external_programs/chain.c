#include "../kernel/kernel.h"
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/utils/mem.h"
int main(env_vars_t *env, char *args) {
    char command[512], keyword[512]; int index = 1, status = 0;
    set_term_color(env->term_color);
    while (index > 0) {
        char *part = splice(args, index, '$');
        if (!*part) return status;
        memcpy(command, part, strlen(part) + 1);
        if (command[0] == '"') {
            int n = strlen(command);
            if (n < 2 || command[n - 1] != '"') return 1;
            command[n - 1] = 0; print(command + 1); print_newline();
        } else if (command[0] == '~' || command[0] == '@') {
            int n = strlen(command); char delimiter = command[0];
            if (n < 3 || command[n - 1] != delimiter) return 1;
            command[n - 1] = 0;
            if (delimiter == '@' || status == atoi(splice(command + 1, 0, ':'))) {
                int next = atoi(delimiter == '@' ? command + 1 : splice(command + 1, 1, ':'));
                if (next <= 0 || !*splice(args, next, '$')) return 1;
                index = next; continue;
            }
        } else {
            part = splice(command, 0, ' '); memcpy(keyword, part, strlen(part) + 1);
            status = execute(keyword, command, env);
        }
        index++;
    }
    return 1;
}
