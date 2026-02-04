// lambda/lambda-decimal.cpp - Centralized decimal handling for Lambda
// =====================================================================
// This is the ONLY file that should include <mpdecimal.h>
// All other files should use the API declared in lambda-decimal.hpp

#include "lambda-decimal.hpp"
#include "lambda-data.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include <mpdecimal.h>  // only included here

// ─────────────────────────────────────────────────────────────────────
// Global Contexts
// ─────────────────────────────────────────────────────────────────────

static mpd_context_t g_fixed_ctx;      // 38-digit precision
static mpd_context_t g_unlimited_ctx;  // Max precision
static bool g_initialized = false;

// ─────────────────────────────────────────────────────────────────────
// Context Management
// ─────────────────────────────────────────────────────────────────────

void decimal_init() {
    if (g_initialized) return;
    
    // Initialize fixed-precision context (38 digits, matches Python default)
    mpd_defaultcontext(&g_fixed_ctx);
    
    // Initialize unlimited-precision context (maximum precision)
    mpd_maxcontext(&g_unlimited_ctx);
    
    g_initialized = true;
    log_debug("decimal_init: fixed_prec=%d, unlimited_prec=%d",
              (int)g_fixed_ctx.prec, (int)g_unlimited_ctx.prec);
}

void decimal_cleanup() {
    // Nothing to cleanup for static contexts
    g_initialized = false;
}

mpd_context_t* decimal_fixed_context() {
    if (!g_initialized) decimal_init();
    return &g_fixed_ctx;
}

mpd_context_t* decimal_unlimited_context() {
    if (!g_initialized) decimal_init();
    return &g_unlimited_ctx;
}

// ─────────────────────────────────────────────────────────────────────
// Parsing
// ─────────────────────────────────────────────────────────────────────

mpd_t* decimal_parse_str(const char* str, mpd_context_t* ctx) {
    if (!str || !ctx) return NULL;
    
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) {
        log_error("decimal_parse_str: failed to allocate mpd_t");
        return NULL;
    }
    
    uint32_t status = 0;
    mpd_qset_string(dec_val, str, ctx, &status);
    
    if (status != 0) {
        log_error("decimal_parse_str: failed to parse '%s' (status: %u)", str, status);
        mpd_del(dec_val);
        return NULL;
    }
    
    return dec_val;
}

mpd_t* decimal_parse_fixed_str(const char* str) {
    return decimal_parse_str(str, decimal_fixed_context());
}

mpd_t* decimal_parse_unlimited_str(const char* str) {
    return decimal_parse_str(str, decimal_unlimited_context());
}

// ─────────────────────────────────────────────────────────────────────
// Forward Declarations
// ─────────────────────────────────────────────────────────────────────

Item decimal_push_result(mpd_t* mpd_val, bool is_unlimited);

// ─────────────────────────────────────────────────────────────────────
// Item Creation (higher-level API)
// ─────────────────────────────────────────────────────────────────────

Item decimal_from_int64(int64_t val, EvalContext* ctx) {
    mpd_context_t* dec_ctx = ctx ? ctx->decimal_ctx : decimal_fixed_context();
    mpd_t* dec_val = mpd_new(dec_ctx);
    if (!dec_val) return ItemError;
    
    mpd_set_ssize(dec_val, val, dec_ctx);
    return decimal_push_result(dec_val, false);  // fixed precision
}

Item decimal_from_double(double val, EvalContext* ctx) {
    mpd_context_t* dec_ctx = ctx ? ctx->decimal_ctx : decimal_fixed_context();
    mpd_t* dec_val = mpd_new(dec_ctx);
    if (!dec_val) return ItemError;
    
    char str_buf[64];
    snprintf(str_buf, sizeof(str_buf), "%.17g", val);
    
    uint32_t status = 0;
    mpd_qset_string(dec_val, str_buf, dec_ctx, &status);
    if (status != 0) {
        mpd_del(dec_val);
        return ItemError;
    }
    
    return decimal_push_result(dec_val, false);  // fixed precision
}

Item decimal_from_string(const char* str, EvalContext* ctx) {
    if (!str) return ItemError;
    
    mpd_context_t* dec_ctx = ctx ? ctx->decimal_ctx : decimal_fixed_context();
    mpd_t* dec_val = mpd_new(dec_ctx);
    if (!dec_val) return ItemError;
    
    uint32_t status = 0;
    mpd_qset_string(dec_val, str, dec_ctx, &status);
    
    if (status != 0 || mpd_isnan(dec_val) || mpd_isinfinite(dec_val)) {
        mpd_del(dec_val);
        return ItemError;
    }
    
    return decimal_push_result(dec_val, false);  // fixed precision
}

