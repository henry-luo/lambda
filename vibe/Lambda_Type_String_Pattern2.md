# Lambda String Pattern Enhancement Proposal (v2)

**Date:** February 28, 2026
**Predecessor:** `vibe/Lambda_Type_String_Pattern.md` (v1, January 27, 2026)
**Status:** Complete (Phases 1–5 implemented)

---

## 1. Overview

This document proposes enhancements to Lambda's string pattern support, focusing on **five core operations** that enable pattern-based string processing. It builds on the existing foundation (pattern definition, `is` operator, RE2 compilation) and identifies gaps that need implementation.

### 1.1 Current State Assessment

| # | Feature | Syntax | Status | Notes |
|---|---------|--------|--------|-------|
| 1 | `is` operator | `str is pattern` | **Working** | Full match via `fn_is()` → `RE2::FullMatch()` |
| 2 | `match` expression | `match str { case pattern: ... }` | **Not Working** | C2MIR transpilation fails; pattern arms generate invalid C code |
| 3 | `replace()` | `replace(str, pattern, repl)` | **Working** | Pattern branch added to `fn_replace()`; uses `RE2::GlobalReplace()` via `pattern_replace_all()` |
| 4 | `split()` | `split(str, pattern)` | **Working** | Pattern branch added to `fn_split()`; `fn_split3()` supports keep-delimiter; uses `pattern_split()` |
| 5 | `find()` | `find(str, pattern)` | **Working** | `fn_find2()`/`fn_find3()` implemented; pattern and plain-string find both return `[{value, index}]` |

> **File-based find/replace** (grep/sed equivalents) have been split into a separate proposal: [`vibe/Lambda_Type_String_Grep.md`](Lambda_Type_String_Grep.md)

### 1.2 Architectural Issue: Anchored Regex — RESOLVED

The compiled `TypePattern` always embeds `^...$` anchors (see `re2_wrapper.cpp:340-342`), making `RE2::FullMatch()` the only valid match mode. For `find()`, `replace()`, and `split()`, we need **unanchored** matching.

**Solution (implemented):** Added `re2_unanchored` field to `TypePattern` with lazy compilation via `pattern_get_unanchored()`. The unanchored regex is derived by stripping `^...$` from the source pattern and compiled on first use. Cleanup handled by `pattern_destroy()`.

---

## 2. Feature Specifications

### 2.1 Feature 1: `str is pattern` (Type Check) — Working

Tests a string against a compiled pattern using full-match semantics.

```lambda
string email = \w+ "@" \w+ "." \w+
string phone = \d[3] "-" \d[3] "-" \d[4]

"hello@world.com" is email       // true
"123-456-7890" is phone          // true
"abc" is phone                   // false
```

**Implementation path:**
- `fn_is()` in `lambda-eval.cpp:627` checks `b_type->kind == TYPE_KIND_PATTERN`
- Calls `pattern_full_match_chars()` → `RE2::FullMatch()`
- MIR transpiler emits `fn_is(boxed_str, boxed_pattern)` call

**Status:** Fully working. All 15 test groups in `test/lambda/string_pattern.ls` pass. Integration with type patterns (`test/lambda/test_string_pattern_integration.ls`) also works.

---

### 2.2 Feature 2: `match` with String Pattern — Needs Fix

Match expressions should support string patterns as case arms.

```lambda
string digits = \d+
string alpha = \a+
string mixed = \w+

fn classify(s) => match s {
    case digits: "number"
    case alpha: "word"
    case mixed: "mixed"
    default: "other"
}

classify("123")         // "number"
classify("hello")       // "word"
classify("hello123")    // "mixed" (note: alpha also matches subset, but digits matches first)
classify("hello world") // "other"
```

**Root cause:** The `emit_single_pattern_test()` function in `transpile-mir.cpp:2086` handles pattern arms by calling `fn_is(scrutinee, pattern)`. This path works conceptually, but the **C2MIR backend** fails to compile the generated C code when pattern match arms are present (syntax errors in intermediate C output).

