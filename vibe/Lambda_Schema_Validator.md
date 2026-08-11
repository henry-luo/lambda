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

## Current Result Convention (as-built, 2026-08-10)

Every function in the recursive core returns `ValidationResult*` — there is no
bool-returning variant. Nested results are consumed by one idiom, used
consistently at all 9 call sites:

```c
ValidationResult* child = validate_against_type(validator, item, type);
if (child && !child->valid) merge_errors(result, child, validator);
```

- **Always `->valid`**, never `->error_count`, at the consumption site.
- **Always NULL-guarded** (`child &&`): 9 guarded sites, 0 unguarded.
- **`merge_errors(dest, src, validator)`** is the only propagation path. It
  early-returns when `!src || src->valid`, so calling it unconditionally is
  safe; on failure it sets `dest->valid = false` and **deep-copies** each
  `ValidationError` into `dest` — a second allocation source beyond the
  `ValidationResult` itself, since messages are copied too.
- Two exit idioms coexist: leaf paths assign `result->valid` directly;
  collection/occurrence paths finish with
  `if (result->error_count == 0) result->valid = true;`.
- `error_count` is load-bearing in exactly one place: **union validation**
  scores branches by `member_result->error_count < min_errors` to report the
  "closest" member when every branch fails.
- **Nothing short-circuits.** There is no early return on first failure
  anywhere; a collection validates every element even after one fails, by
  design — the code comments that occurrence validation "must retain every
  failing element so callers can report all indexed paths".

Two consequences for the fast mode below:

1. The consumption idiom is mechanically convertible — `if (child &&
   !child->valid)` becomes `if (!child_ok)` — because no caller inspects
   anything but `valid`. Union is the only site needing more, and in fast
   mode it needs *less*: "does any branch match?" short-circuits on the first
   success, and the `min_errors` scoring is purely a reporting concern.
2. The absence of short-circuiting is not just a missed optimization, it is
   *required* by the reporting contract. So fast mode's win is larger than
   removing allocation: it can also **stop at the first failure**, turning
   O(N) into O(k) on invalid input, which full mode can never do.

## Validation Modes (PROPOSAL, 2026-08-10)

The validator was designed for **data validation** (`lambda validate`), where
the deliverable is a detailed report: every `ValidationError` carries a
message, a path, expected/actual types, and optional suggestions. The runtime
type checker (`lambda_type_check` at declared boundaries, the `is` operator)
reuses that same machinery — but it is a **predicate**. It needs a yes/no
answer, and on the overwhelmingly common success it discards everything the
reporting path constructed.

The consequence is structural, not incidental: `validate_occurrence_type` →
`validate_list_occurrence` allocates one `ValidationResult` **per element**
via `create_validation_result()`, so a declared `T[]` boundary over an
N-element container costs N allocations that are immediately thrown away.
Measured: with tracking off, ~96% of a `bool[]`-heavy benchmark's runtime sat
inside the validator's allocator calls. Choosing a cheaper allocator does not
fix this — O(N) discarded records is the wrong shape regardless of where the
bytes come from.

### Proposed design

1. **Fast mode — allocation-free, short-circuiting.** `validate_against_type`
   gains a mode that returns only true/false and allocates nothing: no
   `ValidationResult`, no `ValidationError`, no path segments, no message
   formatting, and no `merge_errors` deep copies. All runtime type checking
   uses this mode. Because nothing needs to be reported, it may also **return
   on first failure** (see the convention above) and union members may
   **return on first success** — both impossible in full mode. Predicate
   helpers already exist in spirit (e.g. `validator_array_elem_embeds`); the
   work is threading a mode flag (or a parallel entry point) through the
   recursive walk so no level allocates.
2. **Full mode on demand.** When fast mode reports failure *and* a diagnostic
   is actually needed, re-run the same validation in full mode to build the
   detailed `ValidationResult` for the error report. The detailed cost is
   paid once, on the cold path, for a value already known to be invalid.
   Re-running is strictly cheaper than allocating eagerly on every success,
   and keeps one implementation of the validation rules — the two modes must
   not diverge into two rule sets.
3. **Duplicate-error suppression in collections.** Reporting must not emit
   one error per element when a whole collection fails the same way (`N`
   elements of the wrong type should not produce `N` near-identical errors).
   Needs a collapse policy — first-K plus a count, or grouping by
   (error code, expected type, depth) — chosen so the report stays useful
   without being O(N).

### Singleton results — viable, with three conditions

A minimal-churn way to implement fast mode: predefine two static
`ValidationResult`s (one valid, one invalid) and return their addresses
instead of allocating. The signature stays `ValidationResult*`, so all 9
consumption sites keep working verbatim, and the allocation disappears.

This works, but only under three conditions — each of which is a real defect
if skipped:

1. **The singletons must be `const`, and fast mode must return
   `const ValidationResult*`.** The current code allocates a result and then
   mutates it as it goes: 44 `result->valid = …` assignments and 42
   `add_*_error(result, …)` calls. A single one of those reaching a shared
   singleton corrupts it **process-wide**, and races across threads. Making
   the type `const` converts every such site into a compile error, which is
   what makes the conversion auditable rather than hopeful.

