#ifndef SYSTEMZERO_TEXT_VIEW_H
#define SYSTEMZERO_TEXT_VIEW_H
#include "../kernel/drivers/io/screen.h"
#include "../shared/text.h"
#include "text_mouse.h"
static inline uint32_t text_start(const uint8_t *data, uint32_t size, uint32_t line) {
    uint32_t pos = 0;
    while (line-- && pos < size) {
        text_line_t current = text_line(data, size, pos);
        if (!current.has_next) break;
        pos = current.next;
    }
    return pos;
}
static void text_render(const text_buffer_t *b,int caret,uint8_t color) {
    const uint8_t *data=b->data; uint32_t size=b->size,start=b->top,cursor=caret?b->cursor:TERM_NO_CURSOR;
    screen_begin_update(); uint32_t pos = start, cursor_cell = TERM_NO_CURSOR; int active = 1;
    uint32_t columns = COLS, content_rows = ROWS - 1;
    for (uint32_t row = 0; row < content_rows; row++) {
        for (uint32_t col = 0; col < columns; col++) screen_put_cell(row, col, ' ', color);
        if (!active) continue;
        text_line_t line = text_line(data, size, pos); uint32_t col = 0;
        while (pos < line.end) {
            if (pos == cursor) cursor_cell = row * columns + col;
            uint8_t attr=text_selected(b,pos)?0x70:color;
            uint8_t ch = data[pos++]; uint32_t next = text_advance(col, ch);
            if(ch=='\t') for(uint32_t cell=col;cell<next && cell<columns;cell++) screen_put_cell(row,cell,' ',attr);
            else screen_put_cell(row,col,ch<32?'.':ch,attr);
            col = next;
        }
        if (pos == cursor && col < columns) cursor_cell = row * columns + col;
        if(active && pos<size && data[pos]=='\n' && text_selected(b,pos) && col<columns) screen_put_cell(row,col,' ',0x70);
        pos = line.next; active = line.has_next;
    }
    screen_cursor_visible(cursor_cell != TERM_NO_CURSOR);
    if (cursor_cell != TERM_NO_CURSOR) set_cursor_position(cursor_cell * 2);
    screen_end_update();
}
#endif
