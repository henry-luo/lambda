#pragma once

// Shared cursor primitives for Lambda's post-lexer type and path seams.
// Both parsers receive a committed source span, so these helpers deliberately
// operate on a pointer/end pair and never allocate or own parser state.

#include "../../lib/strview.h"

#include <string.h>

static inline bool lambda_lex_ident_start(char c) {
    return c == '_' || c == '$' || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (unsigned char)c >= 0x80;
}

static inline bool lambda_lex_ident_continue(char c) {
    return lambda_lex_ident_start(c) || (c >= '0' && c <= '9');
}

static inline bool lambda_lex_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline void lambda_lex_skip_space(const char** cursor, const char* end) {
    if (!cursor || !*cursor) return;
    const char*& p = *cursor;
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                *p == '\n' || *p == '\f' || *p == '\v')) p++;
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
            p = p + 2 < end ? p + 2 : end;
            continue;
        }
        return;
    }
}

static inline StrView lambda_lex_peek_word(const char** cursor, const char* end) {
    if (!cursor || !*cursor) return (StrView){end, 0};
    lambda_lex_skip_space(cursor, end);
    const char* p = *cursor;
    StrView word = {p, 0};
    if (p >= end || !lambda_lex_ident_start(*p)) return word;
    const char* q = p;
    while (q < end && lambda_lex_ident_continue(*q)) q++;
    word.length = (size_t)(q - p);
    return word;
}

static inline StrView lambda_lex_take_word(const char** cursor, const char* end) {
    StrView word = lambda_lex_peek_word(cursor, end);
    if (cursor && *cursor) *cursor += word.length;
    return word;
}

static inline bool lambda_lex_word_is(StrView word, const char* text) {
    size_t length = text ? strlen(text) : 0;
    return word.length == length && (!length ||
        (word.str && text && memcmp(word.str, text, length) == 0));
}
