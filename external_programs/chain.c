#include <stdint.h>
#include "../kernel/libc/strings.h"
#include "../kernel/drivers/utils/mem.h"
#include "../kernel/kernel.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/drivers/disk/fs.h"

void main(env_vars_t* env_vars_ptr , char* input_buffer) {
	select_drive(env_vars_ptr->selected_drive);
	set_superblock();
	set_term_color(env_vars_ptr->term_color);
	
	uint32_t phy_addr;
	set_free_ptr(env_vars_ptr->free_mem_ptr);

	char* raw = malloc(2000, 1, &phy_addr);
	char* cmd = malloc(1900, 1, &phy_addr);
	char* keyword = malloc(100, 1, &phy_addr);

	char* buff = malloc(100, 1, &phy_addr);
	
	int return_status = 0;
		
	int i = 1;
	while(strcmp(splice(input_buffer, i, '$'), "") == 1) {
		for(int j = 0; j < 2000; j++) {
			if(j < 1900) cmd[j] = 0;
			if(j < 100) {
				keyword[j] = 0;
				buff[j] = 0;
			}
			raw[j] = 0;
		}

		int start_cpy = 0;
		for(int j = 0; j < 2000; j++) {
			if(start_cpy > 0) {
				raw[j - start_cpy] = input_buffer[j];
			}
			if(input_buffer[j] == 0) break;
			if(input_buffer[j] == 0x20 && start_cpy == 0) start_cpy = j + 1;
		}

		memcpy(cmd, splice(raw, i, '$'), 1900);

		int k = 0;
		while(cmd[k]) {
			if(cmd[k] == '$') {
				cmd[k] = 0;
				break;
			}
			k++;
		}
		memcpy(keyword, splice(cmd, 0, 0x20), 100);

		if(cmd[0] == '"') {
			int j = 1;
			while(cmd[j] != '"') {
				print_char(cmd[j]);
				j++;
			}
			print_newline();
		 } else if(cmd[0] == '~') {
			int j = 1;
			while(cmd[j] != '~') {
				buff[j - 1] = cmd[j];
				j++;
			}
		
			// print_dec(return_status); print(" "); print_dec(atoi(splice(buff, 0, ':'))); print_newline();
			if(return_status == atoi(splice(buff, 0, ':'))) {
				i = atoi(splice(buff, 1, ':')) - 1;
			}
		} else if(cmd[0] == '@') {
			int j = 1;
			while(cmd[j] != '@') {
				buff[j - 1] = cmd[j];
				j++;
			}

			i = atoi(splice(buff, 0, 0)) - 1;
		} else {
			// print("AUTO> "); print(cmd); print_newline();
			return_status = execute(keyword, cmd, env_vars_ptr);
		}

		i++;
	}
}
