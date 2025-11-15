# Lambda Validator Consolidation Plan - Phase 6

**Date**: November 15, 2025
**Author**: Henry Luo
**Status**: Planning

## Executive Summary

Consolidate `AstValidator` and `SchemaValidator` into a single unified `SchemaValidator` structure, eliminating redundancy and simplifying the validator API. The current implementation has `SchemaValidator` as a thin wrapper around `AstValidator` with several unused fields, creating unnecessary complexity.

## Current State Analysis

### AstValidator Structure (Primary Implementation)
```cpp
typedef struct AstValidator {
    Transpiler* transpiler;          // AST building and type extraction
    Pool* pool;                      // memory pool
    HashMap* type_definitions;       // registry of Type* definitions
    PathSegment* current_path;       // current validation path
    int current_depth;               // current recursion depth
    int max_depth;                   // deprecated: use options.max_depth
    ValidationOptions options;       // validation configuration
    HashMap* visited_nodes;          // for circular reference detection
    clock_t validation_start_time;   // for timeout tracking
} AstValidator;
```

### SchemaValidator Structure (Wrapper - To Be Enhanced)
```cpp
typedef struct SchemaValidator {
    HashMap* schemas;              // ❌ UNUSED - set to nullptr, never accessed
    Pool* pool;                    // ✅ USED - but duplicated from AstValidator
    AstValidator* ast_validator;   // ✅ USED - delegates all work here
    void* context;                 // ❌ UNUSED - set to nullptr, never accessed
    void* custom_validators;       // ❌ UNUSED - set to nullptr, never accessed
    ValidationOptions default_options;  // ❌ UNUSED - never accessed
} SchemaValidator;
```

### Current Usage Pattern
```cpp
// All SchemaValidator functions just delegate to AstValidator
SchemaValidator* validator = schema_validator_create(pool);
schema_validator_load_schema(validator, source, root_type);
  └─> ast_validator_load_schema(validator->ast_validator, source, root_type);
validate_document(validator, item, type_name);
  └─> ast_validator_validate(validator->ast_validator, item, type_name);
```

## Problems with Current Design

1. **Redundancy**: SchemaValidator is purely a pass-through wrapper
2. **Memory Waste**: Unused fields consume memory and create confusion
3. **API Complexity**: Two structures for one concept
4. **Maintenance Burden**: Changes must be made in two places
5. **Naming Confusion**: "AstValidator" is the real validator, but name suggests implementation detail

## Proposed Solution

### Merge Strategy: Keep SchemaValidator Name

**Rationale for keeping SchemaValidator as the primary name:**
- "SchemaValidator" better describes what it does (validates against schemas)
- "AstValidator" sounds like an implementation detail
- More intuitive for external API users
- Aligns with domain language (schema validation)

### New Unified SchemaValidator Structure (C++ Class)
```cpp
class SchemaValidator {
private:
    // Core components
    Transpiler* transpiler;          // AST building and type extraction
    Pool* pool;                      // memory pool
    HashMap* type_definitions;       // registry of Type* definitions

    // Validation state
    PathSegment* current_path;       // current validation path
    int current_depth;               // current recursion depth
    int max_depth;                   // deprecated: use options.max_depth
    ValidationOptions options;       // validation configuration
    HashMap* visited_nodes;          // for circular reference detection
    clock_t validation_start_time;   // for timeout tracking

public:
    // Constructor and destructor
    static SchemaValidator* create(Pool* pool);
    void destroy();

    // Schema management
    int load_schema(const char* source, const char* root_type);

    // Validation methods
    ValidationResult* validate(ConstItem item, const char* type_name);
    ValidationResult* validate_with_format(ConstItem item, const char* type_name, const char* input_format);
    ValidationResult* validate_type(ConstItem item, Type* type);

    // Options management
    void set_options(ValidationOptions* options);
    ValidationOptions* get_options();
    void set_strict_mode(bool strict);
    void set_max_errors(int max);
    void set_timeout(int timeout_ms);
    void set_show_suggestions(bool show);
    void set_show_context(bool show);

    // Type lookup
    Type* find_type(const char* type_name);
    Type* resolve_type_reference(const char* type_name);

    // Accessors for internal validation functions
    Pool* get_pool() const { return pool; }
    PathSegment* get_current_path() const { return current_path; }
    int get_current_depth() const { return current_depth; }
    HashMap* get_visited_nodes() const { return visited_nodes; }
};
```

**Removed fields:**
- ❌ `HashMap* schemas` - never used
- ❌ `void* context` - never used
- ❌ `void* custom_validators` - never used
- ❌ `ValidationOptions default_options` - never used
- ❌ `AstValidator* ast_validator` - no longer needed (merged)

**API Changes:**
- ✅ Member functions instead of standalone functions
- ✅ Encapsulation of internal state
- ✅ Cleaner, more intuitive API
- ✅ Better C++ idioms while maintaining C compatibility through wrapper functions

## Implementation Plan

### Phase 1: Preparation and Deprecation (Day 1)

#### 1.1 Create Migration Tracking
**File**: `vibe/Validator6_Progress.md` (create)

Track all files that need to be updated:

