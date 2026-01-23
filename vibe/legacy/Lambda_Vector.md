# Vectorized Numeric Operations Enhancement Plan for Lambda

## Overview

This document outlines a comprehensive plan to enhance Lambda with vectorized numeric operations, enabling scalar-array arithmetic operations like `1 + [2, 3]` resulting in `[3, 4]`.

## Current Architecture Analysis

### Existing Data Structures

Lambda currently has several array-like data structures:
- **`List`**: Dynamic heterogeneous container for `Item` values (type `LMD_TYPE_LIST`)
- **`Array`**: Alias for `List`, used interchangeably (type `LMD_TYPE_ARRAY`)
- **`ArrayLong`**: Homogeneous long integer array (type `LMD_TYPE_ARRAY_INT`)
- **`ArrayFloat`**: Homogeneous double array (type `LMD_TYPE_ARRAY_FLOAT`)

### Current Arithmetic Functions

Existing arithmetic functions in `lambda-eval.cpp`:
- `fn_add()`, `fn_sub()`, `fn_mul()`, `fn_div()`, `fn_mod()`, `fn_pow()`
- Support scalar-scalar operations with type promotion
- Handle string concatenation for `fn_add()`
- Limited to pairwise operations

### Current Transpiler Logic

The transpiler in `transpile.cpp`:
- Fast-path optimization for same-type numeric operations
- Runtime function fallback for mixed types
- Type-specific code generation for performance

## Enhancement Plan

### Phase 1: Core Infrastructure (Foundation)

#### 1.1 New Type Definitions

**File**: `lambda/lambda.h`

Add new type IDs for vectorized containers:
```c
enum EnumTypeId {
    // ... existing types ...
    LMD_TYPE_VECTOR_INT,    // Optimized integer vector
    LMD_TYPE_VECTOR_FLOAT,  // Optimized float vector  
    LMD_TYPE_VECTOR,        // Generic heterogeneous vector
};
```

Add new container structures:
```c
// Optimized integer vector for vectorized operations
struct VectorInt {
    TypeId type_id;
    uint8_t flags;
    uint16_t ref_cnt;
    long* items;
    long length;
    long capacity;
};

// Optimized float vector for vectorized operations  
struct VectorFloat {
    TypeId type_id;
    uint8_t flags;
    uint16_t ref_cnt;
    double* items;
    long length;
    long capacity;
};

// Generic vector (extends List with vectorization metadata)
struct Vector {
    TypeId type_id;
    uint8_t flags;
    uint16_t ref_cnt;
    Item* items;
    long length;
    long capacity;
    TypeId element_type;  // Dominant element type for optimization
};
```

#### 1.2 Vector Constructors and Utilities

**File**: `lambda/lambda-eval.cpp`

Add vector creation functions:
```c
VectorInt* vector_int_new(long count, ...);
VectorFloat* vector_float_new(long count, ...);
Vector* vector_new(TypeId element_type);
Vector* list_to_vector(List* list);  // Smart conversion
List* vector_to_list(Vector* vector);  // Fallback conversion
```

#### 1.3 Type Promotion System

Create type promotion hierarchy for vectorized operations:
```
int -> long -> double -> Item (generic)
VectorInt -> VectorFloat -> Vector -> List
```

### Phase 2: Vectorized Arithmetic Functions

#### 2.1 Scalar-Vector Operations

**File**: `lambda/lambda-eval.cpp`

Implement core vectorized arithmetic functions:

```c
// Scalar + Vector operations
Item fn_add_scalar_vector(Item scalar, Item vector);
Item fn_sub_scalar_vector(Item scalar, Item vector);  
Item fn_mul_scalar_vector(Item scalar, Item vector);
Item fn_div_scalar_vector(Item scalar, Item vector);

// Vector + Scalar operations  
Item fn_add_vector_scalar(Item vector, Item scalar);
Item fn_sub_vector_scalar(Item vector, Item scalar);
Item fn_mul_vector_scalar(Item vector, Item scalar);
Item fn_div_vector_scalar(Item vector, Item scalar);

// Vector + Vector operations
Item fn_add_vector_vector(Item vector_a, Item vector_b);
Item fn_sub_vector_vector(Item vector_a, Item vector_b);
Item fn_mul_vector_vector(Item vector_a, Item vector_b);
Item fn_div_vector_vector(Item vector_a, Item vector_b);
```

