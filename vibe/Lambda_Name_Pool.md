# Lambda Name Pool Enhancement Plan

## Executive Summary

This document outlines a comprehensive plan to enhance Lambda's name and symbol management system by centralizing all names (element names, map keys, attribute names, function names, variable names) and short symbols (≤32 chars) in a hierarchical name pool with parent inheritance support.

**Date**: November 20, 2025  
**Status**: Design & Implementation Plan  
**Priority**: High (Performance & Memory Optimization)

---

## 1. Current State Analysis

### 1.1 Existing Architecture

**Name Pool (`lambda/name_pool.cpp`, `lambda/name_pool.hpp`)**
- Implements string interning using C hashmap
- Each `Input` has its own `NamePool` instance
- Supports parent pool for hierarchical lookup
- Reference counting for lifecycle management
- Hash-based deduplication for string storage

**String Management**
Currently, strings are created through multiple paths:
1. **MarkBuilder** (`mark_builder.cpp`):
   - `createString()` - uses name_pool if `intern_strings_` is enabled
   - Otherwise allocates from arena (fast but no deduplication)
2. **Runtime** (`lambda-mem.cpp`):
   - `heap_strcpy()` - allocates from heap (EvalContext)
   - Used during Lambda script execution
3. **Input Parsers** (`lambda/input/*.cpp`):
   - Mix of pool allocation and direct arena allocation
   - Inconsistent use of name_pool

**Current Usage**
- Name pool is created in `Input::create()` with `NULL` parent
- String interning is available but not consistently used
- No distinction between names, symbols, and regular strings
- No parent inheritance from schema documents

### 1.2 Key Data Structures

```cpp
// Input structure (lambda-data.hpp)
typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    ArrayList* type_list;       // list of types
    Item root;
} Input;

// Script structure (ast.hpp) - extends Input
// Script shares Input's pool and name_pool infrastructure
struct Script {
    const char* reference;      // path and name of the script
    int index;                  // index in runtime scripts list
    const char* source;
    TSTree* syntax_tree;

    // Memory Management - UNIFIED WITH INPUT
    Pool* ast_pool;             // REMOVE: Should use Input's pool
    NamePool* name_pool;        // UNIFIED: Shares Input's name_pool
    
    // AST
    AstNode *ast_root;
    NameScope* current_scope;
    ArrayList* type_list;       // UNIFIED: Shares Input's type_list
    ArrayList* const_list;

    // JIT compilation
    MIR_context_t jit_context;
    main_func_t main_func;
    mpd_context_t* decimal_ctx;
};

// NamePool structure (name_pool.hpp)
typedef struct NamePool {
    Pool* pool;
    struct hashmap* names;      // C hashmap for String* storage
    struct NamePool* parent;    // Parent name pool for hierarchical lookup
    uint32_t ref_count;         // Reference counting
} NamePool;

// String structure (lambda.h)
typedef struct String {
    uint32_t len;
    uint32_t ref_cnt;
    char chars[];
} String;
```

### 1.3 Current Issues

1. **Inconsistent string management**: Multiple allocation paths lead to redundant strings
2. **No symbol optimization**: Short symbols (<32 chars) not pooled
3. **No parent inheritance**: Schema names not shared with data documents
4. **Mixed allocation strategies**: Arena vs pool vs heap allocation
5. **Runtime inefficiency**: `heap_strcpy()` creates new strings without deduplication
6. **Parser inconsistency**: Input parsers don't consistently use name_pool
7. **Script/Input duplication**: Script has separate `ast_pool` and `name_pool` instead of inheriting from Input

---

## 2. Enhancement Requirements

### 2.1 Core Requirements

1. **Unified Name Management**
   - All names (element, map keys, attributes, functions, variables) MUST go through name_pool
   - Automatic deduplication via hash-based lookup
   - Single source of truth for name strings

2. **Symbol Pooling with Size Limit**
   - Symbols ≤32 characters MUST be kept in name_pool
   - Global configuration constant: `NAME_POOL_SYMBOL_LIMIT = 32`
   - Symbols >32 characters allocated normally (arena/heap)
   - Rationale: Short symbols are frequently repeated (e.g., 'x', 'id', 'name')

