# Lambda Decimal Enhancement Proposal

**Author**: Lambda Team  
**Date**: February 4, 2026  
**Status**: Proposal  
**Scope**: Decimal type semantics, syntax enhancement, code consolidation

---

## Executive Summary

This proposal enhances Lambda's decimal support by:
1. Introducing dual decimal types: **fixed-precision** (`123n`) and **unlimited-precision** (`123N`)
2. Consolidating all decimal handling code into a single `lambda-decimal.cpp` module
3. Maintaining backward compatibility with existing decimal syntax

---

## 1. Decimal Support in Other Languages

### 1.1 Survey of Decimal Implementations

| Language | Type | Precision | Syntax | Notes |
|----------|------|-----------|--------|-------|
| **Python** | `decimal.Decimal` | Configurable (default 28, max ~10⁹) | `Decimal("123.45")` | Uses libmpdec (same as Lambda) |
| **Java** | `BigDecimal` | Unlimited | `new BigDecimal("123.45")` | Arbitrary precision, immutable |
| **C#** | `decimal` | 28-29 digits (128-bit) | `123.45m` | Fixed precision, native type |
| **Ruby** | `BigDecimal` | Unlimited | `BigDecimal("123.45")` | Arbitrary precision |
| **Haskell** | `Rational` | Unlimited | `123.45` (with pragma) | Exact rational arithmetic |
| **Go** | `big.Float` / `big.Rat` | Configurable | Constructor call | Standard library types |
| **SQL** | `DECIMAL(p,s)` | Typically 38 max | `123.45` | Database-dependent |
| **JavaScript** | None native | N/A | N/A | Uses `BigInt` for integers only |
| **Rust** | `rust_decimal` (crate) | 28-29 digits | Constructor | Third-party library |

### 1.2 Language Design Patterns

