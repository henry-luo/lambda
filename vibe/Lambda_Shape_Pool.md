# Lambda Shape Pool Implementation Plan

## Executive Summary

This document outlines a comprehensive plan to implement shape pooling for Lambda's type system. Shape pooling will deduplicate map and element type metadata (ShapeEntry chains) by caching and reusing shape structures that have identical field sequences, significantly reducing memory overhead for documents with repeated structural patterns.

**Date**: November 20, 2025  
**Status**: 📋 Planning Phase  
**Priority**: Medium-High (Memory Optimization & Type System Enhancement)  
**Dependencies**: Name Pool (✅ Completed), Arena Allocator (✅ Available)

---

## 1. Problem Analysis

### 1.1 Current State

**Shape Allocation Pattern**
Currently, every time a map or element is created with fields, a new chain of `ShapeEntry` structures is allocated:

```cpp
// From input.cpp - map_put() function
ShapeEntry* alloc_shape_entry(Pool* pool, String* key, TypeId type_id, ShapeEntry* prev_entry) {
    ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView));
    StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
    nv->str = key->chars;  nv->length = key->len;
    shape_entry->name = nv;
    shape_entry->type = type_info[type_id].type;
    shape_entry->byte_offset = prev_entry ? prev_entry->byte_offset + bsize : 0;
    shape_entry->next = NULL;
    return shape_entry;
}

void map_put(Map* mp, String* key, Item value, Input *input) {
    TypeMap *map_type = (TypeMap*)mp->type;
    // ... allocate or get map_type ...
    
    // Every map_put creates a new ShapeEntry
    ShapeEntry* shape_entry = alloc_shape_entry(input->pool, key, type_id, map_type->last);
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;
}
```

**Problem Scenarios**

1. **Repeated JSON Objects**: Document with thousands of objects with same structure
   ```json
   [
     {"id": 1, "name": "Alice", "age": 30},
     {"id": 2, "name": "Bob", "age": 25},
     // ... 10,000 more identical structures
   ]
   ```
   Result: 10,000 identical ShapeEntry chains allocated

2. **HTML Elements**: Document with many elements of same type
   ```html
   <div class="item" id="1">...</div>
   <div class="item" id="2">...</div>
   <!-- ... 1,000 more identical div elements -->
   ```
   Result: 1,000 duplicate TypeElmt structures with identical shape chains

3. **Nested Maps**: Complex nested structures with repeating patterns
   ```yaml
   users:
     - profile: {name: "...", email: "...", phone: "..."}
       address: {street: "...", city: "...", zip: "..."}
     - profile: {name: "...", email: "...", phone: "..."}
       address: {street: "...", city: "...", zip: "..."}
   ```
   Result: Duplicate shape chains for each nested structure

### 1.2 Memory Overhead Analysis

**ShapeEntry Structure Size**
```cpp
typedef struct ShapeEntry {
    StrView* name;           // 8 bytes (pointer)
    Type* type;              // 8 bytes (pointer)
    int64_t byte_offset;     // 8 bytes
    struct ShapeEntry* next; // 8 bytes (pointer)
} ShapeEntry;               // Total: 32 bytes per entry
```

Plus embedded StrView (allocated inline):
```cpp
typedef struct StrView {
    const char* str;         // 8 bytes
    size_t length;           // 8 bytes
} StrView;                  // Total: 16 bytes
```

**Total per ShapeEntry**: 48 bytes (32 + 16)

**Example Calculation**
- Document with 10,000 identical 3-field objects
- Without pooling: 10,000 × 3 × 48 = 1,440,000 bytes (~1.4 MB)
- With pooling: 1 × 3 × 48 = 144 bytes + overhead
- **Savings**: ~1.4 MB (99.99% reduction)

### 1.3 Key Insights from Name Pool Implementation

The name pool implementation provides valuable patterns:
- **Parent Inheritance**: Child pools lookup in parent first
- **Reference Counting**: Track pool lifecycle correctly
- **Hash-based Lookup**: Fast O(1) deduplication
- **Arena Allocation**: Permanent storage for pooled data
- **Hierarchical Structure**: Schema → Input relationship

---

## 2. Design Goals

### 2.1 Functional Requirements

1. **Shape Deduplication**: Identical shape chains should be shared
2. **Fast Lookup**: O(1) shape lookup by signature
3. **Parent Inheritance**: Child Input inherits parent's shapes
4. **Type Safety**: Maintain existing type system semantics
5. **Backward Compatibility**: Existing code continues to work

### 2.2 Non-Functional Requirements

1. **Memory Efficiency**: Minimize overhead for small documents
2. **Performance**: Negligible impact on shape creation time
3. **Thread Safety**: Safe for multi-threaded document parsing
4. **Maintainability**: Clean API similar to name_pool
5. **Debuggability**: Easy to trace shape allocation and reuse

### 2.3 Design Principles

1. **Separation of Concerns**: Shape pool manages shapes, TypeMap uses them
2. **Minimal Invasiveness**: Minimal changes to existing code paths
3. **Progressive Enhancement**: Opt-in for parsers, fallback to old behavior
4. **Test-Driven**: Comprehensive tests before production use

---

## 3. Technical Design

### 3.1 Core Data Structures

