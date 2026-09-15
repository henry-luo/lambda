#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "str.h"
#include "string.h"

typedef void* (*StringAllocFn)(void* owner, size_t size);

static String* string_from_strview_alloc(StrView view, void* owner, StringAllocFn allocator) {
    if (!view.str || !owner || !allocator || view.length > UINT32_MAX ||
        view.length > SIZE_MAX - sizeof(String) - 1) return NULL;
    String* string = (String*)allocator(owner, sizeof(String) + view.length + 1);
    if (!string) return NULL;

    string->len = (uint32_t)view.length;
    string->flags = 0;
    if (view.length > 0) {
        string->is_ascii = str_is_ascii(view.str, view.length) ? 1 : 0;
        str_copy(string->chars, view.length + 1, view.str, view.length);
    } else {
        string->is_ascii = 1;  // empty string is ascii
        string->chars[0] = '\0';
    }

    return string;
}

static void* string_pool_alloc(void* owner, size_t size) {
    return pool_calloc((Pool*)owner, size);
}

static void* string_arena_alloc(void* owner, size_t size) {
    return arena_alloc((Arena*)owner, size);
}

typedef struct {
    MemCategory category;
} StringMemAllocator;

static void* string_mem_alloc(void* owner, size_t size) {
    return mem_alloc(size, ((StringMemAllocator*)owner)->category);
}

/* Simple string creation helper */
String* create_string(Pool* pool, const char* str) {
    return string_from_strview(strview_init(str, str ? strlen(str) : 0), pool);
}

/* Create string from StrView */
String* string_from_strview(StrView view, Pool* pool) {
    return string_from_strview_alloc(view, pool, string_pool_alloc);
}

String* string_from_strview_arena(StrView view, Arena* arena) {
    return string_from_strview_alloc(view, arena, string_arena_alloc);
}

String* string_from_strview_mem(StrView view, MemCategory category) {
    StringMemAllocator allocator = {category};
    return string_from_strview_alloc(view, &allocator, string_mem_alloc);
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
