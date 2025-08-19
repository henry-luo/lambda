# Lambda String Functions Design

## Overview

This document proposes a comprehensive set of built-in string functions for Lambda Script, drawing from the best practices in languages like JavaScript, Python, Go, Ruby, Rust, and Swift. The design emphasizes functional programming principles, Unicode support, and performance.

## Design Principles

### 1. **Functional Programming First**
- All string operations return new strings (immutability)
- Functions are pure (no side effects)
- Support for functional composition and chaining
- Works seamlessly with Lambda's `for` comprehensions

### 2. **Unicode-Aware by Default**  
- All functions operate on Unicode code points, not bytes
- Proper handling of grapheme clusters, normalization
- Configurable Unicode support levels (ASCII, UTF-8, ICU)
- Consistent behavior across platforms

### 3. **Type Safety & Error Handling**
- Clear type signatures for all functions
- Graceful handling of edge cases (null, empty strings - i.e. string of zero length treated as null)
- Consistent error reporting through Lambda's type system. No exception is throw.
- Optional/nullable return types where appropriate

### 4. **Performance Optimization**
- Efficient implementations using Lambda's memory pools
- Minimal copying for substring operations where possible  
- Fast paths for ASCII-only content
- Lazy evaluation where beneficial

## Core String Functions

### Character & Length Operations

```lambda
// Basic length and character operations
len(str: string) -> int                    // UTF-8 character count
str[index]                                 // single character at index
```

### Substring & Slicing Operations

```lambda
// Substring extraction (already implemented, enhance)
substring(str: string, start: int, end: int) -> string   // [start, end)
substr(str: string, start: int, length?: int) -> string  // start + length
slice(str: string, start: int, end?: int) -> string      // Python-style with negatives
head(str: string, count: int) -> string               // first N characters
tail(str: string, count: int) -> string             // last N characters

// Examples:
substring("hello", 1, 4)    // "ell"
substr("hello", 1, 3)       // "ell"  
slice("hello", 1, -1)       // "ell"
```

### Search & Match Operations

```lambda
// Basic search (enhance existing contains)
contains(str: string, substr: string) -> bool           // already implemented
starts_with(str: string, prefix: string) -> bool
ends_with(str: string, suffix: string) -> bool  
index_of(str: string, substr: string) -> int?           // first occurrence (-1 if not found)
last_index_of(str: string, substr: string) -> int?      // last occurrence
count(str: string, substr: string) -> int               // count occurrences

// Pattern matching (simple patterns, not full regex)
matches(str: string, pattern: string) -> bool           // simple glob patterns
find_all(str: string, pattern: string) -> [string]      // all matches

// Examples:
starts_with("hello", "he")     // true
ends_with("hello", "lo")       // true  
index_of("hello", "ll")        // 2
count("hello world", "l")      // 3
matches("hello.txt", "*.txt")  // true (simple glob)
```

### Case Operations

```lambda
// Case transformations
upper(str: string) -> string
lower(str: string) -> string

// Examples:
upper("hello")           // "HELLO"
```

### Whitespace & Trimming Operations

```lambda
// Trimming operations
trim(str: string) -> string                              // trim both ends
trim_start(str: string) -> string                        // trim left
trim_end(str: string) -> string                          // trim right
trim_chars(str: string, chars: string) -> string         // trim specific chars

// Examples:
trim("  hello  ")              // "hello"
trim_chars("...hello...", ".") // "hello"
is_whitespace("   \t\n")       // true
```

### String Building & Manipulation

```lambda
// String concatenation and building  
join(strs: [string], separator: string) -> string        // join with separator

// String replacement
replace(str: string, old: string, new: string) -> string      // replace all
replace_first(str: string, old: string, new: string) -> string // replace first
replace_last(str: string, old: string, new: string) -> string  // replace last

// Examples:
join(["a", "b", "c"], ", ")    // "a, b, c"
repeat("ha", 3)                // "hahaha"
replace("hello hello", "l", "x") // "hexxo hexxo"
```

