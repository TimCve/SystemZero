#include "text.h"
uint32_t text_advance(uint32_t column, uint8_t ch) {
    return column + (ch == '\t' ? 4 - column % 4 : 1);
}
text_line_t text_line(const uint8_t *data, uint32_t size, uint32_t start) {
    uint32_t pos = start < size ? start : size, column = 0;
    while (pos < size) {
        if (data[pos] == '\n') return (text_line_t){pos, pos + 1, column, 1};
        column = text_advance(column, data[pos++]);
        if (column >= TERM_COLS) return (text_line_t){pos, pos, column, 1};
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
    if (count > TERM_ROWS - 1) count = TERM_ROWS - 1;
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
        if (next > buffer->goal_column || next >= TERM_COLS) break;
        column = next; pos++;
    }
    buffer->cursor = pos;
}
void text_ensure_visible(text_buffer_t *buffer) {
    uint32_t line = text_line_start(buffer->data, buffer->size, buffer->cursor);
    buffer->top = text_line_start(buffer->data, buffer->size, buffer->top);
    if (line < buffer->top) { buffer->top = line; return; }
    uint32_t pos = buffer->top;
    for (uint32_t row = 0; row < TERM_ROWS - 1; row++) {
        if (pos == line) return;
        text_line_t current = text_line(buffer->data, buffer->size, pos);
        if (!current.has_next) break;
        pos = current.next;
    }
    buffer->top = text_previous_lines(buffer->data, buffer->size, line, TERM_ROWS - 2);
}
int text_insert(text_buffer_t *buffer, uint8_t ch) {
    if (buffer->size == buffer->capacity || buffer->cursor > buffer->size) return 0;
    for (uint32_t i = buffer->size; i > buffer->cursor; i--) buffer->data[i] = buffer->data[i - 1];
    buffer->data[buffer->cursor++] = ch; buffer->size++; buffer->goal_valid = 0; return 1;
}
int text_delete(text_buffer_t *buffer, int backwards) {
    if (backwards) { if (!buffer->cursor) return 0; buffer->cursor--; }
    if (buffer->cursor >= buffer->size) return 0;
    for (uint32_t i = buffer->cursor; i + 1 < buffer->size; i++) buffer->data[i] = buffer->data[i + 1];
    buffer->size--; buffer->goal_valid = 0; return 1;
}