#### 2.2 Fast-Path Optimizations

For homogeneous numeric vectors, implement SIMD-optimized paths:

```c
// Optimized integer vector operations
VectorInt* vector_int_add_scalar(VectorInt* vec, long scalar);
VectorInt* vector_int_add_vector(VectorInt* a, VectorInt* b);

// Optimized float vector operations  
VectorFloat* vector_float_add_scalar(VectorFloat* vec, double scalar);
VectorFloat* vector_float_add_vector(VectorFloat* a, VectorFloat* b);
```

#### 2.3 Broadcasting Rules

Implement NumPy-style broadcasting:
- Scalar broadcasts to any vector size
- Vectors of different sizes: error or extend smaller with last element
- Mixed types promote to common type

### Phase 3: Enhanced Arithmetic Functions

#### 3.1 Modify Existing Functions

**File**: `lambda/lambda-eval.cpp`

Enhance existing arithmetic functions to handle vectorization:

```c
Item fn_add(Item item_a, Item item_b) {
    // Existing scalar cases...
    
    // New vectorized cases
    if (is_vector_type(item_a) && is_scalar_type(item_b)) {
        return fn_add_vector_scalar(item_a, item_b);
    }
    else if (is_scalar_type(item_a) && is_vector_type(item_b)) {
        return fn_add_scalar_vector(item_a, item_b);
    }
    else if (is_vector_type(item_a) && is_vector_type(item_b)) {
        return fn_add_vector_vector(item_a, item_b);
    }
    
    // Fallback cases...
}
```

#### 3.2 Type Detection Utilities

```c
bool is_scalar_type(Item item);
bool is_vector_type(Item item); 
bool is_homogeneous_vector(Item item, TypeId* element_type);
TypeId get_dominant_type(Item vector);
```

### Phase 4: Transpiler Enhancements

#### 4.1 Vectorization Detection

**File**: `lambda/transpile.cpp`

Enhance the transpiler to detect vectorizable operations:

```c
void transpile_binary_expr(Transpiler* tp, AstBinaryNode* bi_node) {
    // Detect if either operand is a vector type
    bool left_is_vector = is_vector_ast_type(bi_node->left);
    bool right_is_vector = is_vector_ast_type(bi_node->right);
    
    if (left_is_vector || right_is_vector) {
        transpile_vectorized_binary(tp, bi_node);
        return;
    }
    
    // Existing scalar logic...
}
```

#### 4.2 Vector-Aware Code Generation

Generate optimized code for different vector scenarios:

```c
void transpile_vectorized_binary(Transpiler* tp, AstBinaryNode* bi_node) {
    if (bi_node->op == OPERATOR_ADD) {
        if (both_homogeneous_int_vectors(bi_node)) {
            strbuf_append_str(tp->code_buf, "vector_int_add_fast(");
            // Generate fast-path code
        }
        else {
            strbuf_append_str(tp->code_buf, "fn_add(");
            // Generate generic vectorized code
        }
    }
    // Similar for other operators...
}
```

### Phase 5: Error Handling and Edge Cases

#### 5.1 Comprehensive Error Handling

```c
typedef enum {
    VEC_OP_SUCCESS,
    VEC_OP_SIZE_MISMATCH,
    VEC_OP_TYPE_ERROR,
    VEC_OP_DIVISION_BY_ZERO,
    VEC_OP_MEMORY_ERROR
} VectorOpResult;

VectorOpResult validate_vector_operation(Item a, Item b, OperatorType op);
```