**Required changes:**
1. Investigate C code generation for match arms with pattern references in `transpile.cpp`
2. Ensure the pattern reference is correctly emitted as a `const_pattern()` call in the match arm's test condition
3. The MIR path (`transpile-mir.cpp`) uses `fn_is()` for pattern test — verify this path compiles correctly

**Design note:** Pattern matching in `match` uses full-match semantics (consistent with `is` operator). The first matching arm wins, so arm ordering matters when patterns overlap.

---

### 2.3 Feature 3: `replace(str, pattern, repl)` — Implemented

Replace all non-overlapping matches of a pattern in a string.

```lambda
string digit = \d

// String replacement (already works)
replace("hello world", "o", "0")        // "hell0 w0rld"

// Pattern replacement (needs implementation)
replace("a1b2c3", digit, "X")           // "aXbXcX"

string multi_digit = \d+
replace("a1b22c333", multi_digit, "N")   // "aNbNcN"

// Function replacement — DEFERRED to future
// replace("hello", any_char, \c => upper(c))  // "HELLO"
```

**Implementation status:** Completed. Pattern branch added to `fn_replace()` in `lambda-eval.cpp:3277`. When the 2nd argument is `LMD_TYPE_TYPE` with `kind == TYPE_KIND_PATTERN`, it extracts the `TypePattern*`, gets the unanchored regex, and calls `pattern_replace_all()`. The function is registered as overloaded (`is_overloaded=true`) with C2MIR name `fn_replace3`.

**Required changes:**

1. **`re2_wrapper.hpp/cpp`** — Add new function:
   ```cpp
   // Replace all matches of pattern in string with replacement
   // Returns new string with replacements, or nullptr on error
   // Uses unanchored matching (no ^$ anchors)
   String* pattern_replace_all(TypePattern* pattern, String* str, String* replacement);
   ```

2. **`fn_replace()` in `lambda-eval.cpp`** — Add pattern branch:
   ```cpp
   // After existing string/symbol type check:
   if (old_type == LMD_TYPE_TYPE) {
       Type* type = old_item.type;
       if (type->kind == TYPE_KIND_PATTERN) {
           TypePattern* pattern = (TypePattern*)type;
           return pattern_replace_all(pattern, str, new_str);
       }
   }
   ```

3. **Unanchored regex** — `pattern_replace_all()` must use an unanchored regex. Either:
   - (a) Store a second `re2::RE2*` in `TypePattern` for unanchored matching, or
   - (b) Use `RE2::GlobalReplace()` with the pattern (strip the `^` and `$` anchors), or
   - (c) Compile a new unanchored RE2 from the source pattern on first use (lazy)

**RE2 API:** `RE2::GlobalReplace(&str, pattern, replacement)` performs unanchored global replacement.

**Deferred:** Function-based replacement (`replace(str, pattern, fn)`) where `fn` receives the matched substring and returns the replacement. This requires `RE2::FindAndConsume()` iteration + callback invocation. Deferred to a future release.

---

### 2.4 Feature 4: `split(str, pattern)` — Implemented

Split a string by a pattern delimiter.

```lambda
// String splitting (already works)
split("a,b,c", ",")                     // ["a", "b", "c"]

string digit = \d
string ws = \s+
string delim = "," | ";" | ":"

// Pattern splitting (needs implementation)
split("a1b2c3", digit)                   // ["a", "b", "c"]
split("hello   world", ws)               // ["hello", "world"]
split("a,b;c:d", delim)                  // ["a", "b", "c", "d"]

// Keep delimiters (3rd arg = true)
split("a1b2c3", digit, true)             // ["a", "1", "b", "2", "c", "3"]
split("hello   world", ws, true)         // ["hello", "   ", "world"]
split("a,b;c:d", delim, true)            // ["a", ",", "b", ";", "c", ":", "d"]
```

**Implementation status:** Completed. Pattern branch added to `fn_split()` at `lambda-eval.cpp:2878`. A new `fn_split3()` handles the 3-arg form `split(str, sep, keep_delim)`. Both are registered as overloaded with C2MIR names `fn_split2` and `fn_split3`. The `pattern_split()` function in `re2_wrapper.cpp` handles the RE2 matching and supports `keep_delim`.

