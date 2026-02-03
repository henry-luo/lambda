# Lambda String Pattern Type

**Date:** January 27, 2026  
**Part of:** Lambda Type System Enhancement

---

## Executive Summary

This proposal introduces **String Pattern Types** to Lambda, enabling compile-time and runtime validation of string formats through pattern-based type definitions. String patterns reuse Lambda's existing type pattern syntax (`|`, `*`, `+`, `?`) applied to string literals, creating specialized string types that can be used anywhere a type is expected.

**Key Features:**
1. Define string patterns using familiar type syntax
2. Pattern matching via `is` operator
3. System functions for pattern operations
4. Compiled to regex for efficient matching

---

## 1. String Pattern Definition

### 1.1 Basic Syntax

String patterns are defined using the `string` keyword followed by a pattern name and pattern expression:

```lambda
// Basic pattern definitions
string identifier = ("a" to "z" | "A" to "Z" | "_") ("a" to "z" | "A" to "Z" | "0" to "9" | "_")*

string digit = "0" to "9"
string hex_digit = digit | "a" to "f" | "A" to "F"

string email = identifier "@" identifier "." identifier

// Using occurrence operators
string phone_us = digit[3] "-" digit[3] "-" digit[4]
```

### 1.2 Pattern Operators

String patterns reuse Lambda's existing type pattern operators:

| Operator | Meaning                 | Example      | Matches               |
| -------- | ----------------------- | ------------ | --------------------- |
| `\|`     | Alternation             | `"a" \| "b"` | "a" or "b"            |
| `*`      | Zero or more            | `"a"*`       | "", "a", "aa", ...    |
| `+`      | One or more             | `"a"+`       | "a", "aa", "aaa", ... |
| `?`      | Optional (0 or 1)       | `"a"?`       | "" or "a"             |
| (space)  | Concatenation (implicit)| `"a" "b"`    | "ab"                  |
| `&`      | Intersection            | `\w & !"_"`  | word char except "_"  |
| `to`     | Character range         | `"a" to "z"` | a, b, c, ..., z       |
| `[n]`    | Exactly n times         | `digit[3]`   | "000" to "999"        |
| `[n, m]` | Between n and m times   | `digit[2, 4]`| "00" to "9999"        |
| `[n+]`   | At least n times        | `digit[2+]`  | "00", "000", ...      |
| `!`      | Negation                | `!\d`        | any non-digit char    |

### 1.3 Pattern Examples

```lambda
// Simple patterns
string binary = ("0" | "1")+                     // Binary number
string octal = "0" ("0" to "7")*                 // Octal number
string hex = "0x" hex_digit+                     // Hex number

// Common formats
string alpha = ("a" to "z" | "A" to "Z")+
string alphanumeric = ("a" to "z" | "A" to "Z" | "0" to "9")+
string whitespace = (" " | "\t" | "\n" | "\r")+

// Real-world patterns
string ip_octet = digit | digit digit | ("0" to "1") digit digit | "2" ("0" to "4") digit | "25" ("0" to "5")
string ipv4 = ip_octet "." ip_octet "." ip_octet "." ip_octet

string date_iso = digit[4] "-" digit[2] "-" digit[2]
string time_24h = ("0" to "1") digit | "2" ("0" to "3") ":" ("0" to "5") digit

// URL pattern
string scheme = alpha alphanumeric*
string url_path = ("/" alphanumeric*)*
string url = scheme "://" alphanumeric+ ("." alphanumeric+)* url_path?

// Email (simplified)
string local_part = (alphanumeric | "." | "_" | "-")+
string domain = alphanumeric+ ("." alphanumeric+)+
string email = local_part "@" domain
```

### 1.4 Character Classes (Syntactic Sugar)

For convenience, predefined character classes:

```lambda
// Built-in character classes
\d     // digit: "0" to "9"
\w     // word: "a" to "z" | "A" to "Z" | "0" to "9" | "_"
\s     // whitespace: " " | "\t" | "\n" | "\r"
\a     // alpha: "a" to "z" | "A" to "Z"

// Negation (using ! operator)
!\d    // non-digit (any character except 0-9)
!\w    // non-word (any character except letters, digits, underscore)
!\s    // non-whitespace

// Usage
string identifier = \a \w*
string trimmed = !\s (\s* !\s)*
```

