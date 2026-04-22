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
    
    // Initialize unlimited-precision context (high but practical precision)
    // mpd_maxcontext has absurdly high precision (10^18) which crashes mpd_pow.
    // Use 200 digits which is far more than needed for any practical computation.
    mpd_maxcontext(&g_unlimited_ctx);
    g_unlimited_ctx.prec = 200;
    
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
    
    new_dec->unlimited = is_unlimited ? 1 : 0;
    new_dec->dec_val = new_dec_val;
    
    Item result;
    result.item = c2it(new_dec);
    return result;
}

// Create a fixed-precision Decimal from a string, arena-allocated.
// The Decimal struct lives in the arena; the mpd_t* is malloc'd by mpdecimal
// (not GC-managed). Safe to use from input parsers where GC heap allocation
// would cause the object to be collected before it can be traced.
Item decimal_from_string_arena(const char* str, void* arena_ptr) {
    if (!str || !arena_ptr) return ItemNull;
    
    Arena* arena = (Arena*)arena_ptr;
    mpd_context_t* ctx = decimal_fixed_context();
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemNull;
    
    uint32_t status = 0;
    mpd_qset_string(dec_val, str, ctx, &status);
    if (status != 0 || mpd_isnan(dec_val) || mpd_isinfinite(dec_val)) {
        mpd_del(dec_val);
        return ItemNull;
    }
    
    Decimal* dec = (Decimal*)arena_alloc(arena, sizeof(Decimal));
    if (!dec) {
        mpd_del(dec_val);
        return ItemNull;
    }
    
    dec->unlimited = 0;
    dec->dec_val = dec_val;
    
    Item result;
    result.item = c2it(dec);
    return result;
}

// Create a fixed-precision Decimal from a double, arena-allocated.
Item decimal_from_double_arena(double val, void* arena_ptr) {
    if (!arena_ptr) return ItemNull;
    
    Arena* arena = (Arena*)arena_ptr;
    mpd_context_t* ctx = decimal_fixed_context();
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemNull;
    
    char str_buf[64];
    snprintf(str_buf, sizeof(str_buf), "%.17g", val);
    
    uint32_t status = 0;
    mpd_qset_string(dec_val, str_buf, ctx, &status);
    if (status != 0) {
        mpd_del(dec_val);
        return ItemNull;
    }
    
    Decimal* dec = (Decimal*)arena_alloc(arena, sizeof(Decimal));
    if (!dec) {
        mpd_del(dec_val);
        return ItemNull;
    }
    
    dec->unlimited = 0;
    dec->dec_val = dec_val;
    
    Item result;
    result.item = c2it(dec);
    return result;
}

// Create a fixed-precision Decimal from an int64, arena-allocated.
Item decimal_from_int64_arena(int64_t val, void* arena_ptr) {
    if (!arena_ptr) return ItemNull;
    
    Arena* arena = (Arena*)arena_ptr;
    mpd_context_t* ctx = decimal_fixed_context();
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemNull;
    
    mpd_set_ssize(dec_val, (mpd_ssize_t)val, ctx);
    
    Decimal* dec = (Decimal*)arena_alloc(arena, sizeof(Decimal));
    if (!dec) {
        mpd_del(dec_val);
        return ItemNull;
    }
    
    dec->unlimited = 0;
    dec->dec_val = dec_val;
    
    Item result;
    result.item = c2it(dec);
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
// weak fallback for heap_alloc - real implementation in lambda-mem.cpp
// This allows lambda-decimal.cpp to be linked into libraries that don't include lambda-mem
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void* heap_alloc(int size, TypeId type_id) {
    (void)size;
    (void)type_id;
    return NULL;  // default stub - real impl in lambda-mem.cpp
}
#else
extern void* heap_alloc(int size, TypeId type_id);
#endif

Decimal* decimal_create(mpd_t* mpd_val) {
    if (!mpd_val) return NULL;
    
    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!decimal) {
        mpd_del(mpd_val);
        return NULL;
    }
    
    decimal->dec_val = mpd_val;
    return decimal;
}

void decimal_retain(Decimal* dec) {
    // no-op: ref counting removed, GC handles lifetime
}

void decimal_release(Decimal* dec) {
    // no-op: ref counting removed, gc_finalize_all_objects handles cleanup
}

// ─────────────────────────────────────────────────────────────────────
// Type Conversion
// ─────────────────────────────────────────────────────────────────────