void decimal_free_string(char* str) {
    if (str) mpd_free(str);
}

// Deep copy a decimal Item (for arena allocation in MarkBuilder)
#include "../lib/arena.h"
extern mpd_context_t* InputManager_decimal_context();  // forward declare

Item decimal_deep_copy(Item item, void* arena_ptr, bool is_unlimited) {
    if (!decimal_is_any(item)) return ItemNull;
    
    Decimal* src_dec = item.get_decimal();
    if (!src_dec || !src_dec->dec_val) return ItemNull;
    
    Arena* arena = (Arena*)arena_ptr;
    mpd_context_t* ctx = decimal_fixed_context();  // use global context
    
    // Create new mpd_t and copy the value
    mpd_t* new_dec_val = mpd_new(ctx);
    if (!new_dec_val) return ItemNull;
    
    // Copy using qcopy
    uint32_t status = 0;
    mpd_qcopy(new_dec_val, src_dec->dec_val, &status);
    if (status != 0) {
        mpd_del(new_dec_val);
        return ItemNull;
    }
    
    // Allocate Decimal struct in arena
    Decimal* new_dec = (Decimal*)arena_alloc(arena, sizeof(Decimal));
    if (!new_dec) {
        mpd_del(new_dec_val);
        return ItemNull;
    }
    
    new_dec->ref_cnt = 1;
    new_dec->dec_val = new_dec_val;
    
    Item result;
    if (is_unlimited) {
        result.item = c2it_big(new_dec);
    } else {
        result.item = c2it(new_dec);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// Formatting
// ─────────────────────────────────────────────────────────────────────

void decimal_print(StrBuf* strbuf, Decimal* decimal) {
    if (!decimal || !decimal->dec_val) {
        strbuf_append_str(strbuf, "error");
        return;
    }
    
    // Use libmpdec to format - no truncation per design decision
    char* decimal_str = mpd_to_sci(decimal->dec_val, 1);  // scientific notation
    if (!decimal_str) {
        strbuf_append_str(strbuf, "error");
        return;
    }
    
    strbuf_append_str(strbuf, decimal_str);
    mpd_free(decimal_str);
}

void decimal_big_print(StrBuf* strbuf, Decimal* decimal) {
    // Same implementation - no truncation for either type
    decimal_print(strbuf, decimal);
}

// ─────────────────────────────────────────────────────────────────────
// Memory Management  
// ─────────────────────────────────────────────────────────────────────

// External heap allocation function
// We provide a weak default that returns NULL for libraries that don't link heap.
// The actual implementation in lambda-mem.cpp will override this.
#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void* heap_alloc(int size, TypeId type_id) {
    (void)size;
    (void)type_id;
    return NULL;  // default stub - real impl in lambda-mem.cpp
}
#else
extern void* heap_alloc(int size, TypeId type_id);
#endif

#ifdef __cplusplus
}
#endif

Decimal* decimal_create(mpd_t* mpd_val) {
    if (!mpd_val) return NULL;
    
    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!decimal) {
        mpd_del(mpd_val);
        return NULL;
    }
    
    decimal->ref_cnt = 1;
    decimal->dec_val = mpd_val;
    return decimal;
}

void decimal_retain(Decimal* dec) {
    if (dec && dec->ref_cnt < UINT16_MAX) {
        dec->ref_cnt++;
    }
}

void decimal_release(Decimal* dec) {
    if (!dec) return;
    
    if (dec->ref_cnt > 0) {
        dec->ref_cnt--;
    }
    
    if (dec->ref_cnt == 0 && dec->dec_val) {
        mpd_del(dec->dec_val);
        dec->dec_val = NULL;
        // Note: heap memory is freed by garbage collector
    }
}

// ─────────────────────────────────────────────────────────────────────
// Type Conversion
// ─────────────────────────────────────────────────────────────────────

