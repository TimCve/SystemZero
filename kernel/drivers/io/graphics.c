#include "graphics.h"

int sqroot(uint32_t val) {
	int counter = 1, sqroot = 1;

	while(sqroot <= val) {
		counter++;
		sqroot = counter*counter;
	}

	return counter - 1;
}

int abs(int val) {
	if(val < 0) return val * -1;
	else return val;
}

int draw_pixel(uint8_t x, uint8_t y, uint8_t color) {
	if(x < (COLS / 2) && y < (ROWS / 2)) {
		char* video_memory = (char*) VIDEO_ADDRESS;
		uint32_t loc = 4 * x + 2 * 80 * y;

		video_memory[loc] = 0;
		video_memory[loc + 1] = color << 4;
		video_memory[loc + 2] = 0;
		video_memory[loc + 3] = color << 4;

		return 0;
	} else
		return 1;
}

void str_line(uint8_t x, uint8_t y, uint8_t orientation, uint8_t length, uint8_t color) {
	if(orientation == 0 || orientation == 1) 
		for(int i = 0; i < length; i++) {
			if(orientation == 0) draw_pixel(x + i, y, color);
			if(orientation == 1) draw_pixel(x - i, y, color);
		}
	else if(orientation == 2 || orientation == 3)
		for(int i = 0; i < length; i++) {
			if(orientation == 2) draw_pixel(x, y - i, color);
			if(orientation == 3) draw_pixel(x, y + i, color);
		}
}

void draw_line(uint8_t x_start, uint8_t y_start, uint8_t x_end, uint8_t y_end, uint8_t color) {
	uint32_t rise, run;
	uint32_t i_rise, i_run;
	int factor_x, factor_y;

	uint32_t posx, posy;

	// figure out rise and run
	if(x_start > x_end) {
		run = x_start - x_end;
		factor_x = -1;
	}
	else if(x_start < x_end) {
		run = x_end - x_start;
		factor_x = 1;
	}
	else run = 0;
	
	if(y_start > y_end) {
		rise = y_start - y_end;
		factor_y = -1;
	}
	else if(y_start < y_end) {
		rise = y_end - y_start;
		factor_y = 1;
	}
	else rise = 0;

	if(rise == 0) {
		if(x_start > x_end)
			str_line(x_start, y_start, 1, run, color);
		if(x_start < x_end)
			str_line(x_start, y_start, 0, run, color);
	} else if(run == 0) {
		if(y_start > y_end)
			str_line(x_start, y_start, 2, rise, color);
		if(y_start < y_end)
			str_line(x_start, y_start, 3, rise, color);	
	} else {
		// get ratio of rise:run	
		if(run > rise) {
			i_run = run / rise;	
			i_rise = 1;
		}
		else if(rise > run) {
			i_rise = rise / run;
			i_run = 1;
		}
		else {
			i_rise = 1;
			i_run = 1;
		}

		int failsafe = 0;
		posx = x_start;
		posy = y_start;

		// draw line
		while(failsafe < 40) {
			if(factor_y == -1) {
				str_line(posx, posy, 2, i_rise, color);
				posy -= i_rise;
			} else if(factor_y == 1) {
				str_line(posx, posy, 3, i_rise, color);
				posy += i_rise;
			}
		
			if(factor_x == 1) { 
				str_line(posx, posy, 0, i_run, color);
				posx += i_run;
			} else if(factor_x == -1) {
				str_line(posx, posy, 1, i_run, color);
				posx -= i_run;
			}

			if(posx == x_end || posy == y_end) break;

			failsafe++;
		}	
	}
}



