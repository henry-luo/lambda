// slice.c (updated)
#include "strview.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

char strview_get(const StrView* s, size_t index) {
    if (!s || index >= s->len) {
        return '\0';
    }
    return s->data[index];
}

StrView strview_sub(const StrView* s, size_t start, size_t end) {
    if (!s || start > end || end > s->len) {
        return (StrView){.data = NULL, .len = 0};
    }
    return (StrView){.data = s->data + start, .len = end - start};
}

bool strview_equals(const StrView* a, const StrView* b) {
    if (!a || !b || a->len != b->len) {
        return false;
    }
    return strncmp(a->data, b->data, a->len) == 0;
}

bool strview_starts_with(const StrView* s, const char* prefix) {
    if (!s || !prefix) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    if (prefix_len > s->len) {
        return false;
    }
    return strncmp(s->data, prefix, prefix_len) == 0;
}

bool strview_ends_with(const StrView* s, const char* suffix) {
    if (!s || !suffix) {
        return false;
    }
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s->len) {
        return false;
    }
    return strncmp(s->data + s->len - suffix_len, suffix, suffix_len) == 0;
}

int strview_find(const StrView* s, const char* substr) {
    if (!s || !substr || !s->len) {
        return -1;
    }
    char* found = strstr(s->data, substr);
    if (!found) {
        return -1;
    }
    return (int)(found - s->data);
}

void strview_trim(StrView* s) {
    if (!s || !s->len) {
        return;
    }
    
    // Trim start
    size_t start = 0;
    while (start < s->len && isspace((unsigned char)s->data[start])) {
        start++;
    }
    
    // Trim end
    size_t end = s->len;
    while (end > start && isspace((unsigned char)s->data[end - 1])) {
        end--;
    }
    
    s->data += start;
    s->len = end - start;
}

char* strview_to_cstr(const StrView* s) {
    if (!s || !s->len) {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    
    char* result = malloc(s->len + 1);
    if (!result) {
        return NULL;
    }
    
    memcpy(result, s->data, s->len);
    result[s->len] = '\0';
    return result;
}