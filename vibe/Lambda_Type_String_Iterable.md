# Proposal: String as Indexable (Not Iterable) Type

## Design Decision

After surveying how strings behave across languages (Python, R, Scala, Haskell, JavaScript, Kotlin, Elixir, Rust), Lambda adopts the following model:

**String is indexable, but not iterable.** String and symbol are singular (scalar) values in iteration, pipe, and predicate contexts. Use `chars(str)` for explicit character decomposition.

### Rationale

Lambda's pipe syntax `expr | expr` and predicate syntax `expr that cond` treat the left-hand value as a single item (`~`). If strings were iterable, `str that ~ == "abc"` would decompose into characters — inconsistent with the singular `~` binding. Making strings singular everywhere keeps the mental model simple.

Indexing (`str[i]`, `str[a to b]`) is a **positional access** operation, orthogonal to iteration. Arrays, maps, and strings all support `[]` without implying they behave identically in `for` loops.

## Complete Behavior Table

| Operation | String Behavior | Returns |
|-----------|----------------|---------|
| `str[i]` | Positional access | 1-char string |
| `str[a to b]` | Slice | Substring |
| `len(str)` | Character count | Int |
| `take(str, n)` | First n chars | Substring |
| `drop(str, n)` | Skip n chars | Substring |
| `for s in str` | **Singular** — 1 iteration | `s = str` |
| `str \| fn` | **Singular** — apply fn to whole string | `fn(str)` |
| `str that cond` | **Singular** — test whole string | `str` or null |
| `reverse(str)` | **Singular passthrough** | `str` (unchanged) |
| `sort(str)` | **Singular passthrough** | `str` (unchanged) |
| `unique(str)` | **Singular passthrough** | `str` (unchanged) |
| `min(str)` | **Singular passthrough** | `str` (unchanged) |
| `max(str)` | **Singular passthrough** | `str` (unchanged) |
| `concat(str1, str2)` | String concatenation | `str1 ++ str2` |
| `chars(str)` | **Explicit decompose** | Array of 1-char strings |
| `chars('sym')` | **Explicit decompose** | Array of 1-char symbols |

### Symbol Parity

Every operation that works on strings works identically on symbols, preserving type:

| String | Symbol |
|--------|--------|
| `"hello"[1]` → `"e"` | `'hello'[1]` → `'e'` |
| `take("hello", 3)` → `"hel"` | `take('hello', 3)` → `'hel'` |
| `drop("hello", 2)` → `"llo"` | `drop('hello', 2)` → `'llo'` |
| `chars("abc")` → `["a", "b", "c"]` | `chars('abc')` → `['a', 'b', 'c']` |

## Critical Bug Fix: UTF-8 Consistency in `item_at()`

### Problem

`item_at()` in `lambda-data-runtime.cpp` uses **byte indexing**:

```cpp
case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
    const char* chars = data.get_chars();
    uint32_t len = data.get_len();              // byte length
    if (index < 0 || (uint32_t)index >= len) { return ItemNull; }
    char buf[2] = {chars[index], '\0'};         // byte access!
    ...
}
```

But `fn_len()` returns **UTF-8 character count** via `str_utf8_count()`. This mismatch means `"café"[3]` returns a garbled byte instead of `"é"`.

### Fix

`item_at()` must convert character index to byte offset using `str_utf8_char_to_byte()`, then extract the full UTF-8 codepoint (1-4 bytes):

```cpp
case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
    const char* chars = data.get_chars();
    uint32_t byte_len = data.get_len();
    int64_t char_count = (int64_t)str_utf8_count(chars, byte_len);
    if (index < 0) index += char_count;  // negative indexing
    if (index < 0 || index >= char_count) { return ItemNull; }

    uint32_t byte_offset = str_utf8_char_to_byte(chars, byte_len, (uint32_t)index);
    uint32_t next_offset = str_utf8_char_to_byte(chars, byte_len, (uint32_t)(index + 1));
    uint32_t char_bytes = next_offset - byte_offset;

    if (type_id == LMD_TYPE_SYMBOL) {
        Symbol* ch_sym = heap_create_symbol(chars + byte_offset, char_bytes);
        return {.item = y2it(ch_sym)};
    }
    String* ch_str = heap_strcpy(chars + byte_offset, char_bytes);
    return {.item = s2it(ch_str)};
}
```

