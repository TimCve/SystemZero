#ifndef SYSTEMZERO_SHELL_HISTORY_H
#define SYSTEMZERO_SHELL_HISTORY_H
#include <stdint.h>
#define SHELL_HISTORY_LIMIT 64u
#define SHELL_LINE_CAPACITY 2000u
typedef struct {
    char commands[SHELL_HISTORY_LIMIT][SHELL_LINE_CAPACITY];
    char draft[SHELL_LINE_CAPACITY];
    uint32_t count, next, offset;
} shell_history_t;
static void history_copy(char *out, const char *in) {
    uint32_t n=0;
    while(in[n] && n+1<SHELL_LINE_CAPACITY) { out[n]=in[n]; n++; }
    out[n]=0;
}
static int history_equal(const char *a, const char *b) {
    while(*a && *a==*b) { a++; b++; }
    return *a==*b;
}
static void history_begin(shell_history_t *history) { history->offset=0; history->draft[0]=0; }
static void history_add(shell_history_t *history, const char *command) {
    const char *p=command; while(*p==' ' || *p=='\t') p++;
    if(!*p) return;
    if(history->count && history_equal(command,history->commands[(history->next+SHELL_HISTORY_LIMIT-1)%SHELL_HISTORY_LIMIT])) return;
    history_copy(history->commands[history->next],command);
    history->next=(history->next+1)%SHELL_HISTORY_LIMIT;
    if(history->count<SHELL_HISTORY_LIMIT) history->count++;
}
/* direction -1 is Up, +1 is Down. The edit buffer is independent of the
   stored entries, so editing a recalled command cannot alter its history. */
static const char *history_recall(void *context, int direction, const char *current) {
    shell_history_t *history=context;
    if(direction<0) {
        if(!history->count) return 0;
        if(!history->offset) history_copy(history->draft,current);
        if(history->offset<history->count) history->offset++;
    } else {
        if(!history->offset) return 0;
        if(!--history->offset) return history->draft;
    }
    return history->commands[(history->next+SHELL_HISTORY_LIMIT-history->offset)%SHELL_HISTORY_LIMIT];
}
#endif
