#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/libc/strings.h"
#include "../shared/environment.h"
#include "../shared/sys_api.h"
#include "../shared/desktop.h"
static void copy(char *a,const char *b) { while((*a++=*b++)) {} }
static int service(env_vars_t *e,uint32_t id,uint32_t a,uint32_t b) { return e->api->functions[id](e,a,b,0,0,0); }
static void render(env_vars_t *env,uint32_t selected,const char *name,const char *value,int field,const char *notice) {
    screen_begin_update(); set_term_color(0x17); screen_cursor_visible(0);
    for(uint32_t r=0;r<ROWS;r++) for(uint32_t c=0;c<COLS;c++) screen_put_cell(r,c,' ',0x17);
    screen_put_text(1,3,"System Environment Variables",0x1f);
    uint32_t height=ROWS-12,top=selected>=height ? selected-height+1:0;
    for(uint32_t i=top;i<env->variables->count && i<top+height;i++) {
        uint32_t row=3+i-top; uint8_t color=i==selected ? 0x71:0x17;
        screen_put_text(row,3,env->variables->entries[i].name,color);
        screen_put_text(row,30,env->variables->entries[i].value,color);
    }
    screen_put_text(ROWS-8,3,"Name",field==0 ? 0x71:0x1f); screen_put_text(ROWS-7,3,name,0x1f);
    screen_put_text(ROWS-5,3,"Value",field==1 ? 0x71:0x1f);
    const char *visible=value; uint32_t n=strlen((char *)value),width=COLS-7; if(n>width) visible+=n-width;
    screen_put_text(ROWS-4,3,visible,0x1f); screen_put_text(ROWS-2,3,notice,0x1e);
    screen_put_text(ROWS-1,0,"Up/Down: select | Tab: name/value | Ctrl+N: new | Ctrl+S: save | Esc: quit",0x1f);
    screen_end_update();
}
int main(env_vars_t *env,char *args) {
    (void)args; if(!env->variables || !env->api || env->api->count<=SZ_API_env_apply) return 1;
    screen_state_t *console=screen_save_state(); if(!console) return 1;
    uint8_t original_color=env->term_color,original_secondary=env->desktop->secondary_color,original_position=env->desktop->position;
    uint32_t selected=0; int field=1; char name[SZ_ENV_NAME],value[SZ_ENV_VALUE];
    copy(name,env->variables->entries[0].name); copy(value,env->variables->entries[0].value);
    keyboard_state_t keyboard={0}; const char *notice=""; set_scrolling(0); render(env,selected,name,value,field,notice);
    for(;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard,&event);
        if(event.code==ESC) {
            screen_restore_state(console);
            if(env->term_color!=original_color || env->desktop->secondary_color!=original_secondary || env->desktop->position!=original_position)
                service(env,SZ_API_env_apply,0,0);
            return 0;
        }
        if(event.control && (event.character=='s' || event.character=='S')) {
            screen_restore_state(console);
            int status=service(env,SZ_API_env_set,(uintptr_t)name,(uintptr_t)value);
            notice=status ? "Invalid value or unable to save. Text colors must differ from the background.":"Saved.";
            set_scrolling(0);
        } else if(event.code==U_ARROW || event.code==D_ARROW) {
            uint32_t count=env->variables->count;
            selected=(selected+count+(event.code==U_ARROW ? -1:1))%count;
            copy(name,env->variables->entries[selected].name); copy(value,env->variables->entries[selected].value); notice="";
        } else if(event.code==TAB) field=!field;
        else if(event.control && (event.character=='n' || event.character=='N')) { name[0]=value[0]=0; field=0; notice="Enter a name and value, then Ctrl+S."; }
        else if(event.code==BACKSPACE) { char *s=field ? value:name; uint32_t n=strlen(s); if(n) s[n-1]=0; }
        else if(event.character>=32 && event.character<127 && !event.control) {
            char *s=field ? value:name; uint32_t n=strlen(s),cap=field ? sizeof value:sizeof name;
            if(n+1<cap) { s[n]=event.character; s[n+1]=0; }
        } else continue;
        render(env,selected,name,value,field,notice);
    }
}
