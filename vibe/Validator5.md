# Lambda Validator Enhancement Plan

**Date:** November 14, 2025
**Status:** In Progress - AST Parser Integration Complete ✅
**Goal:** Enhance current AST-based validator with proven features from old schema-based validator

---

## Recent Progress Update (November 14, 2025)

### ✅ AST Parser Integration Complete

**What Was Completed:**
1. **Full Transpiler Integration** - Replaced all stub implementations with real Tree-sitter based Lambda parser
   - `transpiler_create()`, `transpiler_build_ast()`, `transpiler_destroy()` fully integrated
   - Lambda files (.ls) now undergo comprehensive AST parsing with syntax error detection

2. **Schema Validation Infrastructure** - Implemented basic schema validation with AST parsing
   - `schema_validator_load_schema()` parses schema source using transpiler
   - `validate_document()` validates JSON/XML document structures
   - Foundation laid for deep type checking

3. **Test Suite Success** - Achieved 99.6% test pass rate
   - 1881/1888 tests passing (7 failures unrelated to validator)
   - 38/38 AST validator tests passing
   - 44/44 integration tests (Sprints 1-5) passing
   - 10/10 comprehensive integration tests passing

**Validation Capabilities Now Working:**
- ✅ Lambda syntax error detection (missing braces, invalid tokens)
- ✅ Parse error reporting with line/column numbers
- ✅ Non-Lambda syntax detection (plain text files)
- ✅ JSON/XML document structure validation
- ✅ CLI options (--strict, --max-errors, --max-depth, --allow-unknown)
- ✅ Format auto-detection
- ✅ Comprehensive error reporting framework

**See:** `test/AST_PARSER_INTEGRATION_COMPLETE.md` for full details

---

## Executive Summary

This plan integrates findings from the validator comparison analysis to enhance Lambda's current AST-based validator (using runtime `Type*` system) with the best features from the old schema-based validator (using `TypeSchema` system), while maintaining the architectural benefits of the unified type system.

**Key Principles:**
1. Keep the current unified architecture (no duplicate type systems)
2. Schema information lives in Lambda schema syntax, parsed into Type AST
3. Validator works with Type* structures, no separate metadata
4. Add validation capabilities that made the old validator more powerful

---

## Phase 1: Foundation - Lambda Schema Enhancement

### 1.1 Enhance Lambda Schema Language

**Status:** 📋 Ready to start (foundation complete with AST integration)

**Goal:** Express validation constraints directly in Lambda schema syntax, which will be parsed and built into Type AST structures.

**Current Lambda Schema:**
```lambda
type Document = {
    title: string,
    author: string,
    body: Element
}
```

**Enhanced Lambda Schema (Future):**
```lambda
// Required fields (default behavior - all fields required)
type Document = {
    title: string,        // required by default
    author: string,       // required by default
    body: Element
}

// Optional fields (use ? suffix)
type DocumentOptional = {
    title: string,
    author: string?,      // optional
    keywords: [string*]?  // optional array
}

// Open types (allow additional fields - future syntax)
// type OpenDocument = { ...rest } | { title: string, ...rest }
```

**Strategy:**
- **Phase 1**: Work with existing Lambda schema capabilities
- **Future Enhancement**: Add syntax for required/optional distinction, open types, etc.
- **Type AST**: All schema information encoded in Type* structures from parsing

**Implementation Steps:**
1. Document current Lambda schema capabilities
2. Identify validation patterns expressible in current syntax
3. Use occurrence operators (?, +, *) for optional/required semantics
4. Design future schema syntax extensions (separate RFC document)
5. Keep validator implementation schema-agnostic

**Files to Modify:**
- `lambda/input/doc_schema.ls` - Document current patterns
- New: `vibe/LambdaSchemaExtensions.md` - Future schema syntax proposals
- `lambda/validate.cpp` - Work with existing Type* structures

**Estimated Effort:** 1-2 days (documentation + analysis)

---

### 1.2 Type AST Structure Analysis

**Status:** ✅ Complete (verified during integration)

**Current Type Structures:**
```cpp
typedef struct Type {
    TypeId type_id;
    int32_t ref_cnt;
} Type;

typedef struct TypeMap : Type {
    Type* key;
    Type* value;
    ShapeEntry* shape;  // Field definitions
} TypeMap;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;
    ShapeEntry* next;
} ShapeEntry;

typedef struct TypeUnary : Type {
    Operator op;        // OPERATOR_OPTIONAL (?), OPERATOR_ONE_MORE (+), etc.
    Type* operand;
} TypeUnary;
```