```markdown
# Validator Refactoring Progress

## Files Using AstValidator (need refactoring)
- [ ] lambda/validator/doc_validator.cpp
- [ ] lambda/validator/validate.cpp
- [ ] lambda/validator/ast_validate.cpp
- [ ] lambda/validator/error_reporting.cpp (check)
- [ ] lambda/validator/suggestions.cpp (check)
- [ ] test/test_validator_integration.cpp
- [ ] test/test_validator_gtest.cpp
- [ ] test/lambda/validator/validator_enhanced.cpp
- [ ] lambda/validate.cpp (if exists)
- [ ] lambda/main.cpp (check if validator used)

## Function Call Replacements Needed
- ast_validator_create → SchemaValidator::create
- ast_validator_destroy → validator->destroy()
- ast_validator_load_schema → validator->load_schema
- ast_validator_validate → validator->validate
- ast_validator_validate_with_format → validator->validate_with_format
- ast_validator_validate_type → validator->validate_type
- ast_validator_set_options → validator->set_options
- ast_validator_get_options → validator->get_options()
- ast_validator_set_strict_mode → validator->set_strict_mode
- ast_validator_set_max_errors → validator->set_max_errors
- ast_validator_set_timeout → validator->set_timeout
- ast_validator_set_show_suggestions → validator->set_show_suggestions
- ast_validator_set_show_context → validator->set_show_context
- ast_validator_find_type → validator->find_type
- ast_validator_resolve_type_reference → validator->resolve_type_reference
```

#### 1.2 Document Migration Strategy
**File**: `doc/Lambda_Validator_Migration.md` (create)

```markdown
## Migration from AstValidator to SchemaValidator (C++ Class)

The validator API has been completely refactored. `AstValidator` struct has been replaced
with a proper C++ `SchemaValidator` class with member functions.

### Quick Migration Guide

**Old Code (C-style with AstValidator):**
```cpp
AstValidator* validator = ast_validator_create(pool);
ast_validator_load_schema(validator, schema_source, "Document");
ValidationResult* result = ast_validator_validate(validator, item, "Document");
ast_validator_set_strict_mode(validator, true);
ast_validator_destroy(validator);
```

**New Code (C++ style with SchemaValidator - Recommended):**
```cpp
SchemaValidator* validator = SchemaValidator::create(pool);
validator->load_schema(schema_source, "Document");
ValidationResult* result = validator->validate(item, "Document");
validator->set_strict_mode(true);
validator->destroy();
```

**New Code (C-style wrapper - For C compatibility):**
```cpp
SchemaValidator* validator = schema_validator_create(pool);
schema_validator_load_schema(validator, schema_source, "Document");
ValidationResult* result = schema_validator_validate(validator, item, "Document");
schema_validator_set_strict_mode(validator, true);
schema_validator_destroy(validator);
```

### What Changed
- `AstValidator` struct → `SchemaValidator` C++ class
- `ast_validator_*()` functions → `validator->method()` member functions
- Proper encapsulation with private fields
- Cleaner, more intuitive API

### Migration Steps
1. Replace `AstValidator*` declarations with `SchemaValidator*`
2. Replace `ast_validator_create(pool)` with `SchemaValidator::create(pool)`
3. Replace `ast_validator_method(validator, args)` with `validator->method(args)`
4. Replace `ast_validator_destroy(validator)` with `validator->destroy()`

### No Compatibility Layer
**Important**: There are NO aliases or macros. All code must be properly refactored.
This ensures clean, maintainable code going forward.
```

### Phase 2: Core Implementation Changes (Day 1-2)

#### 2.1 Update validator.hpp Structure Definition
**File**: `lambda/validator/validator.hpp`

**Before:**
```cpp
// Validation context
typedef struct AstValidator {
    Transpiler* transpiler;
    Pool* pool;
    HashMap* type_definitions;
    PathSegment* current_path;
    int current_depth;
    int max_depth;
    ValidationOptions options;
    HashMap* visited_nodes;
    clock_t validation_start_time;
} AstValidator;

// Schema validator structure
typedef struct SchemaValidator {
    HashMap* schemas;
    Pool* pool;
    AstValidator* ast_validator;
    void* context;
    void* custom_validators;
    ValidationOptions default_options;
} SchemaValidator;
```

**After (C++ Class - No Legacy Typedefs):**
```cpp
// Schema validator class (unified, object-oriented)
class SchemaValidator {
private:
    // Core components
    Transpiler* transpiler;          // AST building and type extraction
    Pool* pool;                      // memory pool
    HashMap* type_definitions;       // registry of Type* definitions

    // Validation state
    PathSegment* current_path;       // current validation path
    int current_depth;               // current recursion depth
    int max_depth;                   // deprecated: use options.max_depth
    ValidationOptions options;       // validation configuration
    HashMap* visited_nodes;          // for circular reference detection
    clock_t validation_start_time;   // for timeout tracking

    // Private constructor (use create() instead)
    SchemaValidator(Pool* pool);

public:
    // Static factory method
    static SchemaValidator* create(Pool* pool);

    // Destructor
    void destroy();

    // Schema management
    int load_schema(const char* source, const char* root_type);

    // Validation methods
    ValidationResult* validate(ConstItem item, const char* type_name);
    ValidationResult* validate_with_format(ConstItem item, const char* type_name, const char* input_format);
    ValidationResult* validate_type(ConstItem item, Type* type);

    // Options management
    void set_options(ValidationOptions* options);
    ValidationOptions* get_options();
    void set_strict_mode(bool strict);
    void set_max_errors(int max);
    void set_timeout(int timeout_ms);
    void set_show_suggestions(bool show);
    void set_show_context(bool show);

    // Type lookup
    Type* find_type(const char* type_name);
    Type* resolve_type_reference(const char* type_name);

    // Accessors (for internal validation functions)
    Pool* get_pool() const { return pool; }
    Transpiler* get_transpiler() const { return transpiler; }
    PathSegment* get_current_path() const { return current_path; }
    void set_current_path(PathSegment* path) { current_path = path; }
    int get_current_depth() const { return current_depth; }
    void set_current_depth(int depth) { current_depth = depth; }
    HashMap* get_visited_nodes() const { return visited_nodes; }
    HashMap* get_type_definitions() const { return type_definitions; }
    ValidationOptions& get_options_ref() { return options; }
    clock_t get_validation_start_time() const { return validation_start_time; }
    void set_validation_start_time(clock_t time) { validation_start_time = time; }
};

// NO legacy typedefs or macros - all code must be properly refactored
```#### 2.2 Update doc_validator.cpp Implementation
**File**: `lambda/validator/doc_validator.cpp`