### 1.5 Special Patterns

```lambda
// Any character
\.     // matches any single character
...    // matches zero or more of any character (shorthand for \.*)
```

> **Note:** Lambda patterns always perform full-match (the entire string must match the pattern). Start/end anchors (`^`, `$`) are automatically applied internally and are not exposed as user syntax.
>
> **Note:** Escape sequences like `\\`, `\"`, `\n`, `\t` are already supported in string literals and can be used directly in patterns (e.g., `"\t"` matches a tab character).

---

## 2. String Pattern as Type

### 2.1 Using Pattern Where Type is Expected

String patterns are types and can be used anywhere types are expected:

```lambda
// Type definition
string email = local_part '@' domain

// Parameter type
fn validate_email(addr: email) -> bool {
    // addr is guaranteed to match email pattern
    true
}

// Return type annotation
fn parse_email(s: string) -> email? {
    if (s is email) {
        s  // type narrowing: s is now email
    } else {
        null
    }
}

// Field type in schema
type User = {
    name: string,
    email: email,        // must match email pattern
    phone: phone_us?     // optional, but if present must match
}
```

### 2.2 Pattern Matching with `is` Operator

The `is` operator checks if a string matches a pattern:

```lambda
// Basic matching
"hello@example.com" is email       // true
"not-an-email" is email            // false

// In conditionals
if (input is email) {
    // input type-narrowed to email
    send_verification(input)
}

// In match expressions
match (code) {
    is binary => parse_binary(code),
    is hex => parse_hex(code),
    is octal => parse_octal(code),
    _ => parse_decimal(code)
}
```

### 2.3 Pattern Composition

Patterns can reference other patterns:

```lambda
string digit = "0" to "9"
string letter = "a" to "z" | "A" to "Z"

// Compose patterns
string alphanumeric = letter | digit
string identifier = (letter | "_") (alphanumeric | "_")*

// Extend patterns with intersection
string positive_int = digit+ & !("0" digit*)  // no leading zeros

// Pattern aliases
string varchar50 = .[1, 50]   // any string 1-50 chars
```

---

## 3. System Functions for String Patterns

### 3.1 Core Pattern Functions

| Function         | Signature                          | Description                             |
| ---------------- | ---------------------------------- | --------------------------------------- |
| `find(s, p)`     | `(string, pattern) -> [string]`    | Find all non-overlapping matches        |
| `find_at(s, p)`  | `(string, pattern) -> [(int, int)]`| Find all match positions (start, end)   |

```lambda
// Examples
// Use 'is' operator to check if string matches pattern
"hello123" is alphanumeric+                // true
"hello 123" is alphanumeric+               // false (space)

find("The price is $42.50", digit+)        // ["42", "50"]
find("a1b2c3", digit)                      // ["1", "2", "3"]

find_at("hello world", "o")                // [(4, 5), (7, 8)]
find_at("abab", "ab")                      // [(0, 2), (2, 4)]
```

### 3.2 Pattern Replacement

| Function            | Signature                                                   | Description                |
| ------------------- | ----------------------------------------------------------- | -------------------------- |
| `replace(s, p, r)`  | `(string, pattern, string \| (string)->string) -> string`   | Replace all matches        |

When the third argument is a string, it replaces all matches with that string. When the third argument is a function, the function is called for each match and its return value is used as the replacement.

```lambda
// Simple replacement
replace("hello world", "o", "0")           // "hell0 w0rld"

// Pattern replacement
replace("a1b2c3", digit, "X")              // "aXbXcX"

// With function (dynamic replacement)
replace("hello", ".", \c -> upper(c))      // "HELLO"
replace("a1b2", digit, \d -> string(int(d) * 2))  // "a2b4"
```

> **KIV**: `replace_first(s, p, r)` - Replace only the first match. Can be added if needed.

### 3.3 Pattern Splitting

| Function                  | Signature                                 | Description                     |
| ------------------------- | ----------------------------------------- | ------------------------------- |
| `split(s, p, keep?)`      | `(string, pattern, bool?) -> [string]`    | Split by pattern                |

The optional third argument `keep` (default: `false`) determines whether to keep the delimiters in the result.

