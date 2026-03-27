// lambda/py/py_bigint.h — Python arbitrary-precision integers
// ===========================================================
// Python bigints are stored as LMD_TYPE_DECIMAL Items with
// Decimal::unlimited == PY_BIGINT_FLAG (value 2, distinct from Lambda's
// fixed=0 and unlimited=1 decimal modes).
// Backed by libmpdecimal with PY_BIGINT_PREC-digit precision (~13,000 bits).
#pragma once

#include "../lambda.h"

// Marker in Decimal::unlimited to identify Python bigint items.
#define PY_BIGINT_FLAG  2

// Precision in decimal digits (~13,000 bits = 2^43300).
#define PY_BIGINT_PREC  4000

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the bigint subsystem (idempotent; called lazily).
void   py_bigint_init(void);

// Type check: returns true when Item is a Python bigint.
bool   py_is_bigint(Item x);

// Create a bigint from a 64-bit signed integer (always returns bigint Item).
Item   py_bigint_from_int64(int64_t val);

// Parse a decimal string literal (e.g. "99999999999999999999999999").
// Used for large integer literals. Returns ItemNull on parse failure.
Item   py_bigint_from_cstr(const char* s);

// Normalize: if the bigint fits in int56, return an int56 Item; otherwise return bigint.
Item   py_bigint_normalize(Item x);

// Arithmetic — operands may be LMD_TYPE_INT or bigint; result is normalized.
Item   py_bigint_add(Item a, Item b);
Item   py_bigint_sub(Item a, Item b);
Item   py_bigint_mul(Item a, Item b);
Item   py_bigint_floordiv(Item a, Item b);  // floor(a/b), Python semantics
Item   py_bigint_mod(Item a, Item b);       // a % b, Python semantics (sign matches b)
Item   py_bigint_pow(Item base_item, Item exp_item);
Item   py_bigint_neg(Item a);
Item   py_bigint_abs(Item a);

// Bit shifts using multiplication/division by powers of 2.
Item   py_bigint_lshift(Item a, Item n);   // a << n
Item   py_bigint_rshift(Item a, Item n);   // a >> n (arithmetic, floor semantics)

// Comparison: returns negative/0/positive (like strcmp).
int    py_bigint_cmp(Item a, Item b);

// Value extraction.
int64_t  py_bigint_to_int64(Item x);      // truncates toward zero
double   py_bigint_to_double(Item x);

// String representations (return Lambda String Items).
Item     py_bigint_to_str_item(Item x);   // decimal digits
Item     py_bigint_to_hex_item(Item x);   // "0x..." or "-0x..."
Item     py_bigint_to_oct_item(Item x);   // "0o..." or "-0o..."
Item     py_bigint_to_bin_item(Item x);   // "0b..." or "-0b..."

#ifdef __cplusplus
}
#endif