3. **String Exclusion (Current Phase)**
   - Regular strings MUST NOT go through name_pool in this phase
   - Exception: Future phase may allow explicit string interning
   - Keep strings in arena (parsers) or heap (runtime)

4. **Parent Inheritance**
   - Name pools MUST support parent linkage
   - Schema document's name_pool becomes parent of data document's name_pool
   - Lookup algorithm: current pool → parent pool → grandparent pool (recursive)
   - Benefits: Schema element names automatically available to data documents

5. **MarkBuilder Integration**
   - Remove `intern_strings_` flag (names always interned)
   - Separate methods for names vs strings
   - Names: `createName()` → always use name_pool
   - Strings: `createString()` → arena allocation (no pooling)

6. **Runtime Integration**
   - Update `heap_strcpy()` to check name_pool for names/symbols
   - Add `heap_create_name()` for explicit name creation
   - Add `heap_create_symbol()` with size check (≤32 chars)

---

## 3. Implementation Plan

### Phase 0: Unify Script and Input (Week 1, Days 1-2)

#### 3.0.1 Refactor Script to Extend Input

**Goal**: Make Script use Input's memory infrastructure (pool, name_pool, type_list) instead of maintaining separate instances.

**Current State**:
- Script has: `Pool* ast_pool`, `NamePool* name_pool`, `ArrayList* type_list`
- Input has: `Pool* pool`, `NamePool* name_pool`, `ArrayList* type_list`
- These are separate and duplicated

**Target State**:
- Script contains an embedded Input or inherits from Input
- Script uses Input's pool via accessor/wrapper
- Script's name_pool IS Input's name_pool (same pointer)
- Script's type_list IS Input's type_list (same pointer)

**File**: `lambda/ast.hpp`

```cpp
struct Script {
    // Core identity
    const char* reference;      // path and name of the script
    int index;                  // index in runtime scripts list
    const char* source;
    TSTree* syntax_tree;

    // Memory Management - UNIFIED WITH INPUT
    Input* input;               // Embedded Input structure (or extend Input)
    // REMOVED: Pool* ast_pool;       // Use input->pool instead
    // REMOVED: NamePool* name_pool;  // Use input->name_pool instead
    // REMOVED: ArrayList* type_list; // Use input->type_list instead
    
    // AST
    AstNode *ast_root;
    NameScope* current_scope;
    ArrayList* const_list;      // Script-specific constants

    // JIT compilation
    MIR_context_t jit_context;
    main_func_t main_func;
    mpd_context_t* decimal_ctx;
    
    // Accessors for unified memory management
    inline Pool* pool() const { return input->pool; }
    inline NamePool* name_pool() const { return input->name_pool; }
    inline ArrayList* type_list() const { return input->type_list; }
};
```

**Migration Steps**:

**Step 1: Add Input field to Script (Non-breaking)**
1. Add `Input* input` field to Script structure in `ast.hpp`
2. Keep existing `ast_pool`, `name_pool`, `type_list` fields (temporary duplication)
3. Run `make test-baseline` - should pass (no behavior change)

**Step 2: Initialize Input in Script creation**
1. In `runner.cpp` `transpile_script()`:
   - Create Input using `Input::create()` before AST pool creation
   - Store in `tp->input`
   - Initialize `tp->ast_pool = tp->input->pool` (point to same pool)
   - Initialize `tp->name_pool = tp->input->name_pool` (point to same name_pool)
   - Initialize `tp->type_list = tp->input->type_list` (point to same type_list)
2. Run `make test-baseline` - should pass (still using same memory)

**Step 3: Add accessor methods to Script**
1. In `ast.hpp` add inline accessors:
   ```cpp
   inline Pool* pool() const { return input ? input->pool : ast_pool; }
   inline NamePool* get_name_pool() const { return input ? input->name_pool : name_pool; }
   inline ArrayList* get_type_list() const { return input ? input->type_list : type_list; }
   ```