```lambda
split("a,b;c:d", "," | ";" | ":")           // ["a", "b", "c", "d"]
split("hello   world", \s+)                 // ["hello", "world"]

// With keep_delimiter = true
split("a1b2c3", digit, true)                // ["a", "1", "b", "2", "c", "3"]
split("hello world", " ", true)             // ["hello", " ", "world"]
```

### 3.4 Pattern Validation - KIV

> **Status**: Keep In View - planned but not yet implemented.

| Function | Signature | Description |
|----------|-----------|-------------|
| `validate(s, p)` | `(string, pattern) -> ValidationResult` | Detailed validation result |

```lambda
type ValidationResult = {
    valid: bool,
    matched: string?,        // matched portion
    remainder: string?,      // unmatched portion
    position: int?,          // failure position
    expected: string?        // expected pattern at failure
}

let result = validate("hello@", email)
// {valid: false, matched: "hello@", remainder: "", position: 6, expected: "domain"}
```

---

## 4. Additional Features (for future)

### 4.1 Pattern Extraction (Capture Groups)

> **Status**: Deferred to future release.

Capture groups allow extracting named parts of a pattern match. This feature is planned but not yet implemented.

```lambda
// Future syntax (deferred)
string email_parts = @local:(alphanumeric | "." | "_")+ "@" @domain:alphanumeric+ ("." alphanumeric+)+
let result = capture("john.doe@example.com", email_parts)
// result = {local: "john.doe", domain: "example"}
```

### 4.2 Case-Insensitive Patterns

```lambda
// Case-insensitive modifier
string http_method = /i ("GET" | "POST" | "PUT" | "DELETE")

// Or using character ranges
string http_method = ("G"|"g")("E"|"e")("T"|"t") | ...
```

### 4.3 Pattern Literals in Expressions

Allow inline pattern literals:

```lambda
// Using / / syntax for inline patterns
if (s is /\d{3}-\d{4}/) {
    // matches phone-like pattern
}

// Named pattern is preferred for reuse
string phone_short = digit{3} '-' digit{4}
if (s is phone_short) { ... }
```

### 4.4 Unicode Support

```lambda
// Unicode character classes
\p{Letter}           // Any Unicode letter
\p{Number}           // Any Unicode number
\p{Emoji}            // Emoji characters
\p{Han}              // Chinese characters
\p{Hiragana}         // Japanese Hiragana

// Example
string cjk_text = \p{Han}+
string mixed = (\p{Letter} | \p{Number})+
```

### 4.5 Pattern Type Coercion

When a string is assigned to a pattern type, implicit validation occurs:

```lambda
fn process(e: email) { ... }

// Compile-time validation (if literal)
process("invalid")                    // Compile error: doesn't match email

// Runtime validation
let s = read_input()
process(s)                            // Runtime error if s doesn't match

// Explicit conversion
let e: email = s as email            // Throws if invalid
let e: email? = s as? email          // Returns null if invalid
```

### 4.6 Pattern Debugging

```lambda
// Get regex representation
pattern_to_regex(email)               // "^[a-zA-Z0-9._-]+@[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)+$"

// Test pattern step-by-step
pattern_trace("john@example.com", email)
// Returns trace of matching steps for debugging
```

---

## 5. Implementation

### 5.1 Compilation Strategy

String patterns are compiled to regular expressions at compile time:

```
Lambda Pattern → Pattern AST → Regex String → Compiled Regex
```

**Example compilation:**

```lambda
// Lambda pattern
string identifier = ("a" to "z" | "A" to "Z" | "_") ("a" to "z" | "A" to "Z" | "0" to "9" | "_")*

// Compiled to regex
"^[a-zA-Z_][a-zA-Z0-9_]*$"
```

### 5.2 Pattern AST

Extend Lambda's Type AST to include pattern-specific nodes:

```cpp
// New AST node types
typedef struct PatternChar {
    TypeId type_id;  // LMD_TYPE_PATTERN_CHAR
    uint32_t codepoint;
} PatternChar;

typedef struct PatternRange {
    TypeId type_id;  // LMD_TYPE_PATTERN_RANGE
    uint32_t start;
    uint32_t end;
} PatternRange;

typedef struct PatternRepeat {
    TypeId type_id;  // LMD_TYPE_PATTERN_REPEAT
    Type* operand;
    int32_t min;
    int32_t max;     // -1 for unlimited
} PatternRepeat;

typedef struct PatternCapture {
    TypeId type_id;  // LMD_TYPE_PATTERN_CAPTURE
    StrView* name;
    Type* pattern;
} PatternCapture;

typedef struct TypeStringPattern {
    TypeId type_id;  // LMD_TYPE_STRING_PATTERN
    StrView* name;
    Type* pattern;         // Pattern AST
    void* compiled_regex;  // Compiled regex (library-specific)
} TypeStringPattern;
```