2. **The invalid singleton has an empty error list, and that breaks the
   collection exit idiom.** `merge_errors(dest, src)` with the invalid
   singleton sets `dest->valid = false`, then walks `src->errors` — which is
   `NULL`, so `add_validation_error` is never called and `dest->error_count`
   stays 0. The two collection paths then finish with
   `if (result->error_count == 0) result->valid = true;` and **silently flip
   the failure back to valid**. A false negative — invalid data reported as
   valid. Fix by either testing `valid` rather than `error_count` in the exit
   idiom, or (preferred) never routing fast-mode results through
   `merge_errors` at all.

3. **Union's `min_errors` scoring degenerates.** Every invalid singleton has
   `error_count == 0`, so branch scoring picks arbitrarily. Harmless once
   fast-mode union short-circuits on first success (it needs no scoring), but
   wrong if the union path is left unmodified and fed singletons.

**What the singleton does not buy.** It preserves the *call-site* idiom, not
the function bodies. Because the current bodies allocate-then-mutate, each
must still be restructured to compute a verdict and return the right
singleton at every exit — the same work as threading a bool through. The
singleton's value is that the 9 call sites, `merge_errors`' signature, and
the full-mode paths stay untouched, and that `const` makes the compiler
enumerate the remaining work.

**Recommended shape.** One recursive implementation carrying a mode, so the
two modes cannot drift into two rule sets: internal predicate helpers return
`bool` and do the actual rule work; full mode wraps them in the
`ValidationResult` construction that only it needs; fast mode returns
`&VALIDATION_OK` / `&VALIDATION_FAIL` and short-circuits. That keeps one rule
set, zero fast-mode allocation, unchanged call sites, and compiler-enforced
immutability of the singletons.

### Implementation status (2026-08-10)

**Stages 1-4 landed.** The mode, the singletons, and every hot kind:

- `SchemaValidator::fast_mode` + `is_fast_mode()`/`set_fast_mode()`, default
  off (`doc_validator.cpp`).
- `const ValidationResult VALIDATION_OK` / `VALIDATION_FAIL` with the
  `validation_verdict(bool)` helper (`validator.hpp`, defined in
  `doc_validator.cpp`).
- **Stage 1 — occurrence walks** (`validate_pattern.cpp`):
  `validate_array_num_occurrence` (O(1) representation check) and
  `validate_list_occurrence` (element walk, **returns on first bad element**).
- **Stage 2 — base type** (`validate.cpp`): `validate_against_base_type` is
  the leaf the element walk calls per item, so its unconditional
  `create_validation_result` was the per-element allocation. Fast mode handles
  `any`, delegating kinds (unary/binary), numeric embedding, the generic
  `TYPE_MAP`/`TYPE_ELMT` singletons, shaped map/element, and plain nominal
  matches; patterns, arrays, and literals fall through to full mode.
- **Stage 3 — map/element fields** (`validate.cpp`): `shape_entries_match_fast`
  mirrors `validate_shape_entries`' field rules as a predicate — no PathScope
  bookkeeping, no merge, stops at the first bad field. Used by both
  `validate_against_map_type` and `validate_against_element_type`.
- **Stage 4 — union** (`validate_pattern.cpp`): returns on the first matching
  member and skips `min_errors` scoring entirely, which exists only to pick
  the closest member for an error message fast mode never produces.
- `runtime_validate_value_against_type` selects the mode from its own
  contract: callers passing `validation = NULL` want a predicate and get fast
  mode; callers passing a slot are building an error message and get the
  reporting walk. This required no call-site changes — the two predicate
  callers already passed `NULL`.

An important discovery while wiring stage 1: **`lambda_type_check` already had
the two-mode structure.** It runs `runtime_type_admit_value` first and only
calls the validator *after* admission fails, to build the message. So the R4
premise ("the runtime allocates a report it discards on success") was true of
the validation walk reached through admission, not of the boundary entry
point itself.

**Measured** (release, 3 runs, vs archived v27, ms):

| row | v27 | before | after stages 2-4 |
|---|---:|---:|---:|
| jetstream/splay2 | 267 | 315 (+18%) | **271** (+1.5%) |
| jetstream/deltablue2 | 71.8 | 88.4 (+22%) | **79.9** (+11%) |
| jetstream/raytrace3d2 | 63.9 | 66.8 (+5%) | **62.8** (flat/better) |
| awfy/list2 | 0.93 | 0.95 | 0.93 (flat) |
| awfy/cd2 | 569 | 570 | 566 (flat — its cost is admission) |

### Implementation plan (as built)

**Mode placement.** `bool fast_mode` is per-validation-call state on
`SchemaValidator`, alongside `current_path`/`current_depth`, with
`is_fast_mode()`/`set_fast_mode()`. It is *not* in `ValidationOptions` —
options are user-facing schema policy, while the mode is a caller ABI choice
that must be set and cleared around a single call.

**The singletons.**

```c
extern const ValidationResult VALIDATION_OK;    // {valid=true,  error_count=0, errors=NULL}
extern const ValidationResult VALIDATION_FAIL;  // {valid=false, error_count=0, errors=NULL}
```