### Splitting Operations

```lambda
// String splitting
split(str: string, separator: string) -> [string]            // split on separator
split_lines(str: string) -> [string]                         // split on line breaks
split_whitespace(str: string) -> [string]                    // split on any whitespace
split_n(str: string, separator: string, max: int) -> [string] // limit splits
partition(str: string, separator: string) -> (string, string, string) // before, sep, after

// Examples:
split("a,b,c", ",")              // ["a", "b", "c"]
split_lines("line1\nline2")      // ["line1", "line2"]
split_whitespace("a  b\tc")      // ["a", "b", "c"]
partition("a=b=c", "=")          // ("a", "=", "b=c")
```

### Character Class & Validation

```lambda
// Character classification
is_alphabetic(str: string) -> bool                      // all letters?
is_numeric(str: string) -> bool                         // all digits?
is_alphanumeric(str: string) -> bool                    // letters or digits?
is_ascii(str: string) -> bool                           // ASCII only?
is_printable(str: string) -> bool                       // printable chars?

// Examples:
is_numeric("123")               // true
is_alphabetic("hello")          // true
is_email("user@domain.com")     // true
```

### Unicode & Encoding Operations

```lambda
// Unicode normalization (enhance existing)
normalize(str: string, form: symbol) -> string          // already implemented

// Case folding (Unicode-aware case insensitive comparison)
case_fold(str: string) -> string                        // Unicode case folding
compare_ignore_case(a: string, b: string) -> int        // case-insensitive compare

// Encoding operations (when needed)
to_bytes(str: string, encoding?: symbol) -> binary      // string to bytes
from_bytes(data: binary, encoding?: symbol) -> string?  // bytes to string

// Examples:
normalize(str, 'nfc')           // canonical composition
case_fold("İstanbul")           // for case-insensitive comparison
```

## Advanced String Functions

### Functional Programming Support

```lambda
// Higher-order string operations
map_chars(str: string, fn: (string) -> string) -> string     // transform each char
filter_chars(str: string, fn: (string) -> bool) -> string    // filter characters
fold_chars(str: string, init: any, fn: (any, string) -> any) -> any  // reduce chars

// String comprehensions (works with existing for syntax)
for (char in chars(str)) transform(char)
for (word in split(str, " ")) process(word)

// Examples:
map_chars("hello", to_upper)         // "HELLO"
filter_chars("hello123", is_alphabetic) // "hello"
```

### String Interpolation & Templates

```lambda
// Template string functions (could be syntax sugar later)
template(format: string, values: map) -> string           // template substitution
sprintf(format: string, args: [any]) -> string           // printf-style formatting

// Examples:
template("Hello, {name}!", {name: "World"})  // "Hello, World!"
sprintf("Value: %d", [42])                   // "Value: 42"
```

### Performance & Utility Functions

```lambda
// String comparison
compare(a: string, b: string) -> int                 // lexicographic comparison
compare_natural(a: string, b: string) -> int         // natural sort order

// Examples:
compare("apple", "banana")        // -1
compare_natural("file2", "file10") // -1 (not 1)
```

## Implementation Strategy

### Phase 1: Core Functions (Immediate)
1. **Character operations**: `char_at`, `chars`, `is_empty`
2. **Enhanced substring**: `substr`, `slice`, `take`, `drop` variations
3. **Basic search**: `starts_with`, `ends_with`, `index_of`, `count`
4. **Case operations**: `to_upper`, `to_lower`, `to_title`
5. **Trimming**: `trim`, `trim_start`, `trim_end`

### Phase 2: String Building (Short-term)
1. **Joining**: `concat`, `join`, `repeat`
2. **Padding**: `pad_start`, `pad_end`, `center`
3. **Replacement**: `replace`, `replace_first`
4. **Splitting**: `split`, `split_lines`, `split_whitespace`

