#ifndef TEST_PROCESS_H
#define TEST_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char* cursor;
    size_t remaining;
} TestProcessLines;

static inline void test_process_lines_init(
    TestProcessLines* lines, const char* output, size_t output_len)
{
    lines->cursor = output;
    lines->remaining = output ? output_len : 0;
}

static inline bool test_process_next_line(
    TestProcessLines* lines, char* buffer, size_t buffer_size)
{
    if (!lines || !buffer || buffer_size < 2 || lines->remaining == 0) return false;
    const char* newline = (const char*)memchr(lines->cursor, '\n', lines->remaining);
    size_t available = newline ? (size_t)(newline - lines->cursor) + 1 : lines->remaining;
    size_t copied = available < buffer_size - 1 ? available : buffer_size - 1;
    memcpy(buffer, lines->cursor, copied);
    buffer[copied] = '\0';
    lines->cursor += copied;
    lines->remaining -= copied;
    return true;
}

#endif