**Signature:** `split(str, separator, keep_delim?) -> [string]`
- `str`: string to split
- `separator`: string literal or string pattern
- `keep_delim`: optional bool (default `false`). When `true`, delimiter matches are interleaved in the result.

**Required changes:**

1. **`re2_wrapper.hpp/cpp`** — Add new function:
   ```cpp
   // Split string by pattern matches, returning list of parts.
   // If keep_delim is true, matched delimiters are included as separate elements.
   // Uses unanchored matching.
   List* pattern_split(TypePattern* pattern, String* str, bool keep_delim);
   ```

2. **`fn_split()` in `lambda-eval.cpp`** — Add pattern branch:
   ```cpp
   // Check if separator is a pattern
   if (sep_type == LMD_TYPE_TYPE) {
       Type* type = sep_item.type;
       if (type->kind == TYPE_KIND_PATTERN) {
           TypePattern* pattern = (TypePattern*)type;
           bool keep = (keep_delim_type == LMD_TYPE_BOOL && keep_delim_item == BOOL_TRUE);
           return {.list = pattern_split(pattern, str, keep)};
       }
   }
   ```
   Also support `keep_delim` for plain-string split (same semantics).

3. **`build_ast.cpp`** — Update `split` registration to accept optional 3rd arg:
   ```cpp
   {SYSFUNC_SPLIT, "split", 2, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false},
   // overloaded: split(str, sep) and split(str, sep, keep)
   ```

4. **RE2 implementation:** Use `re2::RE2::FindAndConsume()` or manual `StringPiece` iteration to find successive matches and split between them. When `keep_delim` is true, push the matched delimiter as a separate list element between the split parts.

**Edge cases:**
- Empty string → empty list (consistent with current behavior)
- Pattern matches at start/end → include empty strings at boundaries
- No matches → single-element list containing the original string
- Zero-length matches → need to advance by one character to avoid infinite loop
- `keep_delim` with adjacent delimiters → empty strings between them

---

### 2.5 Feature 5: `find(str, pattern)` — Implemented

Find all non-overlapping matches of a pattern in a string. Each match is returned as a map with `value` (matched substring) and `index` (start position), following the C#/Rust approach of returning rich match objects.

```lambda
string digits = \d+
string alpha = \a+

find("a1b22c333", digits)
// [{value: "1", index: 1}, {value: "22", index: 3}, {value: "333", index: 6}]

find("hello world", alpha)
// [{value: "hello", index: 0}, {value: "world", index: 6}]

find("no-match", digits)
// []

// Plain string find (also supported)
find("hello world hello", "lo")
// [{value: "lo", index: 3}, {value: "lo", index: 14}]

// Access pattern: extract just the values or indices
find("a1b2c3", digits) | map(~.value)    // ["1", "2", "3"]
find("a1b2c3", digits) | map(~.index)    // [1, 3, 5]
```

**Implementation status:** Completed. `SYSFUNC_FIND` and `SYSFUNC_FIND3` enums added. `fn_find2()` and `fn_find3()` implemented in `lambda-eval.cpp`. Pattern find uses `pattern_find_all()` in `re2_wrapper.cpp` with `RE2::Match(UNANCHORED)`. Plain string find uses `memcmp` iteration. Both return `[{value: string, index: int}]` match maps via `create_match_map()`. Registered as overloaded with C2MIR names `fn_find2`/`fn_find3`.

**Required changes:**

1. **`lambda-data.hpp`** — Add enum:
   ```cpp
   SYSFUNC_FIND,      // after SYSFUNC_REPLACE or similar
   ```

2. **`build_ast.cpp`** — Register system function:
   ```cpp
   {SYSFUNC_FIND, "find", 2, &TYPE_ANY, false, false, false, LMD_TYPE_LIST, false},
   ```

3. **`re2_wrapper.hpp/cpp`** — Add search function:
   ```cpp
   // Find all non-overlapping matches of pattern in string.
   // Returns list of maps: [{value: "match", index: N}, ...]
   List* pattern_find_all(TypePattern* pattern, String* str);
   ```

