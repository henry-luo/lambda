# Lambda Numeric Vector Type Design

## Overview

This document defines the design for **numeric vector support** in Lambda Script, enabling efficient element-wise arithmetic operations similar to NumPy, R, and Julia. The goal is to make Lambda a first-class citizen for data processing and scientific computing while maintaining its pure functional nature.

### Design Goals

1. **NumPy-like Semantics**: Familiar element-wise operations and broadcasting rules
2. **Type Safety**: Compile-time type checking with automatic type promotion
3. **Performance**: JIT-optimized homogeneous vectors with SIMD potential
4. **Integration**: Seamless compatibility with existing Lambda collections and functions
5. **Simplicity**: Minimal syntax changes, leverage existing `[]` array notation

---

## Language Comparison: Best Practices

### NumPy (Python)

NumPy provides the gold standard for array computing:

```python
import numpy as np
a = np.array([1, 2, 3])
b = np.array([4, 5, 6])
a + b          # [5, 7, 9]
a * 2          # [2, 4, 6]
np.sum(a)      # 6
np.mean(a)     # 2.0
a > 2          # [False, False, True]
```

**Best Practices Adopted**:
- Element-wise operations by default
- Scalar broadcasting to vector
- Type promotion (int → float when needed)
- Rich set of aggregation functions

### R

R treats all values as vectors by default:

```r
a <- c(1, 2, 3)
b <- c(4, 5, 6)
a + b          # [5, 7, 9]
a * 2          # [2, 4, 6]
sum(a)         # 6
mean(a)        # 2
a > 2          # [FALSE, FALSE, TRUE]
a[a > 2]       # [3] (filtering)
```

**Best Practices Adopted**:
- Recycling rule (shorter vector repeats) - *adopted as opt-in broadcasting*
- Vectorized comparison returning boolean vector
- Boolean indexing for filtering

### Julia

Julia excels at high-performance computing with explicit vectorization:

```julia
a = [1, 2, 3]
b = [4, 5, 6]
a .+ b         # [5, 7, 9] (explicit broadcasting with dot)
a * 2          # Error! (scalar multiplication needs dot)
a .* 2         # [2, 4, 6]
sum(a)         # 6
mean(a)        # 2.0
a .> 2         # [false, false, true]
```

**Best Practices Adopted**:
- Strong type system with automatic specialization
- SIMD optimization for homogeneous arrays
- Clear distinction between matrix and element-wise ops

### Design Decision: Implicit vs Explicit Vectorization

| Language | Approach | Lambda Adoption |
|----------|----------|-----------------|
| NumPy | Implicit element-wise for ndarray | ✅ Yes |
| R | Implicit for all vectors | ✅ Yes |
| Julia | Explicit with `.` operator | ❌ No (too verbose) |

**Lambda Choice**: Implicit element-wise operations like NumPy/R. The `+`, `-`, `*`, `/`, `%`, `^` operators automatically vectorize when applied to arrays.

---

## Type System Integration

### Vector Types in Lambda

Lambda supports vectorized arithmetic on multiple collection types:

| Type         | Lambda Type ID         | Syntax        | Element Type | Use Case                   |
| ------------ | ---------------------- | ------------- | ------------ | -------------------------- |
| `List`       | `LMD_TYPE_LIST`        | `(1, 2, 3)`   | `Item`       | Immutable tuples           |
| `Range`      | `LMD_TYPE_RANGE`       | `1 to 3`      | `int`        | Lazy integer sequences     |
| `ArrayLong`  | `LMD_TYPE_ARRAY_INT`   | `[1, 2, 3]`   | `int32`      | Integer vectors            |
| `ArrayInt64` | `LMD_TYPE_ARRAY_INT64` | `[1L, 2L]`    | `int64`      | Large integer vectors      |
| `ArrayFloat` | `LMD_TYPE_ARRAY_FLOAT` | `[1.0, 2.0]`  | `float64`    | Floating-point vectors     |
| `Array`      | `LMD_TYPE_ARRAY`       | `[1, "a"]`    | `Item`       | Heterogeneous (fallback)   |

**All collection types support element-wise arithmetic**:

```lambda
// Arrays (mutable)
[1, 2, 3] + 10             // [11, 12, 13]

// Lists (immutable tuples)
(1, 2, 3) + 10             // (11, 12, 13)

// Ranges (lazy sequences)
(1 to 5) * 2               // [2, 4, 6, 8, 10]
```

**Range Materialization**: When a range participates in arithmetic, it materializes to an array for the result.

