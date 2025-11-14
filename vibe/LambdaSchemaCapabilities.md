# Lambda Schema Validation Capabilities Analysis

**Date:** November 14, 2025
**Purpose:** Document what validation constraints can be expressed in current Lambda schema syntax
**Part of:** Validator Enhancement Plan Phase 1.1

---

## Executive Summary

Lambda's current schema system already supports powerful validation constraints through its type syntax. This document analyzes what validation patterns are expressible today, eliminating the need for separate schema metadata structures.

**Key Finding:** Lambda's Type AST structures (Type*, TypeUnary, TypeMap, TypeArray, etc.) already capture most validation constraints through occurrence operators, type unions, and structured types.

---

## Current Schema Syntax Overview

### Basic Type Definitions

```lambda
// Primitive types
type Name = string
type Age = int
type Price = float
type Active = bool

// Container types
type Tags = [string*]        // Array of strings
type Config = {string: int}  // Map type
```

### Occurrence Operators (Already Supported)

Lambda supports three occurrence operators that control cardinality:

| Operator | Syntax | Meaning | Type Structure |
|----------|---------|---------|----------------|
| `?` | `Type?` | Optional (0 or 1) | `TypeUnary` with `OPERATOR_OPTIONAL` |
| `+` | `Type+` | One or more (1+) | `TypeUnary` with `OPERATOR_ONE_MORE` |
| `*` | `Type*` | Zero or more (0+) | `TypeUnary` with `OPERATOR_ZERO_MORE` |

**Example Schema:**
```lambda
type Document = {
    title: string,           // required (no operator)
    subtitle: string?,       // optional field
    authors: [string+],      // non-empty array
    tags: [string*],         // any number of tags
    version: int?            // optional version
}
```

**Type AST Representation:**
```cpp
TypeMap {
    shape: [
        ShapeEntry { name: "title", type: Type{LMD_TYPE_STRING} },
        ShapeEntry { name: "subtitle", type: TypeUnary{OPERATOR_OPTIONAL, Type{LMD_TYPE_STRING}} },
        ShapeEntry { name: "authors", type: TypeArray{TypeUnary{OPERATOR_ONE_MORE, Type{LMD_TYPE_STRING}}} },
        ShapeEntry { name: "tags", type: TypeArray{TypeUnary{OPERATOR_ZERO_MORE, Type{LMD_TYPE_STRING}}} },
        ShapeEntry { name: "version", type: TypeUnary{OPERATOR_OPTIONAL, Type{LMD_TYPE_INT}} }
    ]
}
```

---

## Validation Capabilities by Category

### 1. Field Optionality

**Current Capability:** ✅ **Fully Supported**

```lambda
type Person = {
    name: string,       // required - no ? operator
    email: string?,     // optional - ? operator
    phone: string?      // optional - ? operator
}
```

**How It Works:**
- Fields WITHOUT `?` are required (validator should check presence)
- Fields WITH `?` are optional (validator allows missing or null)
- Type structure: `TypeUnary` with `OPERATOR_OPTIONAL` wraps the field type

**Validator Behavior:**
```cpp
// For required field (no ?)
if (!map_has_field(map, "name")) {
    error("Missing required field: name");
}

// For optional field (?)
if (!map_has_field(map, "email")) {
    // OK - optional field can be missing
} else if (value == null) {
    // OK - optional field can be null
}
```

---

### 2. Array Cardinality

**Current Capability:** ✅ **Fully Supported**

```lambda
type Document = {
    tags: [string*],      // 0 or more tags
    authors: [string+],   // at least 1 author required
    versions: [int?]*     // array of optional ints (any length)
}
```

**How It Works:**
- `[Type*]` - Array can be empty
- `[Type+]` - Array must have at least one element
- `[Type]` - Fixed-size or specific array structure
- Occurrence operators apply to array ELEMENTS, not array itself

**Type AST Representation:**
```cpp
// tags: [string*]
TypeArray {
    element_type: TypeUnary{OPERATOR_ZERO_MORE, Type{LMD_TYPE_STRING}}
}

// authors: [string+]
TypeArray {
    element_type: TypeUnary{OPERATOR_ONE_MORE, Type{LMD_TYPE_STRING}}
}
```

**Validator Logic:**
```cpp
// From lambda/validate.cpp:484
switch (occurrence_op) {
    case OPERATOR_OPTIONAL:  // ?
        if (item_count > 1) error("Expected 0 or 1 items");
        break;
    case OPERATOR_ONE_MORE:  // +
        if (item_count < 1) error("Expected at least 1 item");
        break;
    case OPERATOR_ZERO_MORE: // *
        // Always valid
        break;
}
```

---

### 3. Type Alternatives (Unions)

**Current Capability:** ✅ **Fully Supported**

