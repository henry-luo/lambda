#ifndef SLICE_H
#define SLICE_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char* data;      // Pointer to null-terminated string data
    size_t len;      // Length excluding null terminator
} StrView;

// String View functions
#define strview_new(str, len) ((StrView){.data = (char*)str, .len = len})
#define strview_from_str(str) ((StrView){.data = (char*)str, .len = strlen(str)})
char strview_get(const StrView* s, size_t index);        // Get character at index
StrView strview_sub(const StrView* s, size_t start, size_t end); // Substring
bool strview_equals(const StrView* a, const StrView* b); // String comparison
bool strview_starts_with(const StrView* s, const char* prefix); // Prefix check
bool strview_ends_with(const StrView* s, const char* suffix);   // Suffix check
int strview_find(const StrView* s, const char* substr);   // Find substring position
void strview_trim(StrView* s);                           // Trim whitespace
char* strview_to_cstr(const StrView* s);                 // Convert to null-terminated string (allocates)

#endif