```cpp
// lambda/shape_pool.hpp

#pragma once

#include "lambda.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/hashmap.h"

// Configuration
#define SHAPE_POOL_INITIAL_CAPACITY 128
#define SHAPE_POOL_MAX_CHAIN_LENGTH 64  // Safety limit for signature calculation

// Shape signature - uniquely identifies a shape structure
typedef struct ShapeSignature {
    uint64_t hash;              // Hash of field names + types
    uint32_t length;            // Number of fields
    uint32_t byte_size;         // Total byte size of data structure
} ShapeSignature;

// Cached shape entry - stored in shape pool
typedef struct CachedShape {
    ShapeSignature signature;   // Unique identifier
    ShapeEntry* shape;          // The actual shape chain
    ShapeEntry* last;           // Last entry in chain (for fast append)
    uint32_t ref_count;         // Reference count for lifecycle
    bool is_element;            // true if TypeElmt, false if TypeMap
} CachedShape;

// Shape pool - manages shape deduplication
typedef struct ShapePool {
    Pool* pool;                 // Variable memory pool for allocations
    Arena* arena;               // Arena for permanent shape storage
    struct hashmap* shapes;     // Hashmap: ShapeSignature → CachedShape
    struct ShapePool* parent;   // Parent pool for inheritance
    uint32_t ref_count;         // Reference counting
} ShapePool;

#ifdef __cplusplus
extern "C" {
#endif

// ========== Core Functions ==========

/**
 * Create a new shape pool
 * @param memory_pool Pool for allocations
 * @param arena Arena for permanent storage (must outlive the pool)
 * @param parent Parent shape pool for inheritance (can be NULL)
 * @return New ShapePool instance, or NULL on failure
 */
ShapePool* shape_pool_create(Pool* memory_pool, Arena* arena, ShapePool* parent);

/**
 * Retain a shape pool (increment ref count)
 */
ShapePool* shape_pool_retain(ShapePool* pool);

/**
 * Release a shape pool (decrement ref count, free if zero)
 */
void shape_pool_release(ShapePool* pool);

// ========== Shape Creation/Lookup ==========

/**
 * Create or lookup a shape chain for a map
 * 
 * This function either:
 * 1. Finds an existing identical shape in the pool (current or parent)
 * 2. Creates a new shape and adds it to the pool
 * 
 * @param pool Shape pool to use
 * @param fields Array of (name, type_id) pairs
 * @param field_count Number of fields
 * @return ShapeEntry* to use in TypeMap, or NULL on error
 * 
 * The returned ShapeEntry chain is owned by the pool and must not be freed.
 */
ShapeEntry* shape_pool_get_map_shape(
    ShapePool* pool,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
);

/**
 * Create or lookup a shape chain for an element
 * 
 * Similar to shape_pool_get_map_shape but for TypeElmt (element attributes).
 * Element shapes include the element name in the signature.
 * 
 * @param pool Shape pool to use
 * @param element_name Name of the element (e.g., "div", "span")
 * @param attr_names Array of attribute names
 * @param attr_types Array of attribute types
 * @param attr_count Number of attributes
 * @return ShapeEntry* to use in TypeElmt, or NULL on error
 */
ShapeEntry* shape_pool_get_element_shape(
    ShapePool* pool,
    const char* element_name,
    const char** attr_names,
    TypeId* attr_types,
    size_t attr_count
);

// ========== Incremental Shape Building ==========

/**
 * Builder pattern for incremental shape construction
 * Useful when shape is built field-by-field during parsing
 */
typedef struct ShapeBuilder {
    ShapePool* pool;
    const char* field_names[SHAPE_POOL_MAX_CHAIN_LENGTH];
    TypeId field_types[SHAPE_POOL_MAX_CHAIN_LENGTH];
    size_t field_count;
    bool is_element;
    const char* element_name;
} ShapeBuilder;

/**
 * Initialize a shape builder for map shapes
 */
ShapeBuilder shape_builder_init_map(ShapePool* pool);

/**
 * Initialize a shape builder for element shapes
 */
ShapeBuilder shape_builder_init_element(ShapePool* pool, const char* element_name);

/**
 * Add a field to the builder
 */
void shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type);

/**
 * Finalize the builder and get/create the shape
 * Returns cached shape if identical one exists
 */
ShapeEntry* shape_builder_finalize(ShapeBuilder* builder);

// ========== Utilities ==========

/**
 * Check if two shapes are identical (same fields in same order)
 */
bool shape_pool_shapes_equal(ShapeEntry* shape1, ShapeEntry* shape2);

/**
 * Get statistics about pool usage
 */
void shape_pool_print_stats(ShapePool* pool);

/**
 * Get number of unique shapes in pool (excluding parent)
 */
size_t shape_pool_count(ShapePool* pool);

#ifdef __cplusplus
}
#endif
```

### 3.2 Shape Signature Algorithm

The signature uniquely identifies a shape structure:

```cpp
// Signature calculation (combines field names, types, and order)
static uint64_t calculate_shape_hash(
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    // Use hashmap_sip for consistent hashing
    uint64_t hash = 0x123456789abcdefULL;
    
    for (size_t i = 0; i < field_count; i++) {
        // Hash field name
        size_t name_len = strlen(field_names[i]);
        hash = hashmap_sip(field_names[i], name_len, hash, i);
        
        // Hash field type (combine with existing hash)
        uint64_t type_bits = (uint64_t)field_types[i];
        hash ^= type_bits * 0x9e3779b97f4a7c15ULL;  // Fibonacci hash constant
        hash = (hash << 7) | (hash >> 57);          // Rotate
    }
    
    return hash;
}

static ShapeSignature create_signature(
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    ShapeSignature sig;
    sig.hash = calculate_shape_hash(field_names, field_types, field_count);
    sig.length = field_count;
    
    // Calculate byte_size (sum of field sizes)
    sig.byte_size = 0;
    for (size_t i = 0; i < field_count; i++) {
        sig.byte_size += type_info[field_types[i]].byte_size;
    }
    
    return sig;
}
```

