#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "str.h"
#include "string.h"

/* Simple string creation helper */
String* create_string(Pool* pool, const char* str) {
    if (!str || !pool) return NULL;

    size_t len = strlen(str);
    String* string = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (!string) return NULL;

    string->len = (uint32_t)len;
    string->ref_cnt = 1;
    str_copy(string->chars, len + 1, str, len);

    return string;
}

/* Create string from StrView */
String* string_from_strview(StrView view, Pool* pool) {
    if (!view.str || !pool) return NULL;
    // Allow empty strings (length == 0)

    String* string = (String*)pool_calloc(pool, sizeof(String) + view.length + 1);
    if (!string) return NULL;

    string->len = (uint32_t)view.length;
    string->ref_cnt = 1;
    if (view.length > 0) {
        str_copy(string->chars, view.length + 1, view.str, view.length);
    } else {
        string->chars[0] = '\0';
    }

    return string;
}

/* Equality: two String* by content */
bool string_eq(const String* a, const String* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return str_eq(a->chars, a->len, b->chars, b->len);
}

/* Lexicographic comparison */
int string_cmp(const String* a, const String* b) {
    const char* ap = a ? a->chars : NULL;
    size_t al = a ? a->len : 0;
    const char* bp = b ? b->chars : NULL;
    size_t bl = b ? b->len : 0;
    return str_cmp(ap, al, bp, bl);
}

/* FNV-1a hash */
uint64_t string_hash(const String* s) {
    if (!s) return 0;
    return str_hash(s->chars, s->len);
}

/* Compare String* with NUL-terminated C string */
bool string_eq_cstr(const String* s, const char* cstr) {
    if (!s) return (!cstr || *cstr == '\0');
    if (!cstr) return s->len == 0;
    return str_eq_const(s->chars, s->len, cstr);
}