#### 5.2 Edge Cases

- Empty vectors: Return empty result
- Single-element vectors: Behave like scalars
- Mixed types in heterogeneous vectors
- Memory allocation failures
- Numeric overflow/underflow in vectors

### Phase 6: Performance Optimizations

#### 6.1 Memory Pool Integration

Integrate with Lambda's existing memory pool system:

```c
VectorInt* vector_int_pooled(VariableMemPool* pool, long capacity);
void vector_int_pool_free(VariableMemPool* pool, VectorInt* vector);
```

#### 6.2 Lazy Evaluation

For chained vector operations, implement lazy evaluation:

```c
typedef struct VectorOp {
    OpType type;  // ADD, MUL, etc.
    Item operand_a;
    Item operand_b;
    struct VectorOp* next;
} VectorOp;

Item create_lazy_vector_expression(VectorOp* op_chain);
Item evaluate_lazy_vector(Item lazy_vector);
```

#### 6.3 SIMD Integration

For supported platforms, add SIMD acceleration:

```c
#ifdef __AVX2__
void vector_float_add_simd(double* a, double* b, double* result, long count);
#endif
```

### Phase 7: Integration with Existing Systems

#### 7.1 Update Aggregate Functions

**File**: `lambda/lambda-eval.cpp`

Enhance existing functions to work with vectors:

```c
Item fn_sum(Item item) {
    // ... existing cases ...
    
    else if (item.type_id == LMD_TYPE_VECTOR_INT) {
        VectorInt* vec = item.vector_int;
        long sum = 0;
        for (long i = 0; i < vec->length; i++) {
            sum += vec->items[i];
        }
        return push_l(sum);
    }
    else if (item.type_id == LMD_TYPE_VECTOR_FLOAT) {
        VectorFloat* vec = item.vector_float;
        double sum = 0.0;
        for (long i = 0; i < vec->length; i++) {
            sum += vec->items[i];
        }
        return push_d(sum);
    }
    // Similar for generic vectors...
}
```

#### 7.2 Index Access Support

```c
Item fn_index(Item item, Item index) {
    // ... existing cases ...
    
    if (item.type_id == LMD_TYPE_VECTOR_INT) {
        VectorInt* vec = item.vector_int;
        long idx = it2l(index);
        if (idx < 0 || idx >= vec->length) return ItemNull;
        return push_l(vec->items[idx]);
    }
    // Similar for other vector types...
}
```

#### 7.3 For-Loop Integration

Update for-loop transpilation to work efficiently with vectors:

```c
// Instead of: for (item in vector) { ... }
// Generate: for (long i = 0; i < vector->length; i++) { Item item = vector_get(vector, i); ... }
```

### Phase 8: Advanced Vectorization Features

#### 8.1 Conditional Vectorization

Support vectorized conditional operations:

```c
// Example: vec > 5 ? vec * 2 : vec / 2
Item fn_vector_conditional(Item condition, Item true_vec, Item false_vec);
```

#### 8.2 Reduction Operations

```c
Item fn_vector_reduce(Item vector, Item binary_fn);  // Fold/reduce operations
Item fn_vector_scan(Item vector, Item binary_fn);   // Cumulative operations
```

#### 8.3 Element-wise Functions

```c
Item fn_vector_map(Item vector, Item unary_fn);     // Apply function to each element
Item fn_vector_filter(Item vector, Item predicate); // Filter elements
```

## Testing Strategy

### Test File Structure

Create comprehensive tests under `test/lambda/`:

**`vector_basic.ls`** - Basic vectorized operations:
```lambda
// Scalar + Vector
1 + [2, 3, 4]          // [3, 4, 5]
[1, 2, 3] + 10         // [11, 12, 13]

// Vector + Vector  
[1, 2, 3] + [4, 5, 6]  // [5, 7, 9]

// Mixed types
1.5 + [2, 3]           // [3.5, 4.5]
[1, 2] * 2.0           // [2.0, 4.0]
```

