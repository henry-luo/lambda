#include "unicode_string.h"

// Only compile Unicode functions if Unicode support is enabled at compile time
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef LAMBDA_ICU_SUPPORT
    #include <unicode/ucol.h>
    #include <unicode/ustring.h>
    #include <unicode/unorm2.h>
    #include <unicode/uchar.h>
    #include <unicode/uclean.h>  // For u_init
#endif

// Global ICU collator instances (initialized once)
#ifdef LAMBDA_ICU_SUPPORT
static UCollator* g_default_collator = nullptr;
static UCollator* g_case_insensitive_collator = nullptr;
static const UNormalizer2* g_nfc_normalizer = nullptr;
static bool unicode_initialized = false;
#endif

// ASCII detection helper
bool is_ascii_string(const char* str, int len) {
    for (int i = 0; i < len; i++) {
        if ((unsigned char)str[i] > 127) {
            return false;
        }
    }
    return true;
}

// Initialize ICU components (called during Lambda engine startup)
void init_unicode_support(void) {
#ifdef LAMBDA_ICU_SUPPORT
    if (unicode_initialized) return;
    
    UErrorCode status = U_ZERO_ERROR;
    
    // Initialize ICU explicitly
    u_init(&status);
    if (U_FAILURE(status)) {
        printf("ICU initialization failed (%s), falling back to ASCII-only comparison\n", u_errorName(status));
        unicode_initialized = true; // Mark as initialized but without ICU support
        return;
    }
    
    // Create default collator (root locale, case-sensitive)
    g_default_collator = ucol_open("", &status);
    if (U_FAILURE(status)) {
        printf("ICU collator creation failed (%s), using ASCII fallback\n", u_errorName(status));
        // Don't return - continue with initialization but without ICU collators
    } else {
        // Create case-insensitive collator
        g_case_insensitive_collator = ucol_open("", &status);
        if (U_SUCCESS(status)) {
            ucol_setAttribute(g_case_insensitive_collator, UCOL_CASE_LEVEL, UCOL_OFF, &status);
            ucol_setAttribute(g_case_insensitive_collator, UCOL_STRENGTH, UCOL_SECONDARY, &status);
        }
        
        // Get NFC normalizer
        g_nfc_normalizer = unorm2_getNFCInstance(&status);
        if (U_FAILURE(status)) {
            printf("Failed to get NFC normalizer: %s\n", u_errorName(status));
        }
    }
    
    unicode_initialized = true;
    printf("ICU Unicode support (COMPACT level) initialized successfully\n");
#else
    printf("Unicode support disabled (ASCII-only mode)\n");
#endif
}

// Clean up ICU resources
void cleanup_unicode_support(void) {
#ifdef LAMBDA_ICU_SUPPORT
    if (!unicode_initialized) return;
    
    if (g_default_collator) {
        ucol_close(g_default_collator);
        g_default_collator = nullptr;
    }
    if (g_case_insensitive_collator) {
        ucol_close(g_case_insensitive_collator);
        g_case_insensitive_collator = nullptr;
    }
    
    unicode_initialized = false;
    // printf("ICU Unicode support cleaned up\n");
#endif
}

// Unicode-aware string comparison for equality
// Use external symbol for C linkage function
extern "C" double it2d_c_symbol(Item item) asm("_it2d");