mpd_t* decimal_item_to_mpd(Item item, mpd_context_t* ctx) {
    TypeId type = item._type_id;
    
    // If already a decimal, make a copy
    if (type == LMD_TYPE_DECIMAL) {
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
    if (item._type_id != LMD_TYPE_DECIMAL) return false;
    Decimal* dec_ptr = item.get_decimal();
    return dec_ptr && dec_ptr->unlimited;
}

bool decimal_is_any(Item item) {
    return item._type_id == LMD_TYPE_DECIMAL;
}

// ─────────────────────────────────────────────────────────────────────
// Helper: Push decimal result
// ─────────────────────────────────────────────────────────────────────

// Helper: Push decimal result
// ─────────────────────────────────────────────────────────────────────

Item decimal_push_result(mpd_t* mpd_val, bool is_unlimited) {
    if (!mpd_val) return ItemError;
    
    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!decimal) {
        mpd_del(mpd_val);
        return ItemError;
    }
    
    decimal->unlimited = is_unlimited ? 1 : 0;
    decimal->dec_val = mpd_val;
    
    Item result;
    result.item = c2it(decimal);
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// Arithmetic Operations
// ─────────────────────────────────────────────────────────────────────

// Helper: determine if result should be unlimited based on operands
static bool should_be_unlimited(Item a, Item b) {
    // Check if either operand is an unlimited decimal
    if (a._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_a = a.get_decimal();
        if (dec_a && dec_a->unlimited) return true;
    }
    if (b._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_b = b.get_decimal();
        if (dec_b && dec_b->unlimited) return true;
    }
    return false;
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

// ─────────────────────────────────────────────────────────────────────
// Rounding Operations (floor, ceil, round, trunc)
// ─────────────────────────────────────────────────────────────────────

Item decimal_floor(Item a, EvalContext* ctx) {
    if (!decimal_is_any(a)) return ItemError;
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    Decimal* dec_ptr = a.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return ItemError;
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) return ItemError;
    
    mpd_floor(result, dec_ptr->dec_val, dec_ctx);
    return decimal_push_result(result, is_unlimited);
}

Item decimal_ceil(Item a, EvalContext* ctx) {
    if (!decimal_is_any(a)) return ItemError;
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    Decimal* dec_ptr = a.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return ItemError;
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) return ItemError;
    
    mpd_ceil(result, dec_ptr->dec_val, dec_ctx);
    return decimal_push_result(result, is_unlimited);
}

Item decimal_round(Item a, EvalContext* ctx) {
    if (!decimal_is_any(a)) return ItemError;
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    Decimal* dec_ptr = a.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return ItemError;
    
    // round to nearest integer using quantize with exponent 0
    mpd_t* one = mpd_new(dec_ctx);
    if (!one) return ItemError;
    mpd_set_string(one, "1", dec_ctx);
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) { mpd_del(one); return ItemError; }
    
    mpd_quantize(result, dec_ptr->dec_val, one, dec_ctx);
    mpd_del(one);
    
    if (mpd_isnan(result)) {
        mpd_del(result);
        return ItemError;
    }
    return decimal_push_result(result, is_unlimited);
}

Item decimal_trunc(Item a, EvalContext* ctx) {
    if (!decimal_is_any(a)) return ItemError;
    bool is_unlimited = decimal_is_unlimited(a);
    mpd_context_t* dec_ctx = is_unlimited ? decimal_unlimited_context() : decimal_fixed_context();
    
    Decimal* dec_ptr = a.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return ItemError;
    
    mpd_t* result = mpd_new(dec_ctx);
    if (!result) return ItemError;
    
    mpd_trunc(result, dec_ptr->dec_val, dec_ctx);
    return decimal_push_result(result, is_unlimited);
}

// Convert decimal Item to int64 (truncates toward zero)
int64_t decimal_to_int64(Item item) {
    if (!decimal_is_any(item)) return 0;
    Decimal* dec_ptr = item.get_decimal();
    if (!dec_ptr || !dec_ptr->dec_val) return 0;
    
    mpd_context_t* dec_ctx = dec_ptr->unlimited ?
        decimal_unlimited_context() : decimal_fixed_context();
    
    // truncate first, then convert
    mpd_t* truncated = mpd_new(dec_ctx);
    if (!truncated) return 0;
    mpd_trunc(truncated, dec_ptr->dec_val, dec_ctx);
    
    int64_t result = mpd_get_ssize(truncated, dec_ctx);
    mpd_del(truncated);
    return result;
}

