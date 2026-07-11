// lib/math_utils.h - small numeric helpers, C and C++ compatible.
//
// In C++ these live in the `lib_math` namespace. Callers opt in with
//   using lib_math::clamp;
// to avoid clashes with radiant/view.hpp which defines its own clamp/sign/lerp.
//
// In C, type-generic macros are provided in UPPER_CASE form to avoid stomping
// any function named clamp/sign/etc.

#ifndef LIB_MATH_UTILS_H
#define LIB_MATH_UTILS_H

#ifdef __cplusplus

namespace lib_math {
    template<typename T>
    static inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

    template<typename T, typename U, typename V>
    static inline auto clamp_mixed(T v, U lo, V hi) -> decltype(v + lo + hi) {
        typedef decltype(v + lo + hi) Common;
        Common cv = (Common)v;
        Common clo = (Common)lo;
        Common chi = (Common)hi;
        return cv < clo ? clo : (cv > chi ? chi : cv);
    }

    template<typename T>
    static inline int sign(T v) { return (v > T(0)) - (v < T(0)); }

    template<typename T>
    static inline T lerp(T a, T b, T t) { return a + t * (b - a); }

    template<typename T>
    static inline T abs_val(T v) { return v < T(0) ? -v : v; }

    template<typename T>
    static inline T min_val(T a, T b) { return a < b ? a : b; }

    template<typename T>
    static inline T max_val(T a, T b) { return a > b ? a : b; }

    template<typename T, typename U>
    static inline auto min_mixed(T a, U b) -> decltype(a + b) {
        typedef decltype(a + b) Common;
        Common ca = (Common)a;
        Common cb = (Common)b;
        return ca < cb ? ca : cb;
    }

    template<typename T, typename U>
    static inline auto max_mixed(T a, U b) -> decltype(a + b) {
        typedef decltype(a + b) Common;
        Common ca = (Common)a;
        Common cb = (Common)b;
        return ca > cb ? ca : cb;
    }
}

#endif

// type-generic macros usable in both C and C++. Watch double-evaluation:
// arguments must be free of side effects.
#ifndef LMB_CLAMP
#define LMB_CLAMP(v, lo, hi)  ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define LMB_SIGN(v)           (((v) > 0) - ((v) < 0))
#define LMB_LERP(a, b, t)     ((a) + (t) * ((b) - (a)))
#define LMB_ABS(v)            ((v) < 0 ? -(v) : (v))
#define LMB_MIN(a, b)         ((a) < (b) ? (a) : (b))
#define LMB_MAX(a, b)         ((a) > (b) ? (a) : (b))
#endif

static inline unsigned char clamp_byte(int v) {
    return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline float clamp_unit(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

#endif