### Phase 3: Advanced Features (Medium-term)
1. **Character classes**: `is_alphabetic`, `is_numeric`, etc.
2. **Unicode operations**: enhanced normalization, case folding
3. **Validation**: `is_email`, `is_url`, `is_uuid`
4. **Functional support**: `map_chars`, `filter_chars`

### Phase 4: Specialized Features (Long-term)
1. **Pattern matching**: simple glob support
2. **Templates**: string interpolation
3. **Performance**: `compare_natural`, `levenshtein`
4. **Security**: `secure_compare`, proper encoding handling

## Integration with Lambda Features

### Type System Integration

```lambda
// Type-safe string operations
fn process_text(input: string?) -> string {
    if (input == null) return ""
    
    let cleaned = trim(input)
    let normalized = normalize_nfc(cleaned)
    let title_case = to_title(normalized)
    
    title_case
}

// Union types for error handling  
fn safe_substring(str: string, start: int, end: int) -> string | error {
    if (start < 0 or end > len(str)) {
        error("Index out of bounds")
    } else {
        substring(str, start, end)
    }
}
```

### Functional Composition

```lambda
// Chain string operations functionally
let process_name = fn(name: string) -> string {
    name
    |> trim
    |> to_lower
    |> to_title
    |> normalize_nfc
}

// Use with comprehensions
let clean_names = for (name in raw_names) {
    name |> trim |> to_title
}

// Compose with other Lambda features
let word_count = for (line in split_lines(text)) {
    len(split_whitespace(line))
}
```

### Memory Management

```lambda
// Efficient string building
let build_csv = fn(rows: [[string]]) -> string {
    let lines = for (row in rows) join(row, ",")
    join(lines, "\n")
}

// Pool-based allocations for performance
// (implementation detail - automatic memory management)
```

## Error Handling Patterns

### Nullable Returns for Not Found

```lambda
// Functions that might not find results return optional types
index_of("hello", "x")     // null (not found)
char_at("hello", 10)       // null (index out of bounds)
```

### Error Types for Invalid Operations

```lambda
// Invalid operations return error type
to_int("not a number")     // error("Invalid number format")
normalize("text", 'invalid') // error("Unknown normalization form")
```

### Graceful Degradation

```lambda
// Functions handle edge cases gracefully
trim("")                   // "" (empty string)
split("", ",")             // [] (empty array)
```

## Performance Considerations

### Fast Paths for Common Cases

```c
// Optimize common operations
Item fn_to_upper_optimized(Item str_item) {
    String* str = (String*)str_item.pointer;
    
    // Fast path for ASCII-only strings
    if (is_ascii_only(str->chars, str->len)) {
        return to_upper_ascii_fast(str);
    }
    
    // Unicode path for international text
    return to_upper_unicode(str);
}
```

## Migration & Compatibility

### Backward Compatibility

- Existing `len()`, `substring()`, `contains()`, `normalize()` functions remain unchanged
- New functions use consistent naming conventions
- Type signatures are explicit and checked

### Incremental Rollout

1. **Phase 1**: Core functions that don't conflict with existing code
2. **Phase 2**: Enhanced versions of existing functions with new parameters
3. **Phase 3**: Advanced features that require new syntax or significant changes
4. **Phase 4**: Performance optimizations and specialized use cases

### Documentation & Examples

- Comprehensive documentation for each function
- Migration guide from other languages (JS, Python, etc.)
- Performance characteristics and Unicode behavior notes
- Code examples showing functional composition patterns

## Conclusion

This comprehensive string function library will establish Lambda as a powerful language for text processing while maintaining its functional programming principles. The design emphasizes:

1. **Unicode correctness** - proper handling of international text
2. **Functional purity** - immutable operations that compose well
3. **Type safety** - clear contracts and error handling
4. **Performance** - efficient implementations with multiple optimization levels
5. **Usability** - intuitive naming and consistent behavior

The incremental implementation strategy allows for immediate improvements while building toward a complete, best-in-class string processing capability that rivals or exceeds what's available in other modern languages.