// ═════════════════════════════════════════════════════════════════════
// BigInt Support — JS BigInt backed by libmpdec integer arithmetic
// ═════════════════════════════════════════════════════════════════════

// BigInt context: high precision for arbitrary-precision integers.
// Separate from decimal's unlimited context to allow independent tuning.
static mpd_context_t g_bigint_ctx;
static bool g_bigint_ctx_initialized = false;

static mpd_context_t* bigint_context() {
    if (!g_bigint_ctx_initialized) {
        mpd_maxcontext(&g_bigint_ctx);
        g_bigint_ctx.prec = 2000;  // ~2000-digit integers
        g_bigint_ctx_initialized = true;
    }
    return &g_bigint_ctx;
}

// helper: allocate Decimal struct on GC heap with DECIMAL tag, wrap mpd_t*
static Item bigint_push_result(mpd_t* mpd_val) {
    if (!mpd_val) return ItemError;
    Decimal* dec = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!dec) { mpd_del(mpd_val); return ItemError; }
    dec->dec_val = mpd_val;
    dec->unlimited = DECIMAL_BIGINT;  // mark as BigInt
    // encode as tagged pointer with LMD_TYPE_DECIMAL tag
    uint64_t enc = ((uint64_t)LMD_TYPE_DECIMAL << 56) | ((uint64_t)dec & 0x00FFFFFFFFFFFFFF);
    return (Item){.item = enc};
}

// helper: extract mpd_t* from BigInt Item
mpd_t* bigint_get_mpd(Item bi) {
    Decimal* dec = (Decimal*)(bi.item & 0x00FFFFFFFFFFFFFF);
    if (!dec) return NULL;
    return dec->dec_val;
}

// ─── Creation ────────────────────────────────────────────────────────

Item bigint_from_int64(int64_t val) {
    mpd_context_t* ctx = bigint_context();
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemError;
    mpd_set_ssize(dec_val, (mpd_ssize_t)val, ctx);
    return bigint_push_result(dec_val);
}

Item bigint_from_double(double val) {
    // must be an exact integer
    if (val != val) return ItemError;  // NaN
    mpd_context_t* ctx = bigint_context();
    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemError;
    char str_buf[64];
    snprintf(str_buf, sizeof(str_buf), "%.0f", val);
    uint32_t status = 0;
    mpd_qset_string(dec_val, str_buf, ctx, &status);
    if (status != 0) { mpd_del(dec_val); return ItemError; }
    // truncate to integer
    mpd_t* truncated = mpd_new(ctx);
    if (!truncated) { mpd_del(dec_val); return ItemError; }
    mpd_trunc(truncated, dec_val, ctx);
    mpd_del(dec_val);
    return bigint_push_result(truncated);
}