**Changes needed:**
1. Implement SchemaValidator as a C++ class with member functions
2. Provide C-style wrapper functions for backward compatibility
3. Update all internal references to use `this->` or direct member access

```cpp
// ==================== Core Validator Implementation ====================

// Private constructor
SchemaValidator::SchemaValidator(Pool* pool)
    : pool(pool), transpiler(nullptr), type_definitions(nullptr),
      current_path(nullptr), current_depth(0), max_depth(1024),
      visited_nodes(nullptr), validation_start_time(0) {

    // Initialize default options
    options.strict_mode = false;
    options.allow_unknown_fields = true;
    options.allow_empty_elements = true;
    options.max_depth = 1024;
    options.timeout_ms = 0;
    options.max_errors = 0;
    options.show_suggestions = true;
    options.show_context = true;
    options.enabled_rules = nullptr;
    options.disabled_rules = nullptr;
}

// Static factory method
SchemaValidator* SchemaValidator::create(Pool* pool) {
    if (!pool) return nullptr;

    // Use placement new with pool allocation
    void* mem = pool_calloc(pool, sizeof(SchemaValidator));
    if (!mem) return nullptr;

    SchemaValidator* validator = new (mem) SchemaValidator(pool);

    validator->transpiler = transpiler_create(pool);
    if (!validator->transpiler) {
        return nullptr;
    }

    // Initialize type definitions registry
    validator->type_definitions = hashmap_new(sizeof(TypeRegistryEntry), 0, 0, 0,
                                   type_entry_hash, type_entry_compare, NULL, pool);
    if (!validator->type_definitions) {
        return nullptr;
    }

    // Initialize visited nodes for circular reference detection
    validator->visited_nodes = hashmap_new(sizeof(VisitedEntry), 0, 0, 0,
                                   visited_entry_hash, visited_entry_compare, NULL, pool);
    if (!validator->visited_nodes) {
        return nullptr;
    }

    return validator;
}

// Destructor method
void SchemaValidator::destroy() {
    if (type_definitions) {
        hashmap_free(type_definitions);
        type_definitions = nullptr;
    }

    if (visited_nodes) {
        hashmap_free(visited_nodes);
        visited_nodes = nullptr;
    }

    if (transpiler) {
        transpiler_destroy(transpiler);
        transpiler = nullptr;
    }

    // Note: Memory pool cleanup handled by caller
    // Don't delete this - allocated from pool
}

// Schema loading (now a member function)
int SchemaValidator::load_schema(const char* source, const char* root_type) {
    if (!source || !root_type) return -1;

    log_info("Loading schema with root type: %s", root_type);

    // Build AST using transpiler
    AstNode* ast = transpiler_build_ast(this->transpiler, source);
    if (!ast) {
        log_error("Failed to build AST from source");
        return -1;
    }

    // ... rest of existing implementation using this->field ...
    return 0;
}

// Validation methods
ValidationResult* SchemaValidator::validate(ConstItem item, const char* type_name) {
    // Delegate to validate_with_format with auto-detect
    return validate_with_format(item, type_name, nullptr);
}

ValidationResult* SchemaValidator::validate_with_format(
    ConstItem item, const char* type_name, const char* input_format) {

    if (!type_name) {
        ValidationResult* result = create_validation_result(this->pool);
        ValidationError* error = create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Type name is required",
            this->current_path, this->pool);
        add_validation_error(result, error);
        return result;
    }

    // Format-specific unwrapping
    ConstItem processed_item = item;
    if (input_format) {
        if (strcmp(input_format, "xml") == 0) {
            processed_item = unwrap_xml_document(item, this->pool);
        } else if (strcmp(input_format, "html") == 0) {
            processed_item = unwrap_html_document(item, this->pool);
        }
    }

    // Find type definition
    Type* type = this->find_type(type_name);
    if (!type) {
        ValidationResult* result = create_validation_result(this->pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type '%s' not found in schema", type_name);
        ValidationError* error = create_validation_error(
            VALID_ERROR_REFERENCE_ERROR, error_msg,
            this->current_path, this->pool);
        add_validation_error(result, error);
        return result;
    }

    // Perform validation
    return validate_type(processed_item, type);
}

ValidationResult* SchemaValidator::validate_type(ConstItem item, Type* type) {
    // Initialize validation session
    if (this->options.timeout_ms > 0) {
        this->validation_start_time = clock();
    }

    // Delegate to core validation function
    return validate_against_type(this, item, type);
}

// Options management
void SchemaValidator::set_options(ValidationOptions* opts) {
    if (opts) {
        this->options = *opts;
    }
}

ValidationOptions* SchemaValidator::get_options() {
    return &this->options;
}

void SchemaValidator::set_strict_mode(bool strict) {
    this->options.strict_mode = strict;
}

void SchemaValidator::set_max_errors(int max) {
    this->options.max_errors = max;
}

void SchemaValidator::set_timeout(int timeout_ms) {
    this->options.timeout_ms = timeout_ms;
}

void SchemaValidator::set_show_suggestions(bool show) {
    this->options.show_suggestions = show;
}

void SchemaValidator::set_show_context(bool show) {
    this->options.show_context = show;
}

// Type lookup methods
Type* SchemaValidator::find_type(const char* type_name) {
    if (!type_name || !this->type_definitions) return nullptr;

    StrView name_key = { type_name, strlen(type_name) };
    TypeRegistryEntry lookup = { nullptr, name_key };

    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)
        hashmap_get(this->type_definitions, &lookup);

    return entry ? entry->definition->runtime_type : nullptr;
}

Type* SchemaValidator::resolve_type_reference(const char* type_name) {
    // ... implementation with circular reference detection ...
    return find_type(type_name);
}

// ==================== C-Style Wrapper Functions (For C Compatibility Only) ====================

extern "C" {

// These wrappers exist ONLY for C compatibility, not for backward compatibility with old C++ code
// All C++ code should use the member functions directly

SchemaValidator* schema_validator_create(Pool* pool) {
    return SchemaValidator::create(pool);
}

void schema_validator_destroy(SchemaValidator* validator) {
    if (validator) {
        validator->destroy();
    }
}

int schema_validator_load_schema(SchemaValidator* validator,
                                 const char* source, const char* root_type) {
    return validator ? validator->load_schema(source, root_type) : -1;
}

ValidationResult* schema_validator_validate(SchemaValidator* validator,
                                            ConstItem item, const char* type_name) {
    return validator ? validator->validate(item, type_name) : nullptr;
}

ValidationResult* schema_validator_validate_with_format(
    SchemaValidator* validator, ConstItem item,
    const char* type_name, const char* input_format) {
    return validator ? validator->validate_with_format(item, type_name, input_format) : nullptr;
}

ValidationResult* schema_validator_validate_type(SchemaValidator* validator,
                                                 ConstItem item, Type* type) {
    return validator ? validator->validate_type(item, type) : nullptr;
}

void schema_validator_set_options(SchemaValidator* validator, ValidationOptions* options) {
    if (validator) validator->set_options(options);
}

ValidationOptions* schema_validator_get_options(SchemaValidator* validator) {
    return validator ? validator->get_options() : nullptr;
}

void schema_validator_set_strict_mode(SchemaValidator* validator, bool strict) {
    if (validator) validator->set_strict_mode(strict);
}

void schema_validator_set_max_errors(SchemaValidator* validator, int max) {
    if (validator) validator->set_max_errors(max);
}

void schema_validator_set_timeout(SchemaValidator* validator, int timeout_ms) {
    if (validator) validator->set_timeout(timeout_ms);
}

void schema_validator_set_show_suggestions(SchemaValidator* validator, bool show) {
    if (validator) validator->set_show_suggestions(show);
}

void schema_validator_set_show_context(SchemaValidator* validator, bool show) {
    if (validator) validator->set_show_context(show);
}

Type* schema_validator_find_type(SchemaValidator* validator, const char* type_name) {
    return validator ? validator->find_type(type_name) : nullptr;
}

Type* schema_validator_resolve_type_reference(SchemaValidator* validator, const char* type_name) {
    return validator ? validator->resolve_type_reference(type_name) : nullptr;
}

} // extern "C"
```#### 2.3 Update validate.cpp
**File**: `lambda/validator/validate.cpp`

