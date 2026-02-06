// lambda/lambda-decimal.hpp - Centralized decimal handling for Lambda
// =====================================================================
// This module handles all decimal operations including:
// - Fixed-precision decimals (38 digits, suffix 'n')
// - Unlimited-precision decimals (arbitrary precision, suffix 'N')
#pragma once

// Forward declarations for mpdecimal types - mpdecimal.h is only included in lambda-decimal.cpp
typedef struct mpd_context_t mpd_context_t;
typedef struct mpd_t mpd_t;

#include <cstdint>
#include <cstddef>
#include "../lib/strbuf.h"

// Forward declarations
struct Decimal;
struct String;
struct Item;
struct EvalContext;
typedef uint8_t TypeId;

// ─────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────

// Fixed decimal precision (38 digits - matches mpd_defaultcontext)
#define DECIMAL_FIXED_PRECISION 38

// ─────────────────────────────────────────────────────────────────────
// Context Management
// ─────────────────────────────────────────────────────────────────────

// Initialize decimal subsystem (call once at startup)
void decimal_init();

// Cleanup decimal subsystem (call at shutdown)
void decimal_cleanup();

// Get the fixed-precision context (38 digits)
mpd_context_t* decimal_fixed_context();

// Get the unlimited-precision context
mpd_context_t* decimal_unlimited_context();

// ─────────────────────────────────────────────────────────────────────
// Parsing (from string literals)
// ─────────────────────────────────────────────────────────────────────

// Parse string to mpd_t* using specified context
// Returns NULL on parse error
mpd_t* decimal_parse_str(const char* str, mpd_context_t* ctx);

// Parse "123.45n" → fixed decimal (uses fixed context)
mpd_t* decimal_parse_fixed_str(const char* str);

// Parse "123.45N" → unlimited decimal (uses unlimited context)
mpd_t* decimal_parse_unlimited_str(const char* str);

// ─────────────────────────────────────────────────────────────────────
// Item Creation (higher-level API that returns Item directly)
// ─────────────────────────────────────────────────────────────────────

// Create fixed decimal Item from int64
Item decimal_from_int64(int64_t val, EvalContext* ctx);

// Create fixed decimal Item from double
Item decimal_from_double(double val, EvalContext* ctx);

// Create fixed decimal Item from string (returns ItemError if parse fails)
Item decimal_from_string(const char* str, EvalContext* ctx);

// Free mpdecimal string (wrapper for mpd_free)
void decimal_free_string(char* str);

// Deep copy a decimal Item (for arena allocation in MarkBuilder)
// Uses the provided arena or heap depending on allocation mode
Item decimal_deep_copy(Item item, void* arena, bool is_unlimited);

// ─────────────────────────────────────────────────────────────────────
// Formatting (to string)
// ─────────────────────────────────────────────────────────────────────

// Format decimal to string buffer (no truncation)
void decimal_print(StrBuf* strbuf, Decimal* decimal);

// Format unlimited decimal to string buffer (no truncation)
void decimal_big_print(StrBuf* strbuf, Decimal* decimal);

// ─────────────────────────────────────────────────────────────────────
// Memory Management
// ─────────────────────────────────────────────────────────────────────

// Allocate and initialize a new Decimal from mpd_t* (takes ownership of mpd_val)
Decimal* decimal_create(mpd_t* mpd_val);

// Increment reference count
void decimal_retain(Decimal* dec);

// Decrement reference count, free if zero
void decimal_release(Decimal* dec);

// ─────────────────────────────────────────────────────────────────────
// Type Conversion
// ─────────────────────────────────────────────────────────────────────

// Convert any numeric Item to mpd_t* (caller owns result, must free with mpd_del)
// Returns NULL on conversion error
// If item is already decimal, returns a COPY (caller must free)
mpd_t* decimal_item_to_mpd(Item item, mpd_context_t* ctx);

// Convert mpd_t* to int64 (truncates toward zero)
int64_t decimal_mpd_to_int64(mpd_t* dec, mpd_context_t* ctx);

// Convert mpd_t* to double
double decimal_mpd_to_double(mpd_t* dec, mpd_context_t* ctx);

// ─────────────────────────────────────────────────────────────────────
// Arithmetic Operations
// ─────────────────────────────────────────────────────────────────────

// All arithmetic operations:
// - Use the appropriate context based on operand types
// - If either operand has the unlimited flag set, result is unlimited
// - Otherwise result is fixed precision

// Addition: a + b
Item decimal_add(Item a, Item b, EvalContext* ctx);

// Subtraction: a - b
Item decimal_sub(Item a, Item b, EvalContext* ctx);

// Multiplication: a * b
Item decimal_mul(Item a, Item b, EvalContext* ctx);

// Division: a / b (returns error on division by zero)
Item decimal_div(Item a, Item b, EvalContext* ctx);

// Modulo: a % b
Item decimal_mod(Item a, Item b, EvalContext* ctx);

// Power: a ^ b
Item decimal_pow(Item a, Item b, EvalContext* ctx);

// Negation: -a
Item decimal_neg(Item a, EvalContext* ctx);

// Absolute value: |a|
Item decimal_abs(Item a, EvalContext* ctx);

// ─────────────────────────────────────────────────────────────────────
// Comparison
// ─────────────────────────────────────────────────────────────────────

// Compare two decimal items: returns -1, 0, or 1
// Uses appropriate context based on operand types
int decimal_cmp_items(Item a, Item b);

// ─────────────────────────────────────────────────────────────────────
// Predicates
// ─────────────────────────────────────────────────────────────────────

// Check if decimal item is zero
bool decimal_item_is_zero(Item item);

// Check if mpd_t* is zero
bool decimal_is_zero(mpd_t* dec);

// Check if item is unlimited decimal type
bool decimal_is_unlimited(Item item);

// Check if item is any decimal type (fixed or unlimited)
bool decimal_is_any(Item item);

// ─────────────────────────────────────────────────────────────────────
// Conversion helpers (for integration with existing code)
// ─────────────────────────────────────────────────────────────────────

// Convert decimal Item to double
double decimal_to_double(Item item);

// Convert decimal Item to string (caller must free with decimal_free_string)
char* decimal_to_string(Item item);

// Convert Decimal* to string (caller must free with decimal_free_string)
char* decimal_to_string(Decimal* decimal);