### Type Promotion Rules

When performing arithmetic between different types, Lambda follows NumPy-style promotion:

```
Operand Type A    Operand Type B    Result Type
─────────────────────────────────────────────────
int               int               int
int               int64             int64
int               float             float
int64             float             float
float             float             float

List              scalar            List
Range             scalar            Array (materialized)
ArrayLong         int               ArrayLong
ArrayLong         float             ArrayFloat
ArrayLong         ArrayLong         ArrayLong
ArrayLong         ArrayFloat        ArrayFloat
ArrayFloat        scalar            ArrayFloat
ArrayFloat        ArrayFloat        ArrayFloat

scalar            Array*            Array* (same type)
scalar            List              List
scalar            Range             Array (materialized)
```

**Division Rule**: Division (`/`) always produces `float`/`ArrayFloat` unless using integer division (`_/`).

### Type Inference Examples

```lambda
[1, 2, 3]                      // ArrayLong
[1.0, 2.0, 3.0]                // ArrayFloat
[1, 2.5, 3]                    // ArrayFloat (promoted)

[1, 2, 3] + [4, 5, 6]          // ArrayLong
[1, 2, 3] + [4.0, 5.0, 6.0]    // ArrayFloat (promoted)
[1, 2, 3] + 1.5                // ArrayFloat (promoted)
[1, 2, 3] / 2                  // ArrayFloat (division rule)
[1, 2, 3] _/ 2                 // ArrayLong (integer division)
```

---

## Arithmetic Operations

### Scalar-Vector Operations

When a scalar and vector are operands, the scalar broadcasts to match the vector size:

```lambda
// Scalar + Vector (scalar broadcasts)
1 + [2, 3, 4]              // [3, 4, 5]
10 - [1, 2, 3]             // [9, 8, 7]
3 * [1, 2, 3]              // [3, 6, 9]
12 / [2, 3, 4]             // [6.0, 4.0, 3.0]
2 ^ [1, 2, 3]              // [2, 4, 8]
10 % [3, 4, 6]             // [1, 2, 4]

// Vector + Scalar (order matters for non-commutative ops)
[10, 20, 30] - 5           // [5, 15, 25]
[10, 20, 30] / 5           // [2.0, 4.0, 6.0]
[2, 3, 4] ^ 2              // [4, 9, 16]
```

### Vector-Vector Operations

Element-wise operations between vectors of the same length:

```lambda
// Same-length vectors
[1, 2, 3] + [4, 5, 6]      // [5, 7, 9]
[10, 20, 30] - [1, 2, 3]   // [9, 18, 27]
[2, 3, 4] * [1, 2, 3]      // [2, 6, 12]
[12, 15, 18] / [3, 5, 6]   // [4.0, 3.0, 3.0]
[2, 3, 4] ^ [1, 2, 3]      // [2, 9, 64]

// Mixed types (auto-promotion)
[1, 2, 3] + [1.5, 2.5, 3.5]  // [2.5, 4.5, 6.5] (ArrayFloat)
```

### Size Mismatch Behavior

**Default Behavior**: Vector-vector operations require matching lengths; mismatch causes error.

```lambda
[1, 2] + [3, 4, 5]         // Error: size mismatch (2 vs 3)
```

**Broadcasting Exception**: Single-element vectors broadcast to any size:

```lambda
[5] + [1, 2, 3]            // [6, 7, 8] (single-element broadcasts)
[1, 2, 3] * [2]            // [2, 4, 6]
```