**Changes needed:**
1. Update all function signatures to use `SchemaValidator*`
2. Update field access to use accessor methods where appropriate
3. Keep functions as standalone (not member functions) since they're used internally

Example transformations:
```cpp
// Before
ValidationResult* validate_against_primitive_type(AstValidator* validator, ConstItem item, Type* type) {
    log_debug("[AST_VALIDATOR] Validating primitive: expected=%d, actual=%d",
              type->type_id, item.type_id());
    ValidationResult* result = create_validation_result(validator->pool);
    // ...
    ValidationError* error = create_validation_error(
        AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool);
    // ...
}

// After
ValidationResult* validate_against_primitive_type(SchemaValidator* validator, ConstItem item, Type* type) {
    log_debug("[VALIDATOR] Validating primitive: expected=%d, actual=%d",
              type->type_id, item.type_id());
    ValidationResult* result = create_validation_result(validator->get_pool());
    // ...
    ValidationError* error = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH, error_msg,
        validator->get_current_path(), validator->get_pool());
    // ...
}
```

**Access pattern changes:**
- `validator->pool` → `validator->get_pool()`
- `validator->current_path` → `validator->get_current_path()`
- `validator->current_depth` → `validator->get_current_depth()`
- `validator->options` → `validator->get_options_ref()`
- `validator->visited_nodes` → `validator->get_visited_nodes()`
- `validator->type_definitions` → `validator->get_type_definitions()`

