#include "../kernel/env_vars.h"
#include "../kernel/drivers/io/screen.h"
#include "../kernel/libc/strings.h"
#include <stdint.h>
#include "../shared/sys_api.h"
#include "../shared/desktop.h"

int main(env_vars_t* env_vars_ptr, char* input_buffer) {
	set_term_color(env_vars_ptr->term_color);
	int background = atoi(splice(input_buffer, 1, 0x20));
	int foreground = atoi(splice(input_buffer, 2, 0x20));

	if(((background > 15) || (background < 0)) || ((foreground > 15) || (foreground < 0))) {
		print("Invalid color values given!"); print_newline();
		print("Format: color <0 - 15 (background)> <0 - 15 (foreground)>"); print_newline();
		return 1;
	} else {
        uint32_t secondary = env_vars_ptr->desktop->secondary_color;
        if (secondary == (uint32_t)background) secondary = foreground;
        return env_vars_ptr->api->functions[SZ_API_settings_apply_colors](env_vars_ptr,
            env_vars_ptr->desktop->seconds, env_vars_ptr->desktop->position,
            (background * 16) + foreground, secondary, 0) ? 1 : 0;
	}
}
