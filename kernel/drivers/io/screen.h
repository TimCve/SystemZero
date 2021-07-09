#define VIDEO_ADDRESS 0xb8000
#define ROWS 25
#define COLS 80

/* colors
	0 - black
	1 - blue
	2 - green
	3 - cyan
	4 - red
	5 - magenta
	6 - brown
	7 - white
	8 - gray
	9 - light blue
	a - light green
	b - light cyan
	c - light red
	d - light magenta
	e - yellow
	f - bright white
*/

#define STD_COLOR 0x1f

#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

void clear();
void print(char* str);
void print_dec(int num);
void print_hex(int num);
void print_newline();
void print_char(char ch);
int get_cursor_position();
void set_cursor_position(int offset);