**Why keep as standalone functions:**
These validation functions are internal helpers that operate on the validator state.
Making them standalone keeps the public API clean while maintaining modularity.

#### 2.4 Remove Old Wrapper Implementations
**File**: `lambda/validator/ast_validate.cpp`

**Delete these old wrapper functions** (lines 49-122):
```cpp
// REMOVE - No longer needed
SchemaValidator* schema_validator_create(Pool* pool) { ... }
void schema_validator_destroy(SchemaValidator* validator) { ... }
int schema_validator_load_schema(SchemaValidator* validator, ...) { ... }
ValidationResult* validate_document(SchemaValidator* validator, ...) { ... }
```

**Keep only validate_document as a convenience function:**
```cpp
// Convenience function - wraps member function call
ValidationResult* validate_document(SchemaValidator* validator, Item document,
                                   const char* schema_name) {
    if (!validator || document.item == ITEM_NULL || !schema_name) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        ValidationError* error = create_validation_error(
            VALID_ERROR_PARSE_ERROR,
            "Invalid validation parameters",
            nullptr,
            validator->get_pool()
        );
        add_validation_error(result, error);
        return result;
    }

    ConstItem const_doc = *(ConstItem*)&document;
    return validator->validate(const_doc, schema_name);
}
```

**Refactor run_ast_validation function:**
```cpp
ValidationResult* run_ast_validation(const char* data_file, const char* schema_file,
                                    const char* input_format, ValidationOptions* options) {
    Pool* pool = pool_create();
    // ... setup code ...

    // OLD: SchemaValidator* validator = schema_validator_create(pool);
    // NEW:
    SchemaValidator* validator = SchemaValidator::create(pool);

    // OLD: schema_validator_load_schema(validator, schema_contents, root_type);
    // NEW:
    validator->load_schema(schema_contents, root_type);

    // OLD: validation_result = schema_validator_validate(validator, const_doc, root_type);
    // NEW:
    validation_result = validator->validate(const_doc, root_type);

    // OLD: schema_validator_destroy(validator);
    // NEW:
    validator->destroy();

    // ... cleanup ...
}
```

#### 2.5 Update error_reporting.cpp
**File**: `lambda/validator/error_reporting.cpp`

Update function signatures:
```cpp
// Before
String* generate_validation_report(ValidationResult* result, Pool* pool);

// After (if needed - check for AstValidator references)
// Should be structure-agnostic, only uses ValidationResult
```

#### 2.6 Update suggestions.cpp
**File**: `lambda/validator/suggestions.cpp`

Similar updates if any `AstValidator*` references exist.

### Phase 3: Update Header File API (Day 2)

#### 3.1 Update validator.hpp Function Declarations
**File**: `lambda/validator/validator.hpp`

```cpp
// ==================== SchemaValidator Class (C++ API) ====================

class SchemaValidator {
private:
    Transpiler* transpiler;
    Pool* pool;
    HashMap* type_definitions;
    PathSegment* current_path;
    int current_depth;
    int max_depth;
    ValidationOptions options;
    HashMap* visited_nodes;
    clock_t validation_start_time;

    SchemaValidator(Pool* pool);  // Private constructor

public:
    // Factory method and destructor
    static SchemaValidator* create(Pool* pool);
    void destroy();

    // Schema management
    int load_schema(const char* source, const char* root_type);

    // Validation methods
    ValidationResult* validate(ConstItem item, const char* type_name);
    ValidationResult* validate_with_format(ConstItem item, const char* type_name, const char* input_format);
    ValidationResult* validate_type(ConstItem item, Type* type);

    // Options management
    void set_options(ValidationOptions* options);
    ValidationOptions* get_options();
    static ValidationOptions default_options();
    void set_strict_mode(bool strict);
    void set_max_errors(int max);
    void set_timeout(int timeout_ms);
    void set_show_suggestions(bool show);
    void set_show_context(bool show);

    // Type lookup
    Type* find_type(const char* type_name);
    Type* resolve_type_reference(const char* type_name);

    // Accessors for internal use
    Pool* get_pool() const { return pool; }
    Transpiler* get_transpiler() const { return transpiler; }
    PathSegment* get_current_path() const { return current_path; }
    void set_current_path(PathSegment* path) { current_path = path; }
    int get_current_depth() const { return current_depth; }
    void set_current_depth(int depth) { current_depth = depth; }
    HashMap* get_visited_nodes() const { return visited_nodes; }
    HashMap* get_type_definitions() const { return type_definitions; }
    ValidationOptions& get_options_ref() { return options; }
    clock_t get_validation_start_time() const { return validation_start_time; }
    void set_validation_start_time(clock_t time) { validation_start_time = time; }
};

// ==================== C-Style API (Compatibility Layer) ====================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a new schema validator
 */
SchemaValidator* schema_validator_create(Pool* pool);

/**
 * Destroy validator and cleanup resources
 */
void schema_validator_destroy(SchemaValidator* validator);

/**
 * Load schema from Lambda source code
 */
int schema_validator_load_schema(SchemaValidator* validator, const char* source, const char* root_type);

/**
 * Validate a typed item against a type name
 */
ValidationResult* schema_validator_validate(SchemaValidator* validator, ConstItem item, const char* type_name);

/**
 * Validate a typed item against a type name with format-specific handling
 */
ValidationResult* schema_validator_validate_with_format(
    SchemaValidator* validator,
    ConstItem item,
    const char* type_name,
    const char* input_format
);

/**
 * Validate a typed item against a specific Type*
 */
ValidationResult* schema_validator_validate_type(SchemaValidator* validator, ConstItem item, Type* type);

// ==================== Validation Options ====================

void schema_validator_set_options(SchemaValidator* validator, ValidationOptions* options);
ValidationOptions* schema_validator_get_options(SchemaValidator* validator);
ValidationOptions schema_validator_default_options();
void schema_validator_set_strict_mode(SchemaValidator* validator, bool strict);
void schema_validator_set_max_errors(SchemaValidator* validator, int max);
void schema_validator_set_timeout(SchemaValidator* validator, int timeout_ms);
void schema_validator_set_show_suggestions(SchemaValidator* validator, bool show);
void schema_validator_set_show_context(SchemaValidator* validator, bool show);

// ==================== Type Extraction ====================

Type* schema_validator_find_type(SchemaValidator* validator, const char* type_name);
Type* schema_validator_resolve_type_reference(SchemaValidator* validator, const char* type_name);

#ifdef __cplusplus
}
#endif

// NO LEGACY COMPATIBILITY - All code has been properly refactored
// If you see AstValidator references, they need to be updated to SchemaValidator
```