Item bigint_from_string(const char* str, int len) {
    if (!str) return ItemError;
    // skip leading/trailing whitespace
    while (len > 0 && (str[0] == ' ' || str[0] == '\t' || str[0] == '\n' || str[0] == '\r')) { str++; len--; }
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) { len--; }
    // empty string (or whitespace-only) → 0n
    if (len == 0) return bigint_from_int64(0);
    mpd_context_t* ctx = bigint_context();

    // handle hex, octal, binary prefixes
    char buf[4096];
    if (len >= (int)sizeof(buf)) return ItemError;
    memcpy(buf, str, len);
    buf[len] = '\0';

    mpd_t* dec_val = mpd_new(ctx);
    if (!dec_val) return ItemError;

    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        // hex: parse manually since mpd doesn't support hex
        // use strtoull for up-to-64-bit, fall back to digit-by-digit for larger
        bool negative = false;
        const char* hex = buf + 2;
        int hex_len = len - 2;
        // each hex digit is 4 bits; if > 16 digits, won't fit in uint64
        if (hex_len <= 16) {
            char* endptr;
            unsigned long long uval = strtoull(hex, &endptr, 16);
            if (*endptr != '\0') { mpd_del(dec_val); return ItemError; }
            mpd_set_u64(dec_val, uval, ctx);
        } else {
            // digit-by-digit: result = result * 16 + digit
            mpd_set_u32(dec_val, 0, ctx);
            mpd_t* sixteen = mpd_new(ctx);
            mpd_t* digit = mpd_new(ctx);
            mpd_t* temp = mpd_new(ctx);
            if (!sixteen || !digit || !temp) {
                if (sixteen) mpd_del(sixteen);
                if (digit) mpd_del(digit);
                if (temp) mpd_del(temp);
                mpd_del(dec_val);
                return ItemError;
            }
            mpd_set_u32(sixteen, 16, ctx);
            for (int i = 0; i < hex_len; i++) {
                char c = hex[i];
                uint32_t d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
                else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
                else { mpd_del(sixteen); mpd_del(digit); mpd_del(temp); mpd_del(dec_val); return ItemError; }
                mpd_mul(temp, dec_val, sixteen, ctx);
                mpd_set_u32(digit, d, ctx);
                mpd_add(dec_val, temp, digit, ctx);
            }
            mpd_del(sixteen); mpd_del(digit); mpd_del(temp);
        }
    } else if (len > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
        // octal
        char* endptr;
        unsigned long long uval = strtoull(buf + 2, &endptr, 8);
        if (*endptr != '\0') { mpd_del(dec_val); return ItemError; }
        mpd_set_u64(dec_val, uval, ctx);
    } else if (len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        // binary
        char* endptr;
        unsigned long long uval = strtoull(buf + 2, &endptr, 2);
        if (*endptr != '\0') { mpd_del(dec_val); return ItemError; }
        mpd_set_u64(dec_val, uval, ctx);
    } else {
        // decimal string — ES spec: only digits allowed (with optional leading +/-)
        // reject decimal points, exponents, Infinity, NaN
        int start = 0;
        if (buf[0] == '+' || buf[0] == '-') start = 1;
        if (start >= len) { mpd_del(dec_val); return ItemError; }
        for (int i = start; i < len; i++) {
            if (buf[i] < '0' || buf[i] > '9') { mpd_del(dec_val); return ItemError; }
        }
        uint32_t status = 0;
        mpd_qset_string(dec_val, buf, ctx, &status);
        if (status != 0) { mpd_del(dec_val); return ItemError; }
    }

    return bigint_push_result(dec_val);
}

// ─── Extraction ──────────────────────────────────────────────────────

int64_t bigint_to_int64(Item bi) {
    mpd_t* m = bigint_get_mpd(bi);
    if (!m) return 0;
    // Use quiet version to avoid SIGFPE trap when value exceeds int64 range
    uint32_t status = 0;
    mpd_ssize_t result = mpd_qget_ssize(m, &status);
    if (status & MPD_Invalid_operation) {
        // value doesn't fit in int64; return clamped
        return mpd_isnegative(m) ? INT64_MIN : INT64_MAX;
    }
    return (int64_t)result;
}

double bigint_to_double(Item bi) {
    mpd_t* m = bigint_get_mpd(bi);
    if (!m) return 0.0;
    char* str = mpd_to_sci(m, 1);
    if (!str) return 0.0;
    double result = strtod(str, NULL);
    mpd_free(str);
    return result;
}

bool bigint_is_zero(Item bi) {
    mpd_t* m = bigint_get_mpd(bi);
    return m && mpd_iszero(m);
}

int bigint_cmp(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return 0;
    return mpd_cmp(ma, mb, bigint_context());
}

int bigint_cmp_double(Item bi, double d) {
    mpd_t* m = bigint_get_mpd(bi);
    if (!m) return 0;
    // convert double to mpd for comparison
    mpd_context_t* ctx = bigint_context();
    mpd_t* d_mpd = mpd_new(ctx);
    if (!d_mpd) return 0;
    char str_buf[64];
    snprintf(str_buf, sizeof(str_buf), "%.17g", d);
    uint32_t status = 0;
    mpd_qset_string(d_mpd, str_buf, ctx, &status);
    if (status != 0) { mpd_del(d_mpd); return 0; }
    int result = mpd_cmp(m, d_mpd, ctx);
    mpd_del(d_mpd);
    return result;
}

// ─── Arithmetic ──────────────────────────────────────────────────────

Item bigint_add(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_add(r, ma, mb, ctx);
    return bigint_push_result(r);
}

Item bigint_sub(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_sub(r, ma, mb, ctx);
    return bigint_push_result(r);
}

Item bigint_mul(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_mul(r, ma, mb, ctx);
    return bigint_push_result(r);
}

