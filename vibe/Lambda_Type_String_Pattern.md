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
string identifier = ('a' to 'z' | 'A' to 'Z' | '_') ('a' to 'z' | 'A' to 'Z' | '0' to '9' | '_')*

string digit = '0' to '9'
string hex_digit = digit | 'a' to 'f' | 'A' to 'F'

string email = identifier '@' identifier '.' identifier

// Using occurrence operators
string phone_us = digit{3} '-' digit{3} '-' digit{4}
```

### 1.2 Pattern Operators

String patterns reuse Lambda's existing type pattern operators:

| Operator | Meaning | Example | Matches |
|----------|---------|---------|---------|
| `\|` | Alternation | `"a" \| "b"` | "a" or "b" |
| `*` | Zero or more | `"a"*` | "", "a", "aa", ... |
| `+` | One or more | `"a"+` | "a", "aa", "aaa", ... |
| `?` | Optional (0 or 1) | `"a"?` | "" or "a" |
| `&` | Concatenation (implicit) | `"a" "b"` | "ab" |
| `to` | Character range | `'a' to 'z'` | a, b, c, ..., z |
| `{n}` | Exactly n times | `digit{3}` | "000" to "999" |
| `{n,m}` | Between n and m times | `digit{2,4}` | "00" to "9999" |

### 1.3 Pattern Examples

```lambda
// Simple patterns
string binary = ('0' | '1')+                     // Binary number
string octal = '0' ('0' to '7')*                 // Octal number
string hex = "0x" hex_digit+                     // Hex number

// Common formats
string alpha = ('a' to 'z' | 'A' to 'Z')+
string alphanumeric = ('a' to 'z' | 'A' to 'Z' | '0' to '9')+
string whitespace = (' ' | '\t' | '\n' | '\r')+

// Real-world patterns
string ip_octet = digit | digit digit | ('0' to '1') digit digit | '2' ('0' to '4') digit | "25" ('0' to '5')
string ipv4 = ip_octet '.' ip_octet '.' ip_octet '.' ip_octet

string date_iso = digit{4} '-' digit{2} '-' digit{2}
string time_24h = ('0' to '1') digit | '2' ('0' to '3') ':' ('0' to '5') digit

// URL pattern
string scheme = alpha alphanumeric*
string url_path = ('/' alphanumeric*)*
string url = scheme "://" alphanumeric+ ('.' alphanumeric+)* url_path?

// Email (simplified)
string local_part = (alphanumeric | '.' | '_' | '-')+
string domain = alphanumeric+ ('.' alphanumeric+)+
string email = local_part '@' domain
```

### 1.4 Character Classes (Syntactic Sugar)

For convenience, predefined character classes:

```lambda
// Built-in character classes
\d     // digit: '0' to '9'
\w     // word: 'a' to 'z' | 'A' to 'Z' | '0' to '9' | '_'
\s     // whitespace: ' ' | '\t' | '\n' | '\r'
\a     // alpha: 'a' to 'z' | 'A' to 'Z'

// Negation
\D     // non-digit
\W     // non-word
\S     // non-whitespace

// Usage
string identifier = \a \w*
string trimmed = \S (\s* \S)*
```

### 1.5 Special Patterns

```lambda
// Any character
.      // matches any single character

// Start/end anchors (for full-match vs partial-match)
^      // start of string
$      // end of string

// Escaped special characters
\\     // literal backslash
\'     // literal single quote
\"     // literal double quote
\n     // newline
\t     // tab
```

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
string digit = '0' to '9'
string letter = 'a' to 'z' | 'A' to 'Z'

// Compose patterns
string alphanumeric = letter | digit
string identifier = (letter | '_') (alphanumeric | '_')*

// Extend patterns with intersection
string positive_int = digit+ & !('0' digit*)  // no leading zeros

// Pattern aliases
string varchar50 = . {1,50}   // any string 1-50 chars
```

---

## 3. System Functions for String Patterns

### 3.1 Core Pattern Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `match(s, p)` | `(string, pattern) -> bool` | Check if entire string matches pattern |
| `find(s, p)` | `(string, pattern) -> string?` | Find first match, return matched substring |
| `find_all(s, p)` | `(string, pattern) -> [string]` | Find all non-overlapping matches |
| `find_at(s, p)` | `(string, pattern) -> (int, int)?` | Find first match position (start, end) |
| `find_all_at(s, p)` | `(string, pattern) -> [(int, int)]` | Find all match positions |

```lambda
// Examples
match("hello123", alphanumeric+)           // true
match("hello 123", alphanumeric+)          // false (space)

find("The price is $42.50", digit+)        // "42"
find_all("a1b2c3", digit)                  // ["1", "2", "3"]

find_at("hello world", "world")            // (6, 11)
find_all_at("abab", "ab")                  // [(0, 2), (2, 4)]
```

### 3.2 Pattern Extraction (Capture Groups)

Patterns can define named capture groups:

```lambda
// Named captures with @
string email_parts = @local:(alphanumeric | '.' | '_')+ '@' @domain:alphanumeric+ ('.' alphanumeric+)+

// Extract captures
let result = capture("john.doe@example.com", email_parts)
// result = {local: "john.doe", domain: "example"}

// Multiple captures
string date = @year:digit{4} '-' @month:digit{2} '-' @day:digit{2}
capture("2024-01-27", date)
// {year: "2024", month: "01", day: "27"}
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `capture(s, p)` | `(string, pattern) -> {string: string}?` | Extract named captures |
| `capture_all(s, p)` | `(string, pattern) -> [{string: string}]` | Extract all matches with captures |

### 3.3 Pattern Replacement

| Function | Signature | Description |
|----------|-----------|-------------|
| `replace(s, p, r)` | `(string, pattern, string) -> string` | Replace all matches |
| `replace_first(s, p, r)` | `(string, pattern, string) -> string` | Replace first match |
| `replace_with(s, p, fn)` | `(string, pattern, (string)->string) -> string` | Replace with function |

```lambda
// Simple replacement
replace("hello world", "o", "0")           // "hell0 w0rld"

// Pattern replacement
replace("a1b2c3", digit, "X")              // "aXbXcX"

// With capture references
string swap = @a:\w+ ' ' @b:\w+
replace("hello world", swap, "$b $a")      // "world hello"

// With function
replace_with("hello", '.', \c -> upper(c)) // "HELLO"
```

### 3.4 Pattern Splitting

| Function | Signature | Description |
|----------|-----------|-------------|
| `split(s, p)` | `(string, pattern) -> [string]` | Split by pattern |
| `split_keep(s, p)` | `(string, pattern) -> [string]` | Split, keeping delimiters |

```lambda
split("a,b;c:d", ',' | ';' | ':')          // ["a", "b", "c", "d"]
split("hello   world", \s+)                 // ["hello", "world"]

split_keep("a1b2c3", digit)                 // ["a", "1", "b", "2", "c", "3"]
```

### 3.5 Pattern Validation

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

## 4. Additional Features

### 4.1 Case-Insensitive Patterns

```lambda
// Case-insensitive modifier
string http_method = /i ("GET" | "POST" | "PUT" | "DELETE")

// Or using character ranges
string http_method = ('G'|'g')('E'|'e')('T'|'t') | ...
```

### 4.2 Pattern Literals in Expressions

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

### 4.3 Unicode Support

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

### 4.4 Pattern Type Coercion

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

### 4.5 Pattern Debugging

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
string identifier = ('a' to 'z' | 'A' to 'Z' | '_') ('a' to 'z' | 'A' to 'Z' | '0' to '9' | '_')*

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
    $.string,      // "abc" or 'x'
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
    seq(field('operand', $._pattern_expr), '{', field('count', $.integer), '}'),
    seq(field('operand', $._pattern_expr), '{', field('min', $.integer), ',', field('max', $.integer), '}'),
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
    '\\d', '\\D', '\\w', '\\W', '\\s', '\\S', '\\a', '.',
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
string phone = digit{3} '-' digit{3} '-' digit{4}

type User = {
    name: string,
    email: email,          // validated against pattern
    phone: phone?,
    zip: digit{5}          // inline pattern
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
string semver = digit+ '.' digit+ '.' digit+ ('-' alphanumeric+)?
string env_var = ('A' to 'Z' | '_')+
string file_path = ('/' | './')? (alphanumeric | '_' | '-' | '/')+ ('.' alphanumeric+)?

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
string timestamp = digit{4} '-' digit{2} '-' digit{2} ' ' digit{2} ':' digit{2} ':' digit{2}
string log_level = "DEBUG" | "INFO" | "WARN" | "ERROR"
string log_line = @ts:timestamp ' ' @level:log_level ' ' @msg:.+

fn parse_log(line: string) {
    match capture(line, log_line) {
        {ts, level, msg} => LogEntry{ts: parse_datetime(ts), level, msg},
        null => null
    }
}
```

### 7.3 Input Sanitization

```lambda
string safe_html = (!('<' | '>' | '&' | '"' | "'"))+

fn sanitize(input: string) -> safe_html {
    replace(input, '<', "&lt;")
    |> replace(_, '>', "&gt;")
    |> replace(_, '&', "&amp;")
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

| Feature | Syntax | Description |
|---------|--------|-------------|
| Definition | `string name = pattern` | Define named string pattern |
| Matching | `s is pattern` | Check if string matches |
| As type | `fn f(p: pattern)` | Use pattern as type constraint |
| Find | `find(s, p)` | Find first match |
| Replace | `replace(s, p, r)` | Replace matches |
| Capture | `capture(s, p)` | Extract named groups |
| Split | `split(s, p)` | Split by pattern |

**Recommended regex library: RE2** for its linear-time guarantee, safety, and Unicode support.

---

## References

- [Lambda Type System](./Lambda_Schema.md)
- [Lambda String Type](./Lambda_Type_String.md)
- [RE2 Documentation](https://github.com/google/re2)
- [Unicode Technical Standard #18: Unicode Regular Expressions](https://unicode.org/reports/tr18/)