4. **`lambda-eval.cpp`** — Add `fn_find()`:
   ```cpp
   Item fn_find(Item str_item, Item pattern_item) {
       // If pattern_item is string/symbol: find all literal substring occurrences
       // If pattern_item is TypePattern: call pattern_find_all() using unanchored regex
       // Each match is a map: {value: matched_string, index: start_position}
       // Return list of match maps (empty list if no matches)
   }
   ```

5. **`mir.c`** — Register function pointer:
   ```cpp
   {"fn_find", (fn_ptr) fn_find},
   ```

**RE2 implementation:** Use `re2::RE2::FindAndConsume()` to iterate through all non-overlapping matches:
```cpp
re2::RE2* re = pattern_get_unanchored(pattern);
re2::StringPiece input(str->chars, str->len);
const char* base = str->chars;  // for computing index
re2::StringPiece match;
while (RE2::FindAndConsume(&input, *re, &match)) {
    int index = (int)(match.data() - base);
    // create map: {value: heap_strcpy(match), index: index}
    // push to result list
}
```

**Future extensions:**
- `find_first(str, pattern)` → returns first match map or null
- `capture(str, pattern)` → returns named capture groups as a map

> **File-based find/replace** (grep/sed equivalents) have been moved to a separate proposal: [`vibe/Lambda_Type_String_Grep.md`](Lambda_Type_String_Grep.md)

---

## 3. Unanchored Regex Design

### 3.1 Problem

Currently, `compile_pattern_ast()` wraps the regex with `^...$` anchors:

```cpp
strbuf_append_str(regex, "^");   // line 340
compile_pattern_to_regex(regex, pattern_ast);
strbuf_append_str(regex, "$");   // line 342
```

This means the `TypePattern::re2` field always contains a full-match regex. Functions like `find()`, `replace()`, and `split()` require **unanchored** matching (searching within a string).

### 3.2 Proposed Solution: Dual Regex

Extend `TypePattern` to hold two compiled regex objects:

```cpp
typedef struct TypePattern : Type {
    int pattern_index;
    bool is_symbol;
    re2::RE2* re2;           // existing: anchored regex (^pattern$) for is/match
    re2::RE2* re2_unanchored; // new: unanchored regex (pattern) for find/replace/split
    String* source;           // regex source (without anchors) for debugging
} TypePattern;
```

**Decision: Lazy compilation.** Most patterns are only used with `is`/`match` and never need the unanchored variant. Compiling both eagerly would waste memory.

```cpp
// Get or create unanchored RE2 for partial matching operations.
// Thread-safe: compiled once on first call, cached in pattern->re2_unanchored.
re2::RE2* pattern_get_unanchored(TypePattern* pattern) {
    if (pattern->re2_unanchored) return pattern->re2_unanchored;
    // Strip ^...$ from source to get unanchored regex
    const char* src = pattern->source->chars;
    size_t len = pattern->source->len;
    // source is "^<regex>$", strip first and last char
    const char* inner = src + 1;      // skip ^
    size_t inner_len = len - 2;       // skip ^ and $
    std::string unanchored(inner, inner_len);
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    pattern->re2_unanchored = new re2::RE2(unanchored, opts);
    return pattern->re2_unanchored;
}
```

### 3.3 Source String Storage

Currently `pattern->source` contains the **anchored** regex string (e.g., `^[0-9]+$`). The unanchored variant is derived by stripping the leading `^` and trailing `$`. No additional source field is needed.

---

## 4. Implementation Plan

### Phase 1: Core Infrastructure (Unanchored Regex) — DONE ✅

1. ✅ Extend `TypePattern` struct with `re2_unanchored` field (`lambda-data.hpp`)
2. ✅ Add `pattern_get_unanchored()` helper to `re2_wrapper.cpp` — lazy compilation, strips `^...$`
3. ✅ Add `pattern_destroy()` cleanup for the new field
4. ✅ Initialize `re2_unanchored = nullptr` in `compile_pattern_ast()`

### Phase 2: `find()` Function — DONE ✅

