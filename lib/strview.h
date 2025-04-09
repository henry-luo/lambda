#ifndef SLICE_H
#define SLICE_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char* str;      // pointer to string data (may or may-not be null-terminated)
    size_t length;      // length excluding null terminator
} StrView;

// String View functions
#define strview_init(string, len) ((StrView){.str = (char*)string, .length = len})
#define strview_from_str(string) ((StrView){.str = (char*)string, .length = strlen(string)})
char strview_get(const StrView* s, size_t index);        // Get character at index
StrView strview_sub(const StrView* s, size_t start, size_t end);  // Substring
bool strview_eq(const StrView* a, const StrView* b);  // String comparison
bool strview_start_with(const StrView* s, const char* prefix);  // Prefix check
bool strview_end_with(const StrView* s, const char* suffix);  // Suffix check
int strview_find(const StrView* s, const char* substr);  // Find substring position
void strview_trim(StrView* s);                           // Trim whitespace
int strview_to_int(StrView* s);                          // Convert to integer
char* strview_to_cstr(const StrView* s);                 // Convert to null-terminated string (allocates)

#endif