2. Run `make test-baseline` - should pass (fallback to old fields)

**Step 4: Replace direct field access with accessors**
1. Search and replace in all files:
   - `script->ast_pool` → `script->pool()`
   - `tp->ast_pool` → `tp->pool()`
2. For name_pool and type_list, these already use same name, so just validate they go through Input
3. Run `make test-baseline` after each file change

**Step 5: Remove deprecated fields**
1. Once all access goes through Input, comment out old fields:
   ```cpp
   // DEPRECATED: Use input->pool instead
   // Pool* ast_pool;
   // NamePool* name_pool;  // Use input->name_pool
   // ArrayList* type_list;  // Use input->type_list
   ```
2. Run `make test-baseline` - should fail compilation if any direct access remains
3. Fix any remaining direct accesses
4. Remove commented fields

**Step 6: Clean up Script creation**
1. Simplify `transpile_script()` to just create Input and use it
2. Remove pool_create() calls that are now redundant
3. Run `make test-baseline` - should pass

**Files to Update** (in order):
1. `lambda/ast.hpp` - Add Input field and accessors
2. `lambda/runner.cpp` - Initialize Input in transpile_script()
3. `lambda/build_ast.cpp` - Update AST building to use accessors
4. `lambda/transpile.cpp` - Update transpilation to use accessors
5. `lambda/lambda-eval.cpp` - Update evaluation to use accessors
6. `lambda/schema_builder.cpp` - Update schema transpiler

**Testing at Each Step**:
```bash
cd /Users/henryluo/Projects/Jubily
make build          # Ensure it compiles
make test-baseline  # Ensure all tests pass
```

#### 3.0.2 Benefits of Unification

1. **Single Memory Pool**: Script and Input share same pool - no duplication
2. **Single Name Pool**: Script names and Input names in same pool - better deduplication
3. **Single Type List**: Types registered once, accessible to both Script AST and Input data
4. **Simpler Parent Inheritance**: Script's name_pool can inherit from parent Script's name_pool naturally
5. **Code Simplification**: Remove duplicate initialization code

### Phase 1: Core Infrastructure (Week 1, Days 3-5)

#### 3.1 Add Configuration Constants

**File**: `lambda/lambda.h`

```c
// Name pool configuration
#define NAME_POOL_SYMBOL_LIMIT 32  // Max length for symbols in name_pool
```

#### 3.2 Enhance NamePool API

**File**: `lambda/name_pool.hpp`

Add symbol-aware creation functions:

```cpp
// Symbol creation with size limit check
String* name_pool_create_symbol(NamePool* pool, const char* symbol);
String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len);
String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol);

// Check if a string qualifies for symbol pooling (≤ NAME_POOL_SYMBOL_LIMIT)
bool name_pool_is_poolable_symbol(size_t length);
```

**File**: `lambda/name_pool.cpp`

```cpp
bool name_pool_is_poolable_symbol(size_t length) {
    return length > 0 && length <= NAME_POOL_SYMBOL_LIMIT;
}

String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len) {
    if (!pool || !symbol || len == 0) return nullptr;
    
    // Only pool symbols within size limit
    if (name_pool_is_poolable_symbol(len)) {
        StrView sv = {.str = symbol, .length = len};
        return name_pool_create_strview(pool, sv);
    }
    
    // Symbol too long - allocate normally from pool
    String* str = string_from_strview({.str = symbol, .length = len}, pool->pool);
    if (str) str->ref_cnt = 1;
    return str;
}

String* name_pool_create_symbol(NamePool* pool, const char* symbol) {
    if (!symbol) return nullptr;
    return name_pool_create_symbol_len(pool, symbol, strlen(symbol));
}

String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol) {
    return name_pool_create_symbol_len(pool, symbol.str, symbol.length);
}
```

#### 3.3 Update Input Creation to Support Parent Pools

**File**: `lambda/input/input.hpp`

```cpp
// Add parent_input parameter to creation functions
Input* input_from_source(const char* source, Url* url, String* type, 
                        String* flavor, Input* parent_input = nullptr);
```

**File**: `lambda/input/input.cpp`

Update `Input::create()` to accept parent input:

```cpp
Input* Input::create(Pool* pool, Url* abs_url, Input* parent_input) {
    Input* input = (Input*)pool_alloc(pool, sizeof(Input));
    input->pool = pool;
    input->arena = arena_create_default(pool);
    
    // Create name_pool with parent linkage
    NamePool* parent_pool = parent_input ? parent_input->name_pool : nullptr;
    input->name_pool = name_pool_create(pool, parent_pool);
    
    input->type_list = arraylist_new(16);
    input->url = abs_url;
    input->path = nullptr;
    input->root = (Item){.item = ITEM_NULL};
    return input;
}

// Update InputManager::create_input_instance
Input* InputManager::create_input_instance(Url* abs_url, Input* parent_input) {
    if (!global_pool) return nullptr;
    
    Input* input = Input::create(global_pool, abs_url, parent_input);
    if (!input) return nullptr;
    
    arraylist_append(inputs, input);
    return input;
}
```

### Phase 2: MarkBuilder Enhancements (Week 1-2)

#### 3.4 Refactor MarkBuilder String Management

**File**: `lambda/mark_builder.hpp`

```cpp
class MarkBuilder {
private:
    Input* input_;
    Pool* pool_;
    Arena* arena_;
    NamePool* name_pool_;
    ArrayList* type_list_;
    
    bool auto_string_merge_;    // keep this
    // REMOVE: bool intern_strings_;  // no longer needed

public:
    // Name creation (always uses name_pool)
    String* createName(const char* name);
    String* createName(const char* name, size_t len);
    String* createNameFromStrView(StrView name);
    
    // Symbol creation (uses name_pool for short symbols ≤32 chars)
    String* createSymbol(const char* symbol);
    String* createSymbol(const char* symbol, size_t len);
    String* createSymbolFromStrView(StrView symbol);
    
    // String creation (arena allocation, NO pooling)
    String* createString(const char* str);
    String* createString(const char* str, size_t len);
    String* createStringFromBuf(StringBuf* sb);
    
    // Item creation helpers
    Item createNameItem(const char* name);
    Item createSymbolItem(const char* symbol);
    Item createStringItem(const char* str);  // keep existing
};
```

**File**: `lambda/mark_builder.cpp`

```cpp
MarkBuilder::MarkBuilder(Input* input)
    : input_(input)
    , pool_(input->pool)
    , arena_(input->arena)
    , name_pool_(input->name_pool)
    , type_list_(input->type_list)
    , auto_string_merge_(false)
{
    assert(input != nullptr);
    assert(pool_ != nullptr);
    assert(arena_ != nullptr);
    assert(name_pool_ != nullptr);
    assert(type_list_ != nullptr);
}

// Name creation - always uses name_pool
String* MarkBuilder::createName(const char* name) {
    if (!name) return &EMPTY_STRING;
    return createName(name, strlen(name));
}

String* MarkBuilder::createName(const char* name, size_t len) {
    if (!name || len == 0) return &EMPTY_STRING;
    return name_pool_create_len(name_pool_, name, len);
}

String* MarkBuilder::createNameFromStrView(StrView name) {
    if (!name.str || name.length == 0) return &EMPTY_STRING;
    return name_pool_create_strview(name_pool_, name);
}

// Symbol creation - uses name_pool for short symbols
String* MarkBuilder::createSymbol(const char* symbol) {
    if (!symbol) return &EMPTY_STRING;
    return createSymbol(symbol, strlen(symbol));
}

String* MarkBuilder::createSymbol(const char* symbol, size_t len) {
    if (!symbol || len == 0) return &EMPTY_STRING;
    return name_pool_create_symbol_len(name_pool_, symbol, len);
}

String* MarkBuilder::createSymbolFromStrView(StrView symbol) {
    return name_pool_create_symbol_strview(name_pool_, symbol);
}

// String creation - arena allocation (unchanged)
String* MarkBuilder::createString(const char* str, size_t len) {
    if (!str || len == 0) return &EMPTY_STRING;
    
    // Allocate from arena (fast sequential allocation, no deduplication)
    String* s = (String*)arena_alloc(arena_, sizeof(String) + len + 1);
    s->ref_cnt = 1;
    s->len = len;
    memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

Item MarkBuilder::createNameItem(const char* name) {
    return (Item){.item = y2it(createName(name))};  // use symbol encoding
}

Item MarkBuilder::createSymbolItem(const char* symbol) {
    return (Item){.item = y2it(createSymbol(symbol))};
}
```

