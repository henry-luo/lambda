// Pure scientific kernels shared by T0 and MIR (D6.4.1, D8.1.1).
#include "transpiler.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-error.h"
#include "../../lib/math_checked.hpp"
#include "../../lib/memtrack.h"
#include "../../lib/procgen/wfc.h"
#include <float.h>
#include <math.h>
#include <string.h>

// Scratch contains no Items: GC roots belong to the boundary readers/builders.
struct ScienceScratch {
    void* data;
    ScienceScratch(size_t count, size_t width) : data(NULL) {
        size_t bytes;
        if (math_checked_mul(count, width, &bytes))
            data = mem_calloc(1, bytes ? bytes : 1, MEM_CAT_EVAL);
    }
    ~ScienceScratch() { mem_free(data); }
    ScienceScratch(const ScienceScratch&) = delete;
    ScienceScratch& operator=(const ScienceScratch&) = delete;
};

static Item science_error(const char* message) {
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, NULL));
}

static bool science_real(Item item, double* value) {
    if (lambda_numeric_kind_from_item(item) == LAMBDA_NUM_INVALID) return false;
    *value = it2d(item);
    return isfinite(*value);
}

struct ScienceComplex { double re, im; };

static ScienceComplex science_complex_mul(ScienceComplex a, ScienceComplex b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

// Both transforms use the same butterflies; only the inverse normalizes.
static void science_fft_radix2(ScienceComplex* a, size_t n, bool inverse) {
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { ScienceComplex t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    const double pi = acos(-1.0);
    for (size_t width = 2; width <= n; width *= 2) {
        double angle = (inverse ? 2 : -2) * pi / (double)width;
        ScienceComplex step = {cos(angle), sin(angle)};
        for (size_t start = 0; start < n; start += width) {
            ScienceComplex w = {1, 0};
            for (size_t j = 0; j < width / 2; j++) {
                ScienceComplex u = a[start + j];
                ScienceComplex v = science_complex_mul(w, a[start + j + width / 2]);
                a[start + j] = {u.re + v.re, u.im + v.im};
                a[start + j + width / 2] = {u.re - v.re, u.im - v.im};
                w = science_complex_mul(w, step);
            }
        }
        if (width == n) break;  // do not overflow the final doubling
    }
    if (inverse) for (size_t i = 0; i < n; i++) {
        a[i].re /= (double)n;
        a[i].im /= (double)n;
    }
}

static bool science_fft(ScienceComplex* a, size_t n, bool inverse) {
    if (n < 2) return true;
    if (math_size_is_power_of_two(n)) {
        science_fft_radix2(a, n, inverse);
        return true;
    }
    // Bluestein reduces arbitrary (including prime) lengths to a convolution.
    size_t twice, width = 1;
    if (!math_checked_mul(n, 2, &twice)) return false;
    while (width < twice - 1) {
        if (!math_checked_mul(width, 2, &width)) return false;
    }
    ScienceScratch left_mem(width, sizeof(ScienceComplex));
    ScienceScratch right_mem(width, sizeof(ScienceComplex));
    ScienceScratch chirp_mem(n, sizeof(ScienceComplex));
    if (!left_mem.data || !right_mem.data || !chirp_mem.data) return false;
    ScienceComplex* left = (ScienceComplex*)left_mem.data;
    ScienceComplex* right = (ScienceComplex*)right_mem.data;
    ScienceComplex* chirp = (ScienceComplex*)chirp_mem.data;
    size_t square = 0;
    for (size_t j = 0; j < n; j++) {
        double angle = (inverse ? 1 : -1) * acos(-1.0) * ((double)square / (double)n);
        chirp[j] = {cos(angle), sin(angle)};
        left[j] = science_complex_mul(a[j], chirp[j]);
        right[j] = {chirp[j].re, -chirp[j].im};
        if (j) right[width - j] = right[j];
        // (j+1)^2 mod 2n, without overflowing or rounding a large j*j.
        size_t delta = 2 * j + 1;
        square = square >= twice - delta ? square - (twice - delta) : square + delta;
    }
    science_fft_radix2(left, width, false);
    science_fft_radix2(right, width, false);
    for (size_t j = 0; j < width; j++) left[j] = science_complex_mul(left[j], right[j]);
    science_fft_radix2(left, width, true);
    for (size_t j = 0; j < n; j++) {
        a[j] = science_complex_mul(left[j], chirp[j]);
        if (inverse) { a[j].re /= (double)n; a[j].im /= (double)n; }
    }
    return true;
}

static bool science_read_signal(Item input, ScienceComplex* values, size_t count, bool real_only) {
    RootFrame roots(1);
    Rooted<Item> source(roots, input);
    for (size_t i = 0; i < count; i++) {
        Item value = vector_get(source.get(), (int64_t)i);
        if (!real_only && get_type_id(value) == LMD_TYPE_COMPLEX) {
            if (!complex_components_from_item(value, &values[i].re, &values[i].im) ||
                    !isfinite(values[i].re) || !isfinite(values[i].im)) return false;
        } else if (!science_real(value, &values[i].re)) return false;
    }
    return true;
}

static Item science_complex_array(const ScienceComplex* values, size_t count) {
    RootFrame roots(1);
    Rooted<Array*> result(roots, array_plain());
    if (!result.get()) return ItemError;
    for (size_t i = 0; i < count; i++) {
        Item value = complex_new(values[i].re, values[i].im);
        if (item_is_error(value)) return value;
        array_push_verbatim(result.get(), value);
    }
    return {.array = result.get()};
}

static Item science_transform(Item input, bool inverse, bool real_only) {
    GUARD_ERROR1(input);
    GUARD_NULL1(input);
    int64_t length = vector_length(input);
    if (length < 0 || vector_is_ndim(input))
        return science_error("math FFT: expected a one-dimensional numeric sequence");
    ScienceScratch memory((size_t)length, sizeof(ScienceComplex));
    if (!memory.data) return ItemError;
    ScienceComplex* values = (ScienceComplex*)memory.data;
    if (!science_read_signal(input, values, (size_t)length, real_only))
        return science_error("math FFT: samples must be finite numbers of the supported real/complex domain");
    if (!science_fft(values, (size_t)length, inverse)) return ItemError;
    size_t count = (size_t)length;
    if (real_only && length) {
        count = (size_t)length / 2 + 1;
        values[0].im = 0;
        if (!(length & 1)) values[count - 1].im = 0;
    }
    return science_complex_array(values, count);
}

Item fn_math_fft(Item input) { return science_transform(input, false, false); }
Item fn_math_ifft(Item input) { return science_transform(input, true, false); }
Item fn_math_rfft(Item input) { return science_transform(input, false, true); }

Item fn_math_irfft(Item input, Item length_item) {
    GUARD_ERROR2(input, length_item);
    GUARD_NULL2(input, length_item);
    int64_t length;
    if (!lambda_item_to_int64_exact(length_item, &length) || length < 0)
        return science_error("math.irfft: length must be a nonnegative integer");
    int64_t count = vector_length(input);
    if (vector_is_ndim(input) || count != (length ? length / 2 + 1 : 0))
        return science_error("math.irfft: spectrum length must be floor(n/2)+1 (zero for n=0)");
    ScienceScratch memory((size_t)length, sizeof(ScienceComplex));
    if (!memory.data) return ItemError;
    ScienceComplex* values = (ScienceComplex*)memory.data;
    if (!science_read_signal(input, values, (size_t)count, false))
        return science_error("math.irfft: spectrum must contain finite real or complex numbers");
    if (length && (values[0].im != 0 || (!(length & 1) && values[count - 1].im != 0)))
        return science_error("math.irfft: DC and even-length Nyquist bins must be real");
    for (int64_t i = count; i < length; i++)
        values[i] = {values[length - i].re, -values[length - i].im};
    if (!science_fft(values, (size_t)length, true)) return ItemError;
    ArrayNum* result = array_num_new(ELEM_FLOAT64, length);
    if (!result) return ItemError;
    for (int64_t i = 0; i < length; i++) result->float_items[i] = values[i].re;
    return {.array_num = result};
}

struct ScienceMatrix {
    size_t rows, cols;
    bool vector;
    double* values;
    ScienceMatrix() : rows(0), cols(0), vector(false), values(NULL) {}
    ~ScienceMatrix() { mem_free(values); }
    ScienceMatrix(const ScienceMatrix&) = delete;
    ScienceMatrix& operator=(const ScienceMatrix&) = delete;
};

static bool science_read_matrix(Item input, ScienceMatrix* matrix) {
    RootFrame roots(2);
    Rooted<Item> source(roots, input);
    Rooted<Item> row(roots, ItemNull);
    int64_t length = vector_length(input);
    if (length < 0) return false;
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    bool packed = get_type_id(input) == LMD_TYPE_ARRAY_NUM;
    int ndim = packed ? array_num_get_shape_strides(input.array_num, shape, strides) : 0;
    if (ndim > 2 || (packed && input.array_num->get_elem_type() == ELEM_BOOL)) return false;
    if (packed) {
        matrix->rows = (size_t)shape[0];
        matrix->vector = ndim == 1;
        matrix->cols = ndim == 1 ? 1 : (size_t)shape[1];
    } else {
        matrix->rows = (size_t)length;
        if (length) row.set(vector_get(source.get(), 0));
        int64_t row_length = length ? vector_length(row.get()) : -1;
        matrix->vector = row_length < 0;
        matrix->cols = matrix->vector ? 1 : (size_t)row_length;
    }
    size_t cells, bytes;
    if (!math_checked_mul(matrix->rows, matrix->cols, &cells) ||
            !math_checked_mul(cells, sizeof(double), &bytes)) return false;
    matrix->values = (double*)mem_alloc(bytes ? bytes : 1, MEM_CAT_EVAL);
    if (!matrix->values) return false;
    for (size_t i = 0; i < matrix->rows; i++) {
        if (!packed && !matrix->vector) {
            row.set(vector_get(source.get(), (int64_t)i));
            if (vector_is_ndim(row.get()) || vector_length(row.get()) != (int64_t)matrix->cols)
                return false;
        }
        for (size_t j = 0; j < matrix->cols; j++) {
            double value;
            if (packed) {
                value = array_num_read_double(source.get().array_num,
                    (int64_t)i * strides[0] + (ndim == 2 ? (int64_t)j * strides[1] : 0));
                if (!isfinite(value)) return false;
            } else if (!science_real(vector_get(matrix->vector ? source.get() : row.get(),
                    (int64_t)(matrix->vector ? i : j)), &value)) return false;
            matrix->values[i * matrix->cols + j] = value;
        }
    }
    return true;
}

// A fixed balanced reduction tree is independent of execution tier (S13.4.1).
static double science_dot(const double* a, size_t sa, const double* b, size_t sb, size_t n) {
    if (!n) return 0;
    if (n == 1) return *a * *b;
    size_t half = n / 2;
    return science_dot(a, sa, b, sb, half) +
        science_dot(a + half * sa, sa, b + half * sb, sb, n - half);
}

static double science_norm(const double* a, size_t stride, size_t n) {
    if (!n) return 0;
    if (n == 1) return fabs(*a);
    size_t half = n / 2;
    return hypot(science_norm(a, stride, half), science_norm(a + half * stride, stride, n - half));
}

static double science_scale(double* a, size_t n) {
    double scale = 0;
    for (size_t i = 0; i < n; i++) if (fabs(a[i]) > scale) scale = fabs(a[i]);
    if (scale) for (size_t i = 0; i < n; i++) a[i] /= scale;
    return scale ? scale : 1;
}

static void science_swap_rows(double* a, size_t cols, size_t i, size_t j) {
    for (size_t k = 0; k < cols; k++) {
        double t = a[i * cols + k]; a[i * cols + k] = a[j * cols + k]; a[j * cols + k] = t;
    }
}

// LU with partial pivoting also applies elimination directly to every RHS.
static bool science_lu(double* a, double* b, size_t n, size_t rhs) {
    for (size_t k = 0; k < n; k++) {
        size_t pivot = k;
        for (size_t i = k + 1; i < n; i++)
            if (fabs(a[i * n + k]) > fabs(a[pivot * n + k])) pivot = i;
        if (a[pivot * n + k] == 0) return false;
        science_swap_rows(a, n, k, pivot);
        science_swap_rows(b, rhs, k, pivot);
        for (size_t i = k + 1; i < n; i++) {
            double factor = a[i * n + k] / a[k * n + k];
            for (size_t j = k + 1; j < n; j++) a[i * n + j] -= factor * a[k * n + j];
            for (size_t j = 0; j < rhs; j++) b[i * rhs + j] -= factor * b[k * rhs + j];
        }
    }
    return true;
}

// Column-pivoted Householder QR avoids squaring the condition number in A^T A.
static bool science_qr(double* a, double* b, size_t m, size_t n, size_t rhs, size_t* order) {
    double threshold = 0;
    for (size_t k = 0; k < n; k++) {
        size_t pivot = k;
        double norm = science_norm(a + k * n + k, n, m - k);
        for (size_t j = k + 1; j < n; j++) {
            double candidate = science_norm(a + k * n + j, n, m - k);
            if (candidate > norm) { norm = candidate; pivot = j; }
        }
        if (!k) threshold = DBL_EPSILON * (double)m * norm;
        if (norm <= threshold) return false;
        for (size_t i = 0; i < m; i++) {
            double t = a[i * n + k]; a[i * n + k] = a[i * n + pivot]; a[i * n + pivot] = t;
        }
        size_t index = order[k]; order[k] = order[pivot]; order[pivot] = index;
        double first = a[k * n + k];
        double beta = -copysign(norm, first);
        double tau = (beta - first) / beta;
        for (size_t i = k + 1; i < m; i++) a[i * n + k] /= first - beta;
        a[k * n + k] = 1;
        for (size_t j = k + 1; j < n; j++) {
            double dot = tau * science_dot(a + k * n + k, n, a + k * n + j, n, m - k);
            for (size_t i = k; i < m; i++) a[i * n + j] -= dot * a[i * n + k];
        }
        for (size_t j = 0; j < rhs; j++) {
            double dot = tau * science_dot(a + k * n + k, n, b + k * rhs + j, rhs, m - k);
            for (size_t i = k; i < m; i++) b[i * rhs + j] -= dot * a[i * n + k];
        }
        a[k * n + k] = beta;
    }
    return true;
}

static Item science_linear_solve(Item input_a, Item input_b, bool least_squares) {
    GUARD_ERROR2(input_a, input_b);
    GUARD_NULL2(input_a, input_b);
    RootFrame roots(1);
    Rooted<Item> rhs_input(roots, input_b);
    ScienceMatrix a, b;
    if (!science_read_matrix(input_a, &a) || !science_read_matrix(rhs_input.get(), &b))
        return science_error("math linear solve: expected rectangular finite real arrays");
    if (!a.rows && a.vector) { a.vector = false; a.cols = 0; }
    size_t m = a.rows, n = a.cols, rhs = b.cols;
    if (a.vector || b.rows != m || (least_squares ? m < n : m != n))
        return science_error("math linear solve: incompatible shapes (solve requires square A; lstsq requires rows >= columns)");
    ScienceScratch order_mem(n, sizeof(size_t));
    if (!order_mem.data) return ItemError;
    size_t* order = (size_t*)order_mem.data;
    for (size_t i = 0; i < n; i++) order[i] = i;
    double a_scale = science_scale(a.values, m * n);
    double b_scale = science_scale(b.values, m * rhs);
    bool success = least_squares ? science_qr(a.values, b.values, m, n, rhs, order) :
        science_lu(a.values, b.values, n, rhs);
    if (!success) return science_error("math linear solve: singular or numerically rank-deficient matrix");
    for (size_t i = n; i-- > 0;) for (size_t j = 0; j < rhs; j++) {
        double dot = i + 1 < n ? science_dot(a.values + i * n + i + 1, 1,
            b.values + (i + 1) * rhs + j, rhs, n - i - 1) : 0;
        b.values[i * rhs + j] = (b.values[i * rhs + j] - dot) / a.values[i * n + i];
    }
    int64_t shape[2] = {(int64_t)n, (int64_t)rhs};
    ArrayNum* result = alloc_ndim_arraynum(ELEM_FLOAT64, b.vector ? 1 : 2, shape);
    if (!result) return ItemError;
    // Rescale with binary exponents so b_scale/a_scale cannot overflow first.
    int ae, be;
    double factor = frexp(b_scale, &be) / frexp(a_scale, &ae);
    for (size_t i = 0; i < n; i++) for (size_t j = 0; j < rhs; j++) {
        int xe;
        double value = b.values[i * rhs + j];
        value = scalbn(frexp(value, &xe) * factor, xe + be - ae);
        if (!isfinite(value)) return science_error("math linear solve: solution exceeds finite float range");
        result->float_items[order[i] * rhs + j] = value;
    }
    return {.array_num = result};
}

Item fn_math_solve(Item a, Item b) { return science_linear_solve(a, b, false); }
Item fn_math_lstsq(Item a, Item b) { return science_linear_solve(a, b, true); }

// Shewchuk's stage-A filter ("Adaptive Precision Floating-Point Arithmetic and
// Fast Robust Geometric Predicates", 1997): the binary64 determinant's sign is
// exact whenever |det| exceeds errbound * permanent. Only undecided cases pay
// for the exact decimal path, which costs tens of microseconds per call.
// The bounds assume no underflow or overflow, so the filter declines unless
// every nonzero coordinate difference lies in [1e-60, 1e60] (products of four
// stay normal). Each product is its own statement so a contracted FMA cannot
// replace the separately rounded operations the bounds were derived for.
static bool science_predicate_filter(const double* p, int count, int dimension, bool lifted, int* sign) {
    const double eps = 1.1102230246251565e-16;  // 2^-53
    int rank = count - 1;
    double d[9];
    for (int row = 0; row < rank; row++) {
        for (int col = 0; col < dimension; col++) {
            double v = p[row * dimension + col] - p[rank * dimension + col];
            double m = fabs(v);
            if (v != 0 && (m < 1e-60 || m > 1e60)) return false;
            d[row * 3 + col] = v;
        }
    }
    double det, permanent, bound;
    if (count == 3) {
        double left = d[0] * d[4];
        double right = d[1] * d[3];
        det = left - right;
        permanent = fabs(left) + fabs(right);
        bound = (3.0 + 16.0 * eps) * eps;
    } else {
        double t0 = d[3] * d[7];
        double t1 = d[6] * d[4];
        double t2 = d[6] * d[1];
        double t3 = d[0] * d[7];
        double t4 = d[0] * d[4];
        double t5 = d[3] * d[1];
        double a2, b2, c2;
        if (lifted) {
            double ax2 = d[0] * d[0];
            double ay2 = d[1] * d[1];
            double bx2 = d[3] * d[3];
            double by2 = d[4] * d[4];
            double cx2 = d[6] * d[6];
            double cy2 = d[7] * d[7];
            a2 = ax2 + ay2;
            b2 = bx2 + by2;
            c2 = cx2 + cy2;
            bound = (10.0 + 96.0 * eps) * eps;
        } else {
            a2 = d[2];
            b2 = d[5];
            c2 = d[8];
            bound = (7.0 + 56.0 * eps) * eps;
        }
        double ta = a2 * (t0 - t1);
        double tb = b2 * (t2 - t3);
        double tc = c2 * (t4 - t5);
        det = ta + tb + tc;
        double pa = (fabs(t0) + fabs(t1)) * fabs(a2);
        double pb = (fabs(t2) + fabs(t3)) * fabs(b2);
        double pc = (fabs(t4) + fabs(t5)) * fabs(c2);
        permanent = pa + pb + pc;
    }
    double errbound = bound * permanent;
    if (det > errbound) { *sign = 1; return true; }
    if (-det > errbound) { *sign = -1; return true; }
    return false;
}

static Item science_predicate(Item* points, int count, int dimension, bool lifted) {
    RootFrame roots(4);
    Rooted<Item> a(roots, points[0]), b(roots, points[1]), c(roots, points[2]);
    Rooted<Item> d(roots, count == 4 ? points[3] : ItemNull);
    Rooted<Item>* rooted[] = {&a, &b, &c, &d};
    for (int i = 0; i < count; i++) if (item_is_error(rooted[i]->get())) return rooted[i]->get();
    for (int i = 0; i < count; i++) if (get_type_id(rooted[i]->get()) == LMD_TYPE_NULL) return ItemNull;
    double coordinates[12];
    for (int i = 0; i < count; i++) {
        if (vector_is_ndim(rooted[i]->get()) || vector_length(rooted[i]->get()) != dimension)
            return science_error("math geometry predicate: point has the wrong dimension");
        for (int j = 0; j < dimension; j++)
            if (!science_real(vector_get(rooted[i]->get(), j), &coordinates[i * dimension + j]))
                return science_error("math geometry predicate: coordinates must convert to finite real floats");
    }
    int sign;
    if (science_predicate_filter(coordinates, count, dimension, lifted, &sign)) return {.item = i2it(sign)};
    if (!decimal_geometry_sign(coordinates, count, dimension, lifted, &sign)) return ItemError;
    return {.item = i2it(sign)};
}

Item fn_math_orient2d(Item a, Item b, Item c) {
    Item points[] = {a, b, c};
    return science_predicate(points, 3, 2, false);
}
Item fn_math_orient3d(Item a, Item b, Item c, Item d) {
    Item points[] = {a, b, c, d};
    return science_predicate(points, 4, 3, false);
}
Item fn_math_incircle(Item a, Item b, Item c, Item d) {
    Item points[] = {a, b, c, d};
    return science_predicate(points, 4, 2, true);
}

// the native kernel sees only flat numbers; keep every source/row rooted while
// vector_get boxes lanes, and publish a boxed int array (D5.3.3, D6.4.1).
Item fn_math_wfc(Item key, Item weights, Item masks, Item control) {
    GUARD_ERROR3(key, weights, masks);
    GUARD_ERROR1(control);
    GUARD_NULL1(key);
    GUARD_NULL1(weights);
    GUARD_NULL1(masks);
    GUARD_NULL1(control);
    RootFrame roots(6);
    Rooted<Item> rooted_key(roots, key), rooted_weights(roots, weights);
    Rooted<Item> rooted_masks(roots, masks), rooted_control(roots, control);
    Rooted<Item> direction(roots, ItemNull), row(roots, ItemNull);
    if (get_type_id(key) != LMD_TYPE_UINT64)
        return science_error("math.wfc: key must be u64");
    uint64_t seed = rooted_key.get().get_uint64();
    if (vector_length(control) != 4 || vector_is_ndim(control))
        return science_error("math.wfc: control must be [width, height, periodic, retries]");
    int64_t sizes[3];
    const int indices[] = {0, 1, 3};
    for (int i = 0; i < 3; i++) {
        Item value = vector_get(rooted_control.get(), indices[i]);
        if (get_type_id(value) != LMD_TYPE_INT)
            return science_error("math.wfc: dimensions and retries must be positive ints");
        // machine ints are inline; get_int64() is only for pointer-backed i64.
        sizes[i] = lambda_int_item_to_lane(value.item);
    }
    if (sizes[0] < 1 || sizes[1] < 1 || sizes[0] > 10000 || sizes[1] > 10000 ||
            sizes[0] * sizes[1] > 10000 || sizes[2] < 1)
        return science_error("math.wfc: expected at most 10,000 cells and positive retries");
    Item periodic = vector_get(rooted_control.get(), 2);
    if (get_type_id(periodic) != LMD_TYPE_BOOL)
        return science_error("math.wfc: periodic must be bool");
    bool wrap = it2b(periodic);
    int64_t count = vector_length(rooted_weights.get());
    if (count < 1 || count > 1000 || vector_is_ndim(rooted_weights.get()))
        return science_error("math.wfc: weights must contain 1..1000 patterns");
    int t = (int)count, cells = (int)(sizes[0] * sizes[1]);
    ScienceScratch weight_memory((size_t)t, sizeof(double));
    ScienceScratch mask_memory((size_t)4 * t * t, sizeof(uint8_t));
    ScienceScratch output_memory((size_t)cells, sizeof(int));
    if (!weight_memory.data || !mask_memory.data || !output_memory.data)
        return science_error("math.wfc: cannot allocate boundary scratch");
    double* ws = (double*)weight_memory.data;
    uint8_t* ms = (uint8_t*)mask_memory.data;
    double total = 0.0;
    for (int p = 0; p < t; p++) {
        Item value = vector_get(rooted_weights.get(), p);
        TypeId type = get_type_id(value);
        if ((type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT) ||
                !science_real(value, &ws[p]) || ws[p] <= 0.0)
            return science_error("math.wfc: weights must be positive finite int or float values");
        total += ws[p];
    }
    // integer prefixes must remain exactly representable in binary64, matching cumsum.
    if (total > 9007199254740992.0)
        return science_error("math.wfc: weight sum must be at most 2^53");
    // masks may be packed N-D arrays: item_at/len walk rows, vector_get walks leaves.
    if (fn_len(rooted_masks.get()) != 4)
        return science_error("math.wfc: masks must have shape [4, patterns, patterns]");
    for (int d = 0; d < 4; d++) {
        direction.set(item_at(rooted_masks.get(), d));
        if (fn_len(direction.get()) != t)
            return science_error("math.wfc: masks must have shape [4, patterns, patterns]");
        for (int p = 0; p < t; p++) {
            row.set(item_at(direction.get(), p));
            if (vector_length(row.get()) != t || vector_is_ndim(row.get()))
                return science_error("math.wfc: masks must have shape [4, patterns, patterns]");
            for (int q = 0; q < t; q++) {
                Item value = vector_get(row.get(), q);
                if (get_type_id(value) != LMD_TYPE_INT)
                    return science_error("math.wfc: mask entries must be 0 or 1");
                int64_t bit = lambda_int_item_to_lane(value.item);
                if (bit != 0 && bit != 1)
                    return science_error("math.wfc: mask entries must be 0 or 1");
                ms[(d * t + p) * t + q] = (uint8_t)bit;
            }
        }
    }
    int* output = (int*)output_memory.data;
    ProcgenWfcResult status = procgen_wfc(seed, (int)sizes[0], (int)sizes[1], wrap, sizes[2], t, ws, ms, output);
    if (status == PROCGEN_WFC_CONTRADICTION)
        return science_error("math.wfc: contradiction within retry budget");
    if (status != PROCGEN_WFC_OK) return science_error("math.wfc: cannot allocate solver scratch");
    ArrayNum* result = array_int_new(cells);
    if (!result) return science_error("math.wfc: cannot allocate result");
    for (int c = 0; c < cells; c++) result->items[c] = output[c];
    return {.array_num = result};
}