### 3.3 Shape Pool Integration Points

**Input Structure Enhancement**
```cpp
// lambda/lambda-data.hpp - add shape_pool field
typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    ShapePool* shape_pool;      // NEW: shape deduplication
    ArrayList* type_list;       // list of types
    Item root;
    
    static Input* create(Pool* pool, Url* abs_url = nullptr);
} Input;
```

**Input Creation**
```cpp
// lambda/input/input.cpp
Input* Input::create(Pool* pool, Url* abs_url) {
    Input* input = (Input*)pool_alloc(pool, sizeof(Input));
    input->pool = pool;
    input->arena = arena_create_default(pool);
    input->name_pool = name_pool_create(pool, NULL);
    input->shape_pool = shape_pool_create(pool, input->arena, NULL);  // NEW
    input->type_list = arraylist_new(16);
    input->url = abs_url;
    input->path = nullptr;
    input->root = (Item){.item = ITEM_NULL};
    return input;
}
```

**Parent Inheritance**
```cpp
// When creating child Input from parent (e.g., schema validation)
Input* create_child_input(Input* parent, Url* child_url) {
    Input* child = Input::create(parent->pool, child_url);
    
    // Override with parent's pools
    name_pool_release(child->name_pool);
    child->name_pool = name_pool_create(parent->pool, parent->name_pool);
    
    shape_pool_release(child->shape_pool);  // NEW
    child->shape_pool = shape_pool_create(parent->pool, parent->arena, parent->shape_pool);  // NEW
    
    return child;
}
```

### 3.4 Enhanced map_put() and elmt_put()

**Optimized map_put() with shape pooling**
```cpp
// lambda/input/input.cpp
void map_put(Map* mp, String* key, Item value, Input *input) {
    TypeMap *map_type = (TypeMap*)mp->type;
    
    // Initialize map type if first field
    if (map_type == &EmptyMap) {
        map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) return;
        mp->type = map_type;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;
        
        int byte_cap = 64;
        mp->data = pool_calloc(input->pool, byte_cap);
        mp->data_cap = byte_cap;
        if (!mp->data) return;
    }

    TypeId type_id = get_type_id(value);
    
    // NEW: Check if we're building a new shape or can reuse existing
    // For now, we build incrementally. When map is finalized, we'll normalize.
    ShapeEntry* shape_entry = alloc_shape_entry(input->pool, key, type_id, map_type->last);
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;

    // ... rest of implementation (data storage) remains unchanged ...
}

// NEW: Normalize shape after map construction complete
void map_finalize_shape(Map* mp, Input* input) {
    if (!input->shape_pool) return;  // Shape pooling disabled
    
    TypeMap* map_type = (TypeMap*)mp->type;
    if (map_type == &EmptyMap || !map_type->shape) return;
    
    // Build field arrays for signature
    size_t field_count = map_type->length;
    const char** field_names = (const char**)alloca(field_count * sizeof(char*));
    TypeId* field_types = (TypeId*)alloca(field_count * sizeof(TypeId));
    
    ShapeEntry* entry = map_type->shape;
    for (size_t i = 0; i < field_count && entry; i++) {
        field_names[i] = entry->name->str;
        field_types[i] = entry->type->type_id;
        entry = entry->next;
    }
    
    // Lookup or create pooled shape
    ShapeEntry* pooled_shape = shape_pool_get_map_shape(
        input->shape_pool, field_names, field_types, field_count
    );
    
    if (pooled_shape && pooled_shape != map_type->shape) {
        // Replace with pooled shape (the old one will be garbage collected with pool)
        map_type->shape = pooled_shape;
        map_type->last = pooled_shape;
        while (map_type->last->next) map_type->last = map_type->last->next;
    }
}
```

**Similar enhancement for elmt_put()**
```cpp
void elmt_finalize_shape(Element* elmt, Input* input) {
    if (!input->shape_pool) return;
    
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    if (elmt_type == &EmptyElmt || !elmt_type->shape) return;
    
    // Extract element name
    char element_name[256];
    size_t name_len = elmt_type->name.length < 255 ? elmt_type->name.length : 255;
    memcpy(element_name, elmt_type->name.str, name_len);
    element_name[name_len] = '\0';
    
    // Build attribute arrays
    size_t attr_count = elmt_type->length;
    const char** attr_names = (const char**)alloca(attr_count * sizeof(char*));
    TypeId* attr_types = (TypeId*)alloca(attr_count * sizeof(TypeId));
    
    ShapeEntry* entry = elmt_type->shape;
    for (size_t i = 0; i < attr_count && entry; i++) {
        attr_names[i] = entry->name->str;
        attr_types[i] = entry->type->type_id;
        entry = entry->next;
    }
    
    // Lookup or create pooled shape
    ShapeEntry* pooled_shape = shape_pool_get_element_shape(
        input->shape_pool, element_name, attr_names, attr_types, attr_count
    );
    
    if (pooled_shape && pooled_shape != elmt_type->shape) {
        elmt_type->shape = pooled_shape;
        elmt_type->last = pooled_shape;
        while (elmt_type->last->next) elmt_type->last = elmt_type->last->next;
    }
}
```

### 3.5 MarkBuilder Integration

