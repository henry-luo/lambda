// slice.c (updated)
#include "strview.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

char strview_get(const StrView* s, size_t index) {
    if (!s || index >= s->length) {
        return '\0';
    }
    return s->str[index];
}

StrView strview_sub(const StrView* s, size_t start, size_t end) {
    if (!s || start > end || end > s->length) {
        return (StrView){.str = NULL, .length = 0};
    }
    return (StrView){.str = s->str + start, .length = end - start};
}

bool strview_eq(const StrView* a, const StrView* b) {
    if (!a || !b || a->length != b->length) {
        return false;
    }
    return strncmp(a->str, b->str, a->length) == 0;
}

bool strview_equal(const StrView* a, const char* b) {
    if (!a || !b) { return false; }
    return strncmp(a->str, b, a->length) == 0 && b[a->length] == '\0'; 
}

bool strview_start_with(const StrView* s, const char* prefix) {
    if (!s || !prefix) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    if (prefix_len > s->length) {
        return false;
    }
    return strncmp(s->str, prefix, prefix_len) == 0;
}

bool strview_end_with(const StrView* s, const char* suffix) {
    if (!s || !suffix) {
        return false;
    }
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s->length) {
        return false;
    }
    return strncmp(s->str + s->length - suffix_len, suffix, suffix_len) == 0;
}

int strview_find(const StrView* s, const char* substr) {
    if (!s || !substr || !s->length) {
        return -1;
    }
    char* found = strstr(s->str, substr);
    if (!found) {
        return -1;
    }
    return (int)(found - s->str);
}

void strview_trim(StrView* s) {
    if (!s || !s->length) { return; }
    
    // Trim start
    size_t start = 0;
    while (start < s->length && isspace((unsigned char)s->str[start])) {
        start++;
    }
    
    // Trim end
    size_t end = s->length;
    while (end > start && isspace((unsigned char)s->str[end - 1])) {
        end--;
    }
    
    s->str += start;
    s->length = end - start;
}

// convert substring to integer
int strview_to_int(StrView* s) {
    const char* str = s->str;
    int len = s->length;
    int result = 0, sign = 1;
    if (len <= 0 || str[0] == '\0') {
        return 0;
    }
    // handle sign
    if (*str == '-') {
        sign = -1;  str++;
    } else if (*str == '+') {
        str++;
    }
    // process digits
    const char* end = str + len;
    while (str < end) {
        if (*str >= '0' && *str <= '9') {
            result = result * 10 + (*str - '0');
        } else {
            break;
        }
        str++;
    }
    return sign * result;
}

char* strview_to_cstr(const StrView* s) {
    if (!s || !s->length) {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    
    char* result = malloc(s->length + 1);
    if (!result) {
        return NULL;
    }
    
    memcpy(result, s->str, s->length);
    result[s->length] = '\0';
    return result;
}