### Phase 4: Update Tests (Day 2-3)

#### 4.1 Update Integration Tests
**File**: `test/test_validator_integration.cpp`

**Complete refactoring - use C++ member function style:**

```cpp
// Before (Old C-style with AstValidator)
TEST_F(ValidatorIntegrationTest, BasicValidation) {
    const char* schema = "type Person = { name: string, age: int };";
    int result = ast_validator_load_schema(validator, schema, "Person");
    ASSERT_EQ(result, 0);

    MarkBuilder builder(input);
    Item person = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .final();

    ValidationResult* result = ast_validator_validate(validator, *(ConstItem*)&person, "Person");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
}

// After (New C++ style with SchemaValidator)
TEST_F(ValidatorIntegrationTest, BasicValidation) {
    const char* schema = "type Person = { name: string, age: int };";
    int result = validator->load_schema(schema, "Person");
    ASSERT_EQ(result, 0);

    MarkBuilder builder(input);
    Item person = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .final();

    ValidationResult* result = validator->validate(*(ConstItem*)&person, "Person");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
}

// Test fixture update
class ValidatorIntegrationTest : public ::testing::Test {
protected:
    Pool* pool;
    Pool* input;
    SchemaValidator* validator;  // Changed from AstValidator*

    void SetUp() override {
        pool = pool_create();
        input = pool_create();
        validator = SchemaValidator::create(pool);  // NEW: Use static factory method
    }

    void TearDown() override {
        if (validator) {
            validator->destroy();  // NEW: Call member function
        }
        if (input) pool_destroy(input);
        if (pool) pool_destroy(pool);
    }
};
```

**Update all test cases in the file:**
- Replace `ast_validator_load_schema(validator, ...)` → `validator->load_schema(...)`
- Replace `ast_validator_validate(validator, ...)` → `validator->validate(...)`
- Replace `ast_validator_validate_with_format(validator, ...)` → `validator->validate_with_format(...)`
- Replace `ast_validator_set_strict_mode(validator, true)` → `validator->set_strict_mode(true)`
- Replace `ast_validator_get_options(validator)` → `validator->get_options()`#### 4.2 Update Unit Tests
**File**: `test/test_validator_gtest.cpp`

Similar updates to integration tests.

#### 4.3 Update Enhanced Validator Tests
**File**: `test/lambda/validator/validator_enhanced.cpp`

Review and update any `AstValidator` references if present.

### Phase 5: Update External API Usage (Day 3)

#### 5.1 Update Main Validation Entry Point
**File**: `lambda/validator/ast_validate.cpp`

**Complete refactoring of `run_ast_validation` function:**
```cpp
ValidationResult* run_ast_validation(const char* data_file, const char* schema_file,
                                    const char* input_format, ValidationOptions* options) {
    Pool* pool = pool_create();
    if (!pool) {
        printf("Error: Failed to create memory pool\n");
        return nullptr;
    }

    ValidationResult* validation_result = nullptr;

    if (schema_file) {
        // Read schema file
        char* schema_contents = read_file_content(schema_file);
        if (!schema_contents) {
            printf("Error: Could not read schema file '%s'\n", schema_file);
            pool_destroy(pool);
            return nullptr;
        }

        // NEW: Create validator using static factory method
        SchemaValidator* validator = SchemaValidator::create(pool);
        if (!validator) {
            printf("Error: Failed to create schema validator\n");
            free(schema_contents);
            pool_destroy(pool);
            return nullptr;
        }

        // Determine root type
        const char* root_type = determine_root_type(schema_file, schema_contents, pool);
        if (!root_type) {
            root_type = "Document";
        }

        // NEW: Load schema using member function
        int schema_result = validator->load_schema(schema_contents, root_type);
        if (schema_result != 0) {
            printf("Error: Failed to load schema\n");
            validator->destroy();  // NEW: Use member function
            free(schema_contents);
            pool_destroy(pool);
            return nullptr;
        }

        // Parse data file
        Item data_item = parse_data_file(data_file, input_format, pool);
        if (data_item.item == ITEM_NULL) {
            printf("Error: Failed to parse data file\n");
            validator->destroy();
            free(schema_contents);
            pool_destroy(pool);
            return nullptr;
        }

        // NEW: Validate using member function
        printf("Validating data against schema...\n");
        ConstItem const_doc = *(ConstItem*)&data_item;
        validation_result = validator->validate(const_doc, root_type);

        // NEW: Cleanup using member function
        validator->destroy();
        free(schema_contents);
    }

    // ... rest of function ...
    return validation_result;
}
```