**ElementBuilder Enhancement**
```cpp
// lambda/mark_builder.cpp
class ElementBuilder {
    // ... existing members ...
    
    Item final() {
        Element* elmt = (Element*)this->elmt;
        
        // NEW: Finalize shape if shape_pool is available
        if (builder_->input_ && builder_->input_->shape_pool) {
            elmt_finalize_shape(elmt, builder_->input_);
        }
        
        return {.element = elmt};
    }
};
```

**MapBuilder Enhancement**
```cpp
class MapBuilder {
    // ... existing members ...
    
    Item final() {
        Map* mp = (Map*)this->map;
        
        // NEW: Finalize shape if shape_pool is available
        if (builder_->input_ && builder_->input_->shape_pool) {
            map_finalize_shape(mp, builder_->input_);
        }
        
        return {.map = mp};
    }
};
```

---

## 4. Implementation Plan

### Phase 1: Core Infrastructure (Week 1)

**1.1 Create shape_pool.hpp and shape_pool.cpp**
- [ ] Define ShapePool, ShapeSignature, CachedShape structures
- [ ] Implement shape_pool_create/retain/release
- [ ] Implement shape signature calculation
- [ ] Implement hashmap integration for shape lookup

**1.2 Shape Creation Functions**
- [ ] Implement shape_pool_get_map_shape()
- [ ] Implement shape_pool_get_element_shape()
- [ ] Implement shape comparison logic

**1.3 Basic Unit Tests**
- [ ] Test shape pool creation/destruction
- [ ] Test signature calculation consistency
- [ ] Test shape deduplication (same shape returns same pointer)
- [ ] Test shape differentiation (different shapes return different pointers)

**Success Criteria**: All basic shape pool tests passing

### Phase 2: Shape Builder Pattern (Week 1-2)

**2.1 Implement ShapeBuilder**
- [ ] Implement shape_builder_init_map()
- [ ] Implement shape_builder_init_element()
- [ ] Implement shape_builder_add_field()
- [ ] Implement shape_builder_finalize()

**2.2 Builder Tests**
- [ ] Test incremental field addition
- [ ] Test builder finalization with deduplication
- [ ] Test builder with identical field sequences
- [ ] Test builder with different field orders

**Success Criteria**: Shape builder correctly creates and deduplicates shapes

### Phase 3: Input Integration (Week 2)

**3.1 Update Input Structure**
- [ ] Add shape_pool field to Input struct
- [ ] Update Input::create() to initialize shape_pool
- [ ] Add parent inheritance support

**3.2 Finalization Functions**
- [ ] Implement map_finalize_shape()
- [ ] Implement elmt_finalize_shape()
- [ ] Add finalization calls to MarkBuilder::final()

**3.3 Integration Tests**
- [ ] Test Input with shape_pool initialization
- [ ] Test parent-child shape pool inheritance
- [ ] Test shape deduplication across multiple maps
- [ ] Test shape deduplication across multiple elements

**Success Criteria**: Input objects correctly manage shape pools

### Phase 4: Parser Integration (Week 2-3)

**4.1 Update Structured Parsers**
- [ ] JSON parser - finalize map shapes after parsing
- [ ] XML/HTML parser - finalize element shapes
- [ ] TOML parser - finalize map shapes
- [ ] YAML parser - finalize map shapes
- [ ] CSV parser - finalize map shapes (rows as maps)

**4.2 Parser Tests**
- [ ] Test JSON with repeated object structures
- [ ] Test HTML with repeated element types
- [ ] Test nested structures with shape reuse
- [ ] Benchmark memory usage improvements

**Success Criteria**: Parsers successfully deduplicate shapes, memory usage reduced

### Phase 5: Runtime Integration (Week 3)

**5.1 Update EvalContext**
- [ ] Add shape_pool to EvalContext structure
- [ ] Update runtime map creation to use shape pool
- [ ] Update runtime element creation to use shape pool

**5.2 Runtime Tests**
- [ ] Test map literals with deduplication
- [ ] Test element creation with deduplication
- [ ] Test dynamic map construction
- [ ] Performance benchmarks

**Success Criteria**: Runtime correctly uses shape pool

### Phase 6: Optimization & Validation (Week 4)

**6.1 Performance Optimization**
- [ ] Profile shape lookup performance
- [ ] Optimize signature calculation
- [ ] Add caching for frequently used shapes
- [ ] Add statistics collection

**6.2 Memory Testing**
- [ ] Large document tests (10K+ repeated structures)
- [ ] Memory leak detection
- [ ] Reference counting validation
- [ ] Parent inheritance stress tests

**6.3 Documentation**
- [ ] Update Lambda Runtime documentation
- [ ] Add shape pool API documentation
- [ ] Create usage examples
- [ ] Update type system documentation

**Success Criteria**: Production-ready shape pool with documentation

---

## 5. Implementation Details

### 5.1 Shape Pool Implementation (shape_pool.cpp)