Fast mode returns `&VALIDATION_OK` / `&VALIDATION_FAIL`. Consumers only read
`->valid`, so the existing 9 call sites are unchanged.

**Staged conversion.** The dispatcher checks the mode and routes the hot
shapes into predicate helpers; kinds not yet converted fall through to the
existing full-mode code, which still returns a real `ValidationResult` whose
`->valid` the caller reads identically. This keeps every stage shippable and
green, and avoids a big-bang rewrite of the 44 `result->valid = …` sites.
Conversion order follows measured heat:

1. occurrence/array — the O(n) walk (ArrayNum takes the O(1) embed check;
   generic arrays short-circuit on first failing element);
2. primitive/base type — the leaves the walk calls;
3. map/element fields;
4. union — short-circuits on first matching member, skipping `min_errors`
   scoring entirely.

**Guards required by the singleton contract** (see the three conditions
above): fast mode must never call `merge_errors`, never call
`add_*_error(result, …)` on a singleton, and never reach the
`if (result->error_count == 0) result->valid = true;` exit idiom. Those are
all inside the full-mode-only branches, so the staging keeps them separated
by construction; the `const` on the singletons is what makes any violation a
compile error rather than shared-state corruption.

**Runtime entry.** `lambda_type_check` (declared boundaries, the `is`
operator) sets fast mode for its call. On failure, if a diagnostic is
actually needed, it re-runs the same validation with fast mode off to build
the detailed `ValidationResult` for the error message.

### Landed 2026-08-10: the packed bool lane (prerequisite, not the fast mode)

Before the fast mode, the measured hot case was fixed at its source. `fn_fill`
had lanes for int/uint64/float but **bool fell through to the generic boxed
`Array` branch**, so `fill(n, true)` produced n boxed Items. That value then
reached a declared `bool[]` boundary as a generic array, which is precisely
the shape that degenerates to the O(n) element walk — the O(1)
representation check (`validator_array_elem_embeds`) only fires for
`ARRAY_NUM`.

Four changes, each exposing the next:

1. `fn_fill` gained an `ELEM_BOOL` branch (`lambda-vector.cpp`).
2. `validator_array_elem_embeds` had no `ELEM_BOOL` case — it fell to
   `default: return false`, so a correct packed bool array was *rejected* by
   its own declared type. Added: a packed bool lane embeds exactly into
   `bool` (deliberately not into numeric types).
3. `fn_array_set`'s `ELEM_BOOL` branch never widened, unlike every numeric
   lane: writing `"mixed"` into an untyped `fill(3, true)` array silently
   stored `false` — **data loss**. Now widens on a non-bool write, matching
   the int/float lanes; a declared `bool[]` never reaches that path because
   the checked setter admits the element first.
4. `convert_specialized_to_generic` had no `ELEM_BOOL` branch either; its
   `else` assumed an i64 lane and read eight packed bools per element,
   yielding integers like 65537. Added a byte-wise branch. *(Note: the same
   `else` still mis-handles the sized lanes — ELEM_INT8/16/32, UINT8/16/32,
   FLOAT16/32 — if they ever reach widening. Pre-existing, untouched, worth
   an audit.)*

Measured (release, 3 runs, vs archived v27): kostya/primes2 42.4 → 5.7 ms
(**7.4x faster than v27**), larceny/primes2 41.8 → 6.0 ms, awfy/sieve2
0.267 → 0.084 ms, queens2 0.90 → 0.59 ms, triangl2 358 → 332 ms, puzzle2
flat. An isolated `bool[]` declaration boundary over 1M elements went
728 ms → 1.97 ms (**370x**).

This does not replace the fast mode: any declared container receiving a
genuinely generic array still walks its elements, and that walk still
allocates per element. It removes the *hottest* instance of the problem.

### Open questions

- Does the mode belong in `ValidationOptions`, as a distinct entry point, or
  as a separate lightweight predicate walker sharing the rule tables?
- Which existing callers legitimately need full mode on success? (Suspected:
  none — full mode is only ever consumed after a failure.)
- Where does the collapse policy live — in the validator, or in the error
  formatter that renders `ValidationResult`?
- Interaction with TE-17: representation-level admission for packed arrays
  would remove the per-element walk entirely for the hottest shapes, making
  fast mode's element loop rare rather than merely cheap.

**Memory-design consequence** (owned by
[Lambda_Design_Mem_Heap.md](./Lambda_Design_Mem_Heap.md) R4): the runtime
validation path must allocate nothing. That document does not prescribe how
the modes are structured; it only requires the allocation to disappear.

## Performance Considerations

- Validators are cached per EvalContext to avoid repeated creation
- Type resolution uses caching for named types
- Validation depth is limited to prevent stack overflow
- Timeout support for untrusted input
- Runtime type checking should use the allocation-free fast mode above;
  eager per-element `ValidationResult` construction is the known hot spot

## See Also

- [Lambda Reference](../doc/Lambda_Reference.md) - Language reference
- [Doc Schema](../doc/Doc_Schema.md) - Built-in document schemas
- [Validator Guide](../doc/Lambda_Validator_Guide.md) - User guide
