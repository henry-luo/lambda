#include "input-latex-scanner.h"

#include <ctype.h>
#include <string.h>

static bool scanner_name_char(char c) {
    return isalpha((unsigned char)c) || c == '@';
}

bool latex_scan_starts_with(const char* source, size_t length, size_t start,
                            const char* text) {
    if (!source || !text || start > length) return false;
    size_t text_length = strlen(text);
    return text_length <= length - start &&
        memcmp(source + start, text, text_length) == 0;
}

size_t latex_scan_group_end(const char* source, size_t length, size_t start,
                            char open, char close, size_t* content_start,
                            size_t* content_end) {
    if (!source || start >= length || source[start] != open) return 0;
    size_t cursor = start + 1;
    size_t begin = cursor;
    size_t depth = 1;
    while (cursor < length) {
        char c = source[cursor++];
        if (c == '\\' && cursor < length) {
            cursor++;
            continue;
        }
        if (c == open) depth++;
        else if (c == close && --depth == 0) {
            if (content_start) *content_start = begin;
            if (content_end) *content_end = cursor - 1;
            return cursor;
        }
    }
    if (content_start) *content_start = begin;
    if (content_end) *content_end = length;
    return 0;
}

size_t latex_scan_command(const char* source, size_t length, size_t start,
                          char* name, size_t name_capacity, char* full,
                          size_t full_capacity) {
    if (!source || start >= length || source[start] != '\\' ||
            !name || name_capacity == 0 || !full || full_capacity == 0) return 0;
    size_t cursor = start + 1;
    size_t name_start = cursor;
    if (cursor < length && scanner_name_char(source[cursor])) {
        while (cursor < length && scanner_name_char(source[cursor])) cursor++;
    } else if (cursor < length) {
        cursor++;
    }
    size_t name_length = cursor - name_start;
    if (name_length >= name_capacity) name_length = name_capacity - 1;
    memcpy(name, source + name_start, name_length);
    name[name_length] = '\0';
    size_t full_length = cursor - start;
    if (full_length >= full_capacity) full_length = full_capacity - 1;
    memcpy(full, source + start, full_length);
    full[full_length] = '\0';
    return cursor;
}