## Implementation Plan

### Phase 1: UTF-8 Fix

Fix `item_at()` for UTF-8 correctness. This makes `str[i]` work correctly for non-ASCII strings.

File: `lambda/lambda-data-runtime.cpp`

### Phase 2: Singular Passthrough in Vector Functions

For `unique()`, `sort()`, `reverse()`, `min()`, `max()`: detect string/symbol input at the top of the function and return the value unchanged.

File: `lambda/lambda-vector.cpp`

```cpp
// At top of fn_unique, fn_sort, fn_reverse, etc.:
TypeId type = get_type_id(item);
if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
    return item;  // singular passthrough
}
```

### Phase 3: `take(str, n)` / `drop(str, n)` as Substring

Delegate to `fn_substring()` internally:

```cpp
// In fn_take:
if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
    return fn_substring(item, {.item = i2it(0)}, n_item);
}

// In fn_drop:
if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
    int64_t len = fn_len(item);
    return fn_substring(item, n_item, {.item = i2it(len)});
}
```

### Phase 4: `concat(str1, str2)`

Delegate to `fn_join()` (the `++` operator):

```cpp
// In fn_concat:
if ((type_a == LMD_TYPE_STRING || type_a == LMD_TYPE_SYMBOL) &&
    (type_b == LMD_TYPE_STRING || type_b == LMD_TYPE_SYMBOL)) {
    return fn_join(a, b);
}
```

### Phase 5: `chars()` Function

Add `chars(str)` → array of 1-char strings; `chars('sym')` → array of 1-char symbols.

Equivalent to `split(str, null)` but more readable and works on symbols too.

File: `lambda/lambda-eval.cpp`

## Test Cases

```lambda
// UTF-8 indexing
"café"[3]              // → "é"
len("café")            // → 4

// Singular passthrough
reverse("hello")       // → "hello"
sort("dcba")           // → "dcba"
unique("banana")       // → "banana"
min("cab")             // → "cab"
max("cab")             // → "cab"

// Substring operations
take("hello", 3)       // → "hel"
drop("hello", 2)       // → "llo"
take('world', 2)       // → 'wo'
drop('world', 3)       // → 'ld'

// Explicit decomposition
chars("abc")           // → ["a", "b", "c"]
chars('abc')           // → ['a', 'b', 'c']
chars("café")          // → ["c", "a", "f", "é"]

// Concat
concat("abc", "def")   // → "abcdef"

// Pipe — singular
"hello" | upper        // → "HELLO"
"hello" that len(~) > 3  // → "hello"

// For — singular (1 iteration)
for s in "hello" { s } // → "hello"
```

## Priority

1. **P0 — UTF-8 fix in `item_at()`**: Correctness bug for non-ASCII strings.
2. **P1 — Singular passthrough**: `unique`, `sort`, `reverse`, `min`, `max` return string unchanged.
3. **P1 — `take`/`drop` as substring**: Positional access family.
4. **P2 — `concat` for strings**: Convenience (already have `++`).
5. **P2 — `chars()` function**: Explicit decomposition for when users need character iteration.

## Files to Modify

| File | Changes |
|------|---------|
| `lambda/lambda-data-runtime.cpp` | Fix `item_at()` for UTF-8 |
| `lambda/lambda-vector.cpp` | String passthrough in `fn_unique`, `fn_sort1`, `fn_sort2`, `fn_reverse`; `take`/`drop` as substring; `concat` for strings |
| `lambda/lambda-eval.cpp` | Add `chars()` function; ensure `min`/`max` passthrough |
| `test/lambda/string_indexable.ls` + `.txt` | New test file for string-as-indexable tests |