Item bigint_div(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    if (mpd_iszero(mb)) return ItemError;  // caller should throw RangeError
    mpd_context_t* ctx = bigint_context();
    // integer division: truncate toward zero
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_t* rem = mpd_new(ctx);
    if (!rem) { mpd_del(r); return ItemError; }
    mpd_divmod(r, rem, ma, mb, ctx);
    mpd_del(rem);
    return bigint_push_result(r);
}

Item bigint_mod(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    if (mpd_iszero(mb)) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_rem(r, ma, mb, ctx);
    return bigint_push_result(r);
}

Item bigint_pow(Item base, Item exp) {
    mpd_t* mb = bigint_get_mpd(base);
    mpd_t* me = bigint_get_mpd(exp);
    if (!mb || !me) return ItemError;
    if (mpd_isnegative(me)) return ItemError;  // caller should throw RangeError
    // 0^0 = 1 (ES spec)
    if (mpd_iszero(me)) return bigint_from_int64(1);
    if (mpd_iszero(mb)) return bigint_from_int64(0);
    mpd_context_t* ctx = bigint_context();
    // dynamically increase precision for large exponents
    mpd_context_t pow_ctx = *ctx;
    int64_t exp_val = mpd_get_ssize(me, ctx);
    if (exp_val > 0) {
        // estimate result digits: base_digits * exp
        mpd_ssize_t base_digits = mb->digits;
        mpd_ssize_t est = base_digits * exp_val + 10;
        if (est > pow_ctx.prec) pow_ctx.prec = est < 100000 ? est : 100000;
    }
    mpd_t* r = mpd_new(&pow_ctx);
    if (!r) return ItemError;
    mpd_pow(r, mb, me, &pow_ctx);
    if (mpd_isnan(r) || mpd_isinfinite(r)) { mpd_del(r); return ItemError; }
    return bigint_push_result(r);
}

Item bigint_neg(Item a) {
    mpd_t* m = bigint_get_mpd(a);
    if (!m) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* r = mpd_new(ctx);
    if (!r) return ItemError;
    mpd_minus(r, m, ctx);
    return bigint_push_result(r);
}

Item bigint_inc(Item a) {
    mpd_t* m = bigint_get_mpd(a);
    if (!m) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* one = mpd_new(ctx);
    if (!one) return ItemError;
    mpd_set_u32(one, 1, ctx);
    mpd_t* r = mpd_new(ctx);
    if (!r) { mpd_del(one); return ItemError; }
    mpd_add(r, m, one, ctx);
    mpd_del(one);
    return bigint_push_result(r);
}

Item bigint_dec(Item a) {
    mpd_t* m = bigint_get_mpd(a);
    if (!m) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* one = mpd_new(ctx);
    if (!one) return ItemError;
    mpd_set_u32(one, 1, ctx);
    mpd_t* r = mpd_new(ctx);
    if (!r) { mpd_del(one); return ItemError; }
    mpd_sub(r, m, one, ctx);
    mpd_del(one);
    return bigint_push_result(r);
}

// ─── Bitwise ─────────────────────────────────────────────────────────

