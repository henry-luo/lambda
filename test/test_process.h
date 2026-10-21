#ifndef TEST_PROCESS_H
#define TEST_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char* cursor;
    size_t remaining;
} TestProcessLines;

typedef enum {
    TEST_BATCH_NONE,
    TEST_BATCH_START,
    TEST_BATCH_END,
    TEST_BATCH_TIMING,
    TEST_BATCH_VOLUME
} TestBatchControl;

// a script need not finish its output with a newline before the host's marker.
static inline TestBatchControl test_process_find_batch_control(
    const char* text, size_t length, const char** marker)
{
    static const struct { const char* prefix; size_t length; TestBatchControl kind; } controls[] = {
        {"\x01" "BATCH_START ", 13, TEST_BATCH_START},
        {"\x01" "BATCH_END ", 11, TEST_BATCH_END},
        {"\x01" "COMPILER_TIMING ", 17, TEST_BATCH_TIMING},
        {"\x01" "MIR_VOLUME ", 12, TEST_BATCH_VOLUME}
    };
    *marker = NULL;
    const char* end = text + length;
    while (text < end) {
        text = (const char*)memchr(text, '\x01', (size_t)(end - text));
        if (!text) break;
        for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
            if ((size_t)(end - text) >= controls[i].length &&
                    memcmp(text, controls[i].prefix, controls[i].length) == 0) {
                *marker = text;
                return controls[i].kind;
            }
        }
        text++;
    }
    return TEST_BATCH_NONE;
}

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
