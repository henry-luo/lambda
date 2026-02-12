# Lambda Validator API Documentation

## Overview

The Lambda Validator provides type validation capabilities for the Lambda language. It supports:

1. **Script Evaluation**: Runtime type checking via the `is` operator
2. **CLI Validation**: Schema-based validation of input files via `lambda validate` command
3. **Type Pattern Matching**: Complex type constraints including unions, intersections, and occurrences

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Entry Points                                  │
├─────────────────┬───────────────────┬───────────────────────────────┤
│  Script Eval    │   CLI Validate    │        Tests                   │
│  (is operator)  │  (lambda validate)│                                │
└────────┬────────┴─────────┬─────────┴────────────┬──────────────────┘
         │                  │                      │
         ▼                  ▼                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    SchemaValidator Class                             │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  - validate(item, type_name)                                │    │
│  │  - validate_type(item, Type*)                               │    │
│  │  - load_schema(source)                                      │    │
│  │  - find_type(type_name)                                     │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  Validation Dispatch (validate.cpp)                  │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  validate_against_type() - Main dispatcher                   │    │
│  │    ├─ validate_against_primitive_type()                     │    │
│  │    ├─ validate_against_array_type()                         │    │
│  │    ├─ validate_against_map_type()                           │    │
│  │    ├─ validate_against_element_type()                       │    │
│  │    ├─ validate_against_union_type()                         │    │
│  │    └─ validate_against_occurrence()                         │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

## Core Types

### Type Hierarchy

```c
// Base type structure
struct Type {
    uint16_t type_id;    // LMD_TYPE_* constant
    int      type_index; // Index in type registry (for TypeRef)
};

// Typed array: T[]
struct TypeArray : Type {
    Type* element_type;  // Type of array elements
};

// Typed map: {key: T, ...}
struct TypeMap : Type {
    MapEntry* entries;   // Field definitions
    int       count;     // Number of fields
};

// Typed element: <tag ...>...</tag>
struct TypeElmt : Type {
    String*   name;      // Element tag name (or nullptr for any)
    MapEntry* attrs;     // Attribute definitions
    int       attr_count;
    Type*     content;   // Content type (children)
};

// Type reference: named type from registry
struct TypeRef : Type {
    String* name;        // Type name to lookup
};

// Union/Intersection: T | U or T & U
struct TypeBinary : Type {
    Type*    left;
    Type*    right;
    Operator op;         // OP_PIPE (|) or OP_AMP (&)
};

// Type with occurrence constraint
struct TypeUnary : Type {
    Type*    type;
    Operator op;         // OP_QUESTION, OP_STAR, OP_PLUS, OP_BRACKET
    int      min;        // Minimum count (for bracket notation)
    int      max;        // Maximum count (-1 = unlimited)
};
```

### Type IDs

```c
// Primitive types
LMD_TYPE_NULL     // null
LMD_TYPE_BOOL     // bool
LMD_TYPE_INT      // int
LMD_TYPE_INT64    // int64
LMD_TYPE_FLOAT    // float
LMD_TYPE_STRING   // string
LMD_TYPE_SYMBOL   // symbol
LMD_TYPE_DATETIME // datetime
LMD_TYPE_DECIMAL  // decimal
LMD_TYPE_BINARY   // binary (blob)

// Container types
LMD_TYPE_LIST     // list (untyped)
LMD_TYPE_ARRAY    // array (typed)
LMD_TYPE_MAP      // map (typed)
LMD_TYPE_ELMT     // element (typed)
LMD_TYPE_RANGE    // range

// Meta types
LMD_TYPE_ANY      // any type
LMD_TYPE_TYPE_REF // type reference
LMD_TYPE_TYPE_UNARY   // T?, T*, T+, T[n]
LMD_TYPE_TYPE_BINARY  // T | U, T & U
```

## Public API

### C Wrapper Functions (for MIR/C interop)