// Arbitrary-precision BigInt bitwise operation following the ES spec:
//   BitwiseOp(op, x, y)
//   1. result = 0, shift = 0
//   2. Repeat until (x == 0 || x == -1) && (y == 0 || y == -1):
//      a. xDigit = x mod 2, yDigit = y mod 2
//      b. result += 2^shift * op(xDigit, yDigit)
//      c. shift++, x = (x - xDigit) / 2, y = (y - yDigit) / 2
//   3. If op(x mod 2, y mod 2) != 0, result -= 2^shift (sign extension)
// op: 0=AND, 1=OR, 2=XOR
static Item bigint_bitwise_op(Item a, Item b, int op) {
    mpd_t* x = bigint_get_mpd(a);
    mpd_t* y = bigint_get_mpd(b);
    if (!x || !y) return ItemError;

    mpd_context_t* ctx = bigint_context();

    // working copies
    mpd_t* xw = mpd_new(ctx);
    mpd_t* yw = mpd_new(ctx);
    mpd_t* result = mpd_new(ctx);
    mpd_t* shift_val = mpd_new(ctx);
    mpd_t* two = mpd_new(ctx);
    mpd_t* neg_one = mpd_new(ctx);
    mpd_t* xdigit = mpd_new(ctx);
    mpd_t* ydigit = mpd_new(ctx);
    if (!xw || !yw || !result || !shift_val || !two || !neg_one || !xdigit || !ydigit) {
        if (xw) mpd_del(xw); if (yw) mpd_del(yw);
        if (result) mpd_del(result); if (shift_val) mpd_del(shift_val);
        if (two) mpd_del(two); if (neg_one) mpd_del(neg_one);
        if (xdigit) mpd_del(xdigit); if (ydigit) mpd_del(ydigit);
        return ItemError;
    }

    mpd_copy(xw, x, ctx);
    mpd_copy(yw, y, ctx);
    mpd_set_i32(result, 0, ctx);
    mpd_set_i32(shift_val, 1, ctx);
    mpd_set_i32(two, 2, ctx);
    mpd_set_ssize(neg_one, -1, ctx);

    // loop until both x and y are 0 or -1
    while (!((mpd_iszero(xw) || mpd_cmp(xw, neg_one, ctx) == 0) &&
             (mpd_iszero(yw) || mpd_cmp(yw, neg_one, ctx) == 0))) {
        // xDigit = x mod 2 (mathematical modulo: always 0 or 1)
        mpd_rem(xdigit, xw, two, ctx);
        if (mpd_isnegative(xdigit) && !mpd_iszero(xdigit))
            mpd_add(xdigit, xdigit, two, ctx);
        // yDigit = y mod 2
        mpd_rem(ydigit, yw, two, ctx);
        if (mpd_isnegative(ydigit) && !mpd_iszero(ydigit))
            mpd_add(ydigit, ydigit, two, ctx);

        int xd = mpd_iszero(xdigit) ? 0 : 1;
        int yd = mpd_iszero(ydigit) ? 0 : 1;
        int bit;
        switch (op) {
            case 0: bit = xd & yd; break;
            case 1: bit = xd | yd; break;
            case 2: bit = xd ^ yd; break;
            default: bit = 0;
        }
        if (bit) mpd_add(result, result, shift_val, ctx);

        mpd_mul(shift_val, shift_val, two, ctx);
        // x = (x - xDigit) / 2
        mpd_sub(xw, xw, xdigit, ctx);
        mpd_divint(xw, xw, two, ctx);
        // y = (y - yDigit) / 2
        mpd_sub(yw, yw, ydigit, ctx);
        mpd_divint(yw, yw, two, ctx);
    }

    // sign extension
    int xmod2 = mpd_iszero(xw) ? 0 : 1;
    int ymod2 = mpd_iszero(yw) ? 0 : 1;
    int sign_bit;
    switch (op) {
        case 0: sign_bit = xmod2 & ymod2; break;
        case 1: sign_bit = xmod2 | ymod2; break;
        case 2: sign_bit = xmod2 ^ ymod2; break;
        default: sign_bit = 0;
    }
    if (sign_bit) mpd_sub(result, result, shift_val, ctx);

    mpd_del(xw); mpd_del(yw); mpd_del(shift_val);
    mpd_del(two); mpd_del(neg_one);
    mpd_del(xdigit); mpd_del(ydigit);
    return bigint_push_result(result);
}

Item bigint_bitwise_and(Item a, Item b) {
    return bigint_bitwise_op(a, b, 0);
}

Item bigint_bitwise_or(Item a, Item b) {
    return bigint_bitwise_op(a, b, 1);
}

Item bigint_bitwise_xor(Item a, Item b) {
    return bigint_bitwise_op(a, b, 2);
}

Item bigint_bitwise_not(Item a) {
    // ~x = -(x + 1) for BigInt (two's complement identity)
    mpd_t* m = bigint_get_mpd(a);
    if (!m) return ItemError;
    mpd_context_t* ctx = bigint_context();
    mpd_t* one = mpd_new(ctx);
    if (!one) return ItemError;
    mpd_set_i32(one, 1, ctx);
    mpd_t* r = mpd_new(ctx);
    if (!r) { mpd_del(one); return ItemError; }
    mpd_add(r, m, one, ctx);
    mpd_minus(r, r, ctx);
    mpd_del(one);
    return bigint_push_result(r);
}

