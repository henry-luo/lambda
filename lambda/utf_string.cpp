#include "utf_string.h"

// Only compile utf8proc functions if utf8proc support is enabled at compile time
#ifdef LAMBDA_UTF8PROC_SUPPORT

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <utf8proc.h>

// Global utf8proc state (minimal - utf8proc is mostly stateless)
#ifdef LAMBDA_UTF8PROC_SUPPORT
static bool utf8proc_initialized = false;
#endif

// ASCII detection helper (shared with ICU implementation)
bool is_ascii_string(const char* str, int len) {
    for (int i = 0; i < len; i++) {
        if ((unsigned char)str[i] > 127) {
            return false;
        }
    }
    return true;
}

// UTF-8 validation helper
bool is_valid_utf8(const char* str, int len) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t pos = 0;
    
    while (pos < len) {
        utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)str + pos, 
                                                       len - pos, &codepoint);
        if (bytes_read < 0) {
            return false; // Invalid UTF-8 sequence
        }
        pos += bytes_read;
    }
    return true;
#else
    // Fallback: assume valid (or implement basic UTF-8 validation)
    return true;
#endif
}

// Initialize utf8proc components (called during Lambda engine startup)
void init_utf8proc_support(void) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (utf8proc_initialized) return;
    
    // utf8proc is stateless and doesn't require initialization
    // Just verify the library is working
    const char* version = utf8proc_version();
    if (version) {
        printf("utf8proc Unicode support (version %s) initialized successfully\n", version);
        utf8proc_initialized = true;
    } else {
        printf("utf8proc initialization failed, falling back to ASCII-only comparison\n");
        utf8proc_initialized = false;
    }
#else
    printf("utf8proc support disabled (ASCII-only mode)\n");
#endif
}

// Clean up utf8proc resources
void cleanup_utf8proc_support(void) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) return;
    
    // utf8proc is stateless, no cleanup needed
    utf8proc_initialized = false;
    // printf("utf8proc Unicode support cleaned up\n");
#endif
}

// Unicode normalization functions
char* normalize_utf8proc_nfc(const char* str, int len, int* out_len) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) {
        *out_len = 0;
        return NULL;
    }
    
    // Create null-terminated string for utf8proc
    char* null_term_str = (char*)malloc(len + 1);
    if (!null_term_str) {
        *out_len = 0;
        return NULL;
    }
    memcpy(null_term_str, str, len);
    null_term_str[len] = '\0';
    
    utf8proc_uint8_t* result = utf8proc_NFC((const utf8proc_uint8_t*)null_term_str);
    free(null_term_str);
    
    if (result) {
        *out_len = strlen((const char*)result);
        return (char*)result;
    }
#endif
    *out_len = 0;
    return NULL;
}

char* normalize_utf8proc_nfd(const char* str, int len, int* out_len) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) {
        *out_len = 0;
        return NULL;
    }
    
    // Create null-terminated string for utf8proc
    char* null_term_str = (char*)malloc(len + 1);
    if (!null_term_str) {
        *out_len = 0;
        return NULL;
    }
    memcpy(null_term_str, str, len);
    null_term_str[len] = '\0';
    
    utf8proc_uint8_t* result = utf8proc_NFD((const utf8proc_uint8_t*)null_term_str);
    free(null_term_str);
    
    if (result) {
        *out_len = strlen((const char*)result);
        return (char*)result;
    }
#endif
    *out_len = 0;
    return NULL;
}

char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) {
        *out_len = 0;
        return NULL;
    }
    
    // Create null-terminated string for utf8proc
    char* null_term_str = (char*)malloc(len + 1);
    if (!null_term_str) {
        *out_len = 0;
        return NULL;
    }
    memcpy(null_term_str, str, len);
    null_term_str[len] = '\0';
    
    utf8proc_uint8_t* result = utf8proc_NFKC((const utf8proc_uint8_t*)null_term_str);
    free(null_term_str);
    
    if (result) {
        *out_len = strlen((const char*)result);
        return (char*)result;
    }
#endif
    *out_len = 0;
    return NULL;
}

char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len) {
#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) {
        *out_len = 0;
        return NULL;
    }
    
    // Create null-terminated string for utf8proc
    char* null_term_str = (char*)malloc(len + 1);
    if (!null_term_str) {
        *out_len = 0;
        return NULL;
    }
    memcpy(null_term_str, str, len);
    null_term_str[len] = '\0';
    
    utf8proc_uint8_t* result = utf8proc_NFKD((const utf8proc_uint8_t*)null_term_str);
    free(null_term_str);
    
    if (result) {
        *out_len = strlen((const char*)result);
        return (char*)result;
    }
#endif
    *out_len = 0;
    return NULL;
}