mpd_t* decimal_item_to_mpd(Item item, mpd_context_t* ctx) {
    TypeId type = item._type_id;
    
    // If already a decimal, make a copy
    if (type == LMD_TYPE_DECIMAL || type == LMD_TYPE_DECIMAL_BIG) {
        Decimal* dec_ptr = item.get_decimal();
        if (!dec_ptr || !dec_ptr->dec_val) return NULL;
        
        mpd_t* copy = mpd_new(ctx);
        if (!copy) return NULL;
        mpd_copy(copy, dec_ptr->dec_val, ctx);
        return copy;
    }
    
    mpd_t* result = mpd_new(ctx);
    if (!result) return NULL;
    
    if (type == LMD_TYPE_INT) {
        mpd_set_ssize(result, item.get_int56(), ctx);
    }
    else if (type == LMD_TYPE_INT64) {
        mpd_set_ssize(result, item.get_int64(), ctx);
    }
    else if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", val);
        uint32_t status = 0;
        mpd_qset_string(result, str_buf, ctx, &status);
        if (status != 0) {
            mpd_del(result);
            return NULL;
        }
    }
    else {
        mpd_del(result);
        return NULL;
    }
    
    return result;
}

int64_t decimal_mpd_to_int64(mpd_t* dec, mpd_context_t* ctx) {
    if (!dec) return 0;
    return mpd_get_ssize(dec, ctx);
}