```c
// Create a new schema validator
SchemaValidator* schema_validator_create(Pool* pool);

// Destroy validator and cleanup
void schema_validator_destroy(SchemaValidator* validator);

// Load schema from Lambda source code
// Returns: 0 on success, non-zero on error
int schema_validator_load_schema(
    SchemaValidator* validator,
    const char* source,      // Lambda schema source code
    const char* root_type    // Name of root type (optional)
);

// Validate item against a named type
ValidationResult* schema_validator_validate(
    SchemaValidator* validator,
    ConstItem item,
    const char* type_name
);

// Validate item against a Type* directly
// This is the primary API used by lambda-eval.cpp for `is` operator
ValidationResult* schema_validator_validate_type(
    SchemaValidator* validator,
    ConstItem item,
    Type* type
);

// Find a type by name in the validator's registry
Type* schema_validator_find_type(
    SchemaValidator* validator,
    const char* type_name
);
```

### ValidationResult Structure

```c
struct ValidationResult {
    bool             is_valid;      // True if validation passed
    int              error_count;   // Number of errors
    ValidationError* errors;        // Linked list of errors
};

struct ValidationError {
    ValidationErrorCode code;       // Error type
    String*            message;     // Human-readable message
    PathSegment*       path;        // Path to invalid item
    Type*              expected;    // Expected type (if applicable)
    Item               actual;      // Actual item that failed
    String**           suggestions; // Suggested fixes (optional)
    ValidationError*   next;        // Next error in list
};

enum ValidationErrorCode {
    VALID_ERROR_NONE = 0,
    VALID_ERROR_TYPE_MISMATCH,      // Type doesn't match
    VALID_ERROR_MISSING_FIELD,      // Required field missing
    VALID_ERROR_UNKNOWN_FIELD,      // Unknown field in strict mode
    VALID_ERROR_CONSTRAINT_VIOLATION,// Constraint failed
    VALID_ERROR_INVALID_ELEMENT,    // Invalid element structure
    VALID_ERROR_PARSE_ERROR,        // Parse/syntax error
    VALID_ERROR_SCHEMA_ERROR,       // Schema definition error
    VALID_ERROR_CIRCULAR_REF,       // Circular type reference
    VALID_ERROR_TIMEOUT,            // Validation timeout
};
```

### ValidationOptions

```c
struct ValidationOptions {
    bool  strict_mode;          // Report unknown fields as errors
    bool  allow_unknown_fields; // Allow extra fields in maps/elements
    bool  allow_empty_elements; // Allow elements with no children
    int   max_depth;            // Max nesting depth (default: 100)
    int   max_errors;           // Stop after N errors (default: 100)
    int   timeout_ms;           // Timeout in milliseconds (0 = no limit)
    char** enabled_rules;       // Specific rules to enable
    char** disabled_rules;      // Specific rules to disable
};

// Set validation options
void schema_validator_set_options(
    SchemaValidator* validator,
    ValidationOptions* options
);

// Convenience setters
void schema_validator_set_strict_mode(SchemaValidator* validator, bool strict);
void schema_validator_set_max_errors(SchemaValidator* validator, int max);
void schema_validator_set_timeout(SchemaValidator* validator, int timeout_ms);
```

## Usage Examples

### Script Evaluation (is operator)

```lambda
// Basic type checks
42 is int           // true
"hello" is string   // true
3.14 is float       // true

// Union types
x is int | string   // true if x is int OR string
x is int & string   // true if x is both (rarely useful)

// Array types
[1, 2, 3] is int[]  // true - array of ints
[] is int[]         // true - empty array matches any typed array

// Optional types
null is int?        // true - int? accepts null or int
42 is int?          // true

// Occurrence constraints
[1, 2, 3] is int+   // true - one or more ints
[] is int*          // true - zero or more ints
[1, 2] is int[2]    // true - exactly 2 ints
[1, 2, 3] is int[2, 5] // true - between 2 and 5 ints
[1, 2, 3] is int[2+]   // true - 2 or more ints
```

### CLI Validation

```bash
# Validate JSON against auto-detected schema
lambda validate data.json -s schema.ls

# Validate HTML with built-in HTML5 schema
lambda validate page.html

# Validate with options
lambda validate data.json -s schema.ls --strict --max-errors 50

# Validate Lambda source file (AST validation)
lambda validate script.ls
```