**Pattern A: Fixed Precision with Literal Suffix (C#, F#)**
```csharp
decimal price = 19.99m;  // 'm' suffix for decimal literal
```

**Pattern B: Unlimited Precision via Constructor (Java, Ruby, Python)**
```java
BigDecimal value = new BigDecimal("123.456789012345678901234567890");
```

**Pattern C: Configurable Context (Python)**
```python
from decimal import Decimal, getcontext
getcontext().prec = 50  # Set precision for subsequent operations
value = Decimal("3.14159265358979323846264338327950288")
```

---

## 2. Fixed vs Unlimited Decimal: Trade-offs

### 2.1 Performance Comparison

| Aspect | Fixed (38 digits) | Unlimited |
|--------|-------------------|-----------|
| **Memory per value** | 24-32 bytes | Variable (grows with digits) |
| **Addition** | O(1) ~5 ns | O(n) ~15-1000+ ns |
| **Multiplication** | O(1) ~20 ns | O(n²) to O(n log n) |
| **Division** | O(1) ~50 ns | O(n²) to O(n log n) |
| **Cache efficiency** | Excellent | Degrades with size |
| **Memory allocation** | Predictable | Dynamic, may fragment |
| **Latency variance** | Low | High (can spike) |

### 2.2 Benchmark Estimates

| Operation | 38-digit | 100-digit | 1,000-digit | 10,000-digit |
|-----------|----------|-----------|-------------|--------------|
| Add | ~5 ns | ~15 ns | ~100 ns | ~1 μs |
| Multiply | ~20 ns | ~200 ns | ~10 μs | ~500 μs |
| Divide | ~50 ns | ~500 ns | ~50 μs | ~5 ms |
| Memory | 32 B | ~64 B | ~500 B | ~5 KB |

### 2.3 Use Case Guidance

| Use Case | Recommended Type | Rationale |
|----------|------------------|-----------|
| Financial calculations | Fixed (`n`) | Predictable, fast, 38 digits exceeds any currency need |
| Scientific computation | Fixed (`n`) | 38 digits > IEEE double precision |
| Cryptographic math | Unlimited (`N`) | May need thousands of digits |
| Symbolic/exact math | Unlimited (`N`) | Preserves full precision |
| High-frequency data | Fixed (`n`) | Consistent low latency |
| Educational/exploratory | Unlimited (`N`) | Flexibility over performance |

---

## 3. Proposed Syntax Enhancement

### 3.1 Dual Decimal Literals

| Syntax | Type | Precision | Internal Type |
|--------|------|-----------|---------------|
| `123n` | Fixed decimal | 38 digits | `LMD_TYPE_DECIMAL` |
| `123N` | Unlimited decimal | Unbounded | `LMD_TYPE_DECIMAL_BIG` |

### 3.2 Examples

```lambda
// Fixed-precision decimal (38 digits) - CURRENT BEHAVIOR
let price = 19.99n
let tax_rate = 0.0825n
let total = price * (1n + tax_rate)  // Fast, predictable

// Unlimited-precision decimal - NEW
let pi_100 = 3.14159265358979323846264338327950288419716939937510N
let e_100 = 2.71828182845904523536028747135266249775724709369995N
let result = pi_100 * e_100  // Full precision preserved

// Mixed operations promote to unlimited
let mixed = 123n + 456N  // Result is unlimited decimal
```

### 3.3 Type Promotion Rules

| Operand A | Operand B | Result Type |
|-----------|-----------|-------------|
| `int` | `decimal` | `decimal` (fixed) |
| `int` | `decimal_big` | `decimal_big` (unlimited) |
| `float` | `decimal` | `decimal` (fixed) |
| `float` | `decimal_big` | `decimal_big` (unlimited) |
| `decimal` | `decimal` | `decimal` (fixed) |
| `decimal` | `decimal_big` | `decimal_big` (unlimited) |
| `decimal_big` | `decimal_big` | `decimal_big` (unlimited) |

### 3.4 Explicit Conversion Functions

```lambda
// Convert between decimal types
let fixed = to_decimal(123N)     // Unlimited → Fixed (may lose precision)
let unlimited = to_decimal_big(123n)  // Fixed → Unlimited

// Precision control for unlimited decimals
let pi = compute_pi_to(1000)  // Returns 1000-digit decimal_big
```

---

## 4. Code Consolidation: `lambda-decimal.cpp`

### 4.1 Current State (Fragmented)

Decimal handling code is currently scattered across multiple files:

| File | Decimal Code |
|------|--------------|
| `lambda-eval-num.cpp` | Arithmetic operations (`fn_add`, `fn_sub`, etc.) |
| `lambda-data.cpp` | `Decimal` struct, memory management |
| `build_ast.cpp` | `build_lit_decimal()` parsing |
| `print.cpp` | `print_decimal()` formatting |
| `mark_builder.cpp` | Decimal construction for input parsing |
| `input/input.cpp` | `InputManager::decimal_context()` |
| `runner.cpp` | Context initialization |

### 4.2 Proposed Structure: `lambda/lambda-decimal.cpp`

```cpp
// lambda/lambda-decimal.cpp - Centralized decimal handling
// =========================================================

#include "lambda-decimal.hpp"
#include <mpdecimal.h>

// ─────────────────────────────────────────────────────────
// Section 1: Context Management
// ─────────────────────────────────────────────────────────

// Global contexts for fixed and unlimited precision
static mpd_context_t g_fixed_ctx;      // 38-digit precision
static mpd_context_t g_unlimited_ctx;  // Max precision

void decimal_init() {
    // Initialize fixed-precision context (38 digits)
    mpd_defaultcontext(&g_fixed_ctx);
    
    // Initialize unlimited-precision context
    mpd_maxcontext(&g_unlimited_ctx);
}

mpd_context_t* decimal_fixed_context() { return &g_fixed_ctx; }
mpd_context_t* decimal_unlimited_context() { return &g_unlimited_ctx; }

// ─────────────────────────────────────────────────────────
// Section 2: Parsing (from string literals)
// ─────────────────────────────────────────────────────────

// Parse "123.45n" → fixed decimal
Item decimal_parse_fixed(const char* str, size_t len);

// Parse "123.45N" → unlimited decimal  
Item decimal_parse_unlimited(const char* str, size_t len);

// Auto-detect and parse decimal literal
Item decimal_parse(const char* str, size_t len, bool is_unlimited);

// ─────────────────────────────────────────────────────────
// Section 3: Formatting (to string)
// ─────────────────────────────────────────────────────────

// Format decimal to string (scientific notation for large values)
String* decimal_to_string(Item decimal_item);

// Format with explicit precision/format control
String* decimal_format(Item decimal_item, const char* format_spec);

// ─────────────────────────────────────────────────────────
// Section 4: Memory Management
// ─────────────────────────────────────────────────────────

// Allocate new decimal value
Decimal* decimal_alloc(bool is_unlimited);

// Increment reference count
void decimal_retain(Decimal* dec);

// Decrement reference count, free if zero
void decimal_release(Decimal* dec);

// Create decimal from mpd_t (takes ownership)
Item decimal_from_mpd(mpd_t* value, bool is_unlimited);

// ─────────────────────────────────────────────────────────
// Section 5: Type Conversion
// ─────────────────────────────────────────────────────────

// Convert any numeric Item to mpd_t*
mpd_t* decimal_convert_to_mpd(Item item, mpd_context_t* ctx);

// Convert decimal to other types
int64_t decimal_to_int64(Item decimal_item);
double decimal_to_double(Item decimal_item);

// Promote fixed decimal to unlimited
Item decimal_promote_to_unlimited(Item fixed_decimal);

// Truncate unlimited to fixed (may lose precision)
Item decimal_truncate_to_fixed(Item unlimited_decimal);

// ─────────────────────────────────────────────────────────
// Section 6: Arithmetic Operations
// ─────────────────────────────────────────────────────────

// Core arithmetic (handles both fixed and unlimited)
Item decimal_add(Item a, Item b);
Item decimal_sub(Item a, Item b);
Item decimal_mul(Item a, Item b);
Item decimal_div(Item a, Item b);
Item decimal_mod(Item a, Item b);
Item decimal_pow(Item a, Item b);
Item decimal_neg(Item a);
Item decimal_abs(Item a);

// Comparison
int decimal_cmp(Item a, Item b);  // Returns -1, 0, 1
bool decimal_eq(Item a, Item b);
bool decimal_lt(Item a, Item b);
bool decimal_gt(Item a, Item b);

// ─────────────────────────────────────────────────────────
// Section 7: Mathematical Functions
// ─────────────────────────────────────────────────────────

Item decimal_sqrt(Item a);
Item decimal_exp(Item a);
Item decimal_ln(Item a);
Item decimal_log10(Item a);
Item decimal_floor(Item a);
Item decimal_ceil(Item a);
Item decimal_round(Item a, int places);

// ─────────────────────────────────────────────────────────
// Section 8: Utility Functions
// ─────────────────────────────────────────────────────────

bool decimal_is_zero(Item a);
bool decimal_is_negative(Item a);
bool decimal_is_integer(Item a);
bool decimal_is_unlimited(Item a);
int decimal_precision(Item a);  // Number of significant digits
int decimal_scale(Item a);      // Digits after decimal point
```

### 4.3 Header File: `lambda/lambda-decimal.hpp`

```cpp
// lambda/lambda-decimal.hpp
#ifndef LAMBDA_DECIMAL_HPP
#define LAMBDA_DECIMAL_HPP

#include "lambda-data.hpp"
#include <mpdecimal.h>

// ─────────────────────────────────────────────────────────
// Type IDs
// ─────────────────────────────────────────────────────────

// LMD_TYPE_DECIMAL     = existing (38-digit fixed)
// LMD_TYPE_DECIMAL_BIG = new (unlimited precision)

// ─────────────────────────────────────────────────────────
// Extended Decimal struct for unlimited precision
// ─────────────────────────────────────────────────────────

struct DecimalBig {
    uint16_t ref_cnt;
    mpd_t* dec_val;      // libmpdec value (uses max context)
};

// ─────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────

// Initialization (call once at startup)
void decimal_init();
void decimal_cleanup();

// Context access
mpd_context_t* decimal_fixed_context();
mpd_context_t* decimal_unlimited_context();

// Parsing
Item decimal_parse_fixed(const char* str, size_t len);
Item decimal_parse_unlimited(const char* str, size_t len);

// Formatting
String* decimal_to_string(Item decimal_item);

// Memory
void decimal_retain(Decimal* dec);
void decimal_release(Decimal* dec);

// Conversion
mpd_t* decimal_convert_to_mpd(Item item, mpd_context_t* ctx);
Item decimal_promote_to_unlimited(Item fixed_decimal);

// Arithmetic
Item decimal_add(Item a, Item b);
Item decimal_sub(Item a, Item b);
Item decimal_mul(Item a, Item b);
Item decimal_div(Item a, Item b);
Item decimal_mod(Item a, Item b);
Item decimal_pow(Item a, Item b);

// Comparison
int decimal_cmp(Item a, Item b);

// Predicates
bool decimal_is_zero(Item a);
bool decimal_is_unlimited(Item a);

#endif // LAMBDA_DECIMAL_HPP
```

### 4.4 Migration Plan

| Phase | Task | Files Affected |
|-------|------|----------------|
| 1 | Create `lambda-decimal.cpp/hpp` with existing fixed decimal code | New files |
| 2 | Update `lambda-eval-num.cpp` to call `decimal_*` functions | `lambda-eval-num.cpp` |
| 3 | Move parsing from `build_ast.cpp` to `decimal_parse_*` | `build_ast.cpp` |
| 4 | Move formatting from `print.cpp` to `decimal_to_string` | `print.cpp` |
| 5 | Add `LMD_TYPE_DECIMAL_BIG` type ID | `lambda-data.hpp` |
| 6 | Update grammar to recognize `N` suffix | `grammar.js` |
| 7 | Implement unlimited decimal operations | `lambda-decimal.cpp` |
| 8 | Add tests for unlimited decimals | `test/lambda/decimal.ls` |

---

## 5. Grammar Changes

### 5.1 Current Grammar (tree-sitter)

```javascript
// lambda/tree-sitter-lambda/grammar.js (current)
lit_decimal: $ => seq(
  $.number,
  'n'  // lowercase only
),
```

### 5.2 Proposed Grammar

```javascript
// lambda/tree-sitter-lambda/grammar.js (proposed)
lit_decimal: $ => seq(
  $.number,
  'n'  // fixed precision (38 digits)
),

lit_decimal_big: $ => seq(
  $.number,
  'N'  // unlimited precision
),
```

### 5.3 AST Node Types

```cpp
// New AST node type
AST_LIT_DECIMAL_BIG  // For 123N literals
```

---

## 6. Backward Compatibility

| Aspect | Impact | Mitigation |
|--------|--------|------------|
| Existing `123n` syntax | None | Unchanged behavior |
| Arithmetic semantics | None for fixed-only code | Same 38-digit precision |
| Performance | None for fixed-only code | No overhead if `N` not used |
| Memory usage | None for fixed-only code | `DecimalBig` only allocated when needed |

---

## 7. Implementation Timeline

| Week | Milestone |
|------|-----------|
| 1 | Create `lambda-decimal.cpp/hpp`, migrate existing code |
| 2 | Add `LMD_TYPE_DECIMAL_BIG`, update type system |
| 3 | Update grammar, parser for `N` suffix |
| 4 | Implement unlimited decimal arithmetic |
| 5 | Testing, documentation, edge cases |
| 6 | Performance optimization, benchmarks |

---

## 8. Design Decisions

1. **Precision limit for "unlimited"**: Should we cap at some practical limit (e.g., 1 million digits) to prevent runaway memory usage? *(Open)*

2. **Default type for division**: `1n / 3n` returns **fixed decimal**. Use `1N / 3N` explicitly for unlimited precision. ✅ *Decided*

3. **Serialization**: How should unlimited decimals be represented in JSON output? (Proposed: string with `"N"` suffix marker) *(Open)*

4. **REPL display**: **No truncation** — display full decimal value regardless of length. ✅ *Decided*

---

## 9. References

- [libmpdec Documentation](https://www.bytereef.org/mpdecimal/doc/libmpdec/index.html)
- [IEEE 754-2008 Decimal Floating-Point](https://en.wikipedia.org/wiki/IEEE_754#Decimal_floating-point)
- [Python decimal module](https://docs.python.org/3/library/decimal.html)
- [Java BigDecimal](https://docs.oracle.com/en/java/javase/17/docs/api/java.base/java/math/BigDecimal.html)

---

## Appendix A: Current Implementation Status

Lambda's decimal support was fully implemented in August 2025, migrating from GMP to libmpdec. Current features:

- ✅ 38-digit precision via `mpd_defaultcontext()`
- ✅ All arithmetic operations (+, -, *, /, %, ^)
- ✅ Mixed-type operations (int/float/decimal)
- ✅ IEEE 754-2008 compliance
- ✅ Reference-counted memory management
- ✅ Error handling (division by zero, overflow)

See [Lambda_Type_Decimal.md](./Lambda_Type_Decimal.md) for detailed implementation notes.