**Current Capabilities:**
- ✅ Optional fields: `field: Type?` → TypeUnary with OPERATOR_OPTIONAL
- ✅ Required arrays: `field: [Type+]` → TypeArray with occurrence
- ✅ Type unions: `Type1 | Type2` → Binary type expressions
- ✅ Nested structures: TypeMap with ShapeEntry chain

**What's Already Expressible:**
```lambda
// Optional field (Type?)
type Person = { name: string, email: string? }

// Required vs optional arrays
type Doc1 = { tags: [string*] }   // optional array (0 or more)
type Doc2 = { tags: [string+] }   // required array (1 or more)

// Union types
type Value = string | int | null

// Nested structures
type Document = {
    meta: { title: string, author: string? },
    body: Element
}
```

**No Changes Needed:** Existing Type* structures already capture schema constraints through:
- TypeUnary occurrence operators (?, +, *)
- TypeArray element types and lengths
- TypeMap shape entries
- Type unions for alternatives

**Estimated Effort:** 0 days (no implementation needed)---

## Phase 2: Enhanced Error Reporting

### 2.1 Rich Error Context with Expected vs Actual

**Status:** 🔄 Ready to implement (framework complete, Phase 2.2 provides foundation)

**Note:** Phase 2.2 has been completed first, providing enhanced error reporting with full paths and human-readable type names. This phase will add suggestion generation on top of that foundation.

**Current State:**
```cpp
typedef struct ValidationError {
    ValidationErrorCode code;
    String* message;
    PathSegment* path;
    Type* expected;            // ✅ Already present (not used)
    Item actual;               // ✅ Already present
    List* suggestions;         // ❌ NULL (not implemented)
    struct ValidationError* next;
} ValidationError;
```

**Enhancement:**
```cpp
// Update error creation to populate expected/actual
ValidationError* create_validation_error_full(
    ValidationErrorCode code,
    const char* message,
    PathSegment* path,
    Type* expected_type,        // Populate this
    ConstItem actual_item,      // Populate this
    Pool* pool
);

// Add suggestion generation
List* generate_field_suggestions(
    const char* typo_field,
    TypeMap* map_type,
    Pool* pool
);

List* generate_type_suggestions(
    TypeId actual_type,
    Type* expected_type,
    Pool* pool
);
```

**Implementation Steps:**
1. Update all `create_validation_error()` calls to pass expected type
2. Implement Levenshtein distance for typo detection
3. Add suggestion generation for field names
4. Add suggestion generation for type mismatches
5. Update error formatting to show expected vs actual

**Example Output:**
```
❌ Validation FAILED
Error 1: Type mismatch at .meta.published
  Expected: string
  Actual: int (2025)
  Suggestion: Did you mean to use a date string? Try: "2025-11-14"

Error 2: Unknown field at .meta.autor
  Expected fields: title, author, date, keywords
  Actual field: autor
  Suggestion: Did you mean 'author'?
```

**Files to Modify:**
- `lambda/validate.cpp` - Update error creation calls
- `lambda/validator/error_reporting.cpp` - Add suggestion logic
- New file: `lambda/validator/suggestions.cpp` - String similarity algorithms

**Estimated Effort:** 3-4 days

---

### 2.2 Field Type Validation with Enhanced Error Reporting

**Status:** ✅ Complete (January 2025)

**What Was Implemented:**
1. **Full Type Validation** - Complete type checking for all Lambda types
   - Primitive types: `string`, `int`, `float`, `bool`
   - Complex types: Nested structures, arrays, element types
   - Type references: Named types (Address, Contact, Employee)
   - Recursive validation: 4+ level deep nesting

2. **Enhanced Error Reporting** - Full path reporting from root to error location
   - Flat paths: `.field`
   - Nested paths: `.employee.contact.address.city`
   - Array indices: `.employees[1].contacts[0].phone`
   - Human-readable type names: `'string'` vs `'int'` (not type IDs)

3. **Improved Error Messages**
   - Old: "Type mismatch: expected string, got type 3"
   - New: "Expected type 'string', but got 'int' at .name"
   - Clear missing field messages: "Required field 'age' is missing from object at .age"