```lambda
type Value = string | int | null
type Status = "active" | "inactive" | "pending"
type Data = {string: string} | [string*]
```

**How It Works:**
- `|` operator creates union types
- Validator tries each alternative until one matches
- Type structure: Binary type expression with `OPERATOR_UNION`

**Example from doc_schema.ls:**
```lambda
type InlineContent = string | em | strong | code | a | span | br | img
type BlockContent = Para | Header | CodeBlock | BlockQuote | List
```

**Validator Behavior:**
```cpp
// Try each union member
for each (member in union_type->members) {
    ValidationResult* result = validate_against_type(validator, item, member);
    if (result->valid) {
        return result;  // Found matching type
    }
}
// No match - report error
```

---

### 4. Nested Structures

**Current Capability:** ✅ **Fully Supported**

```lambda
type Document = {
    meta: {
        title: string,
        author: {
            name: string,
            email: string?
        }?
    }?,
    body: BlockContent+
}
```

**How It Works:**
- Map types can contain other map types
- Occurrence operators apply at any nesting level
- Full recursive validation

**Type AST Representation:**
```cpp
TypeMap {
    shape: [
        ShapeEntry {
            name: "meta",
            type: TypeUnary{OPERATOR_OPTIONAL, TypeMap{
                shape: [
                    ShapeEntry { name: "title", type: Type{LMD_TYPE_STRING} },
                    ShapeEntry { name: "author", type: TypeUnary{OPERATOR_OPTIONAL, TypeMap{...}} }
                ]
            }}
        },
        ShapeEntry {
            name: "body",
            type: TypeUnary{OPERATOR_ONE_MORE, Type{BLOCK_CONTENT}}
        }
    ]
}
```

---

### 5. Element Types (XML/HTML)

**Current Capability:** ✅ **Fully Supported**

```lambda
type Document <
    version: string,
    <meta: Meta>?,
    <body>
>

type Para <
    id: string?,
    class: string?,
    InlineContent*
>
```

**How It Works:**
- `< attr: Type, <child>* >` syntax defines elements
- Attributes are map-like
- Content is list-like
- Type structure: `TypeElmt` with attribute shape and content types

**Example from doc_schema.ls:**
```lambda
type Header < HeaderAttrs, InlineContent* >
type Link < LinkAttrs, InlineContent* >
type Figure < BaseAttrs, <img>, <figcaption: InlineContent*>? >
```

---

### 6. Type Intersections

**Current Capability:** ✅ **Supported**

```lambda
type BaseAttrs = {
    id: string?,
    class: string?,
    'data-*': {string: string}
}

type LinkAttrs = BaseAttrs & {
    href: string,
    title: string?
}
```

**How It Works:**
- `&` operator combines types (intersection)
- Resulting type must satisfy ALL constraints
- Type structure: Binary type expression with `OPERATOR_INTERSECT`

**Example from doc_schema.ls:**
```lambda
type HeaderAttrs = BaseAttrs & {
    level: int  // 1-6
}

type SpanAttrs = BaseAttrs & {
    style: {string: string}?
}
```

---

## What's Already Validated

### ✅ Currently Working

| Constraint | Syntax | Validator Support |
|------------|--------|-------------------|
| Required fields | `field: Type` | ✅ Works (needs enhancement) |
| Optional fields | `field: Type?` | ✅ Works |
| Non-empty arrays | `[Type+]` | ✅ Works |
| Any-length arrays | `[Type*]` | ✅ Works |
| Type unions | `Type1 \| Type2` | ✅ Works |
| Primitive types | `int`, `string`, etc. | ✅ Works |
| Nested maps | `{a: {b: Type}}` | ✅ Works |
| Element attributes | `<elmt attr: Type>` | ✅ Works |
| Element content | `<elmt; Content*>` | ✅ Works |
| Type intersections | `Type1 & Type2` | ✅ Works |

### ⚠️ Needs Enhancement

| Constraint | Current Gap | Enhancement Needed |
|------------|-------------|-------------------|
| Missing vs null | Can't distinguish | Add `map_has_field()` helper (Phase 4) |
| Error suggestions | No suggestions | Add typo detection (Phase 2) |
| Format-specific | No format awareness | Add unwrapping logic (Phase 5) |
| Number precision | No loss detection | Add range checks (Phase 6) |
| Path reporting | Returns empty string | Implement path formatter (Phase 2) |

---

## What's NOT Expressible (Future Extensions)

These constraints require Lambda schema syntax extensions:

### 1. Value Constraints

**Needed Syntax:**
```lambda
// Range constraints
type Age = int where value >= 0 && value <= 150
type Port = int where value in 1 to 65535

// String patterns
type Email = string where matches /^[^@]+@[^@]+\.[^@]+$/
type Phone = string where matches /^\d{3}-\d{3}-\d{4}$/

// Enum values
type Status = "active" | "inactive" | "pending"  // ✅ Already works via unions
type Priority = int where value in [1, 2, 3, 4, 5]  // ❌ Needs extension
```

