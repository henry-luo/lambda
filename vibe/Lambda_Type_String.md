# Lambda Type: String and Symbol

## Executive Summary

This document proposes enhancements to Lambda's string and symbol types, including:
1. Documentation of existing string/symbol functionality
2. Proposed new system functions for string manipulation
3. Symbol-specific considerations
4. Implementation recommendations

---

## 1. Existing String Support

> **Note**: Lambda currently does **not** support member function syntax (e.g., `str.method()`). All string functions are global functions. Member function support will be added in a future release.

### 1.1 String Type Overview

| Property | Description |
|----------|-------------|
| **Type ID** | `LMD_TYPE_STRING` |
| **Encoding** | UTF-8 (internal byte storage) |
| **Indexing** | Character-based (Unicode code points), not byte-based |
| **Memory** | Heap-allocated, reference counted |
| **Immutability** | All operations return new strings |

### 1.2 Unicode Handling

Lambda stores strings internally as UTF-8 bytes but exposes a **character-based** (Unicode code point) interface to users.

#### Key Principles

| Aspect | Behavior |
|--------|----------|
| **Storage** | UTF-8 encoded bytes |
| **Indexing unit** | Unicode code point (not byte) |
| **Length unit** | Unicode code point count |
| **Negative index** | Counts from end (-1 = last char) |

#### Examples

```lambda
let s = "Hello, 世界!"

// Length is character count, not byte count
len(s)           // 10 (not 14 bytes)

// Indexing is character-based
s[7]             // "世" (not byte 7)
s[8]             // "界"
```

#### Negative Indexing (KIV)

> **Status**: Keep In View - planned but not yet implemented.

```lambda
// Future support for negative indexing
s[-1]            // Last character
s[-3]            // Third from end
s[0 to -2]       // All except last character
```

#### Implementation Notes

1. **ASCII fast path**: Check if all bytes are < 128; if so, byte offset = character offset (O(1) indexing)
2. **UTF-8 slow path**: Use `utf8_char_to_byte_offset()` to convert character index to byte offset
3. **Library**: Use `utf8proc` for Unicode operations (normalization, case mapping)
4. **Performance**: Cache character-to-byte mapping for repeated access on same string (future optimization)

#### Unicode Levels

| Level | Description | Use Case |
|-------|-------------|----------|
| **Code Point** | Single Unicode scalar value (U+0000 to U+10FFFF) | Default indexing unit |
| **Grapheme Cluster** | User-perceived character (e.g., `é` = `e` + `́`) | Future: `graphemes(s)` function |
| **Byte** | Raw UTF-8 byte | Low-level: `binary(s)` conversion |

**Current implementation**: Uses code points. Grapheme cluster support can be added later via dedicated functions.

#### Edge Cases

```lambda
// Emoji with modifiers (multiple code points)
let emoji = "👨‍👩‍👧"      // Family emoji (7 code points: 👨 + ZWJ + 👩 + ZWJ + 👧)
len(emoji)            // 7 (code points), not 1 (grapheme)

// Combining characters
let accent = "é"      // Could be 1 or 2 code points depending on normalization
normalize(accent, 'nfc')  // Ensures single code point form
```

### 1.3 String Literals

```lambda
// Basic string literals
"Hello, World!"
"Unicode: 你好世界 🎉"

// Escape sequences
"Line 1\nLine 2"      // newline
"Tab\there"           // tab
"Quote: \"text\""     // escaped quote
```

### 1.3 Current String Operators

| Operator      | Syntax        | Description            | Example               | Result          |
| ------------- | ------------- | ---------------------- | --------------------- | --------------- |
| Concatenation | `a ++ b`      | Join two strings       | `"Hello" ++ " World"` | `"Hello World"` |
| Index         | `str[i]`      | Get character at index | `"hello"[1]`          | `"e"`           |
| Range Index   | `str[i to j]` | Get substring [i, j]   | `"hello"[1 to 3]`     | `"ell"`         |
| Length        | `len(str)`    | Get character count    | `len("你好")`           | `2`             |

