// allocation/string helpers are shared; the WASM provider has no tracker state.
#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "math_checked.hpp"
#include "str.h"
#include <stdlib.h>
#include <string.h>

#ifdef LAMBDA_NO_MEMTRACK
// preserve zero-size, overflow and failed-realloc contracts without metadata.
void* mem_alloc_loc(size_t size, MemCategory category, int line) {
    (void)category;
    (void)line;
    return size ? malloc(size) : NULL;
}

void* mem_calloc_loc(size_t count, size_t size, MemCategory category, int line) {
    (void)category;
    (void)line;
    size_t total = 0;
    if (!count || !size || !math_checked_mul(count, size, &total)) return NULL;
    return calloc(count, size);
}

void* mem_realloc_loc(void* ptr, size_t size, MemCategory category, int line) {
    if (!ptr) return mem_alloc_loc(size, category, line);
    if (!size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

void mem_free_loc(void* ptr, int line) {
    (void)line;
    free(ptr);
}
#endif

void* mem_alloc(size_t size, MemCategory category) {
    return mem_alloc_loc(size, category, 0);
}

void* mem_calloc(size_t count, size_t size, MemCategory category) {
    return mem_calloc_loc(count, size, category, 0);
}

void* mem_realloc(void* ptr, size_t new_size, MemCategory category) {
    return mem_realloc_loc(ptr, new_size, category, 0);
}

void mem_free(void* ptr) {
    mem_free_loc(ptr, 0);
}

char* mem_dup_n_loc(const char* data, size_t len, MemCategory category, int line) {
    if (!data) return NULL;
    size_t alloc_size = 0;
    if (!math_checked_add(len, 1, &alloc_size)) return NULL;
    char* dup = (char*)mem_alloc_loc(alloc_size, category, line);
    if (dup) {
        memcpy(dup, data, len);
        dup[len] = '\0';
    }
    return dup;
}

char* mem_dup_n(const char* data, size_t len, MemCategory category) {
    return mem_dup_n_loc(data, len, category, 0);
}

typedef struct MemJoinContext {
    MemCategory category;
    int line;
} MemJoinContext;

static void* mem_join_alloc(void* context, size_t size) {
    MemJoinContext* join = (MemJoinContext*)context;
    return mem_alloc_loc(size, join->category, join->line);
}

char* mem_join_parts_loc(const char* const* parts, const size_t* lengths,
                         size_t count, MemCategory category, int line) {
    MemJoinContext context = {category, line};
    return str_join_parts_alloc(parts, lengths, count, mem_join_alloc, &context);
}

char* mem_join_parts(const char* const* parts, const size_t* lengths, size_t count,
                     MemCategory category) {
    return mem_join_parts_loc(parts, lengths, count, category, 0);
}

char* mem_join2_loc(const char* first, size_t first_len,
                    const char* second, size_t second_len,
                    MemCategory category, int line) {
    const char* parts[] = {first, second};
    const size_t lengths[] = {first_len, second_len};
    return mem_join_parts_loc(parts, lengths, 2, category, line);
}

char* mem_join2(const char* first, size_t first_len,
                const char* second, size_t second_len, MemCategory category) {
    return mem_join2_loc(first, first_len, second, second_len, category, 0);
}

char* mem_join3_loc(const char* first, size_t first_len,
                    const char* second, size_t second_len,
                    const char* third, size_t third_len,
                    MemCategory category, int line) {
    const char* parts[] = {first, second, third};
    const size_t lengths[] = {first_len, second_len, third_len};
    return mem_join_parts_loc(parts, lengths, 3, category, line);
}

char* mem_join3(const char* first, size_t first_len,
                const char* second, size_t second_len,
                const char* third, size_t third_len, MemCategory category) {
    return mem_join3_loc(first, first_len, second, second_len, third, third_len, category, 0);
}

char* mem_strdup_loc(const char* str, MemCategory category, int line) {
    if (!str) return NULL;
    return mem_dup_n_loc(str, strlen(str), category, line);
}

char* mem_strdup(const char* str, MemCategory category) {
    return mem_strdup_loc(str, category, 0);
}

char* mem_strndup_loc(const char* str, size_t max_len, MemCategory category, int line) {
    if (!str) return NULL;
    size_t len = 0;
    while (len < max_len && str[len] != '\0') len++;
    return mem_dup_n_loc(str, len, category, line);
}

char* mem_strndup(const char* str, size_t max_len, MemCategory category) {
    return mem_strndup_loc(str, max_len, category, 0);
}

// ============================================================================
// Raw Allocation Escape Hatches
// ============================================================================

void* raw_malloc(size_t size) {
    return malloc(size);
}

void* raw_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

void* raw_realloc(void* ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

void raw_free(void* ptr) {
    free(ptr);
}

char* raw_strdup(const char* str) {
    return str ? strdup(str) : NULL;
}