**Current State:**
```cpp
String* format_validation_path(PathSegment* path, Pool* pool) {
    // ✅ COMPLETE - Full path formatting with all segment types
    if (!path) return string_from_strview(strview_from_cstr("(root)"), pool);
    // Builds proper paths like: .field[0]<element>@attribute
    // Examples: .employee.contact.address.city
    //          .employees[1].contacts[0].phone
}
```

**Test Coverage:**
- ✅ 18/18 automated tests passing (`test/test_validator_path_reporting.cpp`)
- ✅ 14 demonstration scenarios in shell script (`test/test_validator_errors.sh`)
- ✅ Test data created for nested objects and arrays
- ✅ Comprehensive validation of flat structures, nested objects, and arrays

**Example Output:**
```
Nested Object Validation:
  [TYPE_MISMATCH] Expected type 'string', but got 'int' at .employee.contact.phone
  [TYPE_MISMATCH] Expected type 'string', but got 'int' at .employee.contact.address.city

Array Validation with Indices:
  [TYPE_MISMATCH] Expected type 'string', but got 'int' at .employees[1].contacts[0].phone
  [TYPE_MISMATCH] Expected type 'string', but got 'int' at .employees[2].lastName
```

**Files Modified:**
- `lambda/validator/ast_validate.cpp` - Schema loading and validation entry point
- `lambda/validate.cpp` - Core validation logic with recursive type checking
- `lambda/validator/error_reporting.cpp` - Path construction and formatting
- `lambda/validator.hpp` - SchemaValidator struct with AstValidator integration

**See:** `doc/Validator_Phase2.2_Summary.md` for complete details

**Enhancement:**
```cpp
String* format_validation_path(PathSegment* path, Pool* pool) {
    if (!path) return string_from_strview(strview_from_cstr("(root)"), pool);

    StringBuf* sb = stringbuf_new(pool);

    // Reverse order (path is stored backwards)
    PathSegment* segments[100];
    int count = 0;
    PathSegment* current = path;
    while (current && count < 100) {
        segments[count++] = current;
        current = current->next;
    }

    // Build path string
    for (int i = count - 1; i >= 0; i--) {
        PathSegment* seg = segments[i];
        switch (seg->type) {
            case PATH_FIELD:
                stringbuf_append_format(sb, ".%.*s",
                    (int)seg->data.field_name.length, seg->data.field_name.str);
                break;
            case PATH_INDEX:
                stringbuf_append_format(sb, "[%ld]", seg->data.index);
                break;
            case PATH_ELEMENT:
                stringbuf_append_format(sb, "<%.*s>",
                    (int)seg->data.element_tag.length, seg->data.element_tag.str);
                break;
            case PATH_ATTRIBUTE:
                stringbuf_append_format(sb, "@%.*s",
                    (int)seg->data.attr_name.length, seg->data.attr_name.str);
                break;
            case PATH_UNION:
                stringbuf_append_format(sb, "|%ld", seg->data.index);
                break;
        }
    }

    return stringbuf_to_string(sb);
}
```

**Files to Modify:**
- `lambda/validator/error_reporting.cpp` - Implement proper path formatting

**Estimated Effort:** 1 day

---

## Phase 3: Validation Options and Configuration

### 3.1 Implement Validation Options

**Status:** ✅ Complete (all CLI options working)

**Current State:**
```cpp
typedef struct ValidationOptions {
    bool strict_mode;              // ✅ Working
    bool allow_unknown_fields;     // ✅ Working
    bool allow_empty_elements;     // ✅ Working
    int max_depth;                 // ✅ Working (default: 100)
    int max_errors;                // ✅ Working (default: 100)
    int timeout_ms;                // Future enhancement
    // Custom rules - future enhancement
} ValidationOptions;

// ✅ Options fully integrated with CLI and validation
```

**Enhancement:**
```cpp
typedef struct ValidationOptions {
    // Strictness levels
    bool strict_mode;              // Treat warnings as errors
    bool allow_unknown_fields;     // Allow extra fields in maps
    bool allow_empty_elements;     // Allow elements without content

    // Limits
    int max_depth;                 // Maximum validation depth
    int timeout_ms;                // Validation timeout (0 = no limit)

    // Custom rules
    List* enabled_rules;           // List of String* rule names
    List* disabled_rules;          // List of String* rule names

    // Error reporting
    bool show_suggestions;         // Include suggestions in errors
    bool show_context;             // Show code context for errors
    int max_errors;                // Stop after N errors (0 = unlimited)
} ValidationOptions;
```