Item bigint_left_shift(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    mpd_context_t* ctx = bigint_context();
    // shift amount must fit in a reasonable range
    uint32_t status = 0;
    mpd_ssize_t shift = mpd_qget_ssize(mb, &status);
    if (status & MPD_Invalid_operation) return ItemError;
    if (shift < 0 || shift > 100000) return ItemError;
    // x << y = x * 2^y
    mpd_t* two = mpd_new(ctx);
    mpd_t* shift_mpd = mpd_new(ctx);
    mpd_t* power = mpd_new(ctx);
    mpd_t* result = mpd_new(ctx);
    if (!two || !shift_mpd || !power || !result) {
        if (two) mpd_del(two); if (shift_mpd) mpd_del(shift_mpd);
        if (power) mpd_del(power); if (result) mpd_del(result);
        return ItemError;
    }
    mpd_set_i32(two, 2, ctx);
    mpd_set_ssize(shift_mpd, shift, ctx);
    mpd_pow(power, two, shift_mpd, ctx);
    mpd_mul(result, ma, power, ctx);
    mpd_del(two); mpd_del(shift_mpd); mpd_del(power);
    return bigint_push_result(result);
}

Item bigint_right_shift(Item a, Item b) {
    mpd_t* ma = bigint_get_mpd(a);
    mpd_t* mb = bigint_get_mpd(b);
    if (!ma || !mb) return ItemError;
    mpd_context_t* ctx = bigint_context();
    uint32_t status = 0;
    mpd_ssize_t shift = mpd_qget_ssize(mb, &status);
    if (status & MPD_Invalid_operation) return ItemError;
    if (shift < 0 || shift > 100000) return ItemError;
    // x >> y = floor(x / 2^y)
    mpd_t* two = mpd_new(ctx);
    mpd_t* shift_mpd = mpd_new(ctx);
    mpd_t* power = mpd_new(ctx);
    mpd_t* result = mpd_new(ctx);
    mpd_t* rem = mpd_new(ctx);
    if (!two || !shift_mpd || !power || !result || !rem) {
        if (two) mpd_del(two); if (shift_mpd) mpd_del(shift_mpd);
        if (power) mpd_del(power); if (result) mpd_del(result);
        if (rem) mpd_del(rem);
        return ItemError;
    }
    mpd_set_i32(two, 2, ctx);
    mpd_set_ssize(shift_mpd, shift, ctx);
    mpd_pow(power, two, shift_mpd, ctx);
    mpd_divint(result, ma, power, ctx);
    // floor division: if negative and remainder != 0, subtract 1
    mpd_rem(rem, ma, power, ctx);
    if (mpd_isnegative(ma) && !mpd_iszero(rem)) {
        mpd_t* one = mpd_new(ctx);
        mpd_set_i32(one, 1, ctx);
        mpd_sub(result, result, one, ctx);
        mpd_del(one);
    }
    mpd_del(two); mpd_del(shift_mpd); mpd_del(power); mpd_del(rem);
    return bigint_push_result(result);
}

// ─── String Conversion ───────────────────────────────────────────────

// returns malloc'd string - caller must free with mpd_free() or free()
char* bigint_to_cstring_radix(Item bi, int radix) {
    mpd_t* m = bigint_get_mpd(bi);
    if (!m) return NULL;

    if (radix == 10) {
        // fast path: use mpd_to_sci
        char* str = mpd_to_sci(m, 1);
        if (!str) return NULL;
        // strip trailing ".0" or decimal point artifacts
        char* dot = strchr(str, '.');
        if (dot) *dot = '\0';
        // strip trailing 'E+0' etc from sci notation
        char* e = strchr(str, 'E');
        if (!e) e = strchr(str, 'e');
        if (e) {
            // has exponent - convert via int64 fallback
            int64_t v = bigint_to_int64(bi);
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)v);
            mpd_free(str);
            return strdup(buf);
        }
        return str; // caller frees with mpd_free()
    }

    // non-10 radix: extract int64 and do manual conversion
    int64_t val = bigint_to_int64(bi);
    bool negative = val < 0;
    uint64_t uval = negative ? (uint64_t)(-val) : (uint64_t)val;

    char buf[256];
    int pos = sizeof(buf) - 1;
    buf[pos] = '\0';
    if (uval == 0) {
        buf[--pos] = '0';
    } else {
        while (uval > 0 && pos > 1) {
            int digit = (int)(uval % radix);
            buf[--pos] = digit < 10 ? ('0' + digit) : ('a' + digit - 10);
            uval /= radix;
        }
    }
    if (negative) buf[--pos] = '-';

    return strdup(buf + pos);
}