CompResult equal_comp_unicode(Item a_item, Item b_item) {
    printf("equal_comp_unicode called\n");
    
    if (a_item.type_id != b_item.type_id) {
        // Handle numeric promotion as before
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d_c_symbol(a_item), b_val = it2d_c_symbol(b_item);
            return (a_val == b_val) ? COMP_TRUE : COMP_FALSE;
        }
        return COMP_ERROR;
    }
    
    // Handle non-string types as before
    if (a_item.type_id == LMD_TYPE_NULL) return COMP_TRUE;
    if (a_item.type_id == LMD_TYPE_BOOL) {
        return (a_item.bool_val == b_item.bool_val) ? COMP_TRUE : COMP_FALSE;
    }
    if (a_item.type_id == LMD_TYPE_INT) {
        return (a_item.long_val == b_item.long_val) ? COMP_TRUE : COMP_FALSE;
    }
    if (a_item.type_id == LMD_TYPE_INT64) {
        return (*(long*)a_item.pointer == *(long*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    if (a_item.type_id == LMD_TYPE_FLOAT) {
        return (*(double*)a_item.pointer == *(double*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    
    // Enhanced Unicode string comparison
    if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL ||
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
#ifdef LAMBDA_ASCII_FAST_PATH
        // Fast path for ASCII-only strings
        if (is_ascii_string(str_a->chars, str_a->len) && 
            is_ascii_string(str_b->chars, str_b->len)) {
            bool result = (str_a->len == str_b->len && 
                          strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
            return result ? COMP_TRUE : COMP_FALSE;
        }
#endif

#ifdef LAMBDA_ICU_SUPPORT
        if (!unicode_initialized || !g_default_collator) {
            // Fallback to byte comparison if ICU not available
            bool result = (str_a->len == str_b->len && 
                          strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
            return result ? COMP_TRUE : COMP_FALSE;
        }
        
        // ICU-based Unicode comparison with normalization
        UErrorCode status = U_ZERO_ERROR;
        UCollationResult result = ucol_strcollUTF8(g_default_collator,
            str_a->chars, str_a->len,
            str_b->chars, str_b->len,
            &status);
            
        if (U_FAILURE(status)) {
            printf("ICU string comparison failed: %s\n", u_errorName(status));
            return COMP_ERROR;
        }
        
        return (result == UCOL_EQUAL) ? COMP_TRUE : COMP_FALSE;
#else
        // Fallback to byte comparison for non-ICU builds
        bool result = (str_a->len == str_b->len && 
                      strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
        return result ? COMP_TRUE : COMP_FALSE;
#endif
    }
    
    printf("unknown comparing type %d in equal_comp_unicode\n", a_item.type_id);
    return COMP_ERROR;
}

// Unicode-aware string relational comparison
UnicodeCompareResult string_compare_unicode(const char* str1, int len1, 
                                          const char* str2, int len2) {
#ifdef LAMBDA_ASCII_FAST_PATH
    // Fast path for ASCII-only strings
    if (is_ascii_string(str1, len1) && is_ascii_string(str2, len2)) {
        int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
        if (result == 0) {
            if (len1 < len2) return UNICODE_COMPARE_LESS;
            if (len1 > len2) return UNICODE_COMPARE_GREATER;
            return UNICODE_COMPARE_EQUAL;
        }
        return (result < 0) ? UNICODE_COMPARE_LESS : UNICODE_COMPARE_GREATER;
    }
#endif

#ifdef LAMBDA_ICU_SUPPORT
    if (!unicode_initialized || !g_default_collator) {
        // Fallback to byte comparison if ICU not available
        int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
        if (result == 0) {
            if (len1 < len2) return UNICODE_COMPARE_LESS;
            if (len1 > len2) return UNICODE_COMPARE_GREATER;
            return UNICODE_COMPARE_EQUAL;
        }
        return (result < 0) ? UNICODE_COMPARE_LESS : UNICODE_COMPARE_GREATER;
    }
    
    // ICU-based collation comparison
    UErrorCode status = U_ZERO_ERROR;
    UCollationResult result = ucol_strcollUTF8(g_default_collator,
        str1, len1,
        str2, len2,
        &status);
        
    if (U_FAILURE(status)) {
        printf("ICU string collation failed: %s\n", u_errorName(status));
        return UNICODE_COMPARE_ERROR;
    }
    
    switch (result) {
        case UCOL_LESS: return UNICODE_COMPARE_LESS;
        case UCOL_EQUAL: return UNICODE_COMPARE_EQUAL;
        case UCOL_GREATER: return UNICODE_COMPARE_GREATER;
        default: return UNICODE_COMPARE_ERROR;
    }
#else
    // Fallback to byte comparison for non-ICU builds
    int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
    if (result == 0) {
        if (len1 < len2) return UNICODE_COMPARE_LESS;
        if (len1 > len2) return UNICODE_COMPARE_GREATER;
        return UNICODE_COMPARE_EQUAL;
    }
    return (result < 0) ? UNICODE_COMPARE_LESS : UNICODE_COMPARE_GREATER;
#endif
}

// Enhanced equality comparison function
Item fn_eq_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.long_val == b_item.long_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer == *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.long_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.long_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val == b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val == b_item.bool_val)};
    }
    
    // Fallback to Unicode-enhanced 3-state comparison function
    printf("fn_eq_unicode fallback\n");
    CompResult result = equal_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("equality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_TRUE)};
}

// Enhanced inequality comparison function
Item fn_ne_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.long_val != b_item.long_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer != *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.long_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.long_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val != b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val != b_item.bool_val)};
    }
    
    // Fallback to Unicode-enhanced 3-state comparison function
    printf("fn_ne_unicode fallback\n");
    CompResult result = equal_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("inequality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_FALSE)};
}

// Enhanced less-than comparison function with Unicode string support
Item fn_lt_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val < b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer < *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val < b_val)};
    }
    
    // Enhanced: String comparison now supported with Unicode collation
    if (a_item.type_id == LMD_TYPE_STRING && b_item.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        UnicodeCompareResult result = string_compare_unicode(str_a->chars, str_a->len,
                                                           str_b->chars, str_b->len);
        if (result == UNICODE_COMPARE_ERROR) {
            printf("string comparison error for types: %d, %d\n", a_item.type_id, b_item.type_id);
            return ItemError;
        }
        return {.item = b2it(result == UNICODE_COMPARE_LESS)};
    }
    
    // Error for other non-numeric types
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("less than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    printf("less than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

// Enhanced greater-than comparison function with Unicode string support
Item fn_gt_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val > b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer > *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val > b_val)};
    }
    
    // Enhanced: String comparison now supported with Unicode collation
    if (a_item.type_id == LMD_TYPE_STRING && b_item.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        UnicodeCompareResult result = string_compare_unicode(str_a->chars, str_a->len,
                                                           str_b->chars, str_b->len);
        if (result == UNICODE_COMPARE_ERROR) {
            printf("string comparison error for types: %d, %d\n", a_item.type_id, b_item.type_id);
            return ItemError;
        }
        return {.item = b2it(result == UNICODE_COMPARE_GREATER)};
    }
    
    // Error for other non-numeric types
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("greater than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    printf("greater than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

// Enhanced less-than-or-equal comparison function with Unicode string support
Item fn_le_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val <= b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer <= *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val <= b_val)};
    }
    
    // Enhanced: String comparison now supported with Unicode collation
    if (a_item.type_id == LMD_TYPE_STRING && b_item.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        UnicodeCompareResult result = string_compare_unicode(str_a->chars, str_a->len,
                                                           str_b->chars, str_b->len);
        if (result == UNICODE_COMPARE_ERROR) {
            printf("string comparison error for types: %d, %d\n", a_item.type_id, b_item.type_id);
            return ItemError;
        }
        return {.item = b2it(result == UNICODE_COMPARE_LESS || result == UNICODE_COMPARE_EQUAL)};
    }
    
    // Error for other non-numeric types
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("less than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    printf("less than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

// Enhanced greater-than-or-equal comparison function with Unicode string support
Item fn_ge_unicode(Item a_item, Item b_item) {
    // Fast path for numeric types (unchanged)
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val >= b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer >= *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val >= b_val)};
    }
    
    // Enhanced: String comparison now supported with Unicode collation
    if (a_item.type_id == LMD_TYPE_STRING && b_item.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        UnicodeCompareResult result = string_compare_unicode(str_a->chars, str_a->len,
                                                           str_b->chars, str_b->len);
        if (result == UNICODE_COMPARE_ERROR) {
            printf("string comparison error for types: %d, %d\n", a_item.type_id, b_item.type_id);
            return ItemError;
        }
        return {.item = b2it(result == UNICODE_COMPARE_GREATER || result == UNICODE_COMPARE_EQUAL)};
    }
    
    // Error for other non-numeric types
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("greater than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    printf("greater than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

#else
// Stub implementations when Unicode support is disabled
// These should never be called, but we provide them to avoid linker errors

Item fn_eq_unicode(Item a, Item b) {
    return ItemError;
}

Item fn_ne_unicode(Item a, Item b) {
    return ItemError;
}

Item fn_lt_unicode(Item a, Item b) {
    return ItemError;
}

Item fn_gt_unicode(Item a, Item b) {
    return ItemError;
}

Item fn_le_unicode(Item a, Item b) {
    return ItemError;
}

Item fn_ge_unicode(Item a, Item b) {
    return ItemError;
}

#endif // LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
