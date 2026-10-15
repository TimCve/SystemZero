#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/drivers/disk/block.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

void translate_status(uint8_t status) {
	switch(status) {
		case 0: 
		case 2: {
			print("Absent");
			break;
		}
		case 1: {
			print("Exists");
			break;
		}
	}

	print_newline();
}

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);

	for(uint8_t drive=0;drive<4;drive++) {
        print("["); print_dec(drive); print("] ");
        print((char *)block_drive_kind(env_vars_ptr,drive));
        if(env_vars_ptr->disks && env_vars_ptr->disks->devices[drive].kind==BLOCK_IDE &&
            env_vars_ptr->disks->devices[drive].sectors) print_dec(drive);
        print(" - Status: "); select_drive(drive);
        translate_status(identify_drive(0xa0 | ((drive&1)<<4)));
    }
    select_drive(env_vars_ptr->selected_drive);

	return 0;
}
