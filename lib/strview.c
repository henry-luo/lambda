// strview.c — string view implementation (delegates to str.h)
#include "strview.h"
#include "memtrack.h"
#include "mempool.h"
#include "hashmap.h"
#include "str.h"
#include <string.h>
#include <stdlib.h>

StrView strview_from_cstr(const char* str) {
    StrView sv;
    if (str) { sv.str = str; sv.length = strlen(str); }
    else     { sv.str = "";  sv.length = 0; }
    return sv;
}

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
    return str_eq_const(a->str, a->length, b);
}

bool strview_starts_with(const StrView* s, const char* prefix) {
    if (!s || !prefix) return false;
    return str_starts_with_const(s->str, s->length, prefix);
}

bool strview_ends_with(const StrView* s, const char* suffix) {
    if (!s || !suffix) return false;
    return str_ends_with_const(s->str, s->length, suffix);
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
        char* empty = (char*)mem_alloc(1, MEM_CAT_TEMP);
        if (empty) empty[0] = '\0';
        return empty;
    }
    char* out = (char*)mem_alloc(s->length + 1, MEM_CAT_TEMP);
    if (!out) return NULL;
    memcpy(out, s->str, s->length);
    out[s->length] = '\0';
    return out;
}

bool strview_contains(const StrView* s, const char* substr) {
    if (!s || !substr) return false;
    if (!*substr) return true;
    size_t needle_len = strlen(substr);
    return str_find(s->str, s->length, substr, needle_len) != STR_NPOS;
}

bool strview_to_int64(const StrView* s, int64_t* out) {
    if (!s || !s->length || !out) return false;
    return str_to_int64(s->str, s->length, out, NULL);
}

bool strview_to_double(const StrView* s, double* out) {
    if (!s || !s->length || !out) return false;
    return str_to_double(s->str, s->length, out, NULL);
}

uint64_t strview_hash(const StrView* s) {
    if (!s || !s->length) return 0;
    return hashmap_sip(s->str, s->length, 0, 0);
}

char* strview_dup_with_pool(const StrView* s, Pool* pool) {
    if (!s) return NULL;
    size_t n = s->length;
    char* out;
    if (pool) {
        out = (char*)pool_alloc(pool, n + 1);
    } else {
        out = (char*)mem_alloc(n + 1, MEM_CAT_TEMP);
    }
    if (!out) return NULL;
    if (n) memcpy(out, s->str, n);
    out[n] = '\0';
    return out;
}

void strview_split_init(StrViewSplitIter* it, StrView input, char delimiter) {
    if (!it) return;
    it->rest = input;
    it->delimiter = delimiter;
    it->finished = false;
}

bool strview_split_next(StrViewSplitIter* it, StrView* token) {
    if (!it || !token || it->finished) return false;

    size_t pos = str_find_byte(it->rest.str, it->rest.length, it->delimiter);
    if (pos == STR_NPOS) {
        *token = it->rest;
        it->rest.str = it->rest.str ? it->rest.str + it->rest.length : NULL;
        it->rest.length = 0;
        it->finished = true;
        return true;
    }

    token->str = it->rest.str;
    token->length = pos;
    it->rest.str += pos + 1;
    it->rest.length -= pos + 1;
    return true;
}