#### Design Decision: No String Repeat Operator

Lambda intentionally does **not** support `str * n` for string repetition.

**Rationale**:
1. **Type clarity**: `*` is kept purely numeric - it always returns a number
2. **Predictable semantics**: Overloading `*` for strings creates ambiguity (`3 * x` - is `x` a number or string?)
3. **Explicit is better**: Use a dedicated function if string repetition is needed

**Alternative** (if needed in future):
```lambda
// Could add a repeat() function instead
repeat("ab", 3)    // "ababab"
```

### 1.4 Current String System Functions

| Function | Signature | Description | Example | Result |
|----------|-----------|-------------|---------|--------|
| `string(x)` | `any -> string` | Convert to string | `string(42)` | `"42"` |
| `len(s)` | `string -> int` | Character count (UTF-8) | `len("hello")` | `5` |
| `slice(s, start, end)` | `(string, int, int) -> string` | Extract substring [start, end) | `slice("hello", 1, 4)` | `"ell"` |
| `contains(s, sub)` | `(string, string) -> bool` | Check if contains substring | `contains("hello", "ell")` | `true` |
| `normalize(s, form)` | `(string, symbol) -> string` | Unicode normalization | `normalize("é", 'nfc')` | normalized string |

#### 1.4.1 `slice` Details

The `slice(s, start, end)` function is consistent with `slice()` for arrays and lists.

Supports negative indexing:
```lambda
let s = "Hello, World!"
slice(s, 0, 5)       // "Hello"
slice(s, 7, 12)      // "World"
slice(s, 0, -1)      // "Hello, World" (excludes last char)
slice(s, -6, -1)     // "World"
```

#### 1.4.2 `normalize` Details

Unicode normalization forms:
```lambda
normalize(text, 'nfc')   // Canonical Composition (most common)
normalize(text, 'nfd')   // Canonical Decomposition
normalize(text, 'nfkc')  // Compatibility Composition
normalize(text, 'nfkd')  // Compatibility Decomposition
```

### 1.5 String Concatenation Behavior

The `++` operator (OPERATOR_JOIN) handles mixed types:
```lambda
"value: " ++ 42        // "value: 42" (auto-converts int to string)
"pi = " ++ 3.14        // "pi = 3.14"
"flag: " ++ true       // "flag: true"
```

### 1.6 String Indexing and Slicing

Strings support both single-character indexing and range-based slicing:

```lambda
let s = "Hello, 世界!"

// Single character access (returns string of length 1)
s[0]          // "H"
s[7]          // "世"

// Range-based slicing (returns substring)
s[0 to 4]     // "Hello" (inclusive range)
s[7 to 8]     // "世界"
```

**Design Decision**: Indexing returns a **string** (not a char type) to keep the type system simple. A single character is just a string of length 1.

> **Note**: Negative indexing (`s[-1]`, `s[0 to -2]`) is KIV (Keep In View) - planned but not yet implemented.

---

## 2. Symbol Type Overview

### 2.1 Symbol Basics

| Property | Description |
|----------|-------------|
| **Type ID** | `LMD_TYPE_SYMBOL` |
| **Syntax** | `'symbolName` (single quote prefix) |
| **Pooling** | Conditionally pooled (≤32 chars) in NamePool |
| **Use Case** | Identifiers, enum-like values, keys |

### 2.2 Symbol Literals

```lambda
'hello              // simple symbol
'mySymbol           // camelCase symbol
'json               // format specifier
'nfc                // normalization form
```

### 2.3 Symbol vs String

| Aspect | String | Symbol |
|--------|--------|--------|
| **Literal** | `"text"` | `'text` |
| **Purpose** | Content data | Structural identifiers |
| **Pooling** | Never | If ≤32 chars |
| **Memory** | Arena allocated | Pool + pointer sharing |
| **Comparison** | Character-by-character | Pointer (if pooled) |
| **Use case** | User content, text | Keys, tags, enums |