```cpp
// lambda/shape_pool.cpp

#include "shape_pool.hpp"
#include "lambda-data.hpp"
#include "../lib/log.h"
#include "../lib/string.h"

// ========== Internal Structures ==========

typedef struct ShapePoolEntry {
    ShapeSignature signature;
    CachedShape* cached;
} ShapePoolEntry;

// Hash function for shape signatures
static uint64_t shape_signature_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const ShapePoolEntry* entry = (const ShapePoolEntry*)item;
    // Use the pre-calculated hash from signature
    return hashmap_sip(&entry->signature.hash, sizeof(uint64_t), seed0, seed1);
}

// Compare function for shape signatures
static int shape_signature_compare(const void *a, const void *b, void *udata) {
    const ShapePoolEntry* entry_a = (const ShapePoolEntry*)a;
    const ShapePoolEntry* entry_b = (const ShapePoolEntry*)b;
    
    const ShapeSignature* sig_a = &entry_a->signature;
    const ShapeSignature* sig_b = &entry_b->signature;
    
    // Compare all signature fields
    if (sig_a->hash != sig_b->hash) {
        return (sig_a->hash < sig_b->hash) ? -1 : 1;
    }
    if (sig_a->length != sig_b->length) {
        return (sig_a->length < sig_b->length) ? -1 : 1;
    }
    if (sig_a->byte_size != sig_b->byte_size) {
        return (sig_a->byte_size < sig_b->byte_size) ? -1 : 1;
    }
    
    return 0;
}

// ========== Core Functions ==========

ShapePool* shape_pool_create(Pool* memory_pool, Arena* arena, ShapePool* parent) {
    if (!memory_pool || !arena) return NULL;
    
    ShapePool* pool = (ShapePool*)pool_calloc(memory_pool, sizeof(ShapePool));
    if (!pool) return NULL;
    
    pool->pool = memory_pool;
    pool->arena = arena;
    pool->parent = parent ? shape_pool_retain(parent) : NULL;
    pool->ref_count = 1;
    
    // Create hashmap for shape lookup
    pool->shapes = hashmap_new(
        sizeof(ShapePoolEntry),
        SHAPE_POOL_INITIAL_CAPACITY,
        0x123456789abcdefULL,
        0xfedcba0987654321ULL,
        shape_signature_hash,
        shape_signature_compare,
        NULL, NULL
    );
    
    if (!pool->shapes) {
        if (pool->parent) shape_pool_release(pool->parent);
        return NULL;
    }
    
    log_debug("shape_pool_create: pool=%p, parent=%p", pool, pool->parent);
    return pool;
}

ShapePool* shape_pool_retain(ShapePool* pool) {
    if (!pool) return NULL;
    pool->ref_count++;
    log_debug("shape_pool_retain: pool=%p, ref_count=%u", pool, pool->ref_count);
    return pool;
}

void shape_pool_release(ShapePool* pool) {
    if (!pool) return;
    
    pool->ref_count--;
    log_debug("shape_pool_release: pool=%p, ref_count=%u", pool, pool->ref_count);
    
    if (pool->ref_count == 0) {
        if (pool->parent) {
            shape_pool_release(pool->parent);
        }
        if (pool->shapes) {
            hashmap_free(pool->shapes);
        }
        // Note: pool memory freed when Pool is destroyed
    }
}

// ========== Shape Creation ==========

static ShapeEntry* create_shape_chain(
    Arena* arena,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    if (field_count == 0) return NULL;
    
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    int64_t byte_offset = 0;
    
    for (size_t i = 0; i < field_count; i++) {
        // Allocate ShapeEntry + embedded StrView
        ShapeEntry* entry = (ShapeEntry*)arena_alloc(arena, 
            sizeof(ShapeEntry) + sizeof(StrView));
        if (!entry) {
            log_error("Failed to allocate ShapeEntry from arena");
            return NULL;
        }
        
        // Setup embedded StrView
        StrView* nv = (StrView*)((char*)entry + sizeof(ShapeEntry));
        nv->str = field_names[i];
        nv->length = strlen(field_names[i]);
        
        entry->name = nv;
        entry->type = type_info[field_types[i]].type;
        entry->byte_offset = byte_offset;
        entry->next = NULL;
        
        if (!first) first = entry;
        if (prev) prev->next = entry;
        
        prev = entry;
        byte_offset += type_info[field_types[i]].byte_size;
    }
    
    return first;
}

static CachedShape* lookup_cached_shape(
    ShapePool* pool,
    ShapeSignature* signature
) {
    // Try current pool
    ShapePoolEntry search_entry = {*signature, NULL};
    const ShapePoolEntry* found = (const ShapePoolEntry*)hashmap_get(
        pool->shapes, &search_entry
    );
    
    if (found) {
        log_debug("Shape found in current pool: hash=%lx", signature->hash);
        return found->cached;
    }
    
    // Try parent pool
    if (pool->parent) {
        return lookup_cached_shape(pool->parent, signature);
    }
    
    return NULL;
}

ShapeEntry* shape_pool_get_map_shape(
    ShapePool* pool,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    if (!pool || !field_names || !field_types || field_count == 0) {
        return NULL;
    }
    
    // Calculate signature
    ShapeSignature signature = create_signature(field_names, field_types, field_count);
    
    // Lookup existing shape
    CachedShape* cached = lookup_cached_shape(pool, &signature);
    if (cached) {
        log_debug("Reusing cached shape: hash=%lx, length=%u", 
            signature.hash, signature.length);
        return cached->shape;
    }
    
    // Create new shape chain
    ShapeEntry* shape = create_shape_chain(pool->arena, field_names, field_types, field_count);
    if (!shape) return NULL;
    
    // Find last entry
    ShapeEntry* last = shape;
    while (last->next) last = last->next;
    
    // Create cached shape
    CachedShape* new_cached = (CachedShape*)pool_calloc(pool->pool, sizeof(CachedShape));
    if (!new_cached) return NULL;
    
    new_cached->signature = signature;
    new_cached->shape = shape;
    new_cached->last = last;
    new_cached->ref_count = 0;
    new_cached->is_element = false;
    
    // Store in hashmap
    ShapePoolEntry entry = {signature, new_cached};
    if (!hashmap_set(pool->shapes, &entry)) {
        log_error("Failed to store shape in pool");
        return NULL;
    }
    
    log_debug("Created new cached shape: hash=%lx, length=%u", 
        signature.hash, signature.length);
    return shape;
}

ShapeEntry* shape_pool_get_element_shape(
    ShapePool* pool,
    const char* element_name,
    const char** attr_names,
    TypeId* attr_types,
    size_t attr_count
) {
    if (!pool || !element_name) return NULL;
    
    // Include element name in signature calculation
    size_t total_count = attr_count + 1;
    const char** all_names = (const char**)alloca(total_count * sizeof(char*));
    TypeId* all_types = (TypeId*)alloca(total_count * sizeof(TypeId));
    
    // First entry is element name with special type marker
    all_names[0] = element_name;
    all_types[0] = LMD_TYPE_ELEMENT;  // Use as marker
    
    // Copy attributes
    for (size_t i = 0; i < attr_count; i++) {
        all_names[i + 1] = attr_names[i];
        all_types[i + 1] = attr_types[i];
    }
    
    // Use map shape logic with extended signature
    ShapeEntry* shape = shape_pool_get_map_shape(pool, all_names, all_types, total_count);
    
    // Mark as element shape
    if (shape) {
        ShapeSignature sig = create_signature(all_names, all_types, total_count);
        ShapePoolEntry search = {sig, NULL};
        const ShapePoolEntry* found = (const ShapePoolEntry*)hashmap_get(pool->shapes, &search);
        if (found && found->cached) {
            found->cached->is_element = true;
        }
    }
    
    return shape;
}

// ========== Utilities ==========

bool shape_pool_shapes_equal(ShapeEntry* shape1, ShapeEntry* shape2) {
    ShapeEntry* e1 = shape1;
    ShapeEntry* e2 = shape2;
    
    while (e1 && e2) {
        // Compare name
        if (e1->name->length != e2->name->length) return false;
        if (memcmp(e1->name->str, e2->name->str, e1->name->length) != 0) return false;
        
        // Compare type
        if (e1->type->type_id != e2->type->type_id) return false;
        
        // Compare byte offset
        if (e1->byte_offset != e2->byte_offset) return false;
        
        e1 = e1->next;
        e2 = e2->next;
    }
    
    // Both should be NULL (same length)
    return e1 == NULL && e2 == NULL;
}

void shape_pool_print_stats(ShapePool* pool) {
    if (!pool) return;
    
    size_t count = hashmap_count(pool->shapes);
    printf("ShapePool Statistics:\n");
    printf("  Pool: %p\n", pool);
    printf("  Unique shapes: %zu\n", count);
    printf("  Ref count: %u\n", pool->ref_count);
    printf("  Parent: %p\n", pool->parent);
    
    if (pool->parent) {
        printf("\nParent pool:\n");
        shape_pool_print_stats(pool->parent);
    }
}

size_t shape_pool_count(ShapePool* pool) {
    return pool ? hashmap_count(pool->shapes) : 0;
}

// ========== Shape Builder ==========

ShapeBuilder shape_builder_init_map(ShapePool* pool) {
    ShapeBuilder builder;
    memset(&builder, 0, sizeof(ShapeBuilder));
    builder.pool = pool;
    builder.is_element = false;
    return builder;
}

ShapeBuilder shape_builder_init_element(ShapePool* pool, const char* element_name) {
    ShapeBuilder builder;
    memset(&builder, 0, sizeof(ShapeBuilder));
    builder.pool = pool;
    builder.is_element = true;
    builder.element_name = element_name;
    return builder;
}

void shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type) {
    if (!builder || builder->field_count >= SHAPE_POOL_MAX_CHAIN_LENGTH) {
        log_error("ShapeBuilder: cannot add field, max length reached");
        return;
    }
    
    builder->field_names[builder->field_count] = name;
    builder->field_types[builder->field_count] = type;
    builder->field_count++;
}

ShapeEntry* shape_builder_finalize(ShapeBuilder* builder) {
    if (!builder || !builder->pool) return NULL;
    
    if (builder->is_element) {
        return shape_pool_get_element_shape(
            builder->pool,
            builder->element_name,
            builder->field_names,
            builder->field_types,
            builder->field_count
        );
    } else {
        return shape_pool_get_map_shape(
            builder->pool,
            builder->field_names,
            builder->field_types,
            builder->field_count
        );
    }
}
```