1. ✅ Add `SYSFUNC_FIND`, `SYSFUNC_FIND3` to `lambda-data.hpp`
2. ✅ Register `find` in `build_ast.cpp` sys_func table (2-arg overloaded + 3-arg)
3. ✅ Implement `pattern_find_all()` in `re2_wrapper.cpp` — uses `RE2::Match(UNANCHORED)`
4. ✅ Implement `fn_find2()` and `fn_find3()` in `lambda-eval.cpp` — pattern + plain string find
5. ✅ Implement `create_match_map()` in `re2_wrapper.cpp` — builds `{value: String*, index: int64}` maps
6. ✅ Register `fn_find2`/`fn_find3` in `mir.c`
7. ✅ Add declarations to `lambda.h` + regenerate `lambda-embed.h`
8. ✅ Test coverage in `test/lambda/string_pattern_ops.ls` + expected `.txt`

### Phase 3: Pattern-Aware `replace()` — DONE ✅

1. ✅ Implement `pattern_replace_all()` in `re2_wrapper.cpp` — uses `RE2::GlobalReplace()`
2. ✅ Add pattern branch to `fn_replace()` in `lambda-eval.cpp` — checks `LMD_TYPE_TYPE` + `TYPE_KIND_PATTERN`
3. ✅ Make `replace` overloaded (`is_overloaded=true`) in `build_ast.cpp`; C2MIR name: `fn_replace3`
4. ✅ Register `fn_replace3` in `mir.c`
5. ✅ Test coverage in `test/lambda/string_pattern_ops.ls` + expected `.txt`

### Phase 4: Pattern-Aware `split()` with Keep-Delimiter — DONE ✅

1. ✅ Implement `pattern_split(pattern, str, keep_delim)` in `re2_wrapper.cpp` — uses `RE2::Match` iteration
2. ✅ Add pattern branch to `fn_split()` in `lambda-eval.cpp`
3. ✅ Implement `fn_split3()` for 3-arg form `split(str, sep, keep_delim)`
4. ✅ Make `split` overloaded; register `SYSFUNC_SPLIT3`; C2MIR names: `fn_split2`/`fn_split3`
5. ✅ Support `keep_delim` for both pattern and string separators
6. ✅ Test coverage in `test/lambda/string_pattern_ops.ls` + expected `.txt`

### Phase 5: Fix `match` with Pattern Arms — DONE ✅

1. ✅ Debugged C code generation: pattern identifiers in match arms generated `(Item)()` (invalid C) because `transpile_box_item()` called `transpile_expr()` on bare `AST_NODE_IDENT` which has no switch case
2. ✅ Fixed `transpile_match_condition()` in `transpile.cpp` to detect `AST_NODE_STRING_PATTERN` / `AST_NODE_SYMBOL_PATTERN` entry nodes and emit `const_pattern(index)` + `fn_is()` calls
3. ✅ Verified `transpile-mir.cpp` path works correctly (it already handles pattern references via `const_pattern()` in `transpile_expr` for identifiers)
4. ✅ Added `test/lambda/match_string_pattern.ls` + expected `.txt` (13 test cases across 4 sections)

### Phase 6: File-Based `find()` and `replace()` — MOVED

Split into separate proposal: [`vibe/Lambda_Type_String_Grep.md`](Lambda_Type_String_Grep.md)

### Phase 7 (Future): Extended Features

1. `replace(str, pattern, fn)` → function-based replacement
2. `capture(str, pattern)` → named capture groups (requires `@name:` syntax in patterns)
3. `find_first(str, pattern)` → single match map or null

---

## 5. RE2 API Reference (Relevant Methods)

| RE2 Method | Use Case | Lambda Feature |
|-----------|----------|----------------|
| `RE2::FullMatch(str, re2)` | Full string match | `is`, `match` |
| `RE2::PartialMatch(str, re2)` | Contains match | Internal (partial match check) |
| `RE2::FindAndConsume(&input, re2, &match)` | Iterate all matches | `find()` |
| `RE2::GlobalReplace(&str, re2, repl)` | Replace all matches | `replace()` |
| `RE2::Replace(&str, re2, repl)` | Replace first match | `replace()` with `{limit: 1}` |