### 5.3 Regex Library Recommendation

**Recommended: RE2**

| Library | Pros | Cons |
|---------|------|------|
| **RE2** (Google) | Linear-time guarantee, safe, good Unicode | No backreferences |
| **PCRE2** | Full Perl regex, powerful | Exponential worst-case |
| **std::regex** | Standard library | Slow, inconsistent |
| **Oniguruma** | Unicode, used by Ruby | Less common |
| **Hyperscan** (Intel) | Extremely fast, multi-pattern | Complex, x86 only |

**RE2 Rationale:**
1. **Performance**: Linear time complexity O(n) - no catastrophic backtracking
2. **Safety**: Bounded memory, predictable execution time
3. **Unicode**: Full Unicode support with UTF-8
4. **C API**: Clean C interface, easy to integrate
5. **Battle-tested**: Used by Google in production
6. **License**: BSD-3, compatible with Lambda's MIT license

**Integration:**

```cpp
// Compile pattern to RE2
#include <re2/re2.h>

TypeStringPattern* compile_pattern(const char* pattern_str) {
    TypeStringPattern* pat = pool_alloc<TypeStringPattern>();
    pat->type_id = LMD_TYPE_STRING_PATTERN;
    
    std::string regex = pattern_to_regex(pattern_str);
    pat->compiled_regex = new RE2(regex, RE2::Options().set_utf8(true));
    
    if (!static_cast<RE2*>(pat->compiled_regex)->ok()) {
        log_error("invalid pattern: %s", pattern_str);
        return nullptr;
    }
    return pat;
}

// Runtime matching
bool pattern_match(TypeStringPattern* pat, const char* str) {
    RE2* re = static_cast<RE2*>(pat->compiled_regex);
    return RE2::FullMatch(str, *re);
}
```

### 5.4 Grammar Extension

Extend `lambda/tree-sitter-lambda/grammar.js`:

```javascript
// New pattern-specific rules
string_pattern: $ => seq(
    'string', 
    field('name', $.identifier), 
    '=', 
    field('pattern', $._pattern_expr)
),

_pattern_expr: $ => choice(
    $.pattern_char,
    $.pattern_range,
    $.pattern_group,
    $.pattern_repetition,
    $.pattern_alternation,
    $.pattern_capture,
    $.identifier,  // reference to another pattern
),

pattern_char: $ => choice(
    $.string,      // "abc" or "x"
    $.char_class,  // \d, \w, \s, etc.
),

pattern_range: $ => seq(
    field('start', $.string),
    'to',
    field('end', $.string)
),

pattern_group: $ => seq('(', $._pattern_expr, ')'),

pattern_repetition: $ => choice(
    seq(field('operand', $._pattern_expr), '*'),
    seq(field('operand', $._pattern_expr), '+'),
    seq(field('operand', $._pattern_expr), '?'),
    seq(field('operand', $._pattern_expr), '[', field('count', $.integer), ']'),
    seq(field('operand', $._pattern_expr), '[', field('min', $.integer), ',', field('max', $.integer), ']'),
    seq(field('operand', $._pattern_expr), '[', field('min', $.integer), '+', ']'),
),

pattern_alternation: $ => prec.left('set_union', seq(
    field('left', $._pattern_expr),
    '|',
    field('right', $._pattern_expr)
)),

pattern_capture: $ => seq(
    '@', field('name', $.identifier), ':', field('pattern', $._pattern_expr)
),

char_class: $ => token(choice(
    '\\d', '\\w', '\\s', '\\a', '.',
    seq('\\p{', /[A-Za-z_]+/, '}'),
)),
```

---

## 6. Integration with Existing Features

### 6.1 Schema Validation

String patterns integrate with Lambda's schema validation:

```lambda
// Schema using string patterns
string email = local_part '@' domain
string phone = digit[3] '-' digit[3] '-' digit[4]

type User = {
    name: string,
    email: email,          // validated against pattern
    phone: phone?,
    zip: digit[5]          // inline pattern
}

// Validation
validate_schema(user_data, User)
// Returns detailed errors for pattern mismatches
```

### 6.2 Type Inference

```lambda
// String literals matching a pattern get the pattern type
string url = scheme "://" ...

let google = "https://google.com"   // inferred type: string (not url)
let google: url = "https://google.com"  // type: url, validated

// Pattern-typed parameters enable validation
fn fetch(u: url) { ... }
fetch("not-a-url")                  // Compile/runtime error
```

### 6.3 Pattern-Based Overloading (KIV)

Future consideration: function overloading based on string patterns:

```lambda
fn parse(s: hex) -> int { /* parse hex */ }
fn parse(s: binary) -> int { /* parse binary */ }
fn parse(s: digit+) -> int { /* parse decimal */ }

parse("0xFF")    // calls hex version
parse("0b1010")  // calls binary version (if binary pattern matches)
parse("123")     // calls decimal version
```

---

## 7. Examples

### 7.1 Configuration Validation

```lambda
// Define config patterns
string semver = digit+ "." digit+ "." digit+ ("-" alphanumeric+)?
string env_var = ("A" to "Z" | '_)+
string file_path = ("/" | "./")? (alphanumeric | '_' | '-' | '/')+  ("." alphanumeric+)?

type Config = {
    version: semver,
    environment: "dev" | "staging" | "prod",
    log_level: "debug" | "info" | "warn" | "error",
    api_key: env_var,
    config_path: file_path
}
```

### 7.2 Log Parsing

```lambda
string timestamp = digit[4] "-" digit[2] "-" digit[2] " " digit[2] ":" digit[2] ":" digit[2]
string log_level = "DEBUG" | "INFO" | "WARN" | "ERROR"
string log_line = @ts:timestamp " " @level:log_level " " @msg:.+

fn parse_log(line: string) {
    match capture(line, log_line) {
        {ts, level, msg} => LogEntry{ts: parse_datetime(ts), level, msg},
        null => null
    }
}
```

### 7.3 Input Sanitization

```lambda
string safe_html = (!("<" | ">" | "&" | '"' | "'"))+

fn sanitize(input: string) -> safe_html {
    replace(input, "<", "&lt;")
    |> replace(_, ">", "&gt;")
    |> replace(_, "&", "&amp;")
    |> replace(_, '"', "&quot;")
    |> replace(_, "'", "&#39;")
}
```

---

## 8. Migration and Compatibility

### 8.1 Backward Compatibility

- Existing `string` type continues to work unchanged
- New `string name = pattern` syntax is additive
- `is` operator for patterns extends existing type checking

### 8.2 Gradual Adoption

```lambda
// Phase 1: Use patterns for validation
if (email is email_pattern) { ... }

// Phase 2: Use patterns as types
fn send_email(to: email_pattern) { ... }

// Phase 3: Schema integration
type User = { email: email_pattern }
```

---

## 9. Open Questions

1. **Error Messages**: How detailed should pattern mismatch errors be?
2. **Performance**: Should patterns be cached/interned like symbols?
3. **Debugging**: How to visualize pattern matching for debugging?
4. **Recursion**: Should recursive patterns be allowed (for nested structures)?
5. **Lookahead**: Should lookahead/lookbehind be supported despite RE2 limitations?

---

## 10. Summary

### 10.1 String Patterns

| Feature    | Syntax                  | Description                            |
| ---------- | ----------------------- | -------------------------------------- |
| Definition | `string name = pattern` | Define named string pattern            |
| Matching   | `s is pattern`          | Check if string matches                |
| As type    | `fn f(p: pattern)`      | Use pattern as type constraint         |
| Find       | `find(s, p)`            | Find all matches                       |
| Find At    | `find_at(s, p)`         | Find all match positions               |
| Replace    | `replace(s, p, r)`      | Replace matches (r: string or function)|
| Split      | `split(s, p, keep?)`    | Split by pattern                       |

### 10.2 Symbol Patterns

| Feature | Syntax | Description |
|---------|--------|-------------|
| Definition | `symbol name = pattern` | Define named symbol pattern |
| Matching | `sym is pattern` | Check if symbol matches |
| As type | `fn f(p: pattern)` | Use pattern as type constraint |
| Range | `'a to 'z` | Character range for symbols |

