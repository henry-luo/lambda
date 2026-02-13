// strview.c — string view implementation (delegates to str.h)
#include "strview.h"
#include "str.h"
#include <string.h>
#include <stdlib.h>

char strview_get(const StrView* s, size_t index) {
    if (!s || index >= s->length) return '\0';
    return s->str[index];
}

StrView strview_sub(const StrView* s, size_t start, size_t end) {
    if (!s || start > end || end > s->length)
        return (StrView){.str = NULL, .length = 0};
    return (StrView){.str = s->str + start, .length = end - start};
}

bool strview_eq(const StrView* a, const StrView* b) {
    if (!a || !b) return false;
    return str_eq(a->str, a->length, b->str, b->length);
}

bool strview_equal(const StrView* a, const char* b) {
    if (!a || !b) return false;
    return str_eq_lit(a->str, a->length, b);
}

bool strview_start_with(const StrView* s, const char* prefix) {
    if (!s || !prefix) return false;
    return str_starts_with_lit(s->str, s->length, prefix);
}

bool strview_end_with(const StrView* s, const char* suffix) {
    if (!s || !suffix) return false;
    return str_ends_with_lit(s->str, s->length, suffix);
}

// fixed: old impl used strstr() which requires NUL-terminated haystack;
// str_find() is length-bounded and safe for non-NUL-terminated views.
int strview_find(const StrView* s, const char* substr) {
    if (!s || !substr || !s->length) return -1;
    size_t needle_len = strlen(substr);
    size_t pos = str_find(s->str, s->length, substr, needle_len);
    return (pos == STR_NPOS) ? -1 : (int)pos;
}

void strview_trim(StrView* s) {
    if (!s || !s->length) return;
    str_trim(&s->str, &s->length);
}

int strview_to_int(StrView* s) {
    if (!s || !s->length) return 0;
    int64_t v;
    if (str_to_int64(s->str, s->length, &v, NULL)) return (int)v;
    return 0;
}

char* strview_to_cstr(const StrView* s) {
    if (!s || !s->length) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    return str_dup(s->str, s->length);
}
