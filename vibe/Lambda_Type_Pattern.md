# Lambda Type Pattern Occurrence Syntax Proposal

**Status:** Proposal  
**Date:** 2026-02-03  
**Author:** Lambda Team

## Overview

This proposal introduces occurrence quantifiers for Lambda's type pattern system, allowing precise specification of how many times a type can appear in a sequence or collection context.

## Motivation

Currently, Lambda supports basic occurrence modifiers:
- `T?` — zero or one (optional)
- `T*` — zero or more
- `T+` — one or more

However, there's no way to express:
- Exact count: "exactly 5 integers"
- Bounded range: "between 2 and 5 strings"
- Minimum bound: "at least 3 items"

This is essential for schema validation, document structure definitions, and precise type contracts.

## Options Considered

### Option 1: `T{min, max}` — Regex Style

```lambda
int{2, 5}      // 2 to 5 integers
int{3}         // exactly 3 integers
int{2,}        // 2 or more integers
```

**Pros:**
- Familiar to regex users
- Well-established convention

**Cons:**
- **Conflicts with Lambda's map literal syntax** `{a: int, b: bool}`
- Parser ambiguity: `int{1,3}` could be confused with map construction
- Curly braces already heavily used in Lambda

**Verdict:** ❌ Rejected due to syntax conflict

---

### Option 2: `T[min, max]` — Array Style

```lambda
int[2, 5]      // 2 to 5 integers
int[3]         // exactly 3 integers
int[2, *]      // 2 or more integers
```

**Pros:**
- Matches C/Java/Rust array type conventions (`int[5]`)
- Clean and minimal for exact count
- No conflict with array literals (different context: type vs value)
- Familiar to most programmers

**Cons:**
- Need to distinguish from indexing (context-dependent)
- `*` for unbounded feels slightly ad-hoc

**Verdict:** ✅ Strong candidate

---

### Option 3: `T[min:max]` — Slice Style

```lambda
int[2:5]       // 2 to 5 integers
int[5:5]       // exactly 5 integers
int[2:]        // 2 or more integers
```

**Pros:**
- Python slice familiarity
- Visually distinct from exact count

**Cons:**
- Colon already used for map entries `{key: value}`
- Awkward exact syntax: `int[5:5]`
- Less intuitive than comma-separated

**Verdict:** ❌ Rejected due to awkward exact case and colon overuse

---

### Option 4: `T[min to max]` — Lambda Native Style

```lambda
int[2 to 5]    // 2 to 5 integers
int[5]         // exactly 5 integers
int[2 to *]    // 2 or more integers
```

**Pros:**
- Consistent with Lambda's `to` range operator
- Very readable and self-documenting
- Unique Lambda flavor

**Cons:**
- More verbose than comma syntax
- `to` keyword inside brackets is unusual
- Parsing complexity with optional `to` clause

**Verdict:** ⚠️ Considered but deemed too verbose

---

## Decision

**Selected Syntax: `T[min, max]` with shorthand forms**

### Full Specification

| Syntax | Meaning | Example |
|--------|---------|---------|
| `T[n]` | Exactly n | `int[5]` — exactly 5 integers |
| `T[min, max]` | Between min and max (inclusive) | `int[2, 5]` — 2 to 5 integers |
| `T[n+]` | At least n (unbounded upper) | `int[3+]` — 3 or more integers |

### Relationship to Existing Operators

The new syntax extends naturally from existing occurrence modifiers:

| Existing | New Equivalent | Meaning |
|----------|----------------|---------|
| `T?` | `T[0, 1]` | zero or one |
| `T*` | `T[0+]` | zero or more |
| `T+` | `T[1+]` | one or more |

### Grammar

```
occurrence_type := type occurrence_suffix?
occurrence_suffix := '[' occurrence_spec ']'
occurrence_spec := exact_count | range_count | min_unbounded
exact_count := INTEGER
range_count := INTEGER ',' INTEGER
min_unbounded := INTEGER '+'
```

### Examples

```lambda
// Schema definitions
type IPv4 = int[4];                    // exactly 4 integers (0-255)
type MAC = int[6];                     // exactly 6 bytes
type Coordinates = float[2, 3];        // 2D or 3D coordinates
type Polygon = Point[3+];              // at least 3 points

// Tuple/sequence patterns
type RGB = (int[3]);                   // 3-tuple of ints
type RGBA = (int[3, 4]);               // 3 or 4 ints

// In function signatures
fn validate_range(values: int[2]) bool {
    values[0] <= values[1]
}

// Combined with other type operators
type OptionalPair = int[2]?;           // optional pair
type ListOfTriples = int[3]*;          // zero or more triples
```

---

## String Pattern Alignment

To maintain consistency across Lambda's pattern systems, string patterns should support the same occurrence syntax.

### Current String Pattern Syntax

```lambda
// Existing patterns
"a?"           // zero or one 'a'
"a*"           // zero or more 'a'
"a+"           // one or more 'a'
```

### Updated String Pattern Syntax

```lambda
// Exact count
"a"[3]         // exactly 3 'a's → "aaa"

// Range
"a"[2, 5]      // 2 to 5 'a's

// Minimum unbounded
"a"[3+]        // 3 or more 'a's

// Character class with occurrence
("0" to "9")[4]     // exactly 4 digits
("a" to "z")[2, 8]  // 2 to 8 lowercase letters

// Practical examples
("0" to "9")[3] "-" ("0" to "9")[4]              // US phone: 555-1234
("0" to "9")[4] "-" ("0" to "9")[2] "-" ("0" to "9")[2]  // Date: 2026-02-03
```

### Comparison with Regex

| Regex    | Lambda String Pattern | Meaning   |
| -------- | --------------------- | --------- |
| `a{3}`   | `"a"[3]`              | exactly 3 |
| `a{2,5}` | `"a"[2,5]`            | 2 to 5    |
| `a{3,}`  | `"a"[3+]`             | 3 or more |
| `\d{4}`  | `("0" to "9")[4]`     | 4 digits  |

---

## Summary

| Feature | Syntax | Example |
|---------|--------|---------|
| Exact count | `T[n]` | `int[5]` |
| Range | `T[min, max]` | `int[2, 5]` |
| Minimum unbounded | `T[n+]` | `int[3+]` |
| String pattern exact | `"c"[n]` | `"a"[3]` |
| String pattern range | `"c"[min, max]` | `("0" to "9")[2, 4]` |
| String pattern min | `"c"[n+]` | `"x"[2+]` |

This design provides:
- **Familiarity**: Resembles C/Java/Rust array types
- **Consistency**: Aligns type and string pattern syntax
- **Completeness**: Covers exact, range, and unbounded cases
- **Simplicity**: Minimal syntax with clear semantics