// String collation implementation with different modes
Utf8procCompareResult collate_utf8proc(const char* str1, int len1,
                                      const char* str2, int len2,
                                      Utf8procCollateMode mode) {
#ifdef LAMBDA_ASCII_FAST_PATH
    // Fast path for ASCII-only strings
    if (is_ascii_string(str1, len1) && is_ascii_string(str2, len2)) {
        int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
        if (result == 0) {
            if (len1 < len2) return UTF8PROC_COMPARE_LESS;
            if (len1 > len2) return UTF8PROC_COMPARE_GREATER;
            return UTF8PROC_COMPARE_EQUAL;
        }
        return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
    }
#endif

#ifdef LAMBDA_UTF8PROC_SUPPORT
    if (!utf8proc_initialized) {
        // Fallback to byte comparison if utf8proc not available
        int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
        if (result == 0) {
            if (len1 < len2) return UTF8PROC_COMPARE_LESS;
            if (len1 > len2) return UTF8PROC_COMPARE_GREATER;
            return UTF8PROC_COMPARE_EQUAL;
        }
        return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
    }
    
    switch (mode) {
        case UTF8PROC_COLLATE_BINARY: {
            // Simple byte comparison
            int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
            if (result == 0) {
                if (len1 < len2) return UTF8PROC_COMPARE_LESS;
                if (len1 > len2) return UTF8PROC_COMPARE_GREATER;
                return UTF8PROC_COMPARE_EQUAL;
            }
            return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
        }
        
        case UTF8PROC_COLLATE_NORMALIZED: {
            // NFC normalization + comparison
            int norm_len1, norm_len2;
            char* norm1 = normalize_utf8proc_nfc(str1, len1, &norm_len1);
            char* norm2 = normalize_utf8proc_nfc(str2, len2, &norm_len2);
            
            if (!norm1 || !norm2) {
                if (norm1) free(norm1);
                if (norm2) free(norm2);
                return UTF8PROC_COMPARE_ERROR;
            }
            
            int result = strncmp(norm1, norm2, norm_len1 < norm_len2 ? norm_len1 : norm_len2);
            if (result == 0) {
                if (norm_len1 < norm_len2) result = -1;
                else if (norm_len1 > norm_len2) result = 1;
            }
            
            free(norm1);
            free(norm2);
            
            if (result == 0) return UTF8PROC_COMPARE_EQUAL;
            return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
        }
        
        case UTF8PROC_COLLATE_CASEFOLD: {
            // Case-insensitive comparison using utf8proc casefold
            // Create null-terminated strings for utf8proc
            char* null_str1 = (char*)malloc(len1 + 1);
            char* null_str2 = (char*)malloc(len2 + 1);
            
            if (!null_str1 || !null_str2) {
                if (null_str1) free(null_str1);
                if (null_str2) free(null_str2);
                return UTF8PROC_COMPARE_ERROR;
            }
            
            memcpy(null_str1, str1, len1);
            memcpy(null_str2, str2, len2);
            null_str1[len1] = '\0';
            null_str2[len2] = '\0';
            
            utf8proc_uint8_t* fold1 = utf8proc_NFKC_Casefold((const utf8proc_uint8_t*)null_str1);
            utf8proc_uint8_t* fold2 = utf8proc_NFKC_Casefold((const utf8proc_uint8_t*)null_str2);
            
            free(null_str1);
            free(null_str2);
            
            if (!fold1 || !fold2) {
                if (fold1) free(fold1);
                if (fold2) free(fold2);
                return UTF8PROC_COMPARE_ERROR;
            }
            
            int result = strcmp((const char*)fold1, (const char*)fold2);
            
            free(fold1);
            free(fold2);
            
            if (result == 0) return UTF8PROC_COMPARE_EQUAL;
            return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
        }
        
        default:
            return UTF8PROC_COMPARE_ERROR;
    }
#else
    // Fallback to byte comparison for non-utf8proc builds
    int result = strncmp(str1, str2, len1 < len2 ? len1 : len2);
    if (result == 0) {
        if (len1 < len2) return UTF8PROC_COMPARE_LESS;
        if (len1 > len2) return UTF8PROC_COMPARE_GREATER;
        return UTF8PROC_COMPARE_EQUAL;
    }
    return (result < 0) ? UTF8PROC_COMPARE_LESS : UTF8PROC_COMPARE_GREATER;
#endif
}

// Unicode-aware string relational comparison (replacement for ICU)
Utf8procCompareResult string_compare_utf8proc(const char* str1, int len1, 
                                              const char* str2, int len2) {
    // Use normalized comparison as default for international text
    return collate_utf8proc(str1, len1, str2, len2, UTF8PROC_COLLATE_NORMALIZED);
}