---

## 6. Syntax Summary

```lambda
// === Pattern Definition ===
string digit = \d
string word = \w+
string email = \w+ "@" \w+ "." \w+

// === 1. Type Check (is) — WORKING ===
"hello@world.com" is email              // true
"abc" is email                          // false

// === 2. Match Expression — IMPLEMENTED ✅ ===
match input {
    case email: "it's an email"
    case digit: "it's a digit"
    default: "something else"
}

// === 3. Replace — IMPLEMENTED ✅ ===
replace("a1b2c3", digit, "X")           // "aXbXcX"
string ws = \s+
replace("hello   world", ws, " ")       // "hello world"

// === 4. Split — IMPLEMENTED ✅ ===
split("a1b2c3", digit)                  // ["a", "b", "c"]
split("hello   world", ws)              // ["hello", "world"]
split("a1b2c3", digit, true)            // ["a", "1", "b", "2", "c", "3"]  — keep delimiters

// === 5. Find — IMPLEMENTED ✅ ===
string digits = \d+
string alpha = \a+
find("a1b22c333", digits)               // [{value: "1", index: 1}, {value: "22", index: 3}, ...]
find("hello world", alpha)              // [{value: "hello", index: 0}, {value: "world", index: 6}]
find("hello world hello", "lo")        // [{value: "lo", index: 3}, {value: "lo", index: 14}]

// File-based find/replace (grep/sed) → see Lambda_Type_String_Grep.md
```

---

## 7. Design Decisions & Rationale

### 7.1 `find()` Returns List of Match Maps

Each match is a map `{value: string, index: int}` containing both the matched substring and its start position. This follows the C#/Rust model where a single match object provides all useful information.

**Cross-language survey:**

| Language | Primary find | Returns | Index variant |
|----------|-------------|---------|---------------|
| Python | `re.findall()` | **strings** | `re.finditer()` → Match objects |
| JavaScript | `str.match(/g/)` | **strings** | `str.matchAll()` → objects w/ `.index` |
| Ruby | `str.scan()` | **strings** | `str =~ regex` → first index |
| Go | `FindAllString()` | **strings** | `FindAllStringIndex()` → `[][]int` |
| Rust | `find_iter()` | **Match** (`.as_str()` + `.start()`) | same object |
| C# | `Regex.Matches()` | **Match** (`.Value` + `.Index`) | same object |

**Rationale:** The C#/Rust approach avoids needing separate `find()` and `find_at()` functions. Lambda adopts this: a single `find()` returns match maps. Users access `.value` for the substring or `.index` for the position, or pipe through `map(~.value)` to get a plain string list.

### 7.2 `replace()` Replaces All (Not First)

Following the convention of most functional languages and Python's `re.sub()`, `replace()` replaces all occurrences.

**Rationale:** Lambda already has `replace(str, old, new)` for plain strings that replaces all. Pattern-based replacement should be consistent.

### 7.3 Full Match Semantics for `is` and `match`

The `is` operator and `match` case arms use **full-match** semantics (the entire string must match the pattern). This is consistent with type checking: `"123" is int` checks if the whole value is an int, not if it contains an int somewhere.

### 7.4 Partial Match Semantics for `find`, `replace`, `split`

These functions use **partial/search** semantics, finding pattern occurrences within the string. This is the standard behavior in all major languages.

### 7.5 No Separate `matches()` Function

The `is` operator serves as the match-test function. No separate `matches(str, pattern) -> bool` is needed since `str is pattern` is more idiomatic in Lambda.

---

## 8. Design Decisions (Resolved)

1. **Function replacement:** `replace(str, pattern, fn)` is **deferred** to a future release. Initial implementation supports string-only replacement.

2. **`find()` with string arg:** `find("hello world", "lo")` **will be supported**. When the second argument is a plain string, `find()` performs literal substring search, returning all occurrences.

3. **No inline patterns in expression context:** Inline patterns like `replace(str, \d+, "X")` are **not supported** by the grammar. Patterns must be defined with `string name = ...` and referenced by name. Allowing inline patterns in expression context would cause significant syntax ambiguities (e.g., `\d+` could be confused with escape sequences in string context, `+` with arithmetic, etc.).