double decimal_mpd_to_double(mpd_t* dec, mpd_context_t* ctx) {
    if (!dec) return 0.0;
    
    char* str = mpd_to_sci(dec, 1);
    if (!str) return 0.0;
    
    double result = strtod(str, NULL);
    mpd_free(str);
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// Predicates
// ─────────────────────────────────────────────────────────────────────

bool decimal_is_zero(mpd_t* dec) {
    return dec && mpd_iszero(dec);
}

bool decimal_is_unlimited(Item item) {
    return item._type_id == LMD_TYPE_DECIMAL_BIG;
}

bool decimal_is_any(Item item) {
    return item._type_id == LMD_TYPE_DECIMAL || item._type_id == LMD_TYPE_DECIMAL_BIG;
}

// ─────────────────────────────────────────────────────────────────────
// Helper: Push decimal result
// ─────────────────────────────────────────────────────────────────────

// Macro to convert decimal pointer to Item with appropriate type
#define c2it_big(decimal_ptr) ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL_BIG)<<56) | (uint64_t)(decimal_ptr)): null)

Item decimal_push_result(mpd_t* mpd_val, bool is_unlimited) {
    if (!mpd_val) return ItemError;
    
    TypeId type_id = is_unlimited ? LMD_TYPE_DECIMAL_BIG : LMD_TYPE_DECIMAL;
    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), type_id);
    if (!decimal) {
        mpd_del(mpd_val);
        return ItemError;
    }
    
    decimal->ref_cnt = 1;
    decimal->dec_val = mpd_val;
    
    Item result;
    if (is_unlimited) {
        result.item = c2it_big(decimal);
    } else {
        result.item = c2it(decimal);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// Arithmetic Operations
// ─────────────────────────────────────────────────────────────────────

// Helper: determine if result should be unlimited based on operands
static bool should_be_unlimited(Item a, Item b) {
    return a._type_id == LMD_TYPE_DECIMAL_BIG || b._type_id == LMD_TYPE_DECIMAL_BIG;
}

// Helper: get appropriate context based on operand types
static mpd_context_t* get_decimal_context(Item a, Item b) {
    return should_be_unlimited(a, b) ? decimal_unlimited_context() : decimal_fixed_context();
}

// Helper: cleanup temporary decimal if it was created (not the original)
static void cleanup_temp(mpd_t* dec, bool was_decimal) {
    if (!was_decimal && dec) {
        mpd_del(dec);
    }
}

Item decimal_add(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_add: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_add(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result) || mpd_isinfinite(result)) {
        mpd_del(result);
        log_error("decimal_add: result is NaN or infinite");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_sub(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_sub: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_sub(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result) || mpd_isinfinite(result)) {
        mpd_del(result);
        log_error("decimal_sub: result is NaN or infinite");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_mul(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_mul: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_mul(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result) || mpd_isinfinite(result)) {
        mpd_del(result);
        log_error("decimal_mul: result is NaN or infinite");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_div(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_div: conversion failed");
        return ItemError;
    }
    
    // Check for division by zero
    if (mpd_iszero(b_dec)) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_div: division by zero");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_div(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result) || mpd_isinfinite(result)) {
        mpd_del(result);
        log_error("decimal_div: result is NaN or infinite");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_mod(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_mod: conversion failed");
        return ItemError;
    }
    
    // Check for division by zero
    if (mpd_iszero(b_dec)) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_mod: division by zero");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_rem(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result)) {
        mpd_del(result);
        log_error("decimal_mod: result is NaN");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_pow(Item a, Item b, EvalContext* ctx) {
    bool is_unlimited = should_be_unlimited(a, b);
    mpd_context_t* dec_ctx = get_decimal_context(a, b);
    
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, dec_ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        log_error("decimal_pow: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) cleanup_temp(a_dec, false);
        if (!b_is_dec) cleanup_temp(b_dec, false);
        return ItemError;
    }
    
    mpd_pow(result, a_dec, b_dec, dec_ctx);
    
    if (!a_is_dec) cleanup_temp(a_dec, false);
    if (!b_is_dec) cleanup_temp(b_dec, false);
    
    if (mpd_isnan(result) || mpd_isinfinite(result)) {
        mpd_del(result);
        log_error("decimal_pow: result is NaN or infinite");
        return ItemError;
    }
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_neg(Item a, EvalContext* ctx) {
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    bool a_is_dec = decimal_is_any(a);
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    
    if (!a_dec) {
        log_error("decimal_neg: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) mpd_del(a_dec);
        return ItemError;
    }
    
    mpd_minus(result, a_dec, dec_ctx);
    
    if (!a_is_dec) mpd_del(a_dec);
    
    return decimal_push_result(result, is_unlimited);
}

Item decimal_abs(Item a, EvalContext* ctx) {
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    bool a_is_dec = decimal_is_any(a);
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, dec_ctx);
    
    if (!a_dec) {
        log_error("decimal_abs: conversion failed");
        return ItemError;
    }
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) {
        if (!a_is_dec) mpd_del(a_dec);
        return ItemError;
    }
    
    mpd_abs(result, a_dec, dec_ctx);
    
    if (!a_is_dec) mpd_del(a_dec);
    
    return decimal_push_result(result, is_unlimited);
}

// ─────────────────────────────────────────────────────────────────────
// Comparison
// ─────────────────────────────────────────────────────────────────────

int decimal_cmp(Item a, Item b, mpd_context_t* ctx) {
    bool a_is_dec = decimal_is_any(a);
    bool b_is_dec = decimal_is_any(b);
    
    mpd_t* a_dec = a_is_dec ? a.get_decimal()->dec_val : decimal_item_to_mpd(a, ctx);
    mpd_t* b_dec = b_is_dec ? b.get_decimal()->dec_val : decimal_item_to_mpd(b, ctx);
    
    if (!a_dec || !b_dec) {
        if (!a_is_dec && a_dec) mpd_del(a_dec);
        if (!b_is_dec && b_dec) mpd_del(b_dec);
        return 0;  // error case, treat as equal
    }
    
    int result = mpd_cmp(a_dec, b_dec, ctx);
    
    if (!a_is_dec) mpd_del(a_dec);
    if (!b_is_dec) mpd_del(b_dec);
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// Item-level Comparison (no mpd_context_t* in signature)
// ─────────────────────────────────────────────────────────────────────

int decimal_cmp_items(Item a, Item b) {
    mpd_context_t* ctx = decimal_is_unlimited(a) || decimal_is_unlimited(b) 
        ? decimal_unlimited_context() 
        : decimal_fixed_context();
    return decimal_cmp(a, b, ctx);
}

// ─────────────────────────────────────────────────────────────────────
// Item-level Predicates  
// ─────────────────────────────────────────────────────────────────────

bool decimal_item_is_zero(Item item) {
    if (!decimal_is_any(item)) return false;
    Decimal* dec_ptr = item.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return false;
    return mpd_iszero(dec_ptr->dec_val);
}

// ─────────────────────────────────────────────────────────────────────
// Conversion helpers
// ─────────────────────────────────────────────────────────────────────

double decimal_to_double(Item item) {
    if (!decimal_is_any(item)) return 0.0;
    Decimal* dec_ptr = item.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return 0.0;
    
    char* str = mpd_to_sci(dec_ptr->dec_val, 1);
    if (!str) return 0.0;
    
    double result = strtod(str, NULL);
    mpd_free(str);
    return result;
}

char* decimal_to_string(Item item) {
    if (!decimal_is_any(item)) return NULL;
    Decimal* dec_ptr = item.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return NULL;
    return mpd_to_sci(dec_ptr->dec_val, 1);  // caller must free with decimal_free_string
}

char* decimal_to_string(Decimal* decimal) {
    if (!decimal || !decimal->dec_val) return NULL;
    return mpd_to_sci(decimal->dec_val, 1);  // caller must free with decimal_free_string
}