#### 3.5 Update ElementBuilder to Use createName()

**File**: `lambda/mark_builder.cpp`

```cpp
// In ElementBuilder::attr() - use createName for attribute keys
ElementBuilder& ElementBuilder::attr(const char* key, Item value) {
    if (!key) return *this;
    String* key_str = builder_->createName(key);  // Changed from createString
    elmt_put(elmt_, key_str, value, builder_->pool());
    return *this;
}

// In ElementBuilder constructor - use createName for element name
ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name)
    : builder_(builder)
    , tag_name_(builder->createName(tag_name))  // Changed from createString
    , elmt_(nullptr)
    , parent_(nullptr)
{
    // ... rest of constructor
    String* name_str = builder->createName(tag_name);  // Changed from createString
    // ...
}
```

#### 3.6 Update MapBuilder to Use createName()

**File**: `lambda/mark_builder.cpp`

```cpp
MapBuilder& MapBuilder::put(const char* key, Item value) {
    if (!key) return *this;
    String* key_str = builder_->createName(key);  // Changed from createString
    map_put(map_, key_str, value, builder_->input());
    return *this;
}
```

### Phase 3: Runtime Integration (Week 2)

#### 3.7 Enhance Runtime Memory Management

**File**: `lambda/lambda-mem.cpp`

Add name/symbol-aware allocation:

```cpp
// Create name string (always uses name_pool)
String* heap_create_name(const char* name, int len) {
    if (!name || len <= 0) return &EMPTY_STRING;
    if (!context || !context->name_pool) {
        log_error("heap_create_name: invalid context or name_pool");
        return &EMPTY_STRING;
    }
    return name_pool_create_len(context->name_pool, name, len);
}

// Create symbol string (uses name_pool for short symbols ≤32 chars)
String* heap_create_symbol(const char* symbol, int len) {
    if (!symbol || len <= 0) return &EMPTY_STRING;
    if (!context || !context->name_pool) {
        log_error("heap_create_symbol: invalid context or name_pool");
        return &EMPTY_STRING;
    }
    return name_pool_create_symbol_len(context->name_pool, symbol, len);
}

// Keep heap_strcpy() for regular strings (unchanged)
String* heap_strcpy(char* src, int len) {
    String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
    strcpy(str->chars, src);
    str->len = len;  str->ref_cnt = 0;
    return str;
}
```

**File**: `lambda/transpiler.hpp`

Add declarations:

```cpp
String* heap_create_name(const char* name, int len);
String* heap_create_symbol(const char* symbol, int len);
```

#### 3.8 Update EvalContext to Include NamePool

**File**: `lambda/lambda-data.hpp`

```cpp
typedef struct EvalContext : Context {
    Heap* heap;
    Pool* ast_pool;
    NamePool* name_pool;        // Add name_pool to eval context for runtime names
    ArrayList* type_list;
    num_stack_t* num_stack;
    void* type_info;
    Item result;
    mpd_context_t* decimal_ctx;
    SchemaValidator* validator;
} EvalContext;
```

**File**: `lambda/lambda-eval.cpp`

Initialize name_pool in context creation:

```cpp
// In context initialization
context->name_pool = name_pool_create(context->ast_pool, nullptr);
```

**Note**: Runtime EvalContext has its own name_pool separate from Input's name_pool. This allows runtime-generated names (variable names, function names during execution) to be managed independently.

### Phase 4: Input Parser Updates (Week 2-3)

#### 3.9 Update Input Parsers to Use MarkBuilder Consistently

