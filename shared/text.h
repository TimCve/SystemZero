#ifndef SYSTEMZERO_TEXT_H
#define SYSTEMZERO_TEXT_H
#include <stdint.h>
#include "terminal.h"
typedef struct { uint32_t end, next, columns; int has_next; } text_line_t;
typedef struct {
    uint8_t *data;
    uint32_t size, capacity, cursor, top, goal_column;
    int goal_valid;
} text_buffer_t;
uint32_t text_advance(uint32_t column, uint8_t ch);
text_line_t text_line(const uint8_t *data, uint32_t size, uint32_t start);
uint32_t text_line_start(const uint8_t *data, uint32_t size, uint32_t position);
uint32_t text_previous_lines(const uint8_t *data, uint32_t size, uint32_t start, uint32_t count);
uint32_t text_column(const uint8_t *data, uint32_t start, uint32_t position);
void text_move_rows(text_buffer_t *buffer, int rows);
void text_ensure_visible(text_buffer_t *buffer);
int text_insert(text_buffer_t *buffer, uint8_t ch);
int text_delete(text_buffer_t *buffer, int backwards);
#endif