**Implementation Steps:**
1. Add missing fields to `ValidationOptions`
2. Pass options through validation context
3. Implement timeout checking (check elapsed time periodically)
4. Add max_errors limit (stop validation early)
5. Add custom rule filtering

**Files to Modify:**
- `lambda/validator.hpp` - Update ValidationOptions
- `lambda/validate.cpp` - Check options during validation
- `lambda/validator.cpp` - Add option setter/getter methods

**Estimated Effort:** 2 days

---

### 3.2 Add Public API for Options

**Enhancement:**
```cpp
// Set options
void ast_validator_set_options(AstValidator* validator, ValidationOptions* options);
ValidationOptions* ast_validator_get_options(AstValidator* validator);

// Convenience functions
void ast_validator_set_strict_mode(AstValidator* validator, bool strict);
void ast_validator_set_max_errors(AstValidator* validator, int max);
void ast_validator_enable_rule(AstValidator* validator, const char* rule_name);
void ast_validator_disable_rule(AstValidator* validator, const char* rule_name);
```

**Files to Modify:**
- `lambda/validator.hpp` - Add function declarations
- `lambda/validator.cpp` - Implement option functions

**Estimated Effort:** 1 day

---

## Phase 4: Missing Field vs Null Value Distinction

### 4.1 Distinguish Field Existence from Null Values

**Current Problem:**
```cpp
// Current code can't tell difference between:
// 1. Field missing: { title: "Doc" }  (no author field)
// 2. Field null:    { title: "Doc", author: null }
```

**Enhancement:**
```cpp
// Add helper function
bool map_has_field(const Map* map, const char* field_name);

// Update validation logic
if (!map_has_field(map, field_name)) {
    // Field is truly missing
    if (field->required) {
        add_error("Missing required field");
    }
} else {
    ConstItem value = map_get_const(map, field_key);

    // Field exists - validate its value (even if null)
    if (value.type_id() == LMD_TYPE_NULL) {
        // Check if null is allowed by schema
        if (field->type->type_id == LMD_TYPE_NULL || is_optional(field->type)) {
            // OK: null is allowed
        } else {
            add_error("Field cannot be null");
        }
    } else {
        // Validate non-null value
        validate_against_type(validator, value, field->type);
    }
}
```

**Implementation Steps:**
1. Add `map_has_field()` helper to check field existence
2. Update map validation to distinguish missing vs null
3. Handle optional types (Type?) correctly
4. Add tests for null vs missing scenarios

**Files to Modify:**
- `lambda/lambda-data.hpp` - Add map_has_field() declaration
- `lambda/lambda-data.cpp` - Implement map_has_field()
- `lambda/validate.cpp` - Update map field validation logic

**Estimated Effort:** 2 days

---

## Phase 5: Format-Specific Validation

### 5.1 Add Input Format Validation Support

**Current State:**
```cpp
// ast_validator_validate() doesn't consider input format
ValidationResult* ast_validator_validate(AstValidator* validator, ConstItem item, const char* type_name);
```

**Enhancement:**
```cpp
// Add format-aware validation
ValidationResult* ast_validator_validate_with_format(
    AstValidator* validator,
    ConstItem item,
    const char* type_name,
    const char* input_format  // "html", "xml", "json", etc.
);

// Format-specific unwrapping
ConstItem unwrap_xml_document(ConstItem item);  // Remove <document> wrapper
ConstItem unwrap_html_document(ConstItem item); // Handle HTML quirks
```

**Implementation Steps:**
1. Add format parameter to validation functions
2. Implement format-specific unwrapping logic
3. Add format detection from file extensions
4. Port XML document wrapper handling from old validator

**Files to Modify:**
- `lambda/validator.hpp` - Add format-aware functions
- `lambda/validator.cpp` - Implement format unwrapping
- `lambda/validate.cpp` - Call unwrapping when needed

**Estimated Effort:** 2-3 days

---

### 5.2 Port Virtual Document Handling