**Strategy**: For each input parser, ensure:
1. Element names use `createName()`
2. Attribute keys use `createName()`
3. Map keys use `createName()`
4. Short symbols (≤32 chars) use `createSymbol()`
5. Content strings use `createString()`

**Priority Order** (high to low usage):
1. `input-json.cpp`
2. `input-xml.cpp`
3. `input-html.cpp`
4. `input-yaml.cpp`
5. `input-toml.cpp`
6. `input-csv.cpp`
7. `input-markdown.cpp` (via markup parser)
8. Other format parsers

**Example Pattern** (`input-json.cpp`):

```cpp
void parse_json_object(JsonContext& ctx) {
    MarkBuilder builder(ctx.input());
    MapBuilder map = builder.map();
    
    while (has_more_fields()) {
        const char* key = parse_key();
        // Map keys are names - use createName
        String* key_name = builder.createName(key);
        
        Item value = parse_value();
        map.put(key_name, value);
    }
    
    return map.final();
}

void parse_json_string(JsonContext& ctx) {
    const char* str = parse_string_value();
    // String content - use createString (arena allocation)
    return builder.createString(str);
}
```

#### 3.10 Update Schema Loading to Support Parent Pools

**File**: `lambda/validator/doc_validator.cpp`

```cpp
// When loading schema
Input* load_schema(const char* schema_file) {
    Input* schema_input = InputManager::create_input(schema_url);
    parse_schema(schema_input, schema_content);
    return schema_input;
}

// When validating document with schema
Input* load_document_with_schema(const char* doc_file, Input* schema_input) {
    // Pass schema_input as parent - its name_pool becomes parent
    Input* doc_input = InputManager::create_input(doc_url, schema_input);
    parse_document(doc_input, doc_content);
    return doc_input;
}
```

### Phase 5: Testing & Validation (Week 3-4)

#### 3.11 Unit Tests

**File**: `test/test_name_pool.cpp`

```cpp
TEST(NamePool, BasicNameCreation) {
    Pool* pool = pool_create();
    NamePool* name_pool = name_pool_create(pool, nullptr);
    
    String* name1 = name_pool_create_name(name_pool, "element");
    String* name2 = name_pool_create_name(name_pool, "element");
    
    // Should return same pointer (interned)
    EXPECT_EQ(name1, name2);
    EXPECT_EQ(name1->ref_cnt, 2);
    
    name_pool_release(name_pool);
    pool_destroy(pool);
}

TEST(NamePool, SymbolSizeLimit) {
    Pool* pool = pool_create();
    NamePool* name_pool = name_pool_create(pool, nullptr);
    
    // Short symbol - should be pooled
    String* short_sym = name_pool_create_symbol(name_pool, "x");
    String* short_sym2 = name_pool_create_symbol(name_pool, "x");
    EXPECT_EQ(short_sym, short_sym2);
    
    // Long symbol (>32 chars) - should NOT be pooled
    const char* long_sym_text = "this_is_a_very_long_symbol_name_exceeding_limit";
    String* long_sym1 = name_pool_create_symbol(name_pool, long_sym_text);
    String* long_sym2 = name_pool_create_symbol(name_pool, long_sym_text);
    EXPECT_NE(long_sym1, long_sym2);  // Different pointers
    
    name_pool_release(name_pool);
    pool_destroy(pool);
}

TEST(NamePool, ParentInheritance) {
    Pool* pool = pool_create();
    
    // Create parent pool with schema names
    NamePool* schema_pool = name_pool_create(pool, nullptr);
    String* schema_name = name_pool_create_name(schema_pool, "Person");
    
    // Create child pool inheriting from schema
    NamePool* doc_pool = name_pool_create(pool, schema_pool);
    
    // Should find name in parent
    String* found_name = name_pool_lookup(doc_pool, "Person");
    EXPECT_EQ(found_name, schema_name);
    
    name_pool_release(doc_pool);
    name_pool_release(schema_pool);
    pool_destroy(pool);
}
```

#### 3.12 Integration Tests

**File**: `test/test_input_with_schema.cpp`

Test schema + document name inheritance:

