#ifndef LAMBDA_UTF8PROC_STRING_H
#define LAMBDA_UTF8PROC_STRING_H

#include "lambda.h"

// Only compile utf8proc functions if utf8proc support is enabled at compile time
#ifdef LAMBDA_UTF8PROC_SUPPORT

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef LAMBDA_UTF8PROC_SUPPORT
    #include <utf8proc.h>
#endif

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
CompResult equal_comp_utf8proc(Item a_item, Item b_item);
Utf8procCompareResult string_compare_utf8proc(const char* str1, int len1, 
                                              const char* str2, int len2);

// String collation with different modes
Utf8procCompareResult collate_utf8proc(const char* str1, int len1,
                                      const char* str2, int len2,
                                      Utf8procCollateMode mode);

// Unicode normalization functions
char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);

// utf8proc-aware relational comparison functions
Item fn_eq_utf8proc(Item a_item, Item b_item);
Item fn_ne_utf8proc(Item a_item, Item b_item);
Item fn_lt_utf8proc(Item a_item, Item b_item);
Item fn_gt_utf8proc(Item a_item, Item b_item);
Item fn_le_utf8proc(Item a_item, Item b_item);
Item fn_ge_utf8proc(Item a_item, Item b_item);

// Utility functions
bool is_ascii_string(const char* str, int len);
bool is_valid_utf8(const char* str, int len);

#ifdef __cplusplus
}
#endif

#else
// Stub declarations when utf8proc support is disabled
// These should never be called, but we provide them to avoid linker errors

Item fn_eq_utf8proc(Item a, Item b);
Item fn_ne_utf8proc(Item a, Item b);
Item fn_lt_utf8proc(Item a, Item b);
Item fn_gt_utf8proc(Item a, Item b);
Item fn_le_utf8proc(Item a, Item b);
Item fn_ge_utf8proc(Item a, Item b);

#endif // LAMBDA_UTF8PROC_SUPPORT

#endif // LAMBDA_UTF8PROC_STRING_H