### 5.2 Signature Calculation Helper

```cpp
// Helper in shape_pool.cpp
static ShapeSignature create_signature(
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    ShapeSignature sig;
    sig.length = field_count;
    sig.byte_size = 0;
    
    // Calculate hash
    uint64_t hash = 0x123456789abcdefULL;
    for (size_t i = 0; i < field_count; i++) {
        // Hash field name
        size_t name_len = strlen(field_names[i]);
        hash = hashmap_sip(field_names[i], name_len, hash, i);
        
        // Hash field type
        uint64_t type_bits = (uint64_t)field_types[i];
        hash ^= type_bits * 0x9e3779b97f4a7c15ULL;
        hash = (hash << 7) | (hash >> 57);
        
        // Accumulate byte size
        sig.byte_size += type_info[field_types[i]].byte_size;
    }
    
    sig.hash = hash;
    return sig;
}
```

---

## 6. Testing Strategy

### 6.1 Unit Tests (test_shape_pool_gtest.cpp)

```cpp
#include <gtest/gtest.h>
#include "../lambda/shape_pool.hpp"
#include "../lambda/lambda-data.hpp"

class ShapePoolTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    
    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }
    
    void TearDown() override {
        pool_destroy(pool);
    }
};

TEST_F(ShapePoolTest, CreateAndDestroy) {
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    ASSERT_NE(sp, nullptr);
    EXPECT_EQ(shape_pool_count(sp), 0);
    shape_pool_release(sp);
}

TEST_F(ShapePoolTest, SimpleMapShape) {
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    
    const char* names[] = {"name", "age", "email"};
    TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_INT, LMD_TYPE_STRING};
    
    ShapeEntry* shape1 = shape_pool_get_map_shape(sp, names, types, 3);
    ASSERT_NE(shape1, nullptr);
    EXPECT_EQ(shape_pool_count(sp), 1);
    
    // Same shape should return same pointer
    ShapeEntry* shape2 = shape_pool_get_map_shape(sp, names, types, 3);
    EXPECT_EQ(shape1, shape2);
    EXPECT_EQ(shape_pool_count(sp), 1);  // Still 1 unique shape
    
    shape_pool_release(sp);
}

TEST_F(ShapePoolTest, DifferentShapes) {
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    
    const char* names1[] = {"name", "age"};
    TypeId types1[] = {LMD_TYPE_STRING, LMD_TYPE_INT};
    
    const char* names2[] = {"title", "count"};
    TypeId types2[] = {LMD_TYPE_STRING, LMD_TYPE_INT};
    
    ShapeEntry* shape1 = shape_pool_get_map_shape(sp, names1, types1, 2);
    ShapeEntry* shape2 = shape_pool_get_map_shape(sp, names2, types2, 2);
    
    EXPECT_NE(shape1, shape2);  // Different names = different shapes
    EXPECT_EQ(shape_pool_count(sp), 2);
    
    shape_pool_release(sp);
}

TEST_F(ShapePoolTest, ParentInheritance) {
    ShapePool* parent = shape_pool_create(pool, arena, NULL);
    ShapePool* child = shape_pool_create(pool, arena, parent);
    
    const char* names[] = {"id", "value"};
    TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_STRING};
    
    // Create shape in parent
    ShapeEntry* parent_shape = shape_pool_get_map_shape(parent, names, types, 2);
    ASSERT_NE(parent_shape, nullptr);
    EXPECT_EQ(shape_pool_count(parent), 1);
    EXPECT_EQ(shape_pool_count(child), 0);
    
    // Child should find parent's shape
    ShapeEntry* child_shape = shape_pool_get_map_shape(child, names, types, 2);
    EXPECT_EQ(parent_shape, child_shape);
    EXPECT_EQ(shape_pool_count(child), 0);  // Not stored in child
    
    shape_pool_release(child);
    shape_pool_release(parent);
}

TEST_F(ShapePoolTest, ElementShape) {
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    
    const char* attrs[] = {"class", "id", "href"};
    TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_STRING, LMD_TYPE_STRING};
    
    ShapeEntry* shape1 = shape_pool_get_element_shape(sp, "a", attrs, types, 3);
    ASSERT_NE(shape1, nullptr);
    
    // Same element with same attrs should reuse shape
    ShapeEntry* shape2 = shape_pool_get_element_shape(sp, "a", attrs, types, 3);
    EXPECT_EQ(shape1, shape2);
    
    // Different element name should create different shape
    ShapeEntry* shape3 = shape_pool_get_element_shape(sp, "div", attrs, types, 3);
    EXPECT_NE(shape1, shape3);
    
    shape_pool_release(sp);
}

TEST_F(ShapePoolTest, ShapeBuilder) {
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    
    ShapeBuilder builder = shape_builder_init_map(sp);
    shape_builder_add_field(&builder, "x", LMD_TYPE_INT);
    shape_builder_add_field(&builder, "y", LMD_TYPE_INT);
    
    ShapeEntry* shape1 = shape_builder_finalize(&builder);
    ASSERT_NE(shape1, nullptr);
    
    // Create same shape directly
    const char* names[] = {"x", "y"};
    TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_INT};
    ShapeEntry* shape2 = shape_pool_get_map_shape(sp, names, types, 2);
    
    EXPECT_EQ(shape1, shape2);  // Should be same cached shape
    
    shape_pool_release(sp);
}

// Add more tests for edge cases, stress tests, etc.
```