```cpp
TEST(InputWithSchema, NamePoolInheritance) {
    // Load schema
    Input* schema = input_from_source(schema_content, schema_url, "lambda", nullptr);
    ASSERT_NE(schema, nullptr);
    
    // Load document with schema as parent
    Input* doc = input_from_source(doc_content, doc_url, "json", nullptr, schema);
    ASSERT_NE(doc, nullptr);
    
    // Verify name pool parent linkage
    EXPECT_EQ(doc->name_pool->parent, schema->name_pool);
    
    // Verify schema names available in document
    String* schema_name = name_pool_lookup(schema->name_pool, "Person");
    String* doc_name = name_pool_lookup(doc->name_pool, "Person");
    EXPECT_EQ(schema_name, doc_name);
}
```

#### 3.13 Performance Tests

**File**: `test/benchmark_name_pool.cpp`

```cpp
// Measure memory savings from name deduplication
// Measure lookup performance with parent chains
// Compare arena vs name_pool allocation costs
```

### Phase 6: Documentation & Cleanup (Week 4)

#### 3.14 Update Documentation

**Files to Update**:
- `doc/Lambda_Reference.md` - Document name vs string vs symbol
- `lambda/mark_builder.hpp` - Update API comments
- `lambda/name_pool.hpp` - Document parent inheritance
- `README.md` - Add performance benefits note

#### 3.15 Migration Guide

Create `doc/Name_Pool_Migration.md`:
```markdown
# Name Pool Migration Guide

## API Changes

### MarkBuilder
- `createString()` → Keep for content strings
- NEW `createName()` → Use for all names (element, attributes, keys)
- NEW `createSymbol()` → Use for short symbols (≤32 chars)

### Runtime
- `heap_strcpy()` → Keep for regular strings
- NEW `heap_create_name()` → Use for name strings
- NEW `heap_create_symbol()` → Use for symbol strings

## Migration Steps
1. Identify string usages: name, symbol, or content
2. Replace creation calls with appropriate function
3. Test for memory reduction and performance
```

---

## 4. Implementation Timeline

| Week | Phase | Deliverables |
|------|-------|--------------|
| 1 | Phase 1-2 | Core infrastructure, NamePool API, MarkBuilder refactor |
| 2 | Phase 3-4 | Runtime integration, parser updates (high priority) |
| 3 | Phase 4 cont. | Remaining parser updates, schema integration |
| 4 | Phase 5-6 | Testing, validation, documentation |

**Total Duration**: 4 weeks (with parallel work possible)

---

## 5. Success Metrics

### 5.1 Memory Efficiency
- **Target**: 20-40% reduction in string memory for typical documents
- **Measurement**: Compare memory usage before/after with representative test files
- **Validation**: Run memory profiler on large JSON/XML documents

### 5.2 Performance
- **Name lookup**: O(1) hash lookup (no regression)
- **Parent chain lookup**: O(depth) acceptable for typical schema depth (1-3 levels)
- **String creation**: Name pool overhead < 10% vs arena allocation

### 5.3 Code Quality
- **API consistency**: All parsers use MarkBuilder consistently
- **Test coverage**: >90% coverage for name_pool functionality
- **Documentation**: All new APIs documented with examples

---

## 6. Risk Analysis & Mitigation

### 6.1 Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Performance regression from name_pool lookups | Medium | Low | Benchmark early, optimize hot paths |
| Reference counting bugs | High | Medium | Extensive unit tests, valgrind checks |
| API confusion (name vs string vs symbol) | Medium | Medium | Clear documentation, migration guide |
| Parent chain depth performance | Low | Low | Limit schema inheritance depth (recommended max 3) |

### 6.2 Rollback Plan
- Keep old `intern_strings_` flag temporarily for A/B testing
- Feature flag: `USE_NAME_POOL_ENHANCEMENT` (default ON)
- Easy rollback: Remove `createName()` calls, revert to `createString()`

---

## 7. Future Enhancements (Post Phase 1)

### 7.1 Optional String Pooling
- Add explicit `name_pool_intern_string()` for hot-path strings
- Opt-in via API or heuristic (e.g., string seen >N times)
- Use case: Repeated string values in large datasets

