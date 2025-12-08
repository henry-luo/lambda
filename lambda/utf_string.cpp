#include "utf_string.h"
#include "lambda-data.hpp"
#include <utf8proc.h>
#include <string.h>
#include <stdlib.h>
#include "../lib/log.h"

void init_utf8proc_support(void) {
    // utf8proc is initialized automatically, no setup needed
}

void cleanup_utf8proc_support(void) {
    // utf8proc doesn't require explicit cleanup
}

bool is_ascii_string(const char* str, int len) {
    if (!str) return false;

    for (int i = 0; i < len; i++) {
        if ((unsigned char)str[i] > 127) {
            return false;
        }
    }
    return true;
}

bool is_valid_utf8(const char* str, int len) {
    if (!str) return false;

    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;

    while (pos < len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), len - pos, &codepoint);
        if (bytes_read < 0) {
            return false; // Invalid UTF-8
        }
        if (codepoint == -1) {
            return false; // Invalid codepoint
        }
        pos += bytes_read;
    }

    return true;
}

char* normalize_utf8proc_nfc(const char* str, int len, int* out_len) {
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_COMPOSE)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
}

char* normalize_utf8proc_nfd(const char* str, int len, int* out_len) {
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_DECOMPOSE)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
}

char* normalize_utf8proc_casefold(const char* str, int len, int* out_len) {
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
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
        if (fold1) free(fold1);
        if (fold2) free(fold2);
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

    free(fold1);  free(fold2);
    return result;
}

bool string_equal_unicode(const char* str1, int len1, const char* str2, int len2) {
    return string_compare_unicode(str1, len1, str2, len2) == UTF8PROC_COMPARE_EQUAL;
}

// Lambda Script comparison functions
Bool equal_comp_unicode(Item a_item, Item b_item) {
    log_debug("equal_comp_unicode");
    String* a_str = a_item.get_string();
    String* b_str = b_item.get_string();

    if (!a_str || !b_str) { return BOOL_ERROR; }

    // Quick length check first
    if (a_str->len != b_str->len) { return BOOL_FALSE; }

    // If both are empty strings
    if (a_str->len == 0) { return BOOL_TRUE; }

    // Use byte comparison for identical strings
    if (a_str == b_str) { return BOOL_TRUE; }

    bool result = string_equal_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    return result ? BOOL_TRUE : BOOL_FALSE;
}

Bool less_comp_unicode(Item a_item, Item b_item) {
    String* a_str = a_item.get_string();
    String* b_str = b_item.get_string();

    if (!a_str || !b_str) { return BOOL_ERROR; }

    // Use byte comparison for identical strings
    if (a_str == b_str) { return BOOL_FALSE; }

    UnicodeCompareResult result = string_compare_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    if (result == UTF8PROC_COMPARE_ERROR) {
        return BOOL_ERROR;
    }

    return (result == UTF8PROC_COMPARE_LESS) ? BOOL_TRUE : BOOL_FALSE;
}

Bool greater_comp_unicode(Item a_item, Item b_item) {
    String* a_str = a_item.get_string();
    String* b_str = b_item.get_string();

    if (!a_str || !b_str) { return BOOL_ERROR; }

    // Use byte comparison for identical strings
    if (a_str == b_str) { return BOOL_FALSE; }

    UnicodeCompareResult result = string_compare_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    if (result == UTF8PROC_COMPARE_ERROR) {
        return BOOL_ERROR;
    }

    return (result == UTF8PROC_COMPARE_GREATER) ? BOOL_TRUE : BOOL_FALSE;
}

Bool less_equal_comp_unicode(Item a_item, Item b_item) {
    String* a_str = a_item.get_string();
    String* b_str = b_item.get_string();

    if (!a_str || !b_str) { return BOOL_ERROR; }

    // Use byte comparison for identical strings
    if (a_str == b_str) { return BOOL_TRUE; }

    UnicodeCompareResult result = string_compare_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    if (result == UTF8PROC_COMPARE_ERROR) {
        return BOOL_ERROR;
    }

    return (result == UTF8PROC_COMPARE_LESS || result == UTF8PROC_COMPARE_EQUAL) ? BOOL_TRUE : BOOL_FALSE;
}

Bool greater_equal_comp_unicode(Item a_item, Item b_item) {
    String* a_str = a_item.get_string();
    String* b_str = b_item.get_string();
    if (!a_str || !b_str) { return BOOL_ERROR; }

    // Use byte comparison for identical strings
    if (a_str == b_str) { return BOOL_TRUE; }

    UnicodeCompareResult result = string_compare_unicode(a_str->chars, a_str->len, b_str->chars, b_str->len);
    if (result == UTF8PROC_COMPARE_ERROR) {
        return BOOL_ERROR;
    }

    return (result == UTF8PROC_COMPARE_GREATER || result == UTF8PROC_COMPARE_EQUAL) ? BOOL_TRUE : BOOL_FALSE;
}

// Missing normalization functions
char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len) {
    // NFKC is compatibility + composition
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_COMPAT)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
}

char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len) {
    // NFKD is compatibility + decomposition
    if (!str || len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        len,
        &result,
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT)
    );

    if (result_len < 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    if (out_len) *out_len = (int)result_len;
    return (char*)result;
}
