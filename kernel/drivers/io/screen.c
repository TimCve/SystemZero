#include "screen.h"
#include "../../vga_colors.h"
#include "../utils/ports.h"
#include "../utils/mem.h"
#include "../../env_vars.h"
#include "video.h"
#include "../../../shared/desktop.h"
#include "../../../shared/sys_api.h"

static terminal_t *display;
static desktop_t *desktop;
static env_vars_t *environment;
int scrolling_enabled = 1;
uint8_t term_color;
#define VGA_COLS 80
#define VGA_ROWS 25
static uint32_t vga_rows(void) { return desktop && desktop->enabled ? VGA_ROWS - 1 : VGA_ROWS; }
static uint32_t vga_top(void) { return desktop && desktop->enabled && !desktop->position ? 1 : 0; }
static volatile uint16_t *vga_cells(void) { return (volatile uint16_t *)VIDEO_ADDRESS + vga_top() * VGA_COLS; }
struct screen_state {
    terminal_t *display;
    uint8_t color;
    int scrolling;
    union {
        terminal_t terminal;
        struct {
            uint16_t cells[VGA_COLS * VGA_ROWS];
            int cursor;
            uint8_t cursor_start, cursor_end;
        } vga;
    } saved;
    uint32_t pixels[];
};

screen_state_t *screen_save_state(void) {
    uint32_t count = display ? display->pitch * display->height : 0;
    if (count > (UINT32_MAX - sizeof(screen_state_t)) / sizeof(uint32_t)) return 0;
    screen_state_t *state = (void *)malloc(sizeof(screen_state_t) + count * sizeof(uint32_t), 0, 0);
    if (!state) return 0;
    state->display = display;
    state->color = get_term_color();
    state->scrolling = display ? display->scrolling : scrolling_enabled;
    if (display) {
        state->saved.terminal = *display;
        for (uint32_t i = 0; i < count; i++) state->pixels[i] = display->pixels[i];
    } else {
        volatile uint16_t *video = vga_cells();
        for (uint32_t i = 0; i < VGA_COLS * vga_rows(); i++) state->saved.vga.cells[i] = video[i];
        state->saved.vga.cursor = get_cursor_position();
        port_byte_out(REG_SCREEN_CTRL, 10); state->saved.vga.cursor_start = port_byte_in(REG_SCREEN_DATA);
        port_byte_out(REG_SCREEN_CTRL, 11); state->saved.vga.cursor_end = port_byte_in(REG_SCREEN_DATA);
    }
    return state;
}

void screen_restore_state(const screen_state_t *state) {
    if (!state || state->display != display) return;
    term_color = state->color;
    scrolling_enabled = state->scrolling;
    if (display) {
        uint32_t rendered = display->rendered_cells, presentations = display->presentations;
        *display = state->saved.terminal;
        display->rendered_cells = rendered; display->presentations = presentations + 1;
        if (desktop && desktop->enabled) {
            display->reserved_top = desktop->position ? 0 : STATUS_HEIGHT;
            display->reserved_bottom = desktop->position ? STATUS_HEIGHT : 0;
        }
        for (uint32_t y = display->reserved_top; y < display->height - display->reserved_bottom; y++)
            for (uint32_t x = 0; x < display->pitch; x++) {
                uint32_t i = y * display->pitch + x; display->pixels[i] = state->pixels[i];
            }
        /* Restore the render cache with the pixels; presenting here would erase graphics. */
    } else {
        volatile uint16_t *video = vga_cells();
        for (uint32_t i = 0; i < VGA_COLS * vga_rows(); i++) video[i] = state->saved.vga.cells[i];
        set_cursor_position(state->saved.vga.cursor);
        port_byte_out(REG_SCREEN_CTRL, 10); port_byte_out(REG_SCREEN_DATA, state->saved.vga.cursor_start);
        port_byte_out(REG_SCREEN_CTRL, 11); port_byte_out(REG_SCREEN_DATA, state->saved.vga.cursor_end);
    }
}
uint32_t screen_rows(void) { return display ? TERM_ROWS : vga_rows(); }
uint32_t screen_columns(void) { return display ? TERM_COLS : VGA_COLS; }
void screen_bind_env(env_vars_t *env) {
    environment = env;
    display = env ? env->terminal : 0;
    desktop = env ? env->desktop : 0;
    /* Text mode uses a color variable in each executable rather than a shared terminal. */
    if (env && !display) term_color = env->term_color;
}
void screen_idle(void) {
    if (environment && environment->api && environment->api->count > SZ_API_thread_sleep)
        environment->api->functions[SZ_API_thread_sleep](environment, 1, 0, 0, 0, 0);
}
int screen_init(env_vars_t *env) {
    static terminal_t terminal;
    volatile uint32_t *pixels = video_init();
    if (!pixels) {
        /* Use all sixteen VGA background colors instead of the attribute blink bit. */
        port_byte_in(0x3da); port_byte_out(0x3c0, 0x10);
        uint8_t mode = port_byte_in(0x3c1); port_byte_out(0x3c0, mode & ~8);
        port_byte_in(0x3da); port_byte_out(0x3c0, 0x20);
    }
    if (!pixels || !terminal_init(&terminal, pixels, VIDEO_WIDTH, VIDEO_HEIGHT,
                                  VIDEO_WIDTH, (void *)VIDEO_FONT_ADDRESS, env->term_color)) return 0;
    env->terminal = &terminal; screen_bind_env(env); return 1;
}
void screen_begin_update(void) { terminal_begin(display); }
void screen_end_update(void) { terminal_end(display); }
void screen_put_cell(uint32_t row, uint32_t col, uint8_t ch, uint8_t color) {
    if (row >= ROWS || col >= COLS) return;
    if (display) { terminal_cell(display, row, col, ch, color); terminal_present(display); }
    else {
        volatile uint8_t *video = (void *)vga_cells();
        uint32_t index = (row * VGA_COLS + col) * 2; video[index] = ch; video[index + 1] = color;
    }
}
void screen_put_text(uint32_t row, uint32_t col, const char *text, uint8_t color) {
    screen_begin_update();
    while (text && *text && col < COLS) screen_put_cell(row, col++, *text++, color);
    screen_end_update();
}
void screen_cursor_visible(int visible) {
    if (display) { display->cursor_visible = !!visible; terminal_present(display); }
    else { port_byte_out(REG_SCREEN_CTRL, 10); port_byte_out(REG_SCREEN_DATA, visible ? 14 : 0x20); }
}
int screen_draw_pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (!display || x >= display->width || y >= display->height ||
        y < display->reserved_top || y >= display->height - display->reserved_bottom) return 1;
    display->pixels[y * display->pitch + x] = terminal_palette(color); return 0;
}