### 2.4 Symbol Operations

```lambda
// Conversion
symbol("text")      // string to symbol: 'text
string('text)       // symbol to string: "text"

// Concatenation (returns symbol)
'hello ++ 'world    // 'helloworld
```

### 2.5 Symbol Indexing

Symbols support the same indexing syntax as strings:

```lambda
let sym = 'helloWorld

// Single character access (returns symbol of length 1)
sym[0]            // 'h
sym[5]            // 'W

// Range-based slicing (returns symbol)
sym[0 to 4]       // 'hello
sym[5 to 9]       // 'World
```

**Note**: Symbol indexing returns a symbol, preserving the type. This is consistent with string indexing returning a string.

---

## 3. Proposed New String Functions

### 3.1 Search & Match Functions

| Function | Signature | Description | Symbol? |
|----------|-----------|-------------|---------|
| `starts_with(s, prefix)` | `(string, string) -> bool` | Check prefix | ✅ |
| `ends_with(s, suffix)` | `(string, string) -> bool` | Check suffix | ✅ |
| `index_of(s, sub)` | `(string, string) -> int` | First occurrence (-1 if not found) | ✅ |
| `last_index_of(s, sub)` | `(string, string) -> int` | Last occurrence (-1 if not found) | ✅ |

```lambda
// Examples
starts_with("hello.txt", "hello")   // true
ends_with("hello.txt", ".txt")      // true
index_of("hello world", "o")        // 4
last_index_of("hello world", "o")   // 7
```

> **KIV**: `count(s, sub)` - Count occurrences of substring

**Priority**: HIGH - These are fundamental string operations missing from Lambda.

> **Design Note**: Many global string functions (`index_of`, `starts_with`, `ends_with`, `contains`, `replace`, etc.) can be extended to other collection types (arrays, lists) in the future. For example: `index_of([1,2,3], 2)` to find element position, `replace([1,2,3], 2, 9)` to replace elements.

### 3.2 Case Functions (Deferred - requires member function support)

> **Status**: Deferred until member function syntax is implemented.

| Method | Signature | Description | Symbol? |
|--------|-----------|-------------|---------|
| `s.upper()` | `string -> string` | Convert to uppercase | ✅ |
| `s.lower()` | `string -> string` | Convert to lowercase | ✅ |

```lambda
// Future syntax (when member functions are supported)
"Hello World".upper()    // "HELLO WORLD"
"Hello World".lower()    // "hello world"
```

**Implementation Note**: Should use ICU for proper Unicode case mapping (e.g., `ß` → `SS`, Turkish `i`/`İ`).

### 3.3 Trimming Functions

| Function | Signature | Description | Symbol? |
|----------|-----------|-------------|---------|
| `trim(s)` | `string -> string` | Remove leading/trailing whitespace | ✅ |
| `trim_start(s)` | `string -> string` | Remove leading whitespace | ✅ |
| `trim_end(s)` | `string -> string` | Remove trailing whitespace | ✅ |

```lambda
trim("  hello  ")       // "hello"
trim_start("  hello")   // "hello"
trim_end("hello  ")     // "hello"
```

**Priority**: HIGH - Very common operation in data processing.

### 3.4 Splitting & Joining Functions

| Function          | Signature                      | Description          | Symbol? |
| ----------------- | ------------------------------ | -------------------- | ------- |
| `split(s, sep)`   | `(string, string) -> [string]` | Split by separator   | ❌       |
| `join(strs, sep)` | `([string], string) -> string` | Join with separator  | ❌       |

```lambda
split("a,b,c", ",")           // ["a", "b", "c"]
join(["a", "b", "c"], ", ")   // "a, b, c"
```

> **KIV**: `split_lines(s)` - Split by line breaks (can use `split(s, "\n")` instead)

**Priority**: HIGH - Essential for text processing pipelines.

**Note**: `split` and `join` are inverses: `join(split(s, sep), sep) == s`

### 3.5 Replacement Functions

