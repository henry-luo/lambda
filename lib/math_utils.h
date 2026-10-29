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
#include <stdbool.h>
#include <stdint.h>

// a caller-validated, row-major proper rotation becomes a normalized [x,y,z,w] quaternion.
// largest-diagonal selection keeps half-turn axes stable; failure leaves the result untouched.
static inline bool math_rotation_quaternion(const double rotation[9], double result[4]) {
    if (!rotation || !result) return false;
    for (unsigned i = 0; i < 9; i++) if (!isfinite(rotation[i])) return false;
    double q[4] = {0, 0, 0, 0};
    double trace = rotation[0] + rotation[4] + rotation[8];
    if (trace > 0) {
        double divisor = 2 * sqrt(1 + trace);
        q[3] = divisor / 4;
        for (unsigned axis = 0; axis < 3; axis++) {
            unsigned next = (axis + 1) % 3, last = (axis + 2) % 3;
            q[axis] = (rotation[last * 3 + next] - rotation[next * 3 + last]) / divisor;
        }
    } else {
        unsigned axis = 2;
        for (unsigned remaining = 2; remaining; remaining--) {
            unsigned candidate = remaining - 1;
            if (rotation[candidate * 3 + candidate] > rotation[axis * 3 + axis]) axis = candidate;
        }
        unsigned next = (axis + 1) % 3, last = (axis + 2) % 3;
        double divisor = 2 * sqrt(1 + rotation[axis * 3 + axis] - rotation[next * 3 + next] - rotation[last * 3 + last]);
        if (!(divisor > 0)) return false;
        q[axis] = divisor / 4;
        q[next] = (rotation[axis * 3 + next] + rotation[next * 3 + axis]) / divisor;
        q[last] = (rotation[axis * 3 + last] + rotation[last * 3 + axis]) / divisor;
        q[3] = (rotation[last * 3 + next] - rotation[next * 3 + last]) / divisor;
    }
    double length = hypot(hypot(q[0], q[1]), hypot(q[2], q[3]));
    if (!isfinite(length) || length == 0) return false;
    for (unsigned i = 0; i < 4; i++) result[i] = q[i] / length;
    return true;
}

// numeric extrema preserve NaN and the signed-zero rules on both runtime tiers.
static inline double math_min_double(double a, double b) {
    if (isnan(a) || isnan(b)) return NAN;
    if (a == 0.0 && b == 0.0) return (signbit(a) || signbit(b)) ? -0.0 : 0.0;
    return a < b ? a : b;
}
static inline double math_max_double(double a, double b) {
    if (isnan(a) || isnan(b)) return NAN;
    if (a == 0.0 && b == 0.0) return (signbit(a) && signbit(b)) ? -0.0 : 0.0;
    return a > b ? a : b;
}

// the finalizer also serves keyed draws without inventing a threaded state.
static inline uint64_t math_splitmix64_mix(uint64_t z) {
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static inline double math_splitmix64_unit(uint64_t key) {
    return (double)(math_splitmix64_mix(key) >> 11) * 0x1.0p-53;
}

// shared SplitMix64 step; callers own the state and choose pure or mutable semantics.
static inline double math_splitmix64(uint64_t* state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    return math_splitmix64_unit(*state);
}

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
// Float to int with saturation: NaN becomes 0 and out-of-range values clamp
// to the int range. A plain (int) cast of an out-of-range or NaN float is
// undefined behaviour; values that feed an index, size or count go through
// one of these. Rounding matches the named C function (trunc, floor, ceil,
// lround).
static inline int math_float_to_int_sat(float v) {
    if (!(v == v)) return 0;
    if (v >= 2147483647.0f) return INT32_MAX;
    if (v <= -2147483648.0f) return INT32_MIN;
    return (int)v;
}

static inline int math_floor_to_int_sat(float v) { return math_float_to_int_sat(floorf(v)); }
static inline int math_ceil_to_int_sat(float v) { return math_float_to_int_sat(ceilf(v)); }
static inline int math_round_to_int_sat(float v) {
    return math_float_to_int_sat(v == v ? roundf(v) : v);
}

static inline float math_wrap_positive_f(float value, float period) {
    float wrapped = fmodf(value, period);
    return wrapped < 0.0f ? wrapped + period : wrapped;
}

// exact quadrant directions keep axis-aligned geometry from acquiring trigonometric drift.
static inline void math_sincos_degrees_f(float angle, float* sine, float* cosine) {
    float normalized = math_wrap_positive_f(angle, 360.0f);
    if (normalized == 0.0f) { *sine = 0.0f; *cosine = 1.0f; }
    else if (normalized == 90.0f) { *sine = 1.0f; *cosine = 0.0f; }
    else if (normalized == 180.0f) { *sine = 0.0f; *cosine = -1.0f; }
    else if (normalized == 270.0f) { *sine = -1.0f; *cosine = 0.0f; }
    else {
        float radians = math_degrees_to_radians(normalized);
        *sine = sinf(radians); *cosine = cosf(radians);
    }
}

#endif
