#include "source_includes.h"
#include "../kernel/kernel.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/disk/volume.h"
#include "../shared/text.h"
#include "../shared/ide.h"
#include "text_mouse.h"
#include "../shared/text_gutter.h"

typedef struct {
    text_buffer_t text; ide_index_t *index; uint8_t *styles;
    uint32_t matches[IDE_MATCHES], count, selected, prefix,gutter;
    int dirty, dismissed, forced; const char *notice;
} editor_t;
static void complete(editor_t *e) {
    e->count=e->dismissed?0:ide_complete(e->index,e->text.data,e->text.size,e->text.cursor,
                e->styles,e->forced,e->matches,IDE_MATCHES,&e->prefix);
    if(!e->forced && (e->text.cursor-e->prefix<2 || (e->count==1 &&
       strlen((char *)ide_symbol(e->index,e->matches[0])->name)==(int)(e->text.cursor-e->prefix)))) e->count=0;
    if(e->selected>=e->count) e->selected=0;
}
static uint8_t syntax_color(uint8_t style,uint8_t base) {
    const uint8_t colors[]={15,11,10,8,14,13,15,12};
    uint8_t fg=style?colors[style]:base&15, bg=base>>4;
    if(fg==bg) fg=bg==15?0:15;
    return (base&0xf0)|fg;
}
static void clipped(uint32_t row,uint32_t col,const char *text,uint32_t width,uint8_t color) {
    for(uint32_t i=0;i<width && text[i];i++) screen_put_cell(row,col+i,text[i],color);
}
static void popup(editor_t *e,uint32_t cursor) {
    if(!e->count || cursor==TERM_NO_CURSOR) return;
    const ide_symbol_t *s=ide_symbol(e->index,e->matches[e->selected]);
    uint32_t width=COLS>70?68:COLS-2, inner=width-4;
    uint32_t choices=e->count<5?e->count:5;
    uint32_t signatures=(strlen((char *)s->signature)+inner-1)/inner;
    uint32_t origins=(strlen((char *)s->origin)+inner-1)/inner;
    uint32_t height=choices+signatures+origins+3;
    uint32_t x=cursor%COLS, y=cursor/COLS+1;
    if(x+width>COLS) x=COLS-width;
    if(y+height>ROWS-1) y=cursor/COLS>=height?cursor/COLS-height:0;
    for(uint32_t row=0;row<height;row++) for(uint32_t col=0;col<width;col++) {
        char ch=row==0 || row==height-1?'-':col==0 || col==width-1?'|':' ';
        if((row==0 || row==height-1) && (col==0 || col==width-1)) ch='+';
        screen_put_cell(y+row,x+col,ch,0x70);
    }
    clipped(y,x+2," Completion: Up/Down, Tab/Enter, Esc ",width-4,0x70);
    uint32_t first=e->selected/5*5;
    for(uint32_t row=0;row<5 && first+row<e->count;row++) {
        uint8_t color=first+row==e->selected?0x1f:0x70;
        for(uint32_t col=1;col<width-1;col++) screen_put_cell(y+1+row,x+col,' ',color);
        const ide_symbol_t *s=ide_symbol(e->index,e->matches[first+row]);
        clipped(y+1+row,x+2,s->name,width-4,color);
    }
    uint32_t row=y+choices+1;
    for(uint32_t i=0;i<signatures;i++) clipped(row++,x+2,s->signature+i*inner,inner,0x70);
    clipped(row++,x+2,"From:",inner,0x70);
    for(uint32_t i=0;i<origins;i++) clipped(row++,x+2,s->origin+i*inner,inner,0x70);
}
static void layout(editor_t *e) {
    e->gutter=text_gutter_width(e->text.data,e->text.size);
    text_set_geometry(COLS-e->gutter,ROWS);
}
static void gutter(uint32_t row,uint32_t width,uint32_t number,uint8_t color) {
    for(uint32_t col=0;col<width;col++) screen_put_cell(row,col,' ',color);
    screen_put_cell(row,width-2,'|',color);
    uint32_t column=width-3;
    while(number && column) { screen_put_cell(row,--column,'0'+number%10,color); number/=10; }
}
static void render(editor_t *e,env_vars_t *env) {
    layout(e);
    uint32_t number=text_source_line(e->text.data,e->text.top),width=COLS-e->gutter;
    uint32_t pos=e->text.top,cursor=TERM_NO_CURSOR; int active=1;
    set_scrolling(0); screen_begin_update();
    for(uint32_t row=0;row<ROWS-1;row++) {
        int continuation=pos && e->text.data[pos-1]!='\n';
        uint8_t gutter_color=(env->term_color&0xf0)|env->desktop->secondary_color;
        gutter(row,e->gutter,active && !continuation?number:0,gutter_color);
        text_line_t line=text_line(e->text.data,e->text.size,pos); uint32_t col=0;
        while(active && pos<line.end) {
            if(pos==e->text.cursor) cursor=row*COLS+e->gutter+col;
            uint8_t ch=e->text.data[pos],color=text_selected(&e->text,pos)?0x70:syntax_color(e->styles[pos],env->term_color); pos++;
            uint32_t next=text_advance(col,ch);
            if(ch=='\t') while(col<next && col<width) screen_put_cell(row,e->gutter+col++,' ',color);
            else { screen_put_cell(row,e->gutter+col,ch<32?'.':ch,color); col=next; }
        }
        if(active && pos==e->text.cursor && col<width) cursor=row*COLS+e->gutter+col;
        if(active && pos<e->text.size && e->text.data[pos]=='\n' && text_selected(&e->text,pos) && col<width) screen_put_cell(row,e->gutter+col++,' ',0x70);
        while(col<width) screen_put_cell(row,e->gutter+col++,' ',env->term_color);
        if(active && line.end<e->text.size && e->text.data[line.end]=='\n') number++;
        pos=line.next; active=active && line.has_next;
    }
    uint32_t footer=ROWS-1;
    for(uint32_t col=0;col<COLS;col++) screen_put_cell(footer,col,' ',0x70);
    const char *help="zedit | ^S Save | F5 Run | ^Space Complete | Esc Quit";
    clipped(footer,0,help,COLS,0x70);
    const char *notice=e->notice && *e->notice?e->notice:e->dirty?"* Modified":"Ready";
    uint32_t n=strlen((char *)notice),available=COLS-strlen((char *)help)-1;
    if(n>available) n=available;
    clipped(footer,COLS-n,notice,n,0x70);
    popup(e,cursor);
    screen_cursor_visible(cursor!=TERM_NO_CURSOR);
    if(cursor!=TERM_NO_CURSOR) set_cursor_position(cursor*2);
    screen_end_update();
}
static int save(editor_t *e,const char *filename) {
    if(!file_replace(filename,e->text.data,e->text.size)) { e->notice="Save failed"; return 0; }
    e->dirty=0; e->notice="Saved"; return 1;
}
static void run(editor_t *e,env_vars_t *env,const char *filename,const char *options) {
    if(!save(e,filename)) return;
    char output[560],line[2000]; uint32_t n=strlen((char *)filename);
    memcpy(output,filename,n+1);
    if(n>3 && !strcmp(output+n-3,".sz")) output[n-3]=0;
    else { if(n+5>=sizeof output) { e->notice="Output path too long"; return; } memcpy(output+n,".run",5); }
    /* Source, index and outer console remain allocated above the shell scope.
       This temporary snapshot is reclaimed after every run, including errors. */
    char cwd[560]; int has_cwd=fs_getcwd(cwd,sizeof cwd)>=0;
    uint32_t marker=get_free_ptr(); screen_state_t *editor=screen_save_state();
    if(!editor) { e->notice="Cannot save editor screen"; return; }
    char quoted_source[1122],quoted_output[1122];
    if(sz_arg_quote(filename,quoted_source,sizeof quoted_source)<0 || sz_arg_quote(output,quoted_output,sizeof quoted_output)<0 ||
       strlen(quoted_source)+strlen(quoted_output)+strlen((char *)options)+15>=(int)sizeof line) {
        e->notice="Run command too long"; heap_release(marker); return;
    }
    const char *parts[]={"shell --run ",quoted_source," ",quoted_output," ",options}; n=0;
    for(uint32_t i=0;i<6;i++) for(const char *p=parts[i];*p;p++) line[n++]=*p;
    line[n]=0;
    set_scrolling(1); set_term_color(env->term_color); clear();
    int status=execute("sys:/bin/shell",line,env);
    if(has_cwd) fs_chdir(cwd);
    set_free_ptr(env->free_mem_ptr); screen_bind_env(env);
    screen_restore_state(editor); heap_release(marker);
    text_set_geometry(COLS,ROWS);
    e->notice=status?"Run console unavailable":"Returned from run";
}
int main(env_vars_t *env,char *args) {
    char filename[560],word[560],options[768];
    if(tc_word(&args,word,sizeof word)<1 || tc_word(&args,filename,sizeof filename)<1) {
        print("Usage: zedit SOURCE [CC LINK OPTIONS] (default: system.a)"); print_newline(); return 1;
    }
    while(*args==' ') args++;
    if(strlen(args)>=(int)sizeof options || strlen(filename)>=(int)sizeof filename-5) return 1;
    memcpy(options,*args?args:"system.a",strlen(*args?args:"system.a")+1);
    /* Pin the source to its volume and directory even if the run changes PWD. */
    char path[512]; uint32_t volume,n=0;
    if(!fs_resolve(filename,&volume,path) || !*path) { print("Invalid source path."); print_newline(); return 1; }
    filename[n++]='0'+volume/(SZ_PARTITIONS+1);
    uint32_t partition=volume%(SZ_PARTITIONS+1);
    if(partition) { filename[n++]='p'; if(partition>=10) filename[n++]='1'; filename[n++]='0'+partition%10; }
    filename[n++]=':'; filename[n++]='/'; memcpy(filename+n,path,strlen(path)+1);
    includes_t includes={.env=env}; char *scan=options;
    while(tc_word(&scan,word,sizeof word)>0) if(!strcmp(word,"-I") && tc_word(&scan,includes.extra_include,sizeof includes.extra_include)<1) return 1;
    select_drive(env->selected_drive); inode_t info=get_file_info(filename);
    if((info.valid && info.valid!=ESFS_FILE) || info.size>IDE_SOURCE) {
        print("Source must be a file of at most 64 KiB."); print_newline(); return 1;
    }
    editor_t e={.notice="Ready"};
    e.text=(text_buffer_t){.data=(void *)malloc(IDE_SOURCE,0,0),.size=info.valid?info.size:0,.capacity=IDE_SOURCE};
    e.styles=(void *)malloc(IDE_SOURCE,0,0); e.index=(void *)malloc(ide_workspace_size(),0,0);
    if(!e.text.data || !e.styles || !e.index ||
       (info.valid && file_read_bytes(filename,e.text.data,info.size,0)!=(int)info.size)) return 1;
    screen_state_t *console=screen_save_state();
    if(!console) { print("Not enough memory to save console."); print_newline(); return 1; }
    text_set_geometry(COLS,ROWS);
    ide_analyze(e.index,e.text.data,e.text.size,filename,e.styles,source_include,&includes);
    keyboard_state_t keyboard={0}; render(&e,env);
    for(;;) {
        keyboard_event_t event; kbd_wait_event(&keyboard,&event);
        if(text_mouse_at(&e.text,env,&event,e.gutter)) { e.dismissed=1; e.count=0; e.forced=0; render(&e,env); continue; }
        uint8_t key=event.code; int changed=0;
        if(event.control && key==0x2e) { text_copy(&e.text,env); continue; }
        if((!e.count || event.control) && (key==L_ARROW || key==R_ARROW || key==U_ARROW || key==D_ARROW || key==HOME || key==END || key==PGUP || key==PGDN)) text_select_clear(&e.text);
        if(key==ESC) { if(e.count) { e.dismissed=1; e.forced=0; e.count=0; render(&e,env); continue; } break; }
        if(key==0x3f) { run(&e,env,filename,options); keyboard=(keyboard_state_t){0}; render(&e,env); continue; }
        if(event.control) {
            if(key==0x2f) { changed=text_paste(&e.text,env); e.dismissed=1; e.forced=0; if(!changed) e.notice="Paste empty or source size limit"; }
            else if(key==0x1f) save(&e,filename);
            else if(key==0x39) { e.forced=1; e.dismissed=0; complete(&e); }
            else if(key==0x13 || key==HOME) { e.text.cursor=e.text.top=0; e.text.goal_valid=0; e.count=0; e.dismissed=1; }
            else if(key==END) { e.text.cursor=e.text.size; e.text.goal_valid=0; e.count=0; e.dismissed=1; }
            else continue;
        } else if(e.count && (key==U_ARROW || key==D_ARROW)) {
            if(key==U_ARROW) e.selected=e.selected?e.selected-1:e.count-1;
            else e.selected=(e.selected+1)%e.count;
        } else if(e.count && (key==TAB || key==ENTER)) {
            const char *name=ide_symbol(e.index,e.matches[e.selected])->name;
            uint32_t used=e.text.cursor-e.prefix,needed=strlen((char *)name);
            if(e.text.size-used+needed>e.text.capacity) e.notice="Source size limit";
            else {
                /* Replace the complete identifier, including any suffix after
                   the caret; accepting completion never duplicates that suffix. */
                while(e.text.cursor<e.text.size) {
                    uint8_t c=e.text.data[e.text.cursor];
                    if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_')) break;
                    text_delete(&e.text,0);
                }
                while(e.text.cursor>e.prefix) text_delete(&e.text,1);
                while(*name) text_insert(&e.text,*name++);
                changed=1; e.dismissed=1; e.forced=0;
            }
        } else if(key==U_ARROW || key==D_ARROW || key==PGUP || key==PGDN) {
            int rows=key==U_ARROW?-1:key==D_ARROW?1:key==PGUP?-(int)(ROWS-1):(int)(ROWS-1);
            text_move_rows(&e.text,rows); e.dismissed=1; e.forced=0;
        } else if(key==L_ARROW || key==R_ARROW || key==HOME || key==END) {
            if(key==L_ARROW && e.text.cursor) e.text.cursor--;
            if(key==R_ARROW && e.text.cursor<e.text.size) e.text.cursor++;
            if(key==HOME) e.text.cursor=text_line_start(e.text.data,e.text.size,e.text.cursor);
            if(key==END) e.text.cursor=text_line(e.text.data,e.text.size,text_line_start(e.text.data,e.text.size,e.text.cursor)).end;
            e.text.goal_valid=0; e.dismissed=1; e.forced=0;
        } else if(key==BACKSPACE || key==DELETE) {
            changed=text_delete(&e.text,key==BACKSPACE); e.dismissed=0; e.forced=0;
        } else if(event.character || key==ENTER) {
            changed=text_insert(&e.text,key==ENTER?'\n':event.character);
            e.dismissed=0; e.forced=0;
            if(!changed) e.notice="Source size limit";
        } else continue;
        if(changed) {
            e.dirty=1; e.notice=""; e.selected=0;
            ide_analyze(e.index,e.text.data,e.text.size,filename,e.styles,source_include,&includes);
            if(*ide_notice(e.index)) e.notice=ide_notice(e.index);
        }
        complete(&e); layout(&e); text_ensure_visible(&e.text); render(&e,env);
    }
    screen_restore_state(console); return 0;
}