**From Old Validator:**
```cpp
// Check for virtual XML document wrapper and unwrap if needed
if (ctx->current_depth == 1 && element->type) {
    TypeElmt* elmt_type = (TypeElmt*)element->type;
    if (elmt_type->name.length == 8 &&
        memcmp(elmt_type->name.str, "document", 8) == 0) {
        // Find actual XML element inside wrapper
        Element* actual_element = find_actual_content_element(element);
        if (actual_element) {
            // Validate actual element instead
            return validate_element(validator, actual_element, schema, ctx);
        }
    }
}
```

**Implementation Steps:**
1. Add depth tracking to validator context
2. Check for wrapper elements at depth 1
3. Skip processing instructions and comments
4. Extract actual content element
5. Continue validation with unwrapped element

**Files to Modify:**
- `lambda/validate.cpp` - Add unwrapping to validate_against_element_type()

**Estimated Effort:** 1-2 days

---

## Phase 6: Number Type Promotion Enhancement

### 6.1 Improve Number Type Validation

**Current State:**
```cpp
// In validate_against_base_type():
if (LMD_TYPE_INT <= base_type->type_id && base_type->type_id <= LMD_TYPE_NUMBER) {
    // Allow int/float/decimal interchangeably
    if (LMD_TYPE_INT <= item.type_id() && item.type_id() <= base_type->type_id) {
        result->valid = true;
    }
}
```

**Enhancement:**
```cpp
// Add precision loss checking
if (expected_type == LMD_TYPE_INT && actual_type == LMD_TYPE_FLOAT) {
    double float_value = *(double*)item.pointer;
    if (float_value != floor(float_value)) {
        // Float has fractional part, can't convert to int
        add_error("Cannot convert float to int: has fractional part");
    }
}

// Add range checking
if (expected_type == LMD_TYPE_INT && actual_type == LMD_TYPE_INT) {
    long int_value = *(long*)item.pointer;
    if (int_value < INT_MIN || int_value > INT_MAX) {
        add_warning("Integer value outside safe range");
    }
}
```

**Files to Modify:**
- `lambda/validate.cpp` - Add precision/range checks to primitive validation

**Estimated Effort:** 1 day

---

## Phase 7: Integration and Testing

### 7.1 Update Command-Line Interface

**Status:** ✅ Complete (all options implemented and tested)

**Enhancement:**
```bash
# ✅ ALL WORKING - Validation options in CLI
lambda validate --strict document.ls
lambda validate --max-errors 10 document.ls
lambda validate --max-depth 50 document.ls
lambda validate --allow-unknown document.ls
lambda validate -f json -s schema.ls data.json
lambda validate --format html --schema html5_schema.ls input.html
```

**Implementation Steps:**
1. Add CLI flags for validation options
2. Parse flags in exec_validation()
3. Create ValidationOptions from flags
4. Pass options to validator

**Files to Modify:**
- `lambda/validator/ast_validate.cpp` - Add CLI flag parsing

**Estimated Effort:** 1 day

---

### 7.2 Comprehensive Test Suite

**New Tests:**
```cpp
// Test schema metadata
test_required_field_validation()
test_optional_field_validation()
test_open_map_validation()

// Test error reporting
test_typo_suggestions()
test_type_mismatch_suggestions()
test_path_formatting()

// Test field existence
test_missing_field_vs_null_value()
test_null_allowed_by_schema()

// Test number promotion
test_float_to_int_conversion()
test_precision_loss_detection()

// Test format handling
test_xml_wrapper_unwrapping()
test_html_format_validation()

// Test validation options
test_strict_mode()
test_max_errors_limit()
test_custom_rule_filtering()
```

**Files to Create:**
- `test/lambda/validator/test_schema_metadata.c`
- `test/lambda/validator/test_error_reporting.c`
- `test/lambda/validator/test_field_existence.c`
- `test/lambda/validator/test_format_handling.c`

**Estimated Effort:** 5-7 days

---

## Implementation Timeline

### ✅ Sprint 0: AST Parser Integration (COMPLETED - November 14, 2025)
- [x] Integrate real transpiler (removed all stubs)
- [x] Implement schema validation infrastructure
- [x] Complete CLI options (--strict, --max-errors, --max-depth, --allow-unknown)
- [x] Path formatting implementation
- [x] Comprehensive test suite (1881/1888 tests passing)

**Deliverable:** ✅ Full AST parser integration with 99.6% test pass rate

### Sprint 1: Foundation (1 week)
- [ ] Phase 1.1: Lambda schema analysis and documentation (1-2 days)
- [x] Phase 1.2: Type AST structure review (COMPLETE - verified during integration)
- [ ] Phase 4.1: Field existence checking (2 days)

