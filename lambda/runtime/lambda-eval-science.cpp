// Pure scientific kernels shared by T0 and MIR (D6.4.1, D8.1.1).
#include "transpiler.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-error.h"
#include "../../lib/math_checked.hpp"
#include "../../lib/memtrack.h"
#include "../../lib/procgen/wfc.h"
#include "../../lib/procgen/erosion.h"
#include "../../lib/procgen/delaunay.h"
#include "../../lib/procgen/rng.h"
#include "../../lib/procgen/geometry_buffer.h"
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

static bool science_int_at(Item input, int index, int64_t* value) {
    Item item = vector_get(input, index);
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    *value = lambda_int_item_to_lane(item.item);
    return true;
}

Item fn_math_rng_keys(Item key, Item paths) {
    GUARD_ERROR2(key, paths);
    GUARD_NULL1(key); GUARD_NULL1(paths);
    RootFrame roots(4);
    Rooted<Item> seed(roots, key), input(roots, paths), path(roots, ItemNull);
    int64_t count = fn_len(input.get());
    if (get_type_id(seed.get()) != LMD_TYPE_UINT64 || vector_length(input.get()) < 0 || count < 0 || count > 1000000)
        return science_error("math.rng_keys: expected a u64 key and at most 1,000,000 paths");
    uint64_t bits = seed.get().get_uint64();
    Rooted<ArrayNum*> result(roots, array_num_new(ELEM_UINT64, count));
    if (!result.get()) return ItemError;
    int64_t total = 0;
    for (int64_t i = 0; i < count; i++) {
        // rectangular numeric paths can be packed; item_at retains their row shape.
        path.set(item_at(input.get(), i));
        int64_t length = vector_length(path.get());
        if (length < 0 || vector_is_ndim(path.get()) || length > 1000000 - total)
            return science_error("math.rng_keys: expected flat paths, at most 1,000,000 total parts");
        total += length;
        uint64_t value = bits;
        for (int64_t j = 0; j < length; j++) {
            Item part = vector_get(path.get(), j);
            TypeId type = get_type_id(part);
            uint64_t token;
            if (type == LMD_TYPE_STRING) {
                if (!procgen_text_key(part.get_chars(), part.get_len(), &token))
                    return science_error("math.rng_keys: invalid UTF-8 path part");
            } else if (type == LMD_TYPE_UINT64) token = part.get_uint64();
            else if (type == LMD_TYPE_INT) token = (uint64_t)lambda_int_item_to_lane(part.item);
            else if (type == LMD_TYPE_INT64) token = (uint64_t)part.get_int64();
            else return science_error("math.rng_keys: path parts must be strings or integers");
            value = procgen_key_step(value, token, (uint64_t)j);
        }
        ((uint64_t*)result.get()->data)[i] = value;
    }
    return {.array_num = result.get()};
}

