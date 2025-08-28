#ifndef LAMBDA_UTF8PROC_STRING_H
#define LAMBDA_UTF8PROC_STRING_H

#include "lambda.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <utf8proc.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef enum {
    UTF8PROC_COMPARE_EQUAL = 0,
    UTF8PROC_COMPARE_LESS = -1,
    UTF8PROC_COMPARE_GREATER = 1,
    UTF8PROC_COMPARE_ERROR = 2
} Utf8procCompareResult;

// Collation modes for utf8proc-based comparison
typedef enum {
    UTF8PROC_COLLATE_BINARY,     // Byte comparison (fastest)
    UTF8PROC_COLLATE_NORMALIZED, // NFC normalization + compare
    UTF8PROC_COLLATE_CASEFOLD,   // Case-insensitive comparison
} Utf8procCollateMode;

// utf8proc support initialization and cleanup
void init_utf8proc_support(void);
void cleanup_utf8proc_support(void);

// Enhanced string comparison functions using utf8proc
CompResult equal_comp_unicode(Item a_item, Item b_item);
Utf8procCompareResult string_compare_utf8proc(const char* str1, int len1, 
                                              const char* str2, int len2);

// String collation with different modes
Utf8procCompareResult collate_utf8proc(const char* str1, int len1,
    const char* str2, int len2, Utf8procCollateMode mode);

// Unicode normalization functions
char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);

// Utility functions
bool is_ascii_string(const char* str, int len);
bool is_valid_utf8(const char* str, int len);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_UTF8PROC_STRING_H