**Deliverable:** Understanding of current schema capabilities, field validation distinguishes null from missing

### Sprint 2: Error Reporting (1-2 weeks)
- [x] Phase 2.2: Field Type Validation and Path Formatting (COMPLETE - January 2025)
  - ✅ Complete type validation for all Lambda types
  - ✅ Full path reporting from root (.employee.contact.address.city)
  - ✅ Array index notation (.employees[1].contacts[0].phone)
  - ✅ Human-readable type names in error messages
  - ✅ 18/18 automated tests passing
  - ✅ Comprehensive test coverage
- [ ] Phase 2.1: Expected vs actual with suggestions (3-4 days) - Framework ready
- [ ] Phase 6.1: Number promotion (1 day)

**Deliverable:** ✅ Enhanced error reporting complete - Rich error messages with full paths and type information (Phase 2.2 DONE)

### Sprint 3: Configuration (1 week)
- [x] Phase 3.1: Validation options (COMPLETE - all CLI options working)
- [ ] Phase 3.2: Public API (1 day) - Basic API exists, enhance convenience functions
- [x] Phase 7.1: CLI integration (COMPLETE)

**Deliverable:** ✅ Configurable validation with CLI flags (DONE)

### Sprint 4: Format Support (1 week)
- [ ] Phase 5.1: Format-aware validation (2-3 days)
- [ ] Phase 5.2: Virtual document handling (1-2 days)

**Deliverable:** Format-specific validation for HTML/XML/etc.

### Sprint 5: Testing & Polish (1-2 weeks)
- [ ] Phase 7.2: Comprehensive tests (5-7 days)
- [ ] Documentation updates
- [ ] Performance optimization

**Deliverable:** Production-ready validator with full test coverage

**Total Estimated Time:** 4-6 weeks (reduced by removing SchemaMetadata implementation)

---

## Success Metrics

### Functionality Checklist
- [x] Unified type system (already done)
- [x] **AST parser integration** ✅ **COMPLETE**
- [x] **Transpiler integration** ✅ **COMPLETE**
- [x] **CLI options** ✅ **COMPLETE**
- [x] **Path formatting** ✅ **COMPLETE**
- [x] **Error reporting framework** ✅ **COMPLETE**
- [x] **Field Type Validation (Phase 2.2)** ✅ **COMPLETE - January 2025**
  - ✅ Primitive type validation (string, int, float, bool)
  - ✅ Complex type validation (nested structures, arrays, elements)
  - ✅ Type reference resolution (named types)
  - ✅ Recursive validation (4+ levels deep)
  - ✅ Full path reporting from root
  - ✅ Array index notation in paths
  - ✅ Human-readable type names
- [ ] Lambda schema analysis and documentation
- [ ] Optional field validation (Type? handling) - Infrastructure ready
- [ ] Missing vs null distinction
- [ ] Rich error messages with suggestions - Framework ready (Phase 2.2 provides foundation)
- [ ] Format-specific handling - Basic structure validation working
- [ ] Number type promotion with checks

### Quality Metrics
- [x] **99.6% test pass rate (1881/1888)** ✅ **ACHIEVED**
- [x] **All validator integration tests pass (44/44)** ✅ **ACHIEVED**
- [x] **Phase 2.2 test coverage (18/18 tests)** ✅ **ACHIEVED - January 2025**
- [ ] All old validator test cases pass (in progress)
- [ ] Performance: validate 1MB document in <100ms
- [ ] Zero memory leaks (valgrind clean)
- [x] **Integration documentation complete** ✅ **ACHIEVED**
- [x] **Phase 2.2 documentation complete** ✅ **ACHIEVED**
- [ ] Full API documentation

### User Experience Metrics
- [ ] Error messages are clear and actionable
- [ ] Suggestions help users fix 80%+ of common errors
- [ ] CLI is intuitive and well-documented

---

## Migration Guide for Old Validator Users

### What Changed
1. **Type System**: No more separate `TypeSchema` - use runtime `Type*` directly
2. **API**: Use `AstValidator` instead of `SchemaValidator`
3. **Schema Files**: Same Lambda syntax, but loaded via transpiler

### What Stayed the Same
1. **Schema Language**: Lambda type definitions work the same
2. **Error Reporting**: Enhanced but compatible format
3. **Validation Logic**: Same rules, better implementation

