#ifndef LAMBDA_UTF_STRING_H
#define LAMBDA_UTF_STRING_H

#include "lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

// unicode comparison result type
typedef enum {
    UTF8PROC_COMPARE_EQUAL = 0,
    UTF8PROC_COMPARE_LESS = -1,
    UTF8PROC_COMPARE_GREATER = 1,
    UTF8PROC_COMPARE_ERROR = 2
} UnicodeCompareResult;

// utf8proc support initialization and cleanup
void init_utf8proc_support(void);
void cleanup_utf8proc_support(void);

// string comparison functions using utf8proc normalization
Bool equal_comp_unicode(Item a_item, Item b_item);
Bool less_comp_unicode(Item a_item, Item b_item);
Bool greater_comp_unicode(Item a_item, Item b_item);
Bool less_equal_comp_unicode(Item a_item, Item b_item);
Bool greater_equal_comp_unicode(Item a_item, Item b_item);

UnicodeCompareResult string_compare_unicode(const char* str1, int len1, 
                                           const char* str2, int len2);
bool string_equal_unicode(const char* str1, int len1, const char* str2, int len2);

// unicode normalization functions
char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);
char* normalize_utf8proc_casefold(const char* str, int len, int* out_len);
void free_utf8proc_result(char* str);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_UTF_STRING_H