Item fn_math_rng_uniform(Item keys, Item counters) {
    GUARD_ERROR2(keys, counters);
    GUARD_NULL1(keys); GUARD_NULL1(counters);
    RootFrame roots(3);
    Rooted<Item> input(roots, keys), indices(roots, counters);
    int64_t rows = vector_length(input.get()), columns = vector_length(indices.get());
    if (rows < 0 || columns < 0 || rows > 1000000 || columns > 1000000 ||
            vector_is_ndim(input.get()) || vector_is_ndim(indices.get()) ||
            (rows && columns > 1000000 / rows))
        return science_error("math.rng_uniform: expected flat keys/counters, at most 1,000,000 draws");
    // retain inputs and result while vector access boxes packed u64 lanes (D5.3.3).
    Rooted<ArrayNum*> result(roots, array_num_new(ELEM_FLOAT64, rows * columns));
    if (!result.get()) return ItemError;
    for (int64_t j = 0; j < columns; j++) {
        Item index = vector_get(indices.get(), j);
        if (get_type_id(index) != LMD_TYPE_INT) return science_error("math.rng_uniform: counters must be ints");
    }
    for (int64_t i = 0; i < rows; i++) {
        Item key = vector_get(input.get(), i);
        if (get_type_id(key) != LMD_TYPE_UINT64) return science_error("math.rng_uniform: keys must be u64");
        uint64_t bits = key.get_uint64();
        for (int64_t j = 0; j < columns; j++) {
            uint64_t index = (uint64_t)lambda_int_item_to_lane(vector_get(indices.get(), j).item);
            result.get()->float_items[i * columns + j] = math_splitmix64_unit(procgen_key_at(bits, index));
        }
    }
    return {.array_num = result.get()};
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
    if (procgen_predicate_filter(coordinates, count, dimension, lifted, &sign)) return {.item = i2it(sign)};
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
        if (!science_int_at(rooted_control.get(), indices[i], &sizes[i]))
            return science_error("math.wfc: dimensions and retries must be positive ints");
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

// native buffers contain no Items; matrix readers retain their sources during boxing (D5.3.3).
Item fn_math_hydraulic(Item key, Item field, Item control, Item settings) {
    GUARD_ERROR3(key, field, control); GUARD_ERROR1(settings);
    GUARD_NULL1(key); GUARD_NULL1(field); GUARD_NULL1(control); GUARD_NULL1(settings);
    RootFrame roots(4);
    Rooted<Item> k(roots, key), f(roots, field), c(roots, control), s(roots, settings);
    if (get_type_id(k.get()) != LMD_TYPE_UINT64)
        return science_error("math.hydraulic: key must be u64");
    uint64_t seed = k.get().get_uint64();
    int64_t limits[5];
    if (vector_length(c.get()) != 5 || vector_is_ndim(c.get()))
        return science_error("math.hydraulic: control must be [width, height, droplets, batch, steps]");
    for (int i = 0; i < 5; i++) if (!science_int_at(c.get(), i, &limits[i]))
        return science_error("math.hydraulic: controls must be ints");
    if (limits[0] < 2 || limits[1] < 2 || limits[0] > 250000 || limits[1] > 250000 ||
            limits[0] * limits[1] > 250000 || limits[2] < 0 || limits[2] > 1000000 ||
            limits[3] < 1 || limits[3] > 1000000 || limits[4] < 0 || limits[4] > 1000)
        return science_error("math.hydraulic: control limits exceeded");
    ScienceMatrix values, parameters;
    if (!science_read_matrix(f.get(), &values) || !values.vector || values.rows != (size_t)(limits[0] * limits[1]))
        return science_error("math.hydraulic: field must be a finite flat numeric vector matching the dimensions");
    if (!science_read_matrix(s.get(), &parameters) || !parameters.vector || parameters.rows != 7)
        return science_error("math.hydraulic: settings must contain seven finite reals");
    double* p = parameters.values;
    if (p[0] < 0 || p[0] > 1 || p[1] < 0 || p[2] < 0 || p[3] < 0 || p[3] > 1 ||
            p[4] < 0 || p[4] > 1 || p[5] < 0 || p[5] > 1 || p[6] < 0)
        return science_error("math.hydraulic: rates must be in [0,1], capacity and gravity nonnegative");
    ProcgenErosion config = {p[0], p[1], p[2], p[3], p[4], p[5], p[6], (int)limits[4]};
    ScienceScratch output(values.rows, sizeof(double));
    if (!output.data || !procgen_hydraulic(seed, values.values, (int)limits[0], (int)limits[1],
            (int)limits[2], (int)limits[3], &config, (double*)output.data))
        return science_error("math.hydraulic: allocation failure or non-finite arithmetic");
    ArrayNum* result = array_num_new(ELEM_FLOAT64, (int64_t)values.rows);
    if (!result) return ItemError;
    memcpy(result->float_items, output.data, values.rows * sizeof(double));
    return {.array_num = result};
}

// the caller chooses unique input order and a CCW seed; lib owns only numeric topology.
Item fn_math_delaunay(Item points, Item ids, Item seed) {
    GUARD_ERROR3(points, ids, seed);
    GUARD_NULL1(points); GUARD_NULL1(ids); GUARD_NULL1(seed);
    RootFrame roots(3);
    Rooted<Item> p(roots, points), indices(roots, ids), initial(roots, seed);
    ScienceMatrix coordinates;
    if (!science_read_matrix(p.get(), &coordinates) || coordinates.rows > 20000 ||
            (coordinates.rows && (coordinates.vector || coordinates.cols != 2)))
        return science_error("math.delaunay: expected at most 20,000 finite 2D points");
    int n = (int)coordinates.rows;
    int64_t used = vector_length(indices.get());
    if (used < 0 || used > n || vector_is_ndim(indices.get()))
        return science_error("math.delaunay: invalid unique index vector");
    if (!n && !used && vector_length(initial.get()) == 0) {
        ArrayNum* empty = array_int_new(0);
        return empty ? Item{.array_num = empty} : ItemError;
    }
    if (used < 3 || vector_length(initial.get()) != 3 || vector_is_ndim(initial.get()))
        return science_error("math.delaunay: expected three seed indices");
    ScienceScratch id_memory((size_t)used, sizeof(int));
    ScienceScratch seen_memory((size_t)n, sizeof(uint8_t));
    ScienceScratch output_memory((size_t)(2 * n + 4) * 3, sizeof(int));
    if (!id_memory.data || !seen_memory.data || !output_memory.data) return ItemError;
    int* order = (int*)id_memory.data;
    uint8_t* seen = (uint8_t*)seen_memory.data;
    for (int i = 0; i < used; i++) {
        int64_t index;
        if (!science_int_at(indices.get(), i, &index) || index < 0 || index >= n || seen[index])
            return science_error("math.delaunay: indices must be distinct and in range");
        order[i] = (int)index;
        seen[index] = 1;
    }
    int first[3];
    double triangle[6];
    for (int i = 0; i < 3; i++) {
        int64_t index;
        if (!science_int_at(initial.get(), i, &index) || index < 0 || index >= n || !seen[index])
            return science_error("math.delaunay: seed indices must belong to the input order");
        first[i] = (int)index;
        memcpy(triangle + i * 2, coordinates.values + index * 2, 2 * sizeof(double));
    }
    int sign;
    if (!procgen_predicate_filter(triangle, 3, 2, false, &sign) &&
            !decimal_geometry_sign(triangle, 3, 2, false, &sign)) return ItemError;
    if (sign <= 0) return science_error("math.delaunay: seed must be non-collinear and counterclockwise");
    int triangles = 0;
    int* output = (int*)output_memory.data;
    if (!procgen_delaunay(coordinates.values, n, order, (int)used, first,
            decimal_geometry_sign, output, &triangles))
        return science_error("math.delaunay: allocation, predicate or topology failure");
    ArrayNum* result = array_int_new((int64_t)triangles * 3);
    if (!result) return ItemError;
    for (int i = 0; i < triangles * 3; i++) result->items[i] = output[i];
    return {.array_num = result};
}

Item fn_math_transform_buffer(Item matrix, Item positions, Item normals) {
    GUARD_ERROR3(matrix, positions, normals);
    GUARD_NULL1(matrix); GUARD_NULL1(positions); GUARD_NULL1(normals);
    RootFrame roots(6);
    Rooted<Item> m(roots,matrix), p(roots,positions), n(roots,normals);
    ScienceMatrix transform, vertices, directions;
    if (!science_read_matrix(m.get(),&transform) || !transform.vector || transform.rows!=16 ||
            !science_read_matrix(p.get(),&vertices) || !vertices.vector || vertices.rows%3 || vertices.rows>3000000 ||
            !science_read_matrix(n.get(),&directions) || !directions.vector || directions.rows!=vertices.rows)
        return science_error("math.transform_buffer: expected a 4x4 matrix and matching flat xyz buffers, at most 1,000,000 vertices");
    for (size_t i=0;i<16;i++) if (fabs(transform.values[i])>3.4028234e38)
        return science_error("math.transform_buffer: matrix exceeds affine range");
    for (size_t i=0;i<vertices.rows;i++) if (fabs(vertices.values[i])>3.4028234e38 || fabs(directions.values[i])>3.4028234e38)
        return science_error("math.transform_buffer: position or normal exceeds affine range");
    Rooted<ArrayNum*> out_p(roots,array_num_new(ELEM_FLOAT64,vertices.rows));
    Rooted<ArrayNum*> out_n(roots,array_num_new(ELEM_FLOAT64,vertices.rows));
    if (!out_p.get() || !out_n.get()) return ItemError;
    if (!procgen_transform_buffer(transform.values,vertices.values,directions.values,vertices.rows/3,
            out_p.get()->float_items,out_n.get()->float_items))
        return science_error("math.transform_buffer: matrix must be invertible and affine");
    Rooted<Array*> result(roots,array_plain());
    if (!result.get()) return ItemError;
    array_push_verbatim(result.get(),Item{.array_num=out_p.get()});
    array_push_verbatim(result.get(),Item{.array_num=out_n.get()});
    return {.array=result.get()};
}

Item fn_math_polygon_distances(Item points, Item outline) {
    GUARD_ERROR2(points,outline);
    GUARD_NULL1(points); GUARD_NULL1(outline);
    RootFrame roots(2);
    Rooted<Item> p(roots,points), ring(roots,outline);
    ScienceMatrix coordinates, boundary;
    if (!science_read_matrix(p.get(),&coordinates) || (coordinates.rows && (coordinates.vector || coordinates.cols!=2)) ||
            !science_read_matrix(ring.get(),&boundary) || boundary.vector || boundary.cols!=2 || !boundary.rows ||
            coordinates.rows>1000000 || boundary.rows>4096 || coordinates.rows*boundary.rows>100000000)
        return science_error("math.polygon_distances: expected finite 2D points and a nonempty outline, at most 100,000,000 edge comparisons");
    // bounded coordinates keep squared distances finite; the script oracle handles larger domains.
    for (size_t i=0;i<coordinates.rows*coordinates.cols;i++) if (fabs(coordinates.values[i])>3.4028234e38)
        return science_error("math.polygon_distances: point exceeds coordinate range");
    for (size_t i=0;i<boundary.rows*boundary.cols;i++) if (fabs(boundary.values[i])>3.4028234e38)
        return science_error("math.polygon_distances: outline exceeds coordinate range");
    ArrayNum* result=array_num_new(ELEM_FLOAT64,coordinates.rows);
    if (!result) return ItemError;
    procgen_polygon_distances(coordinates.values,coordinates.rows,boundary.values,boundary.rows,result->float_items);
    return {.array_num=result};
}