#### 5.2 Comprehensive Codebase Refactoring
**Search and replace across entire codebase:**

**Step 1: Find all files using old API**
```bash
# Search for AstValidator usage
grep -r "AstValidator\*" lambda/ test/ --include="*.cpp" --include="*.hpp" --include="*.h"

# Search for ast_validator_ function calls
grep -r "ast_validator_" lambda/ test/ --include="*.cpp" --include="*.hpp" --include="*.h"
```

**Step 2: Refactor each file systematically**

Files that need refactoring (estimated):
- `lambda/main.cpp` - if validator used in REPL
- `lambda/validate.cpp` - standalone validation command
- `test/test_validator_gtest.cpp` - comprehensive test suite
- `test/test_validator_integration.cpp` - integration tests
- Any example files in `examples/`

**Step 3: Refactoring patterns**

For each file, apply these transformations:

**Type declarations:**
```cpp
// OLD
AstValidator* validator;

// NEW
SchemaValidator* validator;
```

**Creation:**
```cpp
// OLD
validator = ast_validator_create(pool);

// NEW
validator = SchemaValidator::create(pool);
```

**Method calls:**
```cpp
// OLD
ast_validator_load_schema(validator, source, type_name);
ast_validator_validate(validator, item, type_name);
ast_validator_set_strict_mode(validator, true);
ast_validator_destroy(validator);

// NEW
validator->load_schema(source, type_name);
validator->validate(item, type_name);
validator->set_strict_mode(true);
validator->destroy();
```

**Step 4: Verify no old API remains**
```bash
# After refactoring, these should return NO results:
grep -r "ast_validator_" lambda/ test/ --include="*.cpp" --include="*.hpp" | grep -v "// OLD:"
grep -r "AstValidator\*" lambda/ test/ --include="*.cpp" --include="*.hpp" | grep -v "// OLD:"
```

### Phase 6: Documentation Updates (Day 3)

#### 6.1 Update Lambda Validator Guide
**File**: `doc/Lambda_Validator_Guide.md`

- Update all code examples to use `SchemaValidator`
- Add migration notice for old code
- Document the unified API

#### 6.2 Update API Reference
**File**: `doc/Lambda_Reference.md`

Update validator section with new unified API.

#### 6.3 Update README
**File**: `README.md`

Update any validator examples.

#### 6.4 Add Migration Guide
**File**: `doc/Validator_Migration_Guide.md` (new)

Detailed guide for users migrating from old `AstValidator` API.

### Phase 7: Build and Test (Day 3-4)

#### 7.1 Build System Verification
```bash
# Clean build
make clean
make build

# Verify no compilation errors
# Check for any remaining AstValidator references in error messages
```

#### 7.2 Run Full Test Suite
```bash
# Run all tests
make test

# Expected: All tests should pass with no changes
# (due to legacy compatibility aliases)
```

#### 7.3 Manual Testing
Test scenarios:
1. Load schema and validate document
2. Validation with format detection
3. Error reporting and suggestions
4. Circular reference detection
5. Timeout and depth limits

### Phase 8: Remove Old AstValidator References (Day 4)

**This phase removes all traces of the old API**

#### 8.1 Delete Old Forward Declarations
**File**: `lambda/validator/validator.hpp`

```cpp
// DELETE this line (around line 16):
typedef struct AstValidator AstValidator;

// Keep only:
// Forward declarations
typedef struct ValidationResult ValidationResult;
typedef struct ValidationError ValidationError;
typedef struct PathSegment PathSegment;
// SchemaValidator is now a class, not a forward declaration
```

#### 8.2 Update Comments and Documentation Strings
Search for references to "AstValidator" in comments:
```bash
grep -r "AstValidator" lambda/ test/ doc/ --include="*.cpp" --include="*.hpp" --include="*.md"
```

Update all documentation comments:
```cpp
// OLD comment:
/**
 * Validate against type using AstValidator
 */

// NEW comment:
/**
 * Validate against type using SchemaValidator
 */
```

#### 8.3 Remove validate.cpp Old API
**File**: `lambda/validator/validate.cpp`

If there are any old function declarations with AstValidator, remove them:
```cpp
// DELETE any remaining:
// ValidationResult* some_function(AstValidator* validator, ...);

// Should be:
// ValidationResult* some_function(SchemaValidator* validator, ...);
```

#### 8.4 Clean Build Verification
```bash
# Clean everything
make clean
rm -rf build/

# Grep for old API (should find NOTHING):
grep -r "typedef.*AstValidator" lambda/ test/
grep -r "#define ast_validator" lambda/ test/

# If anything is found, it's a bug - fix it

# Rebuild
make build

# Should compile cleanly with NO warnings about AstValidator
```

## Testing Strategy

### Unit Tests
- ✅ Test structure size (should be smaller after removing unused fields)
- ✅ Test all public API functions work correctly
- ✅ Test backward compatibility aliases work
- ✅ Test memory pool allocation works correctly

