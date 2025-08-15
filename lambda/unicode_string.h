#ifndef LAMBDA_UNICODE_STRING_H
#define LAMBDA_UNICODE_STRING_H

#include "lambda.h"
#include "unicode_config.h"

#ifdef LAMBDA_ICU_SUPPORT
    #include <unicode/ucol.h>
    #include <unicode/ustring.h>
    #include <unicode/unorm2.h>
    #include <unicode/uchar.h>
#endif

// Forward declarations
typedef enum {
    UNICODE_COMPARE_EQUAL = 0,
    UNICODE_COMPARE_LESS = -1,
    UNICODE_COMPARE_GREATER = 1,
    UNICODE_COMPARE_ERROR = 2
} UnicodeCompareResult;

// Unicode support initialization and cleanup
void init_unicode_support(void);
void cleanup_unicode_support(void);

// Enhanced string comparison functions
CompResult equal_comp_unicode(Item a_item, Item b_item);
UnicodeCompareResult string_compare_unicode(const char* str1, int len1, 
                                          const char* str2, int len2);

// Unicode-aware relational comparison functions
Item fn_eq_unicode(Item a_item, Item b_item);
Item fn_ne_unicode(Item a_item, Item b_item);
Item fn_lt_unicode(Item a_item, Item b_item);
Item fn_gt_unicode(Item a_item, Item b_item);
Item fn_le_unicode(Item a_item, Item b_item);
Item fn_ge_unicode(Item a_item, Item b_item);

// Utility functions
bool is_ascii_string(const char* str, int len);

#endif // LAMBDA_UNICODE_STRING_H
