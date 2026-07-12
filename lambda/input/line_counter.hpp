#pragma once

#include <stddef.h>

typedef struct LineCounter {
    int line;
    int column;
} LineCounter;

static inline void line_counter_init(LineCounter* counter) {
    if (!counter) return;
    counter->line = 1;
    counter->column = 1;
}

static inline void line_counter_advance_char(LineCounter* counter,
                                             char c,
                                             bool is_newline,
                                             bool expand_tabs) {
    if (!counter) return;
    if (is_newline) {
        counter->line++;
        counter->column = 1;
    } else if (expand_tabs && c == '\t') {
        counter->column = ((counter->column / 8) + 1) * 8;
    } else {
        counter->column++;
    }
}

static inline void line_counter_advance_bytes(LineCounter* counter,
                                              const char* text,
                                              size_t len,
                                              bool (*is_newline)(char),
                                              bool expand_tabs) {
    if (!counter || !text) return;
    for (size_t i = 0; i < len; i++) {
        bool newline = is_newline ? is_newline(text[i]) : text[i] == '\n';
        line_counter_advance_char(counter, text[i], newline, expand_tabs);
    }
}

static inline int line_counter_count_lf(const char* text, size_t len) {
    if (!text) return 0;
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') count++;
    }
    return count;
}