| Function | Signature | Description | Symbol? |
|----------|-----------|-------------|---------|
| `replace(s, old, new)` | `(string, string, string) -> string` | Replace all occurrences | ✅ |

```lambda
replace("hello hello", "l", "x")        // "hexxo hexxo"
```

> **KIV**: `replace_first(s, old, new)` and `replace_last(s, old, new)` - Replace first/last occurrence only

**Priority**: MEDIUM - Common text transformation.

### 3.6 Padding Functions (KIV)

> **Status**: Keep In View - planned but not yet implemented.

| Function                  | Signature                         | Description         | Symbol? |
| ------------------------- | --------------------------------- | ------------------- | ------- |
| `pad_start(s, len, char)` | `(string, int, string) -> string` | Left-pad to length  | ✅       |
| `pad_end(s, len, char)`   | `(string, int, string) -> string` | Right-pad to length | ✅       |

```lambda
pad_start("42", 5, "0")   // "00042"
pad_end("hi", 5, " ")     // "hi   "
```

### 3.7 Character Classification Functions (Deferred - requires member function support)

> **Status**: Deferred until member function syntax is implemented.

| Method            | Signature        | Description          | Symbol? |
| ----------------- | ---------------- | -------------------- | ------- |
| `s.is_numeric()`  | `string -> bool` | Check if all digits  | ✅       |
| `s.is_alpha()`    | `string -> bool` | Check if all letters | ✅       |

```lambda
// Future syntax (when member functions are supported)
"123".is_numeric()      // true
"hello".is_alpha()      // true
"hello123".is_alpha()   // false
```

**Note**: There is no `is_empty()` function because Lambda normalizes empty strings to `null`. Check for null instead: `if (s == null) ...`

### 3.8 Slice/Take/Drop Functions

| Function | Signature | Description | Symbol? |
|----------|-----------|-------------|---------|
| `take(s, n)` | `(string, int) -> string` | First n characters | ✅ |
| `drop(s, n)` | `(string, int) -> string` | Drop first n characters | ✅ |

```lambda
take("hello", 3)   // "hel"
drop("hello", 2)   // "llo"
```

**Priority**: MEDIUM - Convenient shortcuts for `slice`.

**Note**: These mirror the existing `take`/`drop` for arrays, providing consistency.

---

## 4. Function Applicability to Symbol

### 4.1 Functions that Should Apply to Symbol

Most string functions should work on symbols, with the return type matching the input:

| Function                   | String Input | Symbol Input | Rationale                  |
| -------------------------- | ------------ | ------------ | -------------------------- |
| `len`                      | ✅            | ✅            | Both have length           |
| `upper`/`lower`            | ✅            | ✅            | Case operations make sense |
| `trim*`                    | ✅            | ✅            | Whitespace handling        |
| `starts_with`/`ends_with`  | ✅            | ✅            | Prefix/suffix matching     |
| `contains`                 | ✅            | ✅            | Substring search           |
| `index_of`/`last_index_of` | ✅            | ✅            | Position finding           |
| `slice`                    | ✅            | ✅            | Extraction                 |
| `replace*`                 | ✅            | ✅            | Transformation             |
| `take`/`drop`              | ✅            | ✅            | Slicing                    |

### 4.2 Functions Not Applicable to Symbol

| Function | Reason |
|----------|--------|
| `split` | Symbols don't represent content to be parsed |
| `join` | Result is string content, not identifier |

### 4.3 Return Type Rules

For functions that transform the string/symbol:
- **String input → String output** (preserves type)
- **Symbol input → Symbol output** (preserves type)

```lambda
upper("hello")       // "HELLO" (string)
upper('hello)        // 'HELLO (symbol)

trim("  x  ")        // "x" (string)  
trim('  x)           // 'x (symbol - though unusual)
```

---

## 5. Additional Suggestions

### 5.1 String Interpolation Syntax

Consider adding string interpolation for cleaner code:

```lambda
// Current
"Hello, " ++ name ++ "! You are " ++ string(age) ++ " years old."

// Proposed interpolation syntax
`Hello, ${name}! You are ${age} years old.`
```

**Implementation**: Parse at compile time, transform to concatenation.

### 5.2 Raw Strings

For regex patterns, file paths, etc.:

```lambda
r"C:\Users\name\file.txt"   // no escape processing
r"\d+\.\d+"                 // regex pattern as-is
```

### 5.3 Multi-line Strings

```lambda
"""
This is a
multi-line string
with preserved whitespace.
"""
```

### 5.4 No Separate Character Type

**Design Decision**: Lambda intentionally does **not** have a separate `char` type.

**Rationale**:
1. **Simplicity**: Fewer types = simpler type system
2. **Consistency**: `str[i]` returns a string, same type as the source
3. **Unicode complexity**: "character" is ambiguous (code point vs grapheme cluster)
4. **Practical**: Single characters are just strings of length 1

```lambda
let c = "hello"[0]     // c is string "h", not a char type
len(c)                  // 1
c ++ "ello"             // "hello" - seamless concatenation
```

If character-level iteration is needed:
```lambda
// Iterate over characters (each is a length-1 string)
for (i in 0 to len(s)-1) {
    let char = s[i]
    // process char...
}
```

### 5.5 String Builder Pattern

For efficient repeated concatenation in procedural code:

```lambda
pn build_csv(rows: [[string]]) {
    var sb = string_builder()
    for (row in rows) {
        sb.append(join(row, ","))
        sb.append("\n")
    }
    sb.to_string()
}
```

---

## 6. Implementation Priority

### Phase 1: Essential (Immediate)

| Function | Effort | Impact |
|----------|--------|--------|
| `starts_with` | Low | High |
| `ends_with` | Low | High |
| `trim` | Low | High |
| `split` | Medium | High |
| `join` | Medium | High |
| `contains` | Low | High |

### Phase 2: Important (Short-term)

| Function | Effort | Impact |
|----------|--------|--------|
| `index_of` | Low | Medium |
| `last_index_of` | Low | Medium |
| `replace` | Medium | Medium |
| `trim_start`/`trim_end` | Low | Medium |
| `take`/`drop` | Low | Medium |

### Phase 3: Nice-to-have (Deferred)

> These functions require member function syntax support.

| Function                      | Effort | Impact | Status |
| ----------------------------- | ------ | ------ | ------ |
| `.upper()`/`.lower()`         | Medium | High   | Deferred |
| `.is_numeric()`/`.is_alpha()` | Medium | Low    | Deferred |

### KIV (Keep In View)

| Feature/Function               | Reason for deferral                        |
| ------------------------------ | ------------------------------------------ |
| **Member function syntax**     | Requires transpiler enhancement            |
| `.upper()`/`.lower()`          | Requires member function syntax            |
| `.is_numeric()`/`.is_alpha()`  | Requires member function syntax            |
| `count(s, sub)`                | Low priority, can iterate with `index_of`  |
| `split_lines(s)`               | Can use `split(s, "\n")` instead           |
| `pad_start`/`pad_end`          | Low priority formatting functions          |
| `replace_first`/`replace_last` | Low priority, `replace` covers most cases  |
| Negative indexing              | Additional complexity                      |

---

## 7. Implementation Notes

### 7.1 ASCII Fast Path

For performance, all string functions should implement an ASCII fast path:

```c
// Check if string is pure ASCII (all bytes < 128)
static inline bool is_ascii_string(String* str) {
    for (size_t i = 0; i < str->len; i++) {
        if ((unsigned char)str->chars[i] >= 128) return false;
    }
    return true;
}

// Example: fast path for indexing
Item fn_string_index(String* str, int64_t index) {
    if (is_ascii_string(str)) {
        // Fast path: byte offset == character offset
        if (index < 0 || index >= str->len) return ItemError;
        // Direct byte access
        char c = str->chars[index];
        return create_string_from_char(c);
    } else {
        // Slow path: UTF-8 character iteration
        long byte_offset = utf8_char_to_byte_offset(str->chars, index);
        // ... handle multi-byte characters
    }
}
```

