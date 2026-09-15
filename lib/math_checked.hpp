#pragma once
// checked size arithmetic and alignment helpers.  Overflow is always an error.
// See vibe/Memory_Safety_Template3.md §3.4.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// C-compatible core so allocation providers and C++ containers share one
// overflow and alignment contract.
static inline bool math_checked_mul(size_t a, size_t b, size_t* out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a != 0 && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
#endif
}

static inline bool math_checked_add(size_t a, size_t b, size_t* out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, out);
#else
    if (b > SIZE_MAX - a) return false;
    *out = a + b;
    return true;
#endif
}

// out = a * b + c, used for a fixed allocation header plus a variable tail.
static inline bool math_checked_mul_add(size_t a, size_t b, size_t c, size_t* out) {
    size_t product = 0;
    return math_checked_mul(a, b, &product) && math_checked_add(product, c, out);
}

static inline bool math_size_is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static inline bool math_size_round_up(size_t value, size_t quantum, size_t* out) {
    if (quantum == 0) return false;
    size_t remainder = value % quantum;
    if (remainder == 0) {
        *out = value;
        return true;
    }
    return math_checked_add(value, quantum - remainder, out);
}

static inline bool math_size_align_up(size_t value, size_t alignment, size_t* out) {
    return math_size_is_power_of_two(alignment) && math_size_round_up(value, alignment, out);
}

#ifdef __cplusplus
namespace lam {

inline bool checked_mul(size_t a, size_t b, size_t* out) {
    return math_checked_mul(a, b, out);
}

inline bool checked_add(size_t a, size_t b, size_t* out) {
    return math_checked_add(a, b, out);
}

// out = a * b + c (the common "header + n*elem" allocation size); returns false on any overflow.
inline bool checked_mul_add(size_t a, size_t b, size_t c, size_t* out) {
    return math_checked_mul_add(a, b, c, out);
}

// Narrow From -> To preserving value; returns false if the value does not round-trip.
// The round-trip comparison is done in From's type, so there is no sign-compare warning.
template<class To, class From>
inline bool checked_narrow(From v, To* out) {
    *out = (To)v;
    return (From)(*out) == v;
}

} // namespace lam
#endif