### 6.2 Integration Tests

```cpp
TEST(ShapePoolIntegration, JSONRepeatedObjects) {
    const char* json = R"([
        {"id": 1, "name": "Alice", "score": 95},
        {"id": 2, "name": "Bob", "score": 87},
        {"id": 3, "name": "Carol", "score": 92}
    ])";
    
    Input* input = input_from_source(json, nullptr, nullptr, nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->shape_pool, nullptr);
    
    // All three objects should share the same shape
    Array* arr = input->root.array;
    ASSERT_EQ(arr->length, 3);
    
    Map* map1 = arr->items[0].map;
    Map* map2 = arr->items[1].map;
    Map* map3 = arr->items[2].map;
    
    TypeMap* type1 = (TypeMap*)map1->type;
    TypeMap* type2 = (TypeMap*)map2->type;
    TypeMap* type3 = (TypeMap*)map3->type;
    
    // All should have the same shape pointer
    EXPECT_EQ(type1->shape, type2->shape);
    EXPECT_EQ(type2->shape, type3->shape);
    
    // Only 1 unique shape in pool
    EXPECT_EQ(shape_pool_count(input->shape_pool), 1);
}

TEST(ShapePoolIntegration, HTMLRepeatedElements) {
    const char* html = R"(
        <div class="item" id="1">First</div>
        <div class="item" id="2">Second</div>
        <div class="item" id="3">Third</div>
    )";
    
    Input* input = input_from_source(html, nullptr, nullptr, nullptr);
    ASSERT_NE(input, nullptr);
    
    // All three divs should share same shape
    // (Verification depends on HTML parser structure)
}
```