**Recommended regex library: RE2** for its linear-time guarantee, safety, and Unicode support.

---

## 11. Symbol Pattern Extension

### 11.1 Motivation

Lambda distinguishes between **strings** (double-quoted, for content data) and **symbols** (single-quoted, for identifiers and structural values). Just as string patterns provide type-safe validation for string content, **symbol patterns** can provide the same benefits for symbol values.

**Use cases for symbol patterns:**
- Validating identifier naming conventions
- Ensuring consistent key formats in maps/elements
- Type-safe enum-like symbol constraints
- API field name validation

### 11.2 Symbol Pattern Syntax

Symbol patterns use the `symbol` keyword instead of `string`:

```lambda
// Basic symbol pattern definitions
symbol identifier = ('a to 'z | 'A to 'Z | '_) ('a to 'z | 'A to 'Z | '0 to '9 | '_)*

symbol snake_case = ('a to 'z) ('a to 'z | '0 to '9 | '_)*
symbol camelCase = ('a to 'z) ('a to 'z | 'A to 'Z | '0 to '9)*
symbol SCREAMING_SNAKE = ('A to 'Z) ('A to 'Z | '0 to '9 | '_)*

// HTML/XML element names
symbol html_tag = ('a to 'z)+ ('-('a to 'z)+)*
symbol xml_name = ('a to 'z | 'A to 'Z | '_) ('a to 'z | 'A to 'Z | '0 to '9 | '_ | '- | '.)*
```

### 11.3 Pattern Operators for Symbols

Symbol patterns use the same operators as string patterns, but with symbol literals:

| Operator | Meaning | Example | Matches |
|----------|---------|---------|---------|
| `\|` | Alternation | `'a \| 'b` | 'a or 'b |
| `*` | Zero or more | `'a*` | ', 'a, 'aa, ... |
| `+` | One or more | `'a+` | 'a, 'aa, 'aaa, ... |
| `?` | Optional (0 or 1) | `'a?` | ' or 'a |
| `to` | Character range | `'a to 'z` | 'a, 'b, 'c, ..., 'z |
| `[n]` | Exactly n times | `('a to 'z)[3]` | 'aaa to 'zzz |
| `[n, m]` | Between n and m times | `('a to 'z)[2, 4]` | 'aa to 'zzzz |
| `[n+]` | At least n times | `('a to 'z)[2+]` | 'aa, 'aaa, ... |

### 11.4 Using Symbol Patterns as Types

```lambda
// Type definition
symbol field_name = snake_case

// Parameter type
fn validate_field(name: field_name) -> bool {
    // name is guaranteed to match snake_case pattern
    true
}

// Schema field validation
type APIResponse = {
    status: 'success | 'error | 'pending,
    data: any,
    @keys: snake_case    // all keys must match snake_case pattern
}

// Element attribute names
element Config {
    @attrs: SCREAMING_SNAKE   // attribute names must be SCREAMING_SNAKE
}
```

### 11.5 Symbol Pattern Matching

```lambda
// Check if symbol matches pattern
'myVariable is camelCase        // true
'my_variable is snake_case      // true
'MY_CONSTANT is SCREAMING_SNAKE // true

// In conditionals
if (field_name is snake_case) {
    // field_name type-narrowed to snake_case
    process_field(field_name)
}

// Pattern-based dispatch
match (naming_convention) {
    is snake_case => format_snake(name),
    is camelCase => format_camel(name),
    is SCREAMING_SNAKE => format_screaming(name),
    _ => name
}
```

### 11.6 Differences from String Patterns

| Aspect | String Pattern | Symbol Pattern |
|--------|----------------|----------------|
| **Keyword** | `string` | `symbol` |
| **Literals** | `"a"`, `"abc"` | `'a`, `'abc` |
| **Range** | `"a" to "z"` | `'a to 'z` |
| **Use case** | Content validation | Identifier validation |
| **Typical length** | Variable, often long | Short (identifiers) |
| **Pooling** | Never | ≤32 chars pooled |

### 11.7 Shared Functions

Most pattern functions work for both string and symbol patterns:

| Function | String Pattern | Symbol Pattern |
|----------|----------------|----------------|
| `match(s, p)` | ✅ | ✅ |
| `find(s, p)` | ✅ | ✅ |
| `capture(s, p)` | ✅ | ✅ (returns symbols) |
| `validate(s, p)` | ✅ | ✅ |

### 11.8 Implementation Notes

Symbol patterns can share the same compilation infrastructure as string patterns:

```cpp
typedef struct TypeSymbolPattern {
    TypeId type_id;  // LMD_TYPE_SYMBOL_PATTERN
    StrView* name;
    Type* pattern;         // Pattern AST (shared with string patterns)
    void* compiled_regex;  // Same RE2 backend
} TypeSymbolPattern;
```

The only difference is:
1. Input/output types are symbols instead of strings
2. The grammar accepts symbol literals (`'x'`) instead of string literals (`"x"`)
3. Capture groups return symbol values

---

## References

- [Lambda Type System](./Lambda_Schema.md)
- [Lambda String Type](./Lambda_Type_String.md)
- [RE2 Documentation](https://github.com/google/re2)
- [Unicode Technical Standard #18: Unicode Regular Expressions](https://unicode.org/reports/tr18/)

---

## Appendix: Implementation Summary

### A.1 Architecture Overview

```
Lambda Pattern Source → Tree-sitter Parser → Pattern AST → RE2 Regex → Compiled Pattern
```

**Key Components:**
- **Grammar**: `lambda/tree-sitter-lambda/grammar.js` - pattern syntax rules
- **AST**: `lambda/ast.hpp` - node types (`AstPatternDefNode`, `AstPatternCharClassNode`, `AstPatternRangeNode`)
- **Builder**: `lambda/build_ast.cpp` - AST construction from parse tree
- **Compiler**: `lambda/re2_wrapper.cpp` - pattern-to-regex conversion and RE2 compilation
- **Runtime**: `lambda/lambda-eval.cpp` - `fn_is()` pattern matching

### A.2 Type System Integration

```cpp
// New type ID
LMD_TYPE_PATTERN = 21

// Pattern type structure
typedef struct TypePattern : Type {
    int pattern_index;   // index in type_list for runtime lookup
    bool is_symbol;      // symbol vs string pattern
} TypePattern;

// Character classes
enum PatternCharClass { PATTERN_DIGIT, PATTERN_WORD, PATTERN_SPACE, PATTERN_ALPHA, PATTERN_ANY };
```

### A.3 Pattern-to-Regex Mapping

| Lambda Pattern | Regex Output |
|----------------|--------------|
| `"abc"` | `abc` (escaped) |
| `\d` | `[0-9]` |
| `\w` | `[a-zA-Z0-9_]` |
| `\s` | `\s` |
| `\a` | `[a-zA-Z]` |
| `\.` | `.` |
| `...` | `.*` |
| `"a" to "z"` | `[a-z]` |
| `a \| b` | `(?:a\|b)` |
| `a?` | `(?:a)?` |
| `a+` | `(?:a)+` |
| `a*` | `(?:a)*` |
| `a[3]` | `(?:a){3}` |
| `a[2, 4]` | `(?:a){2,4}` |
| `a[2+]` | `(?:a){2,}` |

Full-match anchors (`^...$`) are automatically added.

### A.4 Key Files

| File | Purpose |
|------|---------|
| `lambda/re2_wrapper.hpp/cpp` | RE2 integration, pattern compilation |
| `lambda/ast.hpp` | AST node types, symbol macros |
| `lambda/build_ast.cpp` | Pattern AST building |
| `lambda/lambda-eval.cpp` | `fn_is()` runtime matching |
| `lambda/mir.c` | `const_pattern()` registration |
| `build_lambda_config.json` | RE2 library linking |

### A.5 Runtime Flow

1. **Compile-time**: Pattern AST → regex string → RE2 compilation → stored in `type_list`
2. **Runtime**: `const_pattern(index)` retrieves compiled pattern
3. **Matching**: `fn_is(string, pattern)` calls `RE2::FullMatch()`

### A.6 Limitations

- **No backreferences**: RE2 guarantees linear-time matching
- **No lookahead/lookbehind**: RE2 limitation (intersection `&` uses workaround)
- **Empty string**: Lambda treats `""` as null, pattern match returns error
