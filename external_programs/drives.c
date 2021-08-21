#include "../kernel/env_vars.h"
#include "../kernel/drivers/disk/ata.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>

void translate_status(uint8_t status) {
	switch(status) {
		case 0: {
			print("Absent");
			break;
		}
		case 1: {
			print("Exists");
			break;
		}
		case 2: {
			print("Bus absent");
			break;
		}
	}

	print_newline();
}

void main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);

	select_drive(0);
	print("[0] IDE0 - Status: "); translate_status(identify_drive(0xA0));
	print("[1] IDE1 - Status: "); translate_status(identify_drive(0xB0));
	select_drive(2);
	print("[2] IDE2 - Status: "); translate_status(identify_drive(0xA0));
	print("[3] IDE3 - Status: "); translate_status(identify_drive(0xB0));
}