**Optimization Strategy**:

| Function | ASCII Fast Path | UTF-8 Slow Path |
|----------|-----------------|-----------------|
| `len()` | Return `str->len` directly | Count code points |
| `str[i]` | Direct byte access | `utf8_char_to_byte_offset()` |
| `upper()`/`lower()` | Simple ASCII case flip | `utf8proc_toupper/tolower` |
| `trim()` | Check ASCII whitespace | Check Unicode whitespace |
| `starts_with()` | `memcmp()` | `memcmp()` (same, byte-level) |
| `split()` | Byte scan for separator | Byte scan (same, separator is bytes) |

**Note**: Many operations (like `starts_with`, `contains`, `split`) work at byte level regardless, since UTF-8 is self-synchronizing. The fast path mainly benefits indexing and length operations.

### 7.2 UTF-8 Handling

All string functions must be UTF-8 aware:
- Use `utf8_char_to_byte_offset()` for index conversion
- Use `utf8proc` library for Unicode operations
- Case conversion should use ICU or utf8proc for correctness

### 7.2 Memory Allocation

- Use `heap_alloc()` with `LMD_TYPE_STRING` for new strings
- Initialize `ref_cnt = 0` for new allocations
- Null-terminate all strings

### 7.3 Symbol Handling

For functions returning symbols:
- Use `heap_create_symbol()` for poolable symbols (≤32 chars)
- Consider result length when choosing allocation strategy

### 7.4 Error Handling

- Return `ItemError` for invalid inputs (e.g., null, wrong type)
- Log errors with `log_error()` before returning
- Don't throw exceptions

### 7.5 C Runtime Integration

Add declarations to `lambda.h`:
```c
String* fn_upper(String* str);
String* fn_lower(String* str);
String* fn_trim(String* str);
Item fn_split(Item str, Item sep);
Item fn_join(Item arr, Item sep);
int64_t fn_starts_with(Item str, Item prefix);
int64_t fn_ends_with(Item str, Item suffix);
int64_t fn_index_of(Item str, Item sub);
Item fn_replace(Item str, Item old_str, Item new_str);
```

Register in `mir.c` for JIT:
```c
{"fn_upper", (fn_ptr) fn_upper},
{"fn_lower", (fn_ptr) fn_lower},
// ...
```

---

## 8. Related Documents

- [Lib_String.md](Lib_String.md) - Comprehensive string library design
- [Lambda_Reference.md](../doc/Lambda_Reference.md) - Language reference
- [Lambda_Sys_Func_Reference.md](../doc/Lambda_Sys_Func_Reference.md) - System functions
- [Lamdba_Runtime.md](../doc/Lamdba_Runtime.md) - Runtime memory management

---

## Appendix A: Comparison with Other Languages

| Function | Lambda (Proposed) | JavaScript | Python | Go |
|----------|------------------|------------|--------|-----|
| Length | `len(s)` | `s.length` | `len(s)` | `len(s)` |
| Upper | `upper(s)` | `s.toUpperCase()` | `s.upper()` | `strings.ToUpper(s)` |
| Trim | `trim(s)` | `s.trim()` | `s.strip()` | `strings.TrimSpace(s)` |
| Split | `split(s, sep)` | `s.split(sep)` | `s.split(sep)` | `strings.Split(s, sep)` |
| Join | `join(arr, sep)` | `arr.join(sep)` | `sep.join(arr)` | `strings.Join(arr, sep)` |
| Contains | `contains(s, sub)` | `s.includes(sub)` | `sub in s` | `strings.Contains(s, sub)` |
| Replace | `replace(s, old, new)` | `s.replaceAll(old, new)` | `s.replace(old, new)` | `strings.ReplaceAll(s, old, new)` |

Lambda follows a functional style with functions taking the string as the first argument, consistent with other system functions like `len()`, `type()`, etc.
