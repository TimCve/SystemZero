#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/volume.h"
#include "../kernel/drivers/disk/fs.h"
#include "../kernel/drivers/disk/block.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/io/keyboard.h"
#include "../kernel/libc/strings.h"
#include "../shared/sys_api.h"
#include "../shared/environment.h"
static void decimal(char *out,uint32_t n) { char b[10]; uint32_t at=0,i=0; do { b[at++]='0'+n%10; n/=10; } while(n); while(at) out[i++]=b[--at]; out[i]=0; }
static void row_text(uint32_t row,uint32_t col,const char *text,uint8_t color) { screen_put_text(row,col,text,color); }
static void number(uint32_t row,uint32_t col,uint32_t n) { char s[11]; decimal(s,n); row_text(row,col,s,0x1f); }
static int same(const uint8_t *a,const uint8_t *b,uint32_t size) { while(size--) if(*a++!=*b++) return 0; return 1; }
static int efi_partition(const partition_table_t *table,uint32_t index) {
    static const uint8_t guid[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
    return table->kind==SZ_TABLE_GPT && index<table->count && same(table->entries[index].guid,guid,sizeof guid);
}
static void layout(uint32_t sectors,const partition_table_t *table,uint32_t selected) {
    if(ROWS<=30 || !sectors) return;
    uint32_t row=ROWS-10,width=COLS-6;
    row_text(row-1,3,"Disk layout: selected volume / other volumes / free space",0x1f);
    for(uint32_t col=0;col<width;col++) {
        /* Split the calculation to avoid overflowing a 32-bit sector address. */
        uint32_t sector=sectors/width*col+(sectors%width)*col/width+sectors/width/2;
        uint8_t background=8;
        if(!table->kind) background=6;
        else for(uint32_t i=0;i<table->count;i++) {
            const partition_t *p=&table->entries[i];
            if(sector>=p->first && sector-p->first<p->sectors) { background=i==selected ? 6:9+i%6; break; }
        }
        for(uint32_t height=0;height<2;height++) screen_put_cell(row+height,col+3,' ',(background<<4)|15);
    }
}
static void render(env_vars_t *env,uint32_t drive,partition_table_t *table,uint32_t selected,const char *size,int confirm,const char *notice) {
    screen_begin_update(); set_term_color(0x17); screen_cursor_visible(0);
    for(uint32_t r=0;r<ROWS;r++) for(uint32_t c=0;c<COLS;c++) screen_put_cell(r,c,' ',0x17);
    row_text(1,3,"Disk Partitions and Formatting",0x1f); row_text(3,3,"Disk",0x17); number(3,9,drive);
    row_text(3,14,block_drive_kind(env,drive),0x1f); select_drive(drive); uint32_t sectors=ata_sector_count(); select_drive(env->selected_drive);
    number(3,30,sectors/2048); row_text(3,41,"MiB",0x17);
    row_text(5,3,table->kind==SZ_TABLE_GPT ? "GPT partition table":table->kind==SZ_TABLE_MBR ? "MBR partition table":"Whole disk / no partition table",0x1f);
    if(drive==env->system_drive) row_text(5,38,"System disk: table protected",0x1e);
    row_text(7,3,"Volume       First sector    Size (MiB)   Filesystem",0x17);
    uint32_t count=table->kind ? table->count:1;
    uint32_t visible=ROWS-15,top=selected>=visible ? selected-visible+1:0;
    for(uint32_t i=top;i<count && i-top<visible;i++) {
        uint32_t row=8+i-top;
        uint32_t part=table->kind ? table->entries[i].slot:0;
        char volume[12]; uint32_t n=0; volume[n++]='0'+drive; if(part) { volume[n++]='p'; if(part>=10) volume[n++]='1'; volume[n++]='0'+part%10; } volume[n++]=':'; volume[n]=0;
        uint8_t color=i==selected ? 0x71:0x1f; row_text(row,3,volume,color);
        number(row,16,part ? table->entries[i].first:0); number(row,32,(part ? table->entries[i].sectors:sectors)/2048);
        volume_info_t info; int available=!volume_info(env,drive*(SZ_PARTITIONS+1)+part,&info);
        uint8_t previous=env->selected_drive,previous_part=env->selected_partition;
        if(available) { env->selected_drive=drive; env->selected_partition=part; select_drive(drive); }
        int formatted=available && check_disk_fs(); env->selected_drive=previous; env->selected_partition=previous_part; select_drive(previous);
        const char *filesystem=formatted ? "ESFS":"Unformatted / other";
        if(!formatted && efi_partition(table,i)) {
            uint8_t boot[512]; select_drive(drive);
            if(read_sectors_ATA_PIO(boot,table->entries[i].first,1) && same(boot+82,(const uint8_t *)"FAT32   ",8) && boot[510]==0x55 && boot[511]==0xaa)
                filesystem="FAT32 (EFI boot)";
            else filesystem="EFI system partition";
            select_drive(previous);
        }
        row_text(row,45,filesystem,0x1f);
    }
    layout(sectors,table,selected);
    row_text(ROWS-6,3,"New partition size (MiB):",0x17); row_text(ROWS-6,30,size,0x1f);
    row_text(ROWS-5,3,notice,0x1e);
    row_text(ROWS-4,3,confirm ? "Pending change: press Enter to apply, Esc to cancel.":"",0x1e);
    row_text(ROWS-3,3,"Left/Right: disk | Up/Down: volume | Digits: size",0x17);
    row_text(ROWS-2,3,"G: new GPT | M: new MBR | C: create | D: delete | F: format ESFS",0x1f);
    row_text(ROWS-1,3,"Changes erase data in their target. Esc: quit",0x1f); screen_end_update();
}
int main(env_vars_t *env,char *args) {
    (void)args; screen_state_t *console=screen_save_state(); if(!console) return 1;
    uint32_t drive=env->selected_drive,selected=0,action=0,value=0; int confirm=0,format=0,initial=1;
    char size[9]="8"; partition_table_t table; const char *notice=""; keyboard_state_t keyboard={0};
    set_scrolling(0);
    for(;;) {
        if(volume_table(env,drive,&table)) { table=(partition_table_t){0}; notice="Unreadable or unsupported partition table."; }
        if(initial) {
            for(uint32_t i=0;i<table.count;i++) if(table.entries[i].slot==env->selected_partition) selected=i;
            initial=0;
        }
        render(env,drive,&table,selected,size,confirm,notice);
        keyboard_event_t event; kbd_wait_event(&keyboard,&event);
        if(event.code==ESC) { if(confirm) { confirm=0; notice="Cancelled."; continue; } screen_restore_state(console); return 0; }
        if(confirm) {
            if(event.code!=ENTER) continue;
            int rc=format ? env->api->functions[SZ_API_volume_format](env,drive,value,0,0,0):env->api->functions[SZ_API_disk_edit](env,drive,action,value,0,0);
            confirm=0; if(!format && (action==0 || action==2)) selected=0;
            notice=rc ? "Operation failed or target is protected.":"Changes applied."; continue;
        }
        if(event.code==L_ARROW || event.code==R_ARROW) { drive=(drive+4+(event.code==L_ARROW ? -1:1))%4; selected=0; notice=""; }
        else if(event.code==U_ARROW || event.code==D_ARROW) { uint32_t count=table.kind ? table.count:1; if(count) selected=(selected+count+(event.code==U_ARROW ? -1:1))%count; }
        else if(event.code==BACKSPACE) { uint32_t n=strlen(size); if(n) size[n-1]=0; }
        else if(event.character>='0' && event.character<='9') { uint32_t n=strlen(size); if(n+1<sizeof size) { size[n]=event.character; size[n+1]=0; } }
        else {
            uint32_t part=table.kind && selected<table.count ? table.entries[selected].slot:0;
            format=event.character=='f' || event.character=='F';
            if(format) {
                if(drive==env->system_drive && part==env->system_partition) { notice="The running system volume cannot be formatted."; continue; }
                if(drive==env->system_drive && efi_partition(&table,selected)) { notice="The EFI boot partition cannot be formatted."; continue; }
                action=0; value=part; notice="Format the selected volume with a fresh ESFS filesystem.";
            } else if(event.character=='g' || event.character=='G' || event.character=='m' || event.character=='M') {
                action=0; value=event.character=='g' || event.character=='G' ? SZ_TABLE_GPT:SZ_TABLE_MBR; notice="Replace the entire disk's partition table.";
            } else if(event.character=='c' || event.character=='C') {
                uint32_t mb; if(!environment_number(size,&mb) || !mb || mb>4095) { notice="Choose a size from 1 to 4095 MiB."; continue; }
                action=1; value=mb*2048; notice="Create a new partition in free space.";
            } else if(event.character=='d' || event.character=='D') { action=2; value=part; notice="Delete the selected partition."; }
            else continue;
            if(!format && drive==env->system_drive) { notice="The running system disk's partition table is protected."; continue; }
            confirm=1;
        }
    }
}