---

## 7. Performance Considerations

### 7.1 Expected Performance Characteristics

**Shape Lookup**: O(1) average case (hashmap)
**Shape Creation**: O(n) where n = field count
**Memory Overhead**: ~100 bytes per unique shape (CachedShape + hashmap entry)
**Memory Savings**: Up to 99% for documents with many repeated structures

### 7.2 Optimization Strategies

1. **Signature Caching**: Cache signatures for frequently used shapes
2. **Arena Allocation**: All shapes allocated from arena (no fragmentation)
3. **Lazy Finalization**: Only finalize shapes when needed
4. **Adaptive Pooling**: Disable pooling for small documents

### 7.3 Benchmarking Plan

```cpp
// Benchmark repeated structures
void BM_ShapePool_RepeatedMaps(benchmark::State& state) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    ShapePool* sp = shape_pool_create(pool, arena, NULL);
    
    const char* names[] = {"id", "name", "email", "age"};
    TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_STRING, LMD_TYPE_STRING, LMD_TYPE_INT};
    
    for (auto _ : state) {
        ShapeEntry* shape = shape_pool_get_map_shape(sp, names, types, 4);
        benchmark::DoNotOptimize(shape);
    }
    
    shape_pool_release(sp);
    pool_destroy(pool);
}
BENCHMARK(BM_ShapePool_RepeatedMaps);
```

---

## 8. Migration Strategy

### 8.1 Backward Compatibility

The shape pool is **fully backward compatible**:
- Existing code continues to work without changes
- Shape pooling is opt-in via `map_finalize_shape()` / `elmt_finalize_shape()`
- Fallback to old behavior if shape_pool is NULL

### 8.2 Gradual Rollout

**Phase 1**: Infrastructure only (no behavior change)
**Phase 2**: Enable for new parsers first (JSON, XML)
**Phase 3**: Enable for all parsers
**Phase 4**: Enable for runtime (EvalContext)
**Phase 5**: Performance tuning and optimization

### 8.3 Rollback Plan

If issues arise:
1. Set `input->shape_pool = NULL` to disable
2. Parsers automatically fall back to old behavior
3. No data corruption or semantic changes

---

## 9. Success Metrics

### 9.1 Quantitative Metrics

- [ ] Memory reduction ≥50% for repeated structures (target: 80-95%)
- [ ] Performance overhead <5% for shape lookup
- [ ] Zero memory leaks (valgrind clean)
- [ ] All existing tests pass
- [ ] Shape pool tests ≥95% coverage

### 9.2 Qualitative Metrics

- [ ] Code maintainability improved
- [ ] Clear API documentation
- [ ] Easy to debug shape issues
- [ ] Positive developer feedback

---

## 10. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Signature collision | High | Low | Strong hash function, test coverage |
| Memory overhead for small docs | Low | Medium | Lazy initialization, pooling threshold |
| Performance regression | Medium | Low | Comprehensive benchmarking, profiling |
| Parent inheritance bugs | High | Medium | Extensive tests, reference counting validation |
| Integration complexity | Medium | Medium | Incremental rollout, backward compatibility |

---

## 11. Future Enhancements

### 11.1 Shape Validation

Integrate with schema validator:
- Pre-populate shape pool from schema
- Validate document shapes against schema shapes
- Fast shape-based validation

### 11.2 Shape Statistics

Collect runtime statistics:
- Most frequently used shapes
- Shape reuse ratio
- Memory savings metrics
- Hot path optimization opportunities

### 11.3 Shape Serialization

Serialize shape pool with compiled scripts:
- Faster script loading
- Pre-computed shapes
- Cross-document shape sharing

---

## 12. Conclusion

The shape pool implementation will:

1. **Reduce Memory Usage**: 80-95% reduction for documents with repeated structures
2. **Improve Performance**: Faster type checking via cached shapes
3. **Maintain Compatibility**: Zero breaking changes to existing code
4. **Enable Future Features**: Foundation for schema validation and optimization
5. **Follow Best Practices**: Mirroring proven name_pool design

**Timeline**: 4 weeks from start to production-ready
**Risk Level**: Low (backward compatible, incremental rollout)
**Expected Impact**: High (significant memory savings for common use cases)

---

## Appendix A: Code Locations

- **Shape Pool Implementation**: `lambda/shape_pool.hpp`, `lambda/shape_pool.cpp`
- **Input Integration**: `lambda/input/input.cpp`, `lambda/lambda-data.hpp`
- **MarkBuilder Integration**: `lambda/mark_builder.cpp`, `lambda/mark_builder.hpp`
- **Tests**: `test/test_shape_pool_gtest.cpp`
- **Documentation**: `vibe/Lambda_Shape_Pool.md`

## Appendix B: References

- **Name Pool Implementation**: `vibe/Lambda_Name_Pool.md` (✅ Completed)
- **Type System**: `lambda/Lamdba_Runtime.md`
- **Memory Management**: `lib/mempool.c`, `lib/arena.c`
- **Hashmap Library**: `lib/hashmap.c`, `lib/hashmap.h`
