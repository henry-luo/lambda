#pragma once
// checked integer arithmetic — overflow-as-an-error helpers (Rust's checked_mul / checked_add).
// Zero-cost: lowers to a single hardware add/mul + overflow-flag test on gcc/clang.
// See vibe/Memory_Safety_Template3.md §3.4.

#include <stddef.h>
#include <stdint.h>

namespace lam {

// out = a * b; returns false (and leaves *out unspecified) on overflow.
inline bool checked_mul(size_t a, size_t b, size_t* out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a != 0 && b > (size_t)-1 / a) return false;
    *out = a * b;
    return true;
#endif
}

// out = a + b; returns false on overflow.
inline bool checked_add(size_t a, size_t b, size_t* out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, out);
#else
    if (b > (size_t)-1 - a) return false;
    *out = a + b;
    return true;
#endif
}

// out = a * b + c (the common "header + n*elem" allocation size); returns false on any overflow.
inline bool checked_mul_add(size_t a, size_t b, size_t c, size_t* out) {
    size_t prod;
    if (!checked_mul(a, b, &prod)) return false;
    return checked_add(prod, c, out);
}

// Narrow From -> To preserving value; returns false if the value does not round-trip.
// The round-trip comparison is done in From's type, so there is no sign-compare warning.
template<class To, class From>
inline bool checked_narrow(From v, To* out) {
    *out = (To)v;
    return (From)(*out) == v;
}

} // namespace lam