### Integration Tests
- ✅ Full validation workflow with new API
- ✅ Full validation workflow with old API (aliases)
- ✅ Complex schema loading and validation
- ✅ Error reporting remains consistent

### Regression Tests
- ✅ Run existing test suite - all tests must pass
- ✅ Compare validation results before/after merge
- ✅ Verify no memory leaks introduced

## Success Criteria

1. ✅ All code compiles without errors or warnings
2. ✅ All existing tests pass (after refactoring to new API)
3. ✅ Structure size reduced (5 unused fields removed, ~40 bytes saved)
4. ✅ No change in validation behavior or results
5. ✅ NO `AstValidator` references remain in codebase (except in migration docs)
6. ✅ NO `ast_validator_*` function calls remain (except in migration docs)
7. ✅ All code uses new C++ member function style
8. ✅ Documentation updated with new API
9. ✅ Migration guide provided for users
10. ✅ Clean grep results: `grep -r "ast_validator_\|AstValidator" lambda/ test/` returns nothing

## Benefits After Completion

### Immediate Benefits
- **Reduced memory footprint**: ~40 bytes saved per validator instance (5 unused fields removed)
- **Simpler API**: One structure instead of two
- **Better naming**: "SchemaValidator" is more intuitive than "AstValidator"
- **Object-oriented design**: Member functions provide cleaner, more intuitive API
- **Encapsulation**: Private fields prevent accidental misuse
- **Type safety**: C++ class provides better compile-time checking
- **Reduced maintenance**: Single implementation to maintain

### Long-term Benefits
- **Clearer architecture**: No confusion about which validator to use
- **Better extensibility**: Future enhancements go in one place
- **Easier onboarding**: New developers see clear, singular API
- **Modern C++ idioms**: Easier integration with C++ codebases
- **Flexible API**: Can use either C++ methods or C wrapper functions

## Risk Assessment

### Medium Risk
- Structure merge requires comprehensive refactoring
- No backward compatibility - all code must be updated
- No algorithm changes (same validation logic)

### Mitigation Strategies
- Systematic file-by-file refactoring
- Thorough testing after each file
- Clear migration documentation
- Compilation will fail for any missed AstValidator references (good - forces complete migration)
- Use grep/search to find ALL occurrences before starting

## Timeline

- **Day 1**: Phase 1-2 (Preparation, tracking, core SchemaValidator class implementation)
- **Day 2**: Phase 3-4 (Header updates, validate.cpp refactoring, test refactoring)
- **Day 3**: Phase 5-6 (External API refactoring across all files, documentation)
- **Day 4**: Phase 7-8 (Comprehensive testing, cleanup of old references)

**Total estimated time**: 4 days (proper refactoring takes more time than aliasing)

**Note**: This is PROPER refactoring, not just adding aliases. Every file that uses the validator
must be touched and updated. The compiler will help us find any missed occurrences.

## File Change Summary

### Files to Modify
1. `lambda/validator/validator.hpp` - Structure definition and API
2. `lambda/validator/doc_validator.cpp` - Core implementation
3. `lambda/validator/validate.cpp` - Validation functions
4. `lambda/validator/ast_validate.cpp` - Entry point and wrapper removal
5. `test/test_validator_integration.cpp` - Test updates
6. `test/test_validator_gtest.cpp` - Test updates
7. `doc/Lambda_Validator_Guide.md` - Documentation

### Files to Create
1. `doc/Validator_Migration_Guide.md` - Migration documentation

### Files to Review (may need updates)
1. `lambda/validator/error_reporting.cpp`
2. `lambda/validator/suggestions.cpp`
3. `lambda/main.cpp` (if uses validator)
4. `README.md`
5. `doc/Lambda_Reference.md`

## Conclusion

This consolidation eliminates redundancy in the validator implementation while maintaining full backward compatibility. The resulting API is simpler, more intuitive, and easier to maintain.

**Key improvements:**
1. **Single unified class** - No more confusion between AstValidator and SchemaValidator
2. **Better naming** - "SchemaValidator" accurately describes the component's purpose
3. **Member functions** - Clean, object-oriented API (e.g., `validator->validate()`)
4. **Reduced footprint** - Removal of 5 unused fields saves ~40 bytes per instance
5. **Encapsulation** - Private fields with accessor methods prevent misuse
6. **Dual API** - C++ member functions + C wrapper functions for compatibility
7. **Full backward compatibility** - Legacy `ast_validator_*` aliases maintained

**API comparison:**

```cpp
// OLD API (C-style with AstValidator) - REMOVED
AstValidator* validator = ast_validator_create(pool);
ast_validator_load_schema(validator, schema, "Document");
ValidationResult* result = ast_validator_validate(validator, item, "Document");
ast_validator_destroy(validator);

// NEW API (C++ style - RECOMMENDED)
SchemaValidator* validator = SchemaValidator::create(pool);
validator->load_schema(schema, "Document");
ValidationResult* result = validator->validate(item, "Document");
validator->destroy();

// NEW API (C-style wrapper - for pure C code compatibility ONLY)
SchemaValidator* validator = schema_validator_create(pool);
schema_validator_load_schema(validator, schema, "Document");
ValidationResult* result = schema_validator_validate(validator, item, "Document");
schema_validator_destroy(validator);
```

**Important**: This plan does NOT use aliasing or macros. All code is properly refactored to use the new
API. The phased approach ensures safety through incremental changes with validation at each step.
The compiler will catch any missed AstValidator references, forcing complete migration.
