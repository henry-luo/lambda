#ifndef SLICE_H
#define SLICE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* str;    // pointer to string data (may or may-not be null-terminated)
    size_t length;      // length excluding null terminator
} StrView;

#include <stdint.h>

// forward decl - full type in mempool.h. avoids pulling a heavy header.
typedef struct Pool Pool;

// String View functions
#define strview_init(string, len) ((StrView){.str = (char*)string, .length = len})
#define strview_from_str(string) ((StrView){.str = (char*)string, .length = strlen(string)})
// build a StrView from a NUL-terminated cstr; safe for NULL (yields empty view).
StrView strview_from_cstr(const char* str);
char strview_get(const StrView* s, size_t index);        // Get character at index
StrView strview_sub(const StrView* s, size_t start, size_t end);  // Substring
bool strview_eq(const StrView* a, const StrView* b);  // String comparison
bool strview_equal(const StrView* a, const char* b);  // strview, string comparison
bool strview_starts_with(const StrView* s, const char* prefix);  // Prefix check
bool strview_ends_with(const StrView* s, const char* suffix);  // Suffix check
int strview_find(const StrView* s, const char* substr);  // Find substring position
bool strview_contains(const StrView* s, const char* substr);  // contains substring?
void strview_trim(StrView* s);                           // Trim whitespace
int strview_to_int(StrView* s);                          // Convert to integer (legacy: int)
// extended parsers - return true on success, false on parse error.
bool strview_to_int64(const StrView* s, int64_t* out);
bool strview_to_double(const StrView* s, double* out);
// hash compatible with hashmap_sip (uses zero seeds).
uint64_t strview_hash(const StrView* s);
char* strview_to_cstr(const StrView* s);                 // Convert to null-terminated string (allocates)
// pool-allocated NUL-terminated copy. pool may be NULL (falls back to mem_alloc).
char* strview_dup_with_pool(const StrView* s, Pool* pool);

typedef struct {
    StrView rest;
    char delimiter;
    bool finished;
} StrViewSplitIter;

void strview_split_init(StrViewSplitIter* it, StrView input, char delimiter);
bool strview_split_next(StrViewSplitIter* it, StrView* token);

#ifdef __cplusplus
}
#endif

#endif