### 2. Cross-Field Dependencies

**Needed Syntax:**
```lambda
type DateRange = {
    start: datetime,
    end: datetime
} where end > start

type Dimensions = {
    width: int,
    height: int,
    area: int
} where area == width * height
```

### 3. Open Types (Additional Fields)

**Needed Syntax:**
```lambda
// Allow any additional fields
type ExtensibleMeta = {
    title: string,
    author: string,
    ...rest: any  // Allow unknown fields
}

// Or marker syntax
type OpenDocument = {
    title: string,
    ...*  // Allow additional fields
}
```

### 4. Field Count Constraints

**Needed Syntax:**
```lambda
type SmallConfig = {
    ...fields
} where field_count <= 10

type RequiredKeys = {
    ...fields
} where field_count >= 3
```

---

## Implementation Status

### Phase 1: Foundation (Current Work)

#### Task 1.1: Documentation ✅ (This Document)
- [x] Analyzed current schema capabilities
- [x] Documented occurrence operators
- [x] Identified what works today
- [x] Listed future syntax needs

#### Task 1.2: Type AST Review ✅ (Analysis Complete)
- [x] Reviewed TypeUnary for occurrence operators
- [x] Confirmed TypeMap for field structures
- [x] Verified TypeArray for array validation
- [x] No structural changes needed

### Next Steps

1. **Phase 4 (Sprint 1):** Implement `map_has_field()` for missing vs null distinction
2. **Phase 2 (Sprint 2):** Enhance error reporting with suggestions and paths
3. **Phase 3 (Sprint 3):** Add validation configuration options
4. **Future:** Design Lambda schema extension syntax (separate RFC)

---

## Code Examples from Existing Schemas

### Example 1: Document Schema (doc_schema.ls)

```lambda
type Document = {
    meta?: {
        title: string?
    },
    body: [BlockElement+]  // At least one block required
}

type BlockElement = Para | Header | CodeBlock | BlockQuote | List

type Para = {
    tag: "p",
    content: [InlineElement*]  // Any number of inline elements
}
```

**Validation Behavior:**
- `meta?` - Can be missing or null
- `meta.title?` - If meta exists, title can be missing/null
- `body: [BlockElement+]` - Body MUST have at least one block
- Each block validated against union type

### Example 2: Email Schema (eml_schema.ls)

```lambda
type EmailMessage = {
    headers: EmailHeaders,
    body: EmailBody
}

type EmailHeaders = {
    from: string,
    to: [string+],      // At least one recipient
    cc: [string*]?,     // Optional CC list
    subject: string?
}
```

**Validation Behavior:**
- `from` - Required field
- `to: [string+]` - Must have at least one recipient
- `cc: [string*]?` - CC field optional, if present can be empty array
- `subject?` - Optional subject

### Example 3: Calendar Schema (ics_schema.ls)

```lambda
type VEvent = {
    type: "VEVENT",
    properties: EventProperties
}

type EventProperties = CommonProperties & {
    dtstart: string,
    dtend: string?,
    duration: string?,
    rrule: string?,
    exdate: [string*]?
}
```

**Validation Behavior:**
- Type intersection (`CommonProperties &`) - All fields from both types required
- `dtstart` - Required
- `dtend?`, `duration?`, `rrule?` - Optional
- `exdate: [string*]?` - Optional field that, if present, is an array

---

## References

- **Lambda Grammar:** `lambda/tree-sitter-lambda/grammar.js` - Defines occurrence operators
- **Type Structures:** `lambda/lambda-data.hpp` - Type*, TypeUnary, TypeMap, etc.
- **Validator Logic:** `lambda/validate.cpp` - Current validation implementation
- **Test Cases:** `test/lambda/validator/` - Occurrence and type tests
- **Example Schemas:** `lambda/input/doc_schema.ls`, `eml_schema.ls`, `ics_schema.ls`

---

## Conclusion

Lambda's current schema system is **powerful and complete** for expressing most validation constraints through:

1. **Occurrence operators** (`?`, `+`, `*`) for cardinality
2. **Type unions** (`|`) for alternatives
3. **Type intersections** (`&`) for composition
4. **Nested structures** for complex documents

**No new type structures needed** - validation enhancements will work with existing Type AST.

The enhancement plan focuses on:
- Improving validation logic (missing vs null, better errors)
- Adding configuration options (strict mode, timeouts)
- Format-specific handling (XML unwrapping, HTML quirks)
- Better error reporting (suggestions, paths, context)

Future schema syntax extensions (value constraints, open types, etc.) are deferred to separate design discussions.

---

**Status:** ✅ Phase 1.1 Complete