4. **`split()` keep-delimiter mode:** Supported via optional 3rd boolean argument: `split(str, sep, true)`. When `true`, matched delimiters are interleaved in the result list. Works for both plain-string and pattern separators.

5. **Empty pattern match behavior:** Follow RE2 default behavior (advance past zero-length matches). Document this.

6. **`find()` return type:** Returns list of **match maps** `{value: string, index: int}`, following C#'s `Regex.Matches()` and Rust's `find_iter()`. This eliminates the need for a separate `find_at()` function — one `find()` provides both substrings and positions.

> Design decisions 7–11 (file-based find/replace, directory recursion, procedural separation, options map) have been moved to [`vibe/Lambda_Type_String_Grep.md`](Lambda_Type_String_Grep.md).

## 9. Files to Modify

| File | Changes |
|------|---------|
| `lambda/lambda-data.hpp` | Add `re2_unanchored` to `TypePattern`; add `SYSFUNC_FIND` |
| `lambda/re2_wrapper.hpp` | Declare `pattern_find_all`, `pattern_replace_all`, `pattern_split`, `pattern_get_unanchored` |
| `lambda/re2_wrapper.cpp` | Implement above functions; lazy unanchored regex compilation |
| `lambda/lambda-eval.cpp` | Add pattern branches to `fn_replace()` and `fn_split()`; implement `fn_find()` (string mode) |
| `lambda/build_ast.cpp` | Register `find` sys func (2-3 args) |
| `lambda/mir.c` | Register `fn_find` function pointers |
| `lambda/transpile.cpp` | ✅ Fixed match arm generation for pattern types (C transpiler path) |
| `lambda/transpile-mir.cpp` | ✅ Verified MIR match arm with pattern works (already had correct `const_pattern()` handling) |

| `test/lambda/` | Add test scripts + expected results for string find, file find, file replace |

---

## 10. Implementation Notes & Bugs Found

### 10.1 Overloaded Function Registration Pattern

When a sys func is marked `is_overloaded=true` in `build_ast.cpp`, the C transpiler appends the argument count to the function name (e.g., `fn_replace` → `fn_replace3`, `fn_split` → `fn_split2`/`fn_split3`, `fn_find` → `fn_find2`/`fn_find3`). These overloaded names must be:
1. Declared in `lambda.h` (e.g., `Item fn_replace3(Item, Item, Item);`)
2. Regenerated into `lambda-embed.h` via `xxd -i lambda/lambda.h > lambda/lambda-embed.h`
3. Registered in `mir.c` `func_list[]` with the overloaded name pointing to the actual function

### 10.2 Shared Library Guard

`re2_wrapper.cpp` is linked into both the main executable and the `lambda-input-full-cpp` shared library. The new runtime functions (`pattern_find_all`, `pattern_split`, `pattern_replace_all`, `create_match_map`, etc.) depend on symbols only available in the main executable (`heap_alloc`, `list`, `list_push`, `heap_calloc`, `context`). These are guarded with `#ifndef SIMPLE_SCHEMA_PARSER` to prevent linker errors in the shared library build.

### 10.3 Bug: `replace(str, old, "")` Crash — Pre-existing, Fixed

**Root cause:** The Lambda transpiler converts empty string literal `""` to `ITEM_NULL` in the generated C code. When `fn_replace()` received `ITEM_NULL` as the 3rd argument (replacement), it set `new_type = LMD_TYPE_STRING` but still called `new_item.get_chars()` on the null item, causing a NULL pointer dereference (fault_addr=0x0, misreported as stack overflow by the signal handler).

**Fix:** Added a `new_is_null` flag in `fn_replace()` to track when the replacement is `ITEM_NULL`. When true, uses `""` / `0` directly instead of calling `.get_chars()` / `.get_len()` on the null item. Applied to both the pattern-based and plain-string replacement paths.

**Note:** This was a pre-existing bug (confirmed by testing on pre-change code via `git stash`), not introduced by the pattern enhancement.