void set_cursor_position(int offset);
int get_cursor_position();

int print(char* string);
int print_newline();

void set_term_color(uint8_t color) {
	term_color = color;
	if (display) display->color = color;
}

uint8_t get_term_color() {
	return display ? display->color : term_color;
}

void set_scrolling(int value) {
	if(value <= 0) scrolling_enabled = 0;
	else if(value > 0) scrolling_enabled = 1;
	if (display) display->scrolling = scrolling_enabled;
}

// clears screen & sets cursor position to screen start
void clear() {
	if (display) { terminal_clear(display); return; }
	char* video_memory = (char*)vga_cells();

	for(uint32_t i = 0; i < VGA_COLS * vga_rows() * 2; i += 2) {
		video_memory[i] = 0x00;
		video_memory[i + 1] = term_color;
	}	

	set_cursor_position(0x00);
}

int scroll_terminal() {
	if (display) return terminal_scroll(display);
	char* video_memory = (char*)vga_cells();
	int cursor_pos = get_cursor_position();

	if(cursor_pos >= (int)(vga_rows() * VGA_COLS * 2)) {
		for(uint32_t i = VGA_COLS * 2; i < vga_rows() * VGA_COLS * 2; i++) {
			video_memory[i - (VGA_COLS * 2)] = video_memory[i];
		}

		for(uint32_t i = VGA_COLS * (vga_rows() - 1) * 2; i < vga_rows() * VGA_COLS * 2; i += 2) {
			video_memory[i] = 0x00;
			video_memory[i + 1] = term_color;
		}

		set_cursor_position(VGA_COLS * (vga_rows() - 1) * 2);
		return 1;
	} else return 0;
}

// print single character at cursor position
int print_char(char ch) {
    if (display) return terminal_char(display, (uint8_t)ch);
    if (ch == '\n') return print_newline();
    if (ch == '\r') return 0;
	char* video_memory = (char*)vga_cells();
	int cursor_pos = get_cursor_position();

	if (cursor_pos >= (int)(VGA_COLS * vga_rows() * 2)) return 0;
	video_memory[cursor_pos] = ch;
	video_memory[cursor_pos + 1] = term_color;
	cursor_pos += 2;
	set_cursor_position(cursor_pos);

	if(scrolling_enabled == 1) return scroll_terminal();
	else return 0;
}

// prints ASCII string
int print(char* string) {
	screen_begin_update();
	int pos = 0;
	int scroll_lines = 0;
	
	while(string[pos] != 0x00) {
		if(string[pos] == 0xA) scroll_lines += print_newline();
		else scroll_lines += print_char(string[pos]);
		pos++;
	}

	screen_end_update(); return scroll_lines;
}

// recursive function to print decimal integer
void print_dec(int num) {
	screen_begin_update();
	if(num / 10) 
		print_dec(num / 10);
	print_char(48 + (num % 10));
	screen_end_update();
}

void print_hex(int num) {
	screen_begin_update();
	if(num / 16)
		print_hex(num / 16);
	if((num % 16) <= 9) {
		print_char(48 + (num % 16));
	} else {
		print_char(55 + (num % 16));
	}
	screen_end_update();
}

// new line & carriage return
int print_newline() {
    if (display) return terminal_char(display, '\n');
    int row = get_cursor_position() / (VGA_COLS * 2);
    set_cursor_position((row + 1) * VGA_COLS * 2);
    return scrolling_enabled ? scroll_terminal() : 0;
}

char char_under_cursor() {
	if (display) return display->cursor < TERM_CELLS ? display->cells[display->cursor].ch : 0;
	char* video_memory = (char*)vga_cells();
	return video_memory[get_cursor_position()];
}

int get_cursor_position() {
	if (display) return display->cursor * 2;
	port_byte_out(REG_SCREEN_CTRL, 14);
	int position = port_byte_in(REG_SCREEN_DATA) << 8;

	port_byte_out(REG_SCREEN_CTRL, 15);
	position += port_byte_in(REG_SCREEN_DATA);

    position -= vga_top() * VGA_COLS;
	return position < 0 ? 0 : position * 2;
}

void set_cursor_position(int offset) {
	if (display) { terminal_cursor(display, offset < 0 ? 0 : (uint32_t)offset / 2); return; }
	offset /= 2;
    offset += vga_top() * VGA_COLS;
	port_byte_out(REG_SCREEN_CTRL, 14);
	port_byte_out(REG_SCREEN_DATA, offset >> 8);

	port_byte_out(REG_SCREEN_CTRL, 15);
	port_byte_out(REG_SCREEN_DATA, offset & 0xff);
}