### 7.2 Name Pool Statistics
- Track pooling efficiency (hit rate, memory saved)
- Expose via API: `name_pool_get_stats()`
- Useful for optimization decisions

### 7.3 Name Pool Serialization
- Save/load name pool state for faster document reloading
- Binary format with hash table dump
- Use case: Large schema files loaded repeatedly

---

## 8. Implementation Strategy

### 8.1 Incremental Development Approach

**Critical Rule**: Each change must pass `make test-baseline` before proceeding to the next step.

**Step-by-Step Process**:
1. Make small, focused changes (one file or one feature at a time)
2. Run `make test-baseline` after each change
3. Fix any test failures immediately
4. Commit working changes before moving to next step
5. Document any test changes needed

**Test-First Approach**:
- Add new unit tests for new functionality before implementation
- Ensure existing tests still pass (no regressions)
- Update tests if API changes require it (document why)

### 8.2 Rollback Safety
- Keep changes in feature branch: `feature/name-pool-enhancement`
- Each commit should be independently buildable and testable
- Tag stable checkpoints: `name-pool-phase1-stable`, etc.

## 9. References

### 9.1 Existing Code Files
- `lambda/name_pool.cpp` - Current implementation
- `lambda/name_pool.hpp` - API definitions
- `lambda/mark_builder.cpp` - Document builder
- `lambda/lambda-mem.cpp` - Runtime memory management
- `lambda/input/input.cpp` - Input system
- `lambda/validator/doc_validator.cpp` - Schema validation

### 8.2 Related Documentation
- `vibe/String.md` - String management overview
- `vibe/Arena.md` - Arena allocation details
- `doc/Lambda_Reference.md` - Language reference

---

## Appendix A: Configuration Constants

```c
// lambda/lambda.h
#define NAME_POOL_SYMBOL_LIMIT 32       // Max length for symbols in name_pool
#define NAME_POOL_INITIAL_SIZE 128      // Initial hash table size
#define NAME_POOL_LOAD_FACTOR 0.75      // Hash table resize threshold
```

---

## Appendix B: API Summary

### NamePool API (Enhanced)

```cpp
// Core lifecycle
NamePool* name_pool_create(Pool* pool, NamePool* parent);
NamePool* name_pool_retain(NamePool* pool);
void name_pool_release(NamePool* pool);

// Name creation (always interned)
String* name_pool_create_name(NamePool* pool, const char* name);
String* name_pool_create_len(NamePool* pool, const char* name, size_t len);
String* name_pool_create_strview(NamePool* pool, StrView name);

// Symbol creation (interned if ≤32 chars)
String* name_pool_create_symbol(NamePool* pool, const char* symbol);
String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len);
String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol);

// Utilities
bool name_pool_is_poolable_symbol(size_t length);
String* name_pool_lookup(NamePool* pool, const char* name);
bool name_pool_contains(NamePool* pool, const char* name);
size_t name_pool_count(NamePool* pool);
void name_pool_print_stats(NamePool* pool);
```

### MarkBuilder API (Enhanced)

```cpp
// Names (always name_pool)
String* createName(const char* name);
String* createName(const char* name, size_t len);
String* createNameFromStrView(StrView name);
Item createNameItem(const char* name);

// Symbols (name_pool if ≤32 chars)
String* createSymbol(const char* symbol);
String* createSymbol(const char* symbol, size_t len);
String* createSymbolFromStrView(StrView symbol);
Item createSymbolItem(const char* symbol);

// Strings (arena allocation)
String* createString(const char* str);
String* createString(const char* str, size_t len);
String* createStringFromBuf(StringBuf* sb);
Item createStringItem(const char* str);
```

### Runtime API (Enhanced)

```cpp
// Names (always name_pool)
String* heap_create_name(const char* name, int len);

// Symbols (name_pool if ≤32 chars)
String* heap_create_symbol(const char* symbol, int len);

// Strings (heap allocation)
String* heap_strcpy(char* src, int len);
```

---

**End of Document**
