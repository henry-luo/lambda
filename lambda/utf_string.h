#ifndef LAMBDA_UTF_STRING_H
#define LAMBDA_UTF_STRING_H

#include "lambda.h"

// UTF8PROC support is always enabled
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <utf8proc.h>

#ifdef __cplusplus
extern "C" {
#endif

// Unicode comparison result type
typedef enum {
    UTF8PROC_COMPARE_EQUAL = 0,
    UTF8PROC_COMPARE_LESS = -1,
    UTF8PROC_COMPARE_GREATER = 1,
    UTF8PROC_COMPARE_ERROR = 2
} UnicodeCompareResult;

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
CompResult less_comp_unicode(Item a_item, Item b_item);
CompResult greater_comp_unicode(Item a_item, Item b_item);
CompResult less_equal_comp_unicode(Item a_item, Item b_item);
CompResult greater_equal_comp_unicode(Item a_item, Item b_item);

UnicodeCompareResult string_compare_unicode(const char* str1, int len1, 
                                           const char* str2, int len2);
bool string_equal_unicode(const char* str1, int len1, const char* str2, int len2);

// Unicode normalization functions
char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);
char* normalize_utf8proc_casefold(const char* str, int len, int* out_len);

// Utility functions
bool is_ascii_string(const char* str, int len);
bool is_valid_utf8(const char* str, int len);

// Lambda Script wrapper functions for eval system
Item fn_lt_unicode(Item a_item, Item b_item);
Item fn_gt_unicode(Item a_item, Item b_item);
Item fn_le_unicode(Item a_item, Item b_item);
Item fn_ge_unicode(Item a_item, Item b_item);

// UTF8PROC variants (called by lambda-eval.cpp)
CompResult equal_comp_unicode(Item a_item, Item b_item);
Item fn_lt_utf8proc(Item a_item, Item b_item);
Item fn_gt_utf8proc(Item a_item, Item b_item);
Item fn_le_utf8proc(Item a_item, Item b_item);
Item fn_ge_utf8proc(Item a_item, Item b_item);


#ifdef __cplusplus
}
#endif

#endif // LAMBDA_UTF_STRING_H