### 10.4 Bug: `create_match_map()` Stored Wrong Pointer Type — Fixed

The initial implementation stored `val_str->chars` (a `char*` inside the String struct) instead of `val_str` (the `String*` itself) at the map's value field offset. Since the map field type is `LMD_TYPE_STRING`, the runtime expects a `String*` pointer. Fixed to store the `String*` directly.

### 10.5 Bug: Missing `__thread` on `extern` Context Declaration — Fixed

The `context` variable is defined as `__thread EvalContext* context` in `runner.cpp`. The `extern` declaration in `re2_wrapper.cpp` was initially `extern Context* context;` (wrong type, missing `__thread`), causing it to read from an uninitialized global instead of the thread-local storage. Fixed to `extern __thread EvalContext* context;`.

### 10.6 Test Suite

All features are tested in `test/lambda/string_pattern_ops.ls` with expected output in `test/lambda/string_pattern_ops.txt`. The test covers:
- Pattern replace (6 cases including empty replacement, no-match)
- Pattern split (4 cases)
- Pattern split with keep delimiters (2 cases)
- Pattern find (3 cases including no-match)
- Plain string find (3 cases)
- String split with keep delimiters (2 cases)
- Replace with empty string edge cases (3 cases)

All 525 Lambda baseline tests pass with zero regressions (523 pre-existing + 2 new test files).

---

## 11. Files Modified

| File | Changes |
|------|---------|  
| `lambda/lambda-data.hpp` | Added `re2_unanchored` to `TypePattern`; added `SYSFUNC_FIND`, `SYSFUNC_FIND3`, `SYSFUNC_SPLIT3` enums |
| `lambda/re2_wrapper.hpp` | Declared `pattern_get_unanchored()`, `pattern_find_all()`, `pattern_replace_all()`, `pattern_split()` |
| `lambda/re2_wrapper.cpp` | Implemented all above functions + `create_match_map()` + `make_heap_string()` + `create_match_map_ext()`. Fixed `extern __thread` declaration. Added `#ifndef SIMPLE_SCHEMA_PARSER` guards. |
| `lambda/lambda-eval.cpp` | Added pattern branches to `fn_replace()` and `fn_split()`. Implemented `fn_find2()`, `fn_find3()`, `fn_split3()`. Fixed `ITEM_NULL` replacement crash. |
| `lambda/build_ast.cpp` | Registered `SYSFUNC_FIND` (2-arg, overloaded), `SYSFUNC_FIND3` (3-arg). Made `SYSFUNC_SPLIT` and `SYSFUNC_REPLACE` overloaded. Added `SYSFUNC_SPLIT3`. |
| `lambda/mir.c` | Registered `fn_replace3`, `fn_split2`, `fn_split3`, `fn_find2`, `fn_find3` in `func_list[]` |
| `lambda/lambda.h` | Added declarations: `fn_replace3()`, `fn_split2()`, `fn_split3()`, `fn_find2()`, `fn_find3()` |
| `lambda/lambda-embed.h` | Regenerated via `xxd -i lambda/lambda.h` |
| `lambda/transpile.cpp` | Added pattern ident check in `transpile_match_condition()` — emits `const_pattern(index)` for `AST_NODE_STRING_PATTERN`/`AST_NODE_SYMBOL_PATTERN` |
| `test/lambda/string_pattern_ops.ls` | New test script (23 test cases across 7 sections) |
| `test/lambda/string_pattern_ops.txt` | Expected output file |
| `test/lambda/match_string_pattern.ls` | New test script for match+pattern arms (13 test cases across 4 sections) |
| `test/lambda/match_string_pattern.txt` | Expected output file |

---

## References

- [Lambda String Pattern v1](./Lambda_Type_String_Pattern.md)
- [Lambda Type System](../doc/Lambda_Type.md)
- [Lambda System Functions](../doc/Lambda_Sys_Func.md)
- [RE2 Documentation](https://github.com/google/re2/wiki/Syntax)
- [RE2 C++ API](https://github.com/google/re2/blob/main/re2/re2.h)