CompResult equal_comp_utf8proc(Item a_item, Item b_item) {
    printf("equal_comp_utf8proc called\n");
    
    if (a_item.type_id != b_item.type_id) {
        // Handle numeric promotion as before
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = a_item.type_id == LMD_TYPE_INT ? a_item.int_val: *(double*)a_item.pointer,
                b_val = b_item.type_id == LMD_TYPE_INT ? b_item.int_val: *(double*)b_item.pointer;
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
        return (a_item.int_val == b_item.int_val) ? COMP_TRUE : COMP_FALSE;
    }
    if (a_item.type_id == LMD_TYPE_INT64) {
        return (*(long*)a_item.pointer == *(long*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    if (a_item.type_id == LMD_TYPE_FLOAT) {
        return (*(double*)a_item.pointer == *(double*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    
    // Enhanced Unicode string comparison using utf8proc
    if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL ||
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        Utf8procCompareResult result = string_compare_utf8proc(str_a->chars, str_a->len,
                                                              str_b->chars, str_b->len);
        if (result == UTF8PROC_COMPARE_ERROR) {
            printf("utf8proc string comparison failed\n");
            return COMP_ERROR;
        }
        
        return (result == UTF8PROC_COMPARE_EQUAL) ? COMP_TRUE : COMP_FALSE;
    }
    
    printf("unknown comparing type %d in equal_comp_utf8proc\n", a_item.type_id);
    return COMP_ERROR;
}

// Stub implementations for relational operators (to be implemented in Phase 4)
Item fn_eq_utf8proc(Item a_item, Item b_item) {
    CompResult result = equal_comp_utf8proc(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("equality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_TRUE)};
}

Item fn_ne_utf8proc(Item a_item, Item b_item) {
    CompResult result = equal_comp_utf8proc(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("inequality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_FALSE)};
}

// Relational operators using utf8proc string comparison (Phase 4)
Item fn_lt_utf8proc(Item a_item, Item b_item) {
    // Handle numeric types first
    if ((a_item.type_id == LMD_TYPE_INT || a_item.type_id == LMD_TYPE_FLOAT) &&
        (b_item.type_id == LMD_TYPE_INT || b_item.type_id == LMD_TYPE_FLOAT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? 
                       a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? 
                       b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val < b_val)};
    }
    
    // Handle string types
    if ((a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL) &&
        (b_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_SYMBOL)) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        Utf8procCompareResult result = string_compare_utf8proc(str_a->chars, str_a->len,
                                                              str_b->chars, str_b->len);
        if (result == UTF8PROC_COMPARE_ERROR) {
            printf("utf8proc string comparison failed in fn_lt\n");
            return ItemError;
        }
        
        return {.item = b2it(result == UTF8PROC_COMPARE_LESS)};
    }
    
    // Error for unsupported type combinations
    printf("less than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

Item fn_gt_utf8proc(Item a_item, Item b_item) {
    // Handle numeric types first
    if ((a_item.type_id == LMD_TYPE_INT || a_item.type_id == LMD_TYPE_FLOAT) &&
        (b_item.type_id == LMD_TYPE_INT || b_item.type_id == LMD_TYPE_FLOAT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? 
                       a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? 
                       b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val > b_val)};
    }
    
    // Handle string types
    if ((a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL) &&
        (b_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_SYMBOL)) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        Utf8procCompareResult result = string_compare_utf8proc(str_a->chars, str_a->len,
                                                              str_b->chars, str_b->len);
        if (result == UTF8PROC_COMPARE_ERROR) {
            printf("utf8proc string comparison failed in fn_gt\n");
            return ItemError;
        }
        
        return {.item = b2it(result == UTF8PROC_COMPARE_GREATER)};
    }
    
    // Error for unsupported type combinations
    printf("greater than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

Item fn_le_utf8proc(Item a_item, Item b_item) {
    // Handle numeric types first
    if ((a_item.type_id == LMD_TYPE_INT || a_item.type_id == LMD_TYPE_FLOAT) &&
        (b_item.type_id == LMD_TYPE_INT || b_item.type_id == LMD_TYPE_FLOAT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? 
                       a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? 
                       b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val <= b_val)};
    }
    
    // Handle string types
    if ((a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL) &&
        (b_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_SYMBOL)) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        Utf8procCompareResult result = string_compare_utf8proc(str_a->chars, str_a->len,
                                                              str_b->chars, str_b->len);
        if (result == UTF8PROC_COMPARE_ERROR) {
            printf("utf8proc string comparison failed in fn_le\n");
            return ItemError;
        }
        
        return {.item = b2it(result == UTF8PROC_COMPARE_LESS || result == UTF8PROC_COMPARE_EQUAL)};
    }
    
    // Error for unsupported type combinations
    printf("less than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

Item fn_ge_utf8proc(Item a_item, Item b_item) {
    // Handle numeric types first
    if ((a_item.type_id == LMD_TYPE_INT || a_item.type_id == LMD_TYPE_FLOAT) &&
        (b_item.type_id == LMD_TYPE_INT || b_item.type_id == LMD_TYPE_FLOAT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? 
                       a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? 
                       b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val >= b_val)};
    }
    
    // Handle string types
    if ((a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL) &&
        (b_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_SYMBOL)) {
        String *str_a = (String*)a_item.pointer;
        String *str_b = (String*)b_item.pointer;
        
        Utf8procCompareResult result = string_compare_utf8proc(str_a->chars, str_a->len,
                                                              str_b->chars, str_b->len);
        if (result == UTF8PROC_COMPARE_ERROR) {
            printf("utf8proc string comparison failed in fn_ge\n");
            return ItemError;
        }
        
        return {.item = b2it(result == UTF8PROC_COMPARE_GREATER || result == UTF8PROC_COMPARE_EQUAL)};
    }
    
    // Error for unsupported type combinations
    printf("greater than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
}

#endif // LAMBDA_UTF8PROC_SUPPORT
