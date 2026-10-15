#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../external_programs/shell_history.h"
int main(void) {
    shell_history_t *history=calloc(1,sizeof *history); assert(history);
    history_begin(history); assert(!history_recall(history,-1,"draft"));
    assert(!history_recall(history,1,"draft"));
    history_add(history,""); history_add(history,"  \t"); assert(!history->count);
    history_add(history,"cc example.sz example io.o"); history_add(history,"pwd");
    history_add(history,"pwd"); assert(history->count==2);
    history_begin(history);
    assert(!strcmp(history_recall(history,-1,"unfinished"),"pwd"));
    assert(!strcmp(history_recall(history,-1,"edited recall"),"cc example.sz example io.o"));
    assert(!strcmp(history_recall(history,-1,""),"cc example.sz example io.o"));
    assert(!strcmp(history_recall(history,1,""),"pwd"));
    assert(!strcmp(history_recall(history,1,""),"unfinished"));
    assert(!history_recall(history,1,"unfinished"));
    history_begin(history); assert(!strcmp(history_recall(history,-1,""),"pwd"));
    /* Reset after cancelled input; the next draft must be saved independently. */
    history_begin(history); history_recall(history,-1,"new draft");
    assert(!strcmp(history_recall(history,1,""),"new draft"));
    for(uint32_t i=0;i<SHELL_HISTORY_LIMIT+10;i++) {
        char command[40]; snprintf(command,sizeof command,"command %u",i); history_add(history,command);
    }
    assert(history->count==SHELL_HISTORY_LIMIT); history_begin(history);
    for(uint32_t i=0;i<SHELL_HISTORY_LIMIT;i++) {
        char command[40]; snprintf(command,sizeof command,"command %u",SHELL_HISTORY_LIMIT+9-i);
        assert(!strcmp(history_recall(history,-1,"current"),command));
    }
    assert(!strcmp(history_recall(history,-1,""),"command 10"));
    for(uint32_t i=0;i<SHELL_HISTORY_LIMIT;i++) history_recall(history,1,"");
    assert(!history->offset && !strcmp(history->draft,"current"));
    char *large=malloc(SHELL_LINE_CAPACITY+100); assert(large);
    memset(large,'x',SHELL_LINE_CAPACITY+99); large[SHELL_LINE_CAPACITY+99]=0;
    history_add(history,large); history_begin(history);
    assert(strlen(history_recall(history,-1,""))==SHELL_LINE_CAPACITY-1);
    history_begin(history); history_recall(history,-1,large);
    assert(strlen(history_recall(history,1,""))==SHELL_LINE_CAPACITY-1);
    free(large); free(history); puts("Shell history bounds, navigation, drafts and eviction passed."); return 0;
}