### Programmatic Usage (C++)

```cpp
#include "lambda/validator/validator.hpp"

void validate_data(Pool* pool, ConstItem data) {
    // Create validator
    SchemaValidator* validator = schema_validator_create(pool);
    
    // Load schema
    const char* schema = R"(
        type Person = {
            name: string,
            age: int,
            email: string?
        }
    )";
    schema_validator_load_schema(validator, schema, "Person");
    
    // Validate
    ValidationResult* result = schema_validator_validate(
        validator, data, "Person"
    );
    
    if (result->is_valid) {
        printf("Validation passed!\n");
    } else {
        printf("Validation failed with %d errors:\n", result->error_count);
        for (ValidationError* err = result->errors; err; err = err->next) {
            printf("  - %s\n", err->message->chars);
        }
    }
    
    // Cleanup
    schema_validator_destroy(validator);
}
```

## Type Pattern Syntax

### Basic Types

| Pattern | Matches |
|---------|---------|
| `null` | null value |
| `bool` | true or false |
| `int` | 32-bit integer |
| `int64` | 64-bit integer |
| `float` | floating point |
| `string` | string value |
| `symbol` | symbol value |
| `datetime` | date/time value |
| `decimal` | arbitrary precision decimal |
| `any` | any value |

### Container Types

| Pattern | Matches |
|---------|---------|
| `T[]` | array with elements of type T |
| `list` | any list (untyped) |
| `{key: T, ...}` | map with typed fields |
| `<tag attr: T>content</tag>` | element with type constraints |

### Union and Intersection

| Pattern | Matches |
|---------|---------|
| `T \| U` | T or U (union) |
| `T & U` | both T and U (intersection) |
| `T \| U \| V` | T or U or V (multiple union) |

### Occurrence Constraints

| Pattern | Matches |
|---------|---------|
| `T?` | zero or one T (optional) |
| `T*` | zero or more T |
| `T+` | one or more T |
| `T[n]` | exactly n items of type T |
| `T[min, max]` | between min and max items |
| `T[n+]` | n or more items |

## File Organization

```
lambda/validator/
├── validator.hpp           # Public API declarations
├── validator_internal.hpp  # Internal helpers (PathScope, DepthScope)
├── doc_validator.cpp       # SchemaValidator implementation
├── validate.cpp            # Main validation dispatch
├── validate_pattern.cpp    # Type pattern matching
├── validate_helpers.cpp    # Error reporting helpers
├── ast_validate.cpp        # CLI entrypoints
└── schema_builder.cpp      # Schema construction from AST
```

## Error Handling

### Path Reporting

Validation errors include paths to the invalid item:

```
Error at $.users[0].email: Expected type 'string', but got 'int'
Error at $.config.timeout: Missing required field
```

Path components:
- `$` - root
- `.field` - map field access
- `[0]` - array index
- `@attr` - element attribute

### Suggestions

Errors may include suggested fixes:

```
Error at $.person.emial: Unknown field 'emial'
  Suggestion: Did you mean 'email'?
```

## Integration with Lambda Runtime

The validator integrates with the Lambda runtime through:

1. **Type Building**: `build_ast.cpp` constructs Type* from parsed type expressions
2. **Evaluation**: `lambda-eval.cpp` uses `schema_validator_validate_type()` for `is` operator
3. **Print**: `print.cpp` formats Type* for error messages

The validator is created lazily in `EvalContext` when first needed:

```cpp
// In lambda-eval.cpp
if (!context->validator) {
    context->validator = schema_validator_create(context->pool);
}
```

## Performance Considerations

- Validators are cached per EvalContext to avoid repeated creation
- Type resolution uses caching for named types
- Validation depth is limited to prevent stack overflow
- Timeout support for untrusted input

## See Also

- [Lambda Reference](../doc/Lambda_Reference.md) - Language reference
- [Doc Schema](../doc/Doc_Schema.md) - Built-in document schemas
- [Validator Guide](../doc/Lambda_Validator_Guide.md) - User guide
