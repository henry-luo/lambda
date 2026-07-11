#include "utf_string.h"
#include "lambda-data.hpp"
#include <utf8proc.h>
#include <string.h>
#include <stdlib.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"

void init_utf8proc_support(void) {
    // utf8proc is initialized automatically, no setup needed
}

void cleanup_utf8proc_support(void) {
    // utf8proc doesn't require explicit cleanup
}

static char* normalize_utf8proc_with_options(const char* str, int len, int* out_len, utf8proc_option_t options) {
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | options)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
}

char* normalize_utf8proc_nfc(const char* str, int len, int* out_len) {
    return normalize_utf8proc_with_options(str, len, out_len, UTF8PROC_COMPOSE);
}

char* normalize_utf8proc_nfd(const char* str, int len, int* out_len) {
    return normalize_utf8proc_with_options(str, len, out_len, UTF8PROC_DECOMPOSE);
}

char* normalize_utf8proc_casefold(const char* str, int len, int* out_len) {
    return normalize_utf8proc_with_options(str, len, out_len, (utf8proc_option_t)(UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
}

UnicodeCompareResult string_compare_unicode(const char* str1, int len1, const char* str2, int len2) {
    if (!str1 || !str2) {
        return UTF8PROC_COMPARE_ERROR;
    }

    // For proper Unicode collation, we need to use utf8proc's casefold + normalization
    // which handles proper collation order for accented characters
    int fold1_len, fold2_len;
    char* fold1 = normalize_utf8proc_casefold(str1, len1, &fold1_len);
    char* fold2 = normalize_utf8proc_casefold(str2, len2, &fold2_len);

    if (!fold1 || !fold2) {
        if (fold1) raw_free(fold1);  // RAWALLOC_OK: utf8proc internal allocation
        if (fold2) raw_free(fold2);  // RAWALLOC_OK: utf8proc internal allocation
        return UTF8PROC_COMPARE_ERROR;
    }
    log_debug("fold1: origin %s vs. %s", str1, fold1);
    log_debug("fold2: origin %s vs. %s", str2, fold2);

    // Compare casefolded strings byte by byte for proper collation
    int min_len = fold1_len < fold2_len ? fold1_len : fold2_len;
    int cmp = memcmp(fold1, fold2, min_len);

    UnicodeCompareResult result;
    if (cmp == 0) {
        if (fold1_len == fold2_len) {
            result = UTF8PROC_COMPARE_EQUAL;
        } else if (fold1_len < fold2_len) {
            result = UTF8PROC_COMPARE_LESS;
        } else {
            result = UTF8PROC_COMPARE_GREATER;
        }
    } else if (cmp < 0) {
        result = UTF8PROC_COMPARE_LESS;
    } else {
        result = UTF8PROC_COMPARE_GREATER;
    }

    raw_free(fold1);  raw_free(fold2);  // RAWALLOC_OK: utf8proc internal allocation
    return result;
}

bool string_equal_unicode(const char* str1, int len1, const char* str2, int len2) {
    return string_compare_unicode(str1, len1, str2, len2) == UTF8PROC_COMPARE_EQUAL;
}

// Lambda Script comparison functions
typedef enum {
    UNICODE_PRED_EQUAL,
    UNICODE_PRED_LESS,
    UNICODE_PRED_GREATER,
    UNICODE_PRED_LESS_EQUAL,
    UNICODE_PRED_GREATER_EQUAL
} UnicodeComparePredicate;

static Bool unicode_compare_result_matches(UnicodeCompareResult result, UnicodeComparePredicate predicate) {
    if (result == UTF8PROC_COMPARE_ERROR) {
        return BOOL_ERROR;
    }
    switch (predicate) {
        case UNICODE_PRED_EQUAL:
            return result == UTF8PROC_COMPARE_EQUAL ? BOOL_TRUE : BOOL_FALSE;
        case UNICODE_PRED_LESS:
            return result == UTF8PROC_COMPARE_LESS ? BOOL_TRUE : BOOL_FALSE;
        case UNICODE_PRED_GREATER:
            return result == UTF8PROC_COMPARE_GREATER ? BOOL_TRUE : BOOL_FALSE;
        case UNICODE_PRED_LESS_EQUAL:
            return (result == UTF8PROC_COMPARE_LESS || result == UTF8PROC_COMPARE_EQUAL) ? BOOL_TRUE : BOOL_FALSE;
        case UNICODE_PRED_GREATER_EQUAL:
            return (result == UTF8PROC_COMPARE_GREATER || result == UTF8PROC_COMPARE_EQUAL) ? BOOL_TRUE : BOOL_FALSE;
    }
    return BOOL_ERROR;
}

static Bool unicode_compare_items(Item a_item, Item b_item, UnicodeComparePredicate predicate) {
    String* a_str = a_item.get_safe_string();
    String* b_str = b_item.get_safe_string();

    if (!a_str || !b_str) { return BOOL_ERROR; }

    if (predicate == UNICODE_PRED_EQUAL) {
        // equality historically short-circuits before normalization; preserve that surface.
        if (a_str->len != b_str->len) { return BOOL_FALSE; }
        if (a_str->len == 0 || a_str == b_str) { return BOOL_TRUE; }
    } else if (a_str == b_str) {
        return (predicate == UNICODE_PRED_LESS_EQUAL || predicate == UNICODE_PRED_GREATER_EQUAL) ? BOOL_TRUE : BOOL_FALSE;
    }

    UnicodeCompareResult result = string_compare_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    return unicode_compare_result_matches(result, predicate);
}

Bool equal_comp_unicode(Item a_item, Item b_item) {
    log_debug("equal_comp_unicode");
    return unicode_compare_items(a_item, b_item, UNICODE_PRED_EQUAL);
}

Bool less_comp_unicode(Item a_item, Item b_item) {
    return unicode_compare_items(a_item, b_item, UNICODE_PRED_LESS);
}

Bool greater_comp_unicode(Item a_item, Item b_item) {
    return unicode_compare_items(a_item, b_item, UNICODE_PRED_GREATER);
}

Bool less_equal_comp_unicode(Item a_item, Item b_item) {
    return unicode_compare_items(a_item, b_item, UNICODE_PRED_LESS_EQUAL);
}

Bool greater_equal_comp_unicode(Item a_item, Item b_item) {
    return unicode_compare_items(a_item, b_item, UNICODE_PRED_GREATER_EQUAL);
}

char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len) {
    return normalize_utf8proc_with_options(str, len, out_len, (utf8proc_option_t)(UTF8PROC_COMPOSE | UTF8PROC_COMPAT));
}

char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len) {
    return normalize_utf8proc_with_options(str, len, out_len, (utf8proc_option_t)(UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT));
}

void free_utf8proc_result(char* str) {
    // utf8proc_map allocates outside Lambda memtrack, so normalized buffers must bypass mem_free().
    raw_free(str);  // RAWALLOC_OK: paired with utf8proc_map allocation
}