**Rationale**: Full broadcasting (like NumPy's) adds complexity and can mask bugs. Single-element broadcasting covers 90% of use cases with minimal risk.

### Empty Vector Handling

```lambda
[] + []                    // []
[] + 5                     // []
5 * []                     // []
sum([])                    // 0
avg([])                    // null (undefined for empty)
len([])                    // 0
```

### Operator Summary

| Operator | Name | Scalar-Vector | Vector-Vector | Notes |
|----------|------|---------------|---------------|-------|
| `+` | Add | ✅ | ✅ | Element-wise |
| `-` | Subtract | ✅ | ✅ | Order matters |
| `*` | Multiply | ✅ | ✅ | Element-wise |
| `/` | Divide | ✅ | ✅ | Always float result |
| `_/` | Int Divide | ✅ | ✅ | Integer result |
| `%` | Modulo | ✅ | ✅ | Integer remainder |
| `^` | Power | ✅ | ✅ | Element-wise |

---

## Comparison Operations (Future Extension)

Comparison operators return boolean vectors:

```lambda
[1, 2, 3] > 2              // [false, false, true]
[1, 2, 3] == [1, 0, 3]     // [true, false, true]
[1, 2, 3] >= [1, 2, 2]     // [true, true, true]
```

Boolean vectors enable filtering:

```lambda
let values = [10, 25, 5, 30, 15]
let mask = values > 12
filter(values, mask)       // [25, 30, 15]
```

---

## System Functions for Vectors

### Aggregation Functions

Functions that reduce a vector to a scalar:

| Function | Description | Example | Result | Status |
|----------|-------------|---------|--------|--------|
| `sum(vec)` | Sum of elements | `sum([1, 2, 3])` | `6` | ✅ |
| `avg(vec)` | Arithmetic mean | `avg([1, 2, 3])` | `2.0` | ✅ |
| `min(vec)` | Minimum element | `min([3, 1, 2])` | `1` | ✅ |
| `max(vec)` | Maximum element | `max([3, 1, 2])` | `3` | ✅ |
| `len(vec)` | Number of elements | `len([1, 2, 3])` | `3` | ✅ |
| `prod(vec)` | Product of elements | `prod([2, 3, 4])` | `24` | ✅ |

### Statistical Functions

| Function           | Description        | Example                    | Result     | Status |
| ------------------ | ------------------ | -------------------------- | ---------- | ------ |
| `mean(vec)`        | Alias for avg      | `mean([1, 2, 3])`          | `2.0`      | ✅ |
| `median(vec)`      | Median value       | `median([1, 3, 2])`        | `2`        | ✅ |
| `deviation(vec)`   | Standard deviation | `deviation([1, 2, 3])`     | `0.816...` | ✅ |
| `variance(vec)`    | Variance           | `variance([1, 2, 3])`      | `0.666...` | ✅ |
| `quantile(vec, p)` | p-th quantile      | `quantile([1,2,3,4], 0.5)` | `2.5`      | ✅ |

### Element-wise Math Functions

Functions that apply to each element and return a vector:

| Function     | Description     | Example               | Result        | Status |
| ------------ | --------------- | --------------------- | ------------- | ------ |
| `abs(vec)`   | Absolute value  | `abs([-1, 2, -3])`    | `[1, 2, 3]`   | ✅ |
| `sqrt(vec)`  | Square root     | `sqrt([1, 4, 9])`     | `[1, 2, 3]`   | ✅ |
| `log(vec)`   | Natural log     | `log([1, e, e^2])`    | `[0, 1, 2]`   | ✅ |
| `log10(vec)` | Base-10 log     | `log10([1, 10, 100])` | `[0, 1, 2]`   | ✅ |
| `exp(vec)`   | Exponential     | `exp([0, 1, 2])`      | `[1, e, e^2]` | ✅ |
| `sin(vec)`   | Sine            | `sin([0, π/2, π])`    | `[0, 1, 0]`   | ✅ |
| `cos(vec)`   | Cosine          | `cos([0, π/2, π])`    | `[1, 0, -1]`  | ✅ |
| `tan(vec)`   | Tangent         | `tan([0, π/4])`       | `[0, 1]`      | ✅ |
| `floor(vec)` | Floor           | `floor([1.7, 2.3])`   | `[1, 2]`      | ✅ |
| `ceil(vec)`  | Ceiling         | `ceil([1.2, 2.8])`    | `[2, 3]`      | ✅ |
| `round(vec)` | Round           | `round([1.4, 1.6])`   | `[1, 2]`      | ✅ |
| `sign(vec)`  | Sign (-1, 0, 1) | `sign([-5, 0, 3])`    | `[-1, 0, 1]`  | ✅ |

### Vector Construction Functions

| Function            | Description             | Example           | Result            | Status |
| ------------------- | ----------------------- | ----------------- | ----------------- | ------ |
| `fill(n, v)`        | Vector of n copies of v | `fill(3, 5)`      | `[5, 5, 5]`       | ✅ |
| `range(a, b, step)` | Range with step         | `range(0, 10, 2)` | `[0, 2, 4, 6, 8]` | ✅ |

### Vector Manipulation Functions

| Function | Description | Example | Result | Status |
|----------|-------------|---------|--------|--------|
| `reverse(vec)` | Reverse order | `reverse([1, 2, 3])` | `[3, 2, 1]` | ✅ |
| `sort(vec)` | Sort ascending | `sort([3, 1, 2])` | `[1, 2, 3]` | ✅ |
| `sort(vec, 'desc')` | Sort descending | `sort([1, 2, 3], 'desc')` | `[3, 2, 1]` | ✅ |
| `unique(vec)` | Remove duplicates | `unique([1, 2, 2, 3])` | `[1, 2, 3]` | ✅ |
| `concat(v1, v2)` | Concatenate | `concat([1, 2], [3, 4])` | `[1, 2, 3, 4]` | ✅ |
| `slice(vec, i, j)` | Extract slice | `slice([1,2,3,4], 1, 3)` | `[2, 3]` | ✅ |
| `take(vec, n)` | First n elements | `take([1, 2, 3], 2)` | `[1, 2]` | ✅ |
| `drop(vec, n)` | Drop first n | `drop([1, 2, 3], 1)` | `[2, 3]` | ✅ |

### Reduction Functions

| Function       | Description        | Example                  | Result      | Status |
| -------------- | ------------------ | ------------------------ | ----------- | ------ |
| `all(vec)`     | All true?          | `all([true, true])`      | `true`      | ✅ |
| `any(vec)`     | Any true?          | `any([false, true])`     | `true`      | ✅ |
| `argmin(vec)`  | Index of min       | `argmin([3, 1, 2])`      | `1`         | ✅ |
| `argmax(vec)`  | Index of max       | `argmax([3, 1, 2])`      | `0`         | ✅ |
| `cumsum(vec)`  | Cumulative sum     | `cumsum([1, 2, 3])`      | `[1, 3, 6]` | ✅ |
| `cumprod(vec)` | Cumulative product | `cumprod([1, 2, 3])`     | `[1, 2, 6]` | ✅ |
| `dot(a, b)`    | Dot product        | `dot([1,2,3], [4,5,6])`  | `32`        | ✅ |
| `norm(vec)`    | Euclidean norm     | `norm([3, 4])`           | `5`         | ✅ |

### Functional Operations

| Function | Description | Example | Result | Status |
|----------|-------------|---------|--------|--------|
| `map(vec, fn)` | Apply function | `map([1, 2, 3], fn(x) => x*2)` | `[2, 4, 6]` | ✅ |
| `filter(vec, fn)` | Keep if true | `filter([1, 2, 3], fn(x) => x > 1)` | `[2, 3]` | ✅ |
| `reduce(vec, fn, init)` | Fold left | `reduce([1,2,3], fn(a,b) => a+b, 0)` | `6` | ✅ |
| `zip(v1, v2)` | Pair elements | `zip([1, 2], [3, 4])` | `[(1,3), (2,4)]` | ✅ |

---

## Test Script Examples

### Basic Operations (`vector_basic.ls`)

The test script demonstrates fundamental vector arithmetic:

```lambda
"=== Scalar + Vector Operations ==="
1 + [2, 3, 4]              // Expected: [3, 4, 5]
1.5 + [2, 3, 4]            // Expected: [3.5, 4.5, 5.5]
2.5 + [1.5, 2.5, 3.5]      // Expected: [4.0, 5.0, 6.0]

"=== Vector + Vector Operations ==="
[1, 2, 3] + [4, 5, 6]      // Expected: [5, 7, 9]
[1.5, 2.5] + [0.5, 1.5]    // Expected: [2.0, 4.0]

"=== Mixed Type Promotion ==="
[1, 2, 3] + [1.5, 2.5, 3.5]  // Expected: [2.5, 4.5, 6.5]
[1, 2, 3] / 2              // Expected: [0.5, 1.0, 1.5]

"=== Chained Operations ==="
[1, 2, 3] + [1, 1, 1] * 2      // Expected: [3, 4, 5] (precedence: * first)
([1, 2, 3] + [1, 1, 1]) * 2    // Expected: [4, 6, 8]
```

### Advanced Operations (`vector_advanced.ls`)

The test script covers complex scenarios:

```lambda
"=== Broadcasting ==="
[5] + [1, 2, 3]            // Expected: [6, 7, 8]
3 + [1, 2, 3, 4, 5]        // Expected: [4, 5, 6, 7, 8]

"=== Integration with Variables ==="
let vec1 = [1, 2, 3]
let vec2 = [4, 5, 6]
let scalar = 10
(vec1 + vec2) * scalar     // Expected: [50, 70, 90]

"=== Integration with Functions ==="
sum([1, 2, 3, 4])          // Expected: 10
avg([1, 2, 3, 4])          // Expected: 2.5
min([5, 2, 8, 1])          // Expected: 1
max([5, 2, 8, 1])          // Expected: 8

"=== Error Cases ==="
[1, 2] + [3, 4, 5]         // Expected: Error (size mismatch)
[1, 2, 3] / 0              // Expected: [inf, inf, inf] or Error
```

---

## Implementation Architecture

### Runtime Function Integration

Enhance existing arithmetic functions in `lambda-eval.cpp`:

```cpp
Item fn_add(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    // Existing scalar-scalar fast path
    if (is_scalar_numeric(ta) && is_scalar_numeric(tb)) {
        return fn_add_scalar(a, b);
    }
    
    // New: Vector operations
    if (is_vector_type(ta) || is_vector_type(tb)) {
        return fn_add_vectorized(a, b);
    }
    
    // Fallback
    return ItemError;
}
```

### Vectorized Operation Dispatch

```cpp
Item fn_add_vectorized(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    // Determine result type and dispatch
    if (is_scalar_numeric(ta) && is_array_type(tb)) {
        return fn_add_scalar_vector(a, b);
    }
    else if (is_array_type(ta) && is_scalar_numeric(tb)) {
        return fn_add_vector_scalar(a, b);
    }
    else if (is_array_type(ta) && is_array_type(tb)) {
        return fn_add_vector_vector(a, b);
    }
    return ItemError;
}
```

### Fast-Path Optimizations

For homogeneous arrays, use specialized loops:

```cpp
Item fn_add_arraylong_arraylong(ArrayLong* a, ArrayLong* b) {
    if (a->length != b->length) {
        if (a->length == 1) return fn_add_scalar_arraylong(a->items[0], b);
        if (b->length == 1) return fn_add_arraylong_scalar(a, b->items[0]);
        log_error("vector size mismatch: %ld vs %ld", a->length, b->length);
        return ItemError;
    }
    
    ArrayLong* result = array_long_new(a->length);
    for (long i = 0; i < a->length; i++) {
        result->items[i] = a->items[i] + b->items[i];
    }
    return (Item){ .array_long = result };
}
```

### SIMD Potential

For future optimization, structure data for SIMD alignment:

```cpp
#ifdef __AVX2__
void vector_add_float_simd(double* a, double* b, double* result, long n) {
    long i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        __m256d vr = _mm256_add_pd(va, vb);
        _mm256_storeu_pd(result + i, vr);
    }
    for (; i < n; i++) {
        result[i] = a[i] + b[i];
    }
}
#endif
```

---

## Transpiler Integration

### Type Detection at Compile Time

The transpiler can optimize when array types are known:

```cpp
void transpile_binary_expr(Transpiler* tp, AstBinaryNode* node) {
    AstType* left_type = node->left->resolved_type;
    AstType* right_type = node->right->resolved_type;
    
    bool left_is_array = is_array_ast_type(left_type);
    bool right_is_array = is_array_ast_type(right_type);
    
    if (left_is_array || right_is_array) {
        // Generate vectorized code path
        transpile_vectorized_binary(tp, node);
        return;
    }
    
    // Existing scalar code generation
    transpile_scalar_binary(tp, node);
}
```

### MIR Code Generation

Generate efficient loops for JIT:

```c
// For: [1, 2, 3] + [4, 5, 6]
// Generated MIR:
ArrayLong* _result = array_long_new(_left->length);
for (long _i = 0; _i < _left->length; _i++) {
    _result->items[_i] = _left->items[_i] + _right->items[_i];
}
```

---

## Error Handling

### Design Philosophy: No Exceptions, Per-Element Errors

Lambda follows a **continuation-based error model**: operations never throw exceptions. Instead, invalid operations on individual elements produce `ERROR` values, and computation continues for remaining elements.

This design enables:
1. **Partial results**: Valid elements are computed even when some fail
2. **Error propagation**: Downstream operations can detect and handle errors
3. **Debugging**: Error positions reveal exactly which elements failed
4. **Robustness**: Long-running computations don't abort on single failures

### Heterogeneous Vector Operations

When operating on mixed-type vectors, each element is processed independently:

```lambda
[3, "str", 5] + 1          // [4, ERROR, 6]
[10, null, 20] * 2         // [20, ERROR, 40]
[1, true, 3] + [1, 2, 3]   // [2, ERROR, 6]
```

The result vector has the same length as the input, with `ERROR` at positions where the operation was invalid.

### Error Conditions by Category

#### Per-Element Errors (Computation Continues)

| Condition | Behavior | Example | Result |
|-----------|----------|---------|--------|
| Type mismatch in element | `ERROR` at position | `[1, "x", 3] + 1` | `[2, ERROR, 4]` |
| Null element | `ERROR` at position | `[1, null, 3] * 2` | `[2, ERROR, 6]` |
| Division by zero | `inf`, `nan`, or `ERROR` | `[6, 4] / [2, 0]` | `[3, inf]` |
| Invalid math operation | `ERROR` at position | `sqrt([-1, 4])` | `[ERROR, 2]` |
| Overflow (if checked) | `ERROR` or wrap | Platform-dependent | - |

#### Structural Errors (Operation Fails)

| Condition | Behavior | Example | Result |
|-----------|----------|---------|--------|
| Size mismatch | `ERROR` | `[1, 2] + [3, 4, 5]` | `ERROR` |
| Non-vector operand (when required) | `ERROR` | `sum("hello")` | `ERROR` |

### Error Detection and Handling

Check for errors in results:

```lambda
let result = [3, "str", 5] + 1     // [4, ERROR, 6]

// Check if any element is an error
any(map(result, fn(x) => x is error))  // true

// Filter out errors
filter(result, fn(x) => not (x is error))  // [4, 6]

// Replace errors with default value
map(result, fn(x) => if (x is error) 0 else x)  // [4, 0, 6]
```

### Error Propagation

Errors propagate through subsequent operations:

```lambda
let a = [1, "bad", 3] + 1          // [2, ERROR, 4]
let b = a * 2                       // [4, ERROR, 8]
sum(b)                              // ERROR (contains error element)
```

### Implementation Notes

Element-wise operation with error handling:

```cpp
Item fn_add_vector_scalar(Item vec, Item scalar) {
    List* list = get_list(vec);
    List* result = list_new(list->length);
    
    for (long i = 0; i < list->length; i++) {
        Item elem = list->items[i];
        if (is_numeric(elem)) {
            result->items[i] = fn_add(elem, scalar);
        } else {
            // non-numeric element: produce ERROR, continue
            result->items[i] = ItemError;
        }
    }
    return (Item){ .list = result };
}
```

---

## Performance Considerations

### Memory Efficiency

1. **Avoid Unnecessary Copies**: Return new vector, don't modify in place (immutable)
2. **Pool Allocation**: Use Lambda's memory pools for vector storage
3. **Reference Counting**: Share vectors when possible

### Computational Efficiency

1. **Loop Fusion**: For chained operations, consider lazy evaluation
2. **Type Specialization**: Use homogeneous array types for fast paths
3. **SIMD**: Align data for SIMD on supported platforms

### Benchmarks (Targets)

| Operation | Size | Target Time |
|-----------|------|-------------|
| Vector + Vector | 1000 | < 1μs |
| Vector + Scalar | 1000 | < 0.5μs |
| sum(vector) | 1000 | < 0.5μs |
| Chained (a + b) * c | 1000 | < 2μs |

---

## Migration Path

### Phase 1: Core Arithmetic (Current)
- [x] Design document
- [ ] Enhance `fn_add()`, `fn_sub()`, `fn_mul()`, `fn_div()`
- [ ] Add size validation and broadcasting
- [ ] Update transpiler for vector detection

### Phase 2: Extended Operators
- [ ] Implement `fn_mod()`, `fn_pow()` for vectors
- [ ] Add integer division `_/`
- [ ] Implement comparison operators returning boolean vectors

### Phase 3: System Functions
- [ ] Vectorize math functions (`abs`, `sqrt`, `sin`, etc.)
- [ ] Add statistical functions (`median`, `deviation`, `variance`)
### Phase 4: Advanced Features
- [ ] Boolean indexing and filtering
- [ ] Cumulative operations (`cumsum`, `cumprod`)
- [ ] `where()` for conditional selection

---

## Summary

Lambda's vector support brings NumPy/R-style element-wise operations to the language:

| Feature | Status | Priority |
|---------|--------|----------|
| Scalar-Vector arithmetic | Required | P0 |
| Vector-Vector arithmetic | Required | P0 |
| Type promotion | Required | P0 |
| Size validation | Required | P0 |
| Single-element broadcasting | Required | P0 |
| Aggregation functions | Required | P1 |
| Element-wise math | High | P1 |
| Statistical functions | Medium | P2 |
| Boolean vectors/filtering | Medium | P2 |
| SIMD optimization | Low | P3 |

This design provides a solid foundation for scientific computing in Lambda while maintaining the language's pure functional semantics and type safety.
