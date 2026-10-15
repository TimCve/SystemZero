#include "../kernel/kernel.h"
#include "../kernel/desktop.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
#include "shell_history.h"
#include "shell_completion.h"
#include "../shared/arguments.h"

static char *word(char *input, char *out, uint32_t capacity) {
    const char *p=input;
    if(sz_arg_next(&p,out,capacity)<0) return 0;
    while(sz_arg_space(*p)) p++;
    return (char *)p;
}
static int command(env_vars_t *env, char *line) {
    char executable[560];
    if (!word(line, executable, sizeof executable)) {
        print("Command name is too long."); print_newline(); return 1;
    }
    if (!*executable) return 0;
    int result = execute(executable, line, env);
    /* The kernel has reclaimed the child's heap; synchronize this allocator. */
    set_free_ptr(env->free_mem_ptr);
    if (get_cursor_position() % (COLS * 2)) print_newline();
    return result;
}
static void prompt(env_vars_t *env) {
    select_drive(env->selected_drive); scroll_terminal(); screen_begin_update();
    uint32_t row = get_cursor_position() / (COLS * 2);
    for (uint32_t col = 0; col < COLS; col++) screen_put_cell(row, col, ' ', env->term_color);
    set_term_color((env->term_color & 0xf0) | env->desktop->secondary_color);
    screen_secondary_text(1);
    print("DISK"); print_dec(env->selected_drive);
    if (env->selected_partition) { print("p"); print_dec(env->selected_partition); }
    char cwd[560];
    if (fs_getcwd(cwd, sizeof cwd) >= 0) {
        char *directory = cwd;
        while (*directory && *directory != ':') directory++;
        if (*directory && directory[2]) {
            uint32_t length = strlen(directory + 1);
            if (length > COLS / 2) { print("/..."); print(directory + 1 + length - COLS / 2); }
            else print(directory + 1);
        }
    }
    print("> "); screen_secondary_text(0); set_term_color(env->term_color);
    screen_end_update();
}
int main(env_vars_t *env, char *args) {
    char option[560], source[560], output[560], line[SHELL_LINE_CAPACITY];
    args = word(args, option, sizeof option);
    if (!args || !(args = word(args, option, sizeof option))) return 1;
    if (!strcmp(option, "--run")) {
        /* Modal build/run console used by zedit. Compiler failure never runs
           a previous executable. The remaining words are cc link options. */
        args = word(args, source, sizeof source);
        if (!args || !(args = word(args, output, sizeof output)) || !*source || !*output ||
            strlen(source) + strlen(output) + strlen(args) + 5 >= (int)sizeof line) {
            print("Usage: shell --run SOURCE OUTPUT [CC LINK OPTIONS]"); print_newline(); return 1;
        }
        print("Build: "); print(source); print_newline();
        uint32_t n = 0; char quoted_source[1122],quoted_output[1122];
        if(sz_arg_quote(source,quoted_source,sizeof quoted_source)<0 || sz_arg_quote(output,quoted_output,sizeof quoted_output)<0 ||
           strlen(quoted_source)+strlen(quoted_output)+strlen(args)+5>=(int)sizeof line) return 1;
        const char *parts[] = {"cc ", quoted_source, " ", quoted_output, " ", args};
        for (uint32_t i = 0; i < 6; i++) for (const char *p = parts[i]; *p; p++) line[n++] = *p;
        line[n] = 0;
        if (!command(env, line)) {
            print_newline(); print("Run: "); print(output); print_newline();
            /* An explicit path bypasses PATH, including for a relative output. */
            int qualified = output[0] == '/';
            for (char *p = output; *p; p++) if (*p == ':') qualified = 1;
            char program[564]; n=0;
            if(!qualified) { program[n++]='.'; program[n++]='/'; }
            memcpy(program+n,output,strlen(output)+1);
            if(sz_arg_quote(program,line,sizeof line)<0) return 1;
            command(env,line);
        }
        print_newline(); print("Press any key to return to zedit.");
        keyboard_state_t state = {0}; keyboard_event_t event;
        do { kbd_wait_event(&state,&event); } while(event.kind==INPUT_MOUSE);
        return 0;
    }
    if (*option) { print("Usage: shell [--run SOURCE OUTPUT [CC LINK OPTIONS]]"); print_newline(); return 1; }
    shell_state_t *state=(void *)malloc(sizeof *state,0,0);
    if(!state) return 1;
    memset(state,0,sizeof *state); state->env=env;
    shell_history_t *history=&state->history;
    print("Type \"list\" for this directory, \"list sys:/bin\" for commands."); print_newline();
    print("Use \"view sys:/docs/manual.txt\" for help. Sources: sys:/home/examples."); print_newline(); print_newline();
    for (;;) {
        prompt(env);
        history_begin(history);
        if (!kbd_readline_complete(line,sizeof line,shell_recall,shell_complete,state)) { print_newline(); continue; }
        const char *nonblank=line; while(sz_arg_space(*nonblank)) nonblank++;
        if(!*nonblank) continue;
        history_add(history,line);
        if (!strcmp(line, "exit")) return 0;
        command(env, line);
    }
}
