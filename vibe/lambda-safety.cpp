// Enhanced string operations with bounds checking
// Replace the unsafe strcpy calls in lambda-eval.cpp

#include "lambda.h"
#include "../lib/log.h"

// Safe string copy with bounds checking
static void safe_string_copy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        log_error("safe_string_copy: invalid parameters");
        return;
    }
    
    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

// Enhanced string creation with overflow protection
String* create_string_safe(const char* buffer, size_t buffer_len) {
    if (!buffer) {
        log_error("create_string_safe: null buffer");
        return NULL;
    }
    
    // Validate buffer length is reasonable
    if (buffer_len > MAX_STRING_LENGTH) {
        log_error("create_string_safe: buffer too large (%zu bytes)", buffer_len);
        return NULL;
    }
    
    String* str = (String*)heap_alloc(sizeof(String) + buffer_len + 1, LMD_TYPE_STRING);
    if (!str) {
        log_error("create_string_safe: allocation failed");
        return NULL;
    }
    
    str->len = buffer_len;
    str->ref_cnt = 0;
    
    // Use safe copy instead of strcpy
    safe_string_copy(str->chars, buffer, buffer_len + 1);
    
    return str;
}

// Enhanced array access with bounds checking
Item array_get_safe(Array* array, long index) {
    if (!array) {
        log_error("array_get_safe: null array");
        return ItemError;
    }
    
    // Check for array corruption
    if (array->length < 0 || array->length > MAX_ARRAY_LENGTH) {
        log_error("array_get_safe: corrupted array (length: %ld)", array->length);
        return ItemError;
    }
    
    if (index < 0) {
        log_warn("array_get_safe: negative index %ld", index);
        return ItemNull;
    }
    
    if (index >= array->length) {
        log_warn("array_get_safe: index %ld out of bounds (length: %ld)", index, array->length);
        return ItemNull;
    }
    
    return array->items[index];
}

// Memory pool pointer validation
bool is_valid_pool_pointer(VariableMemPool* pool, void* ptr) {
    if (!pool || !ptr) {
        return false;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    
    // Check for obviously invalid addresses
    if (addr < 0x1000 || addr == 0x6e6120646c6f6230ULL) {
        log_debug("is_valid_pool_pointer: suspicious address 0x%lx", addr);
        return false;
    }
    
    // Check alignment (optional but good practice)
    if (addr % alignof(max_align_t) != 0) {
        log_debug("is_valid_pool_pointer: misaligned address 0x%lx", addr);
        return false;
    }
    
    // Verify pointer is within pool buffers
    return buffer_list_find(pool->buff_head, ptr) != NULL;
}

// Enhanced memory pool free with validation
MemPoolError pool_variable_free_safe(VariableMemPool* pool, void* ptr) {
    if (!pool) {
        log_error("pool_variable_free_safe: null pool");
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    }
    
    if (!ptr) {
        log_warn("pool_variable_free_safe: attempting to free null pointer");
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    }
    
    // Validate pointer before attempting to free
    if (!is_valid_pool_pointer(pool, ptr)) {
        log_error("pool_variable_free_safe: invalid pointer %p", ptr);
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    }
    
    return pool_variable_free(pool, ptr);
}

// Input validation for file paths
bool is_safe_file_path(const char* path) {
    if (!path) {
        return false;
    }
    
    size_t path_len = strlen(path);
    if (path_len == 0 || path_len > MAX_PATH_LENGTH) {
        return false;
    }
    
    // Check for path traversal attempts
    if (strstr(path, "../") || strstr(path, "..\\")) {
        log_warn("is_safe_file_path: path traversal attempt in '%s'", path);
        return false;
    }
    
    // Check for absolute paths (depending on security policy)
    if (path[0] == '/' || (path_len >= 3 && path[1] == ':' && path[2] == '\\')) {
        log_warn("is_safe_file_path: absolute path not allowed '%s'", path);
        return false;
    }
    
    // Check for special characters that might be dangerous
    const char* dangerous_chars = "<>|\"*?";
    for (const char* dc = dangerous_chars; *dc; dc++) {
        if (strchr(path, *dc)) {
            log_warn("is_safe_file_path: dangerous character '%c' in path '%s'", *dc, path);
            return false;
        }
    }
    
    return true;
}

// Parsing context for limiting resource usage
typedef struct {
    size_t operation_count;
    size_t current_depth;
    size_t max_depth;
    bool should_abort;
    clock_t start_time;
} ParseContext;

ParseContext* parse_context_create(size_t max_depth) {
    ParseContext* ctx = (ParseContext*)calloc(1, sizeof(ParseContext));
    if (ctx) {
        ctx->max_depth = max_depth ? max_depth : MAX_PARSING_DEPTH;
        ctx->start_time = clock();
    }
    return ctx;
}

bool parse_context_check_limits(ParseContext* ctx) {
    if (!ctx) return false;
    
    ctx->operation_count++;
    
    // Check operation count limit
    if (ctx->operation_count > MAX_PARSING_OPERATIONS) {
        log_error("parse_context_check_limits: operation limit exceeded (%zu)", ctx->operation_count);
        ctx->should_abort = true;
        return false;
    }
    
    // Check depth limit
    if (ctx->current_depth > ctx->max_depth) {
        log_error("parse_context_check_limits: depth limit exceeded (%zu)", ctx->current_depth);
        ctx->should_abort = true;
        return false;
    }
    
    // Check time limit (30 seconds default)
    clock_t elapsed = clock() - ctx->start_time;
    if (elapsed > MAX_PARSING_TIME * CLOCKS_PER_SEC) {
        log_error("parse_context_check_limits: time limit exceeded");
        ctx->should_abort = true;
        return false;
    }
    
    return true;
}

void parse_context_enter_depth(ParseContext* ctx) {
    if (ctx) {
        ctx->current_depth++;
    }
}

void parse_context_exit_depth(ParseContext* ctx) {
    if (ctx && ctx->current_depth > 0) {
        ctx->current_depth--;
    }
}

void parse_context_destroy(ParseContext* ctx) {
    if (ctx) {
        free(ctx);
    }
}