### Migration Steps
1. Replace `SchemaValidator*` with `AstValidator*`
2. Replace `schema_validator_create()` with `ast_validator_create()`
3. Replace `schema_validator_load_schema()` with `ast_validator_load_schema()`
4. Replace `validate_document()` with `ast_validator_validate()`
5. Update error handling to use new rich error format

---

## Open Questions

1. **Lambda Schema Extensions**: What syntax for required/optional/open types?
   - **Decision**: Document current capabilities first, propose extensions separately
   - **Future**: Consider syntax like `field!: Type` (required) vs `field?: Type` (optional)
   - **Future**: Consider `{ ...rest }` syntax for open types

2. **Backward Compatibility**: Support old validation API?
   - **Decision**: Provide compatibility wrapper in separate file

3. **Custom Validators**: Port doc_validators.c or drop it?
   - **Decision**: Put aside for now (per user request)

4. **Performance**: Impact of enhanced validation?
   - **Mitigation**: Keep validation logic efficient, avoid redundant traversals

5. **Schema Constraint Language**: Add `where` clauses or constraint expressions?
   - **Decision**: Defer to future (Phase 8+)
   - **Example**: `type Age = int where value >= 0 && value <= 150`

---

## References

- `VALIDATOR_COMPARISON.md` - Detailed comparison of old vs new
- `DOC_VALIDATORS_SCHEMA_ANALYSIS.md` - Analysis of semantic validation
- `lambda/validator.hpp` - Current validator API
- `doc/validator.hpp` - Old validator API

---

## Next Steps

1. **Review this plan** with team
2. **Prioritize sprints** based on user needs
3. **Start Sprint 1** - Foundation work
4. **Set up tracking** - Create issues for each phase
5. **Regular check-ins** - Weekly progress reviews

---

## Current Status Summary

**Overall Progress:** 🟢 Foundation Complete - Ready for Enhancement Sprints

### What's Working Now (January 2025)
✅ **Lambda Syntax Validation**
- Full Tree-sitter AST parsing
- Comprehensive error detection (syntax errors, parse errors)
- Line/column error reporting

✅ **Schema Infrastructure**
- Schema loading via transpiler
- Basic document structure validation (JSON/XML)
- Foundation for deep type checking

✅ **Field Type Validation (Phase 2.2 - NEW)**
- Complete type validation for all Lambda types
- Primitive types: string, int, float, bool
- Complex types: nested structures, arrays, elements
- Type references: named types (Address, Contact, Employee)
- Recursive validation: 4+ level deep nesting
- Full path reporting: .employee.contact.address.city
- Array index notation: .employees[1].contacts[0].phone
- Human-readable error messages with type names

✅ **CLI Integration**
- All validation options functional
- Format auto-detection
- Help system with examples

✅ **Test Coverage**
- 1881/1888 tests passing (99.6%)
- 44/44 validator integration tests passing
- 18/18 Phase 2.2 path reporting tests passing
- Comprehensive negative test suite

### Next Priority Phases
1. **Phase 2.1** - Add suggestion generation (typo detection, type suggestions) (3-4 days)
2. **Phase 4.1** - Missing vs null field distinction (2 days)
3. **Phase 6.1** - Number type promotion with checks (1 day)
4. **Phase 1.1** - Document Lambda schema patterns (1-2 days)

### Files Modified in Recent Integration
- `lambda/validator/ast_validate.cpp` - Full AST integration + Schema loading
- `lambda/validator/error_reporting.cpp` - Path formatting
- `lambda/validator.cpp` - Real transpiler implementation
- `lambda/validate.cpp` - Core validation logic with recursive type checking (Phase 2.2)
- `test/AST_PARSER_INTEGRATION_COMPLETE.md` - Documentation
- `test/test_validator_path_reporting.cpp` - Comprehensive Phase 2.2 tests (18 tests)
- `doc/Validator_Phase2.2_Summary.md` - Complete Phase 2.2 documentation

---

**Status:** ✅ Phase 2.2 Complete (January 2025) - Field Type Validation with Enhanced Error Reporting

**Recent Achievement:** Full type validation system implemented with comprehensive path reporting, array index notation, and human-readable error messages. All 18 automated tests passing. See `doc/Validator_Phase2.2_Summary.md` for details.
