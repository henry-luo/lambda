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

#include <math.h>
#include <stdint.h>

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

// count of leading zero bits in v, 64 for 0 (the portable fallback used to
// return 63 for 0)
static inline int math_clz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return v ? __builtin_clzll(v) : 64;
#else
    if (!v) return 64;
    int n = 0;
    if (!(v & 0xFFFFFFFF00000000ULL)) { n += 32; v <<= 32; }
    if (!(v & 0xFFFF000000000000ULL)) { n += 16; v <<= 16; }
    if (!(v & 0xFF00000000000000ULL)) { n += 8;  v <<= 8;  }
    if (!(v & 0xF000000000000000ULL)) { n += 4;  v <<= 4;  }
    if (!(v & 0xC000000000000000ULL)) { n += 2;  v <<= 2;  }
    if (!(v & 0x8000000000000000ULL)) { n += 1; }
    return n;
#endif
}

static inline unsigned char clamp_byte(int v) {
    return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// NaN maps to zero before the float-to-integer conversion.
static inline unsigned char clamp_byte_round(float v) {
    if (!(v > 0.0f)) return 0;
    return v >= 254.5f ? 255 : (unsigned char)(v + 0.5f);
}

static inline float clamp_unit(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float math_pi_f(void) {
    return 3.14159265358979323846f;
}

static inline double math_pi_d(void) {
    return 3.141592653589793238462643383279502884;
}

static inline float math_tau_f(void) {
    return 2.0f * math_pi_f();
}

static inline float math_degrees_to_radians(float degrees) {
    return degrees * math_pi_f() / 180.0f;
}

static inline double math_degrees_to_radians_d(double degrees) {
    return degrees * math_pi_d() / 180.0;
}

static inline double math_radians_to_degrees_d(double radians) {
    return radians * 180.0 / math_pi_d();
}

static inline float math_gradians_to_radians(float gradians) {
    return gradians * math_pi_f() / 200.0f;
}

static inline float math_turns_to_radians(float turns) {
    return turns * math_tau_f();
}

// period must be finite and positive. A nonfinite value retains fmodf's NaN result.
static inline float math_wrap_positive_f(float value, float period) {
    float wrapped = fmodf(value, period);
    return wrapped < 0.0f ? wrapped + period : wrapped;
}

#endif
