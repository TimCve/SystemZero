#include "text.h"
static uint32_t columns = TERM_COLS, rows = TERM_ROWS;
void text_set_geometry(uint32_t width, uint32_t height) {
    if (width && width <= TERM_COLS && height >= 2 && height <= TERM_ROWS) {
        columns = width; rows = height;
    }
}
uint32_t text_advance(uint32_t column, uint8_t ch) {
    return column + (ch == '\t' ? 4 - column % 4 : 1);
}
text_line_t text_line(const uint8_t *data, uint32_t size, uint32_t start) {
    uint32_t pos = start < size ? start : size, column = 0;
    while (pos < size) {
        if (data[pos] == '\n') return (text_line_t){pos, pos + 1, column, 1};
        column = text_advance(column, data[pos++]);
        if (column >= columns) return (text_line_t){pos, pos, column, 1};
    }
    return (text_line_t){pos, pos, column, 0};
}
uint32_t text_line_start(const uint8_t *data, uint32_t size, uint32_t position) {
    if (position > size) position = size;
    uint32_t start = 0;
    for (;;) {
        text_line_t line = text_line(data, size, start);
        if (!line.has_next || position < line.next) return start;
        start = line.next;
    }
}
uint32_t text_previous_lines(const uint8_t *data, uint32_t size, uint32_t start, uint32_t count) {
    if (!count) return start;
    if (count > rows - 1) count = rows - 1;
    uint32_t history[TERM_ROWS - 1], n = 0, pos = 0;
    while (pos < start) {
        history[n++ % count] = pos;
        text_line_t line = text_line(data, size, pos);
        if (!line.has_next || line.next <= pos) break;
        pos = line.next;
    }
    return n >= count ? history[n % count] : 0;
}
uint32_t text_column(const uint8_t *data, uint32_t start, uint32_t position) {
    uint32_t column = 0;
    while (start < position) column = text_advance(column, data[start++]);
    return column;
}
void text_move_rows(text_buffer_t *buffer, int rows) {
    uint32_t start = text_line_start(buffer->data, buffer->size, buffer->cursor);
    if (!buffer->goal_valid) {
        buffer->goal_column = text_column(buffer->data, start, buffer->cursor);
        buffer->goal_valid = 1;
    }
    if (rows < 0) start = text_previous_lines(buffer->data, buffer->size, start, -rows);
    else while (rows-- > 0) {
        text_line_t line = text_line(buffer->data, buffer->size, start);
        if (!line.has_next) break;
        start = line.next;
    }
    text_line_t line = text_line(buffer->data, buffer->size, start);
    uint32_t pos = start, column = 0;
    while (pos < line.end) {
        uint32_t next = text_advance(column, buffer->data[pos]);
        if (next > buffer->goal_column || next >= columns) break;
        column = next; pos++;
    }
    buffer->cursor = pos;
}
void text_ensure_visible(text_buffer_t *buffer) {
    uint32_t line = text_line_start(buffer->data, buffer->size, buffer->cursor);
    buffer->top = text_line_start(buffer->data, buffer->size, buffer->top);
    if (line < buffer->top) { buffer->top = line; return; }
    uint32_t pos = buffer->top;
    for (uint32_t row = 0; row < rows - 1; row++) {
        if (pos == line) return;
        text_line_t current = text_line(buffer->data, buffer->size, pos);
        if (!current.has_next) break;
        pos = current.next;
    }
    buffer->top = text_previous_lines(buffer->data, buffer->size, line, rows - 2);
}
int text_selected(const text_buffer_t *b,uint32_t position) {
    uint32_t a=b->anchor<b->cursor?b->anchor:b->cursor,z=b->anchor<b->cursor?b->cursor:b->anchor;
    return b->selection && position>=a && position<z;
}
void text_select_clear(text_buffer_t *b) { b->selecting=b->selection=0; }
int text_replace(text_buffer_t *b,const uint8_t *data,uint32_t count) {
    if(b->cursor>b->size || (b->selection && b->anchor>b->size)) return 0;
    uint32_t a=b->cursor,z=a;
    if(b->selection) { a=b->anchor<b->cursor?b->anchor:b->cursor; z=b->anchor<b->cursor?b->cursor:b->anchor; }
    if(count>b->capacity-(b->size-(z-a))) return 0;
    uint32_t end=a+count,new_size=b->size-(z-a)+count;
    if(end>z) for(uint32_t i=b->size;i>z;i--) b->data[end+i-z-1]=b->data[i-1];
    else for(uint32_t i=z;i<b->size;i++) b->data[end+i-z]=b->data[i];
    for(uint32_t i=0;i<count;i++) b->data[a+i]=data[i];
    b->size=new_size; b->cursor=end; b->goal_valid=0; text_select_clear(b); return 1;
}
int text_insert(text_buffer_t *b,uint8_t ch) { return text_replace(b,&ch,1); }
int text_delete(text_buffer_t *b,int backwards) {
    if(b->selection && b->anchor!=b->cursor) return text_replace(b,0,0);
    if(backwards) { if(!b->cursor) return 0; b->anchor=b->cursor-1; }
    else { if(b->cursor>=b->size) return 0; b->anchor=b->cursor+1; }
    b->selection=1; return text_replace(b,0,0);
}
uint32_t text_hit(const text_buffer_t *b,uint32_t row,uint32_t column) {
    uint32_t pos=b->top;
    while(row--) { text_line_t line=text_line(b->data,b->size,pos); if(!line.has_next) return b->size; pos=line.next; }
    text_line_t line=text_line(b->data,b->size,pos); uint32_t col=0;
    while(pos<line.end) {
        uint32_t next=text_advance(col,b->data[pos]);
        if(column<(col+next+1)/2) break;
        col=next; pos++; if(col>=columns) break;
    }
    return pos;
}
void text_scroll(text_buffer_t *b,int amount) {
    if(amount<0) b->top=text_previous_lines(b->data,b->size,b->top,0u-(uint32_t)amount);
    else while(amount--) {
        text_line_t line=text_line(b->data,b->size,b->top);
        if(!line.has_next) break;
        b->top=line.next;
    }
}