**`vector_advanced.ls`** - Complex scenarios:
```lambda
// Chained operations
([1, 2, 3] + [4, 5, 6]) * 2    // [10, 14, 18]

// Mixed with aggregation
sum([1, 2, 3] * 5)             // 30

// Error cases  
[1, 2] + [3, 4, 5]             // Error: size mismatch (or broadcasting)
```

**`vector_performance.ls`** - Performance benchmarks:
```lambda
// Large vector operations
let big_vec = for (i in 1 to 10000) i
let result = big_vec * 2 + 1   // Should be fast
```

### Test Categories

1. **Basic Operations**: All arithmetic operators with vectors
2. **Type Promotion**: Mixed scalar/vector types
3. **Broadcasting**: Different vector sizes
4. **Edge Cases**: Empty vectors, single elements, nulls
5. **Error Handling**: Invalid operations, memory errors
6. **Performance**: Large vectors, chained operations
7. **Integration**: Vectors with existing Lambda features

## Implementation Milestones

### Milestone 1: Foundation (1-2 weeks)
- [ ] Add vector type definitions to `lambda.h`
- [ ] Implement basic vector constructors
- [ ] Add type detection utilities
- [ ] Create basic test framework

### Milestone 2: Core Arithmetic (2-3 weeks)
- [ ] Implement scalar-vector arithmetic functions
- [ ] Add vector-vector arithmetic functions
- [ ] Integrate with existing `fn_add()`, `fn_sub()`, etc.
- [ ] Basic transpiler support

### Milestone 3: Optimization (1-2 weeks)
- [ ] Fast-path optimizations for homogeneous vectors
- [ ] Memory pool integration
- [ ] SIMD support (if applicable)

### Milestone 4: Integration (1-2 weeks)
- [ ] Update aggregate functions (`fn_sum`, `fn_avg`, etc.)
- [ ] Vector indexing and member access
- [ ] For-loop optimization

### Milestone 5: Advanced Features (2-3 weeks)
- [ ] Broadcasting rules
- [ ] Conditional vectorization
- [ ] Reduction and mapping operations
- [ ] Comprehensive error handling

### Milestone 6: Testing and Polish (1-2 weeks)
- [ ] Complete test suite
- [ ] Performance benchmarks
- [ ] Documentation updates
- [ ] Integration testing

## Compatibility Considerations

### Backward Compatibility
- All existing Lambda scripts must continue to work
- No changes to core syntax or semantics
- Vectorization is an enhancement, not a replacement

### Memory Impact
- Vector operations should be memory-efficient
- Avoid unnecessary copying where possible
- Integrate with existing garbage collection

### Performance Goals
- Vectorized operations should be significantly faster than loops
- Memory usage should scale linearly with vector size
- Minimal overhead for scalar operations

## Future Extensions

### Potential Future Features
1. **Matrix Operations**: 2D vector support
2. **Complex Numbers**: Vector of complex types
3. **String Vectors**: Vectorized string operations
4. **GPU Acceleration**: CUDA/OpenCL support
5. **Distributed Vectors**: Large-scale parallel operations

### API Evolution
Design the vector API to support future extensions without breaking changes.

## Risk Mitigation

### Technical Risks
- **Memory Overhead**: Monitor memory usage carefully
- **Performance Regression**: Benchmark scalar operations
- **Type System Complexity**: Keep type promotion rules simple

### Implementation Risks
- **Code Complexity**: Maintain clear separation of concerns
- **Testing Coverage**: Ensure comprehensive test coverage
- **Integration Issues**: Test with all existing Lambda features

## Conclusion

This plan provides a comprehensive roadmap for adding vectorized numeric operations to Lambda while maintaining backward compatibility and performance. The phased approach allows for incremental development and testing, reducing risk and ensuring quality.

The implementation will significantly enhance Lambda's capability for data processing and mathematical computations, making it more competitive with languages like Python/NumPy and R for scientific computing tasks.
