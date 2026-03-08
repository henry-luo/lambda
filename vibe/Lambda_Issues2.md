# Lambda Issues Encountered During JetStream Benchmark Porting

Issues discovered while porting JetStream JavaScript benchmarks to Lambda Script.

---

## 1. `float[]` Type Annotation Rejects List Literals (Type 17 vs 22)

**Severity**: Compilation error  
**Affected benchmarks**: cube3d, navier_stokes, raytrace3d

When a function parameter is annotated with `float[]`, passing a list literal like `[1.0, 0.0, 0.0]` fails with:

```
error[E201]: argument N has incompatible type 17, expected 22
```

Type 17 is a list (from `[...]` literal), type 22 is a typed array (from `fill()`). These are incompatible even though both support indexed access.

**Workaround**: Remove `float[]` type annotations from function parameters — use untyped parameters instead.

```lambda
// FAILS: list literal [1.0, 0.0] is type 17, float[] expects type 22
pn vec_add(v1: float[], v2: float[]) { ... }
var r = vec_add([1.0, 0.0], [0.0, 1.0])  // error

// WORKS: remove type annotation
pn vec_add(v1, v2) { ... }
var r = vec_add([1.0, 0.0], [0.0, 1.0])  // ok
```

**Suggestion**: Either allow implicit coercion from list literal to `float[]`, or provide a way to create typed arrays from literals (e.g., `float[1.0, 0.0]`).

---

## 2. Parenthesized Field Access Cannot Be Used as Assignment Target

**Severity**: Compilation error (syntax error near `=`)  
**Affected benchmarks**: splay, richards, hashmap, deltablue

Assigning through a parenthesized expression like `(map.field)[idx] = value` or `(tree.root).right = null` produces a syntax error:

```
error[E100]: Unexpected syntax near '=' [=]
```

Read-only access `(map.field)[idx]` works fine — only assignment is broken.

**Workaround**: Extract to a local variable first, then mutate through the local.

```lambda
// FAILS
(tree.root).right = null
(hm.buckets)[idx] = entry

// WORKS
var root_ref = tree.root
root_ref.right = null

var buckets = hm.buckets
buckets[idx] = entry
```

**Note**: This is especially painful for nested data structures (trees, hash maps) where you frequently need to mutate fields of objects reached through another object's field.

---

## 3. `list` Is a Reserved Keyword (Silent Failure)

**Severity**: Runtime bug (silent, no error)  
**Affected benchmarks**: richards

Using `list` as a map field name does not produce a compile error, but assignments to it silently fail — the value is always `null` when read back.

```lambda
var sched = {list: null, count: 0}
sched.list = some_value
print(string(sched.list))  // prints "null" — assignment was silently ignored
```

**Workaround**: Rename the field (e.g., `task_list`).

**Suggestion**: Either produce a compile-time error when reserved words are used as field names, or allow them (since field names are strings, not identifiers in most languages).

---

## 4. No Hexadecimal Literal Support

**Severity**: Compilation error  
**Affected benchmarks**: crypto_sha1, richards

Lambda does not support hex literals like `0xFF` or `0xD008`. These produce parse errors.

**Workaround**: Manually convert all hex values to decimal.

```lambda
// FAILS
var mask = 0xFFFFFFFF

// WORKS
var mask = 4294967295
```

Common conversions needed:
- `0x80` → `128`
- `0xFF` → `255`
- `0xF` → `15`
- `0xD008` → `53256`
- `0x5A827999` → `1518500249`
- `0xFFFFFFFF` → `4294967295`

**Suggestion**: Add hex literal support (`0x...`). This is standard in nearly every language and essential for bitwise/crypto code.

---

## 5. Integer Overflow in Multiplication (No Silent Wraparound)

**Severity**: Runtime error  
**Affected benchmarks**: splay, crypto_sha1

Lambda's `int` type overflows on multiplication of large values, producing `<error>` / `nan` instead of wrapping. JavaScript uses floats for all numbers, so `49734321 * 1103515245` wraps silently in 32-bit PRNG code.

```
[ERR!] integer overflow in multiplication
```

This makes it impossible to directly port standard LCG pseudo-random number generators or 32-bit hash computations.

**Workaround for PRNG**: Use a different PRNG algorithm that avoids large intermediate products (e.g., Park-Miller with Schrage's method):

```lambda
pn next_random(state) {
    var s = state.seed
    var hi = s / 127773
    var lo = s % 127773
    s = 16807 * lo - 2836 * hi
    if (s <= 0) { s = s + 2147483647 }
    state.seed = s
    return float(s) / 2147483647.0
}
```

**Suggestion**: Either provide explicit 32-bit wrapping arithmetic functions (e.g., `mul32`, `add32`), or add an `int32` type with wraparound semantics.

---

## 6. `shr` Performs Arithmetic (Sign-Extending) Right Shift

**Severity**: Wrong results  
**Affected benchmarks**: crypto_sha1

Lambda's `shr` function performs an arithmetic right shift, preserving the sign bit. JavaScript's `>>>` is a logical (unsigned) right shift that fills with zeros. This produces different results for negative numbers, which is critical in SHA-1 and other bitwise algorithms.

```lambda
// Lambda: shr(-1, 1) → -1  (sign bit preserved)
// JS:     -1 >>> 1  → 2147483647  (zero-filled)
```

**Workaround**: Mask with `band(x, 4294967295)` before shifting, or implement a `ushr` function:

```lambda
pn ushr(x: int, n: int) {
    return shr(band(x, 4294967295), n)
}
```

**Note**: This workaround may still not work correctly if `shr` sign-extends regardless of the input range. A native unsigned right shift (`ushr` or `>>>`) would be needed.

**Suggestion**: Add a `ushr` (unsigned/logical right shift) built-in function.

---

## 7. `fill(0, value)` Returns Null Instead of Empty Array

**Severity**: Runtime error (null concatenation)  
**Affected benchmarks**: deltablue

`fill(0, 0)` returns `null` rather than an empty array. Subsequent concatenation with `++` fails:

```
runtime error [201]: fn_join: unsupported operand types: null and array
```

**Workaround**: Guard concatenation with null checks:

```lambda
var cl = var1.constraints  // may be null from fill(0, 0)
if (cl == null) {
    var1.constraints = [idx]
} else {
    var1.constraints = cl ++ [idx]
}
```

**Suggestion**: `fill(0, value)` should return an empty array `[]`, not `null`.

---

## 8. Missing `substr` and `charcode` Functions

**Severity**: Compilation error  
**Affected benchmarks**: crypto_sha1

Lambda does not have `substr(str, start, len)` or `charcode(str, idx)` functions that are common in other languages.

**Workaround**:
- `substr(str, start, len)` → `slice(str, start, start + len)`
- `charcode(str, idx)` → `ord(slice(str, idx, idx + 1))`

---

## 9. `div` Is a Reserved Word

**Severity**: Compilation error (syntax error near `=`)  
**Affected benchmarks**: navier_stokes

Using `div` as a variable or parameter name produces a syntax error when it appears as an assignment target.

**Workaround**: Rename the variable (e.g., `dv`).

```lambda
// FAILS
pn project(u, v, p, div, iterations: int) {
    div[idx] = ...  // syntax error near '='

// WORKS
pn project(u, v, p, dv, iterations: int) {
    dv[idx] = ...
```

---

## 10. Parameters Are Immutable (No Reassignment)

**Severity**: Compilation error  
**Affected benchmarks**: splay

Function parameters cannot be reassigned. This is intentional design but surprising when porting from JavaScript where parameters are mutable.

```
error[E211]: cannot assign to 'idx': parameter bindings are immutable
```

**Workaround**: Copy the parameter to a `var` local:

```lambda
// FAILS
pn traverse(node, keys, idx) {
    idx = traverse(node.left, keys, idx)  // error: idx is immutable

// WORKS
pn traverse(node, keys, idx_in) {
    var idx = traverse(node.left, keys, idx_in)  // ok: idx is a new local var
```

---

## 11. Integer Division Returns Float, Causing Silent Failures in `slice`/`ord`

**Severity**: Runtime bug (silent wrong results — returns 0 instead of correct value)  
**Affected benchmarks**: crypto_sha1

When dividing two integers with `/`, the result is a float rather than an integer — even when the division is exact (e.g., `0 / 8 = 0.0`). The float value `string()`-ifies identically to the int (`"0"` not `"0.0"`), making it very hard to diagnose.

When this float is passed to `slice(str, start, end)` or `ord()`, the functions silently return wrong results (empty strings or 0) instead of raising a type error.

```lambda
var i: int = 0
let CHRSZ = 8
var char_idx = i / CHRSZ      // char_idx is 0.0 (float), not 0 (int)
ord(slice(s, char_idx, char_idx + 1))  // returns 0 instead of 97!

var char_idx2: int = i / CHRSZ  // explicitly typed as int — works correctly
ord(slice(s, char_idx2, char_idx2 + 1))  // returns 97 ✓
```

**Workaround**: Always explicitly type variables that hold division results as `int`:

```lambda
var char_idx: int = i / CHRSZ  // explicit int annotation
```

**Suggestion**: Either (1) make integer `/` integer produce an integer result (like Python's `//`), or (2) have `slice` and `ord` accept float indices by truncating to int, or (3) produce a type error when a float is passed where an int is expected.

---

## 12. `shl` Operates on 64-bit Integers (No 32-bit Masking)

**Severity**: Wrong results in bitwise code  
**Affected benchmarks**: crypto_sha1

Lambda's `shl` operates on full 64-bit integers, so left-shifting a 32-bit value can produce results exceeding 32 bits. JavaScript's `<<` implicitly truncates to 32 bits.

```lambda
shl(1732584193, 5)   // Lambda: 55442694176 (33+ bits)
// JavaScript: 1732584193 << 5 → -390880736 (truncated to 32-bit signed)
```

This affects any code that rotates or shifts 32-bit values (SHA-1, MD5, CRC, etc.).

**Workaround**: Always mask `shl` results with `band(shl(x, n), 4294967295)`.

```lambda
pn rol(num: int, cnt: int) {
    var n = band(num, 4294967295)
    return band(bor(shl(n, cnt), shr(n, 32 - cnt)), 4294967295)
}
```

**Suggestion**: Consider adding `shl32`/`shr32` variants, or a general `u32(x)` masking function.

---

## 13. `@` Path Literals Do Not Parse

**Severity**: Compilation error (parse error)  
**Affected benchmarks**: regex_dna

The documentation describes `@` path literals for file I/O operations (e.g., `input(@./data.json)`), but these do not parse at all — neither relative nor absolute paths:

```
error[E100]: Unexpected syntax near '@./' [ERROR, ., /]
error[E100]: Unexpected syntax near '@/' [ERROR, /]
```

Both forms fail:

```lambda
// FAILS: relative path literal
let data = input(@./data.json, 'json')

// FAILS: absolute path literal
let data = input(@/Users/name/data.json, 'json')
```

**Workaround**: Use plain string paths instead of `@` path literals.

```lambda
// WORKS
let data^err = input("./data.json", "json")
```

**Note**: The `Lambda_Sys_Func.md` documentation extensively shows `@` path literals in examples, but they appear to be unimplemented (or the grammar doesn't support them in the current build).

---

## 14. `slice()` Has No 2-Argument Form (Missing Slice-to-End)

**Severity**: Compilation error (`call to undefined function 'slice'`)  
**Affected benchmarks**: regex_dna

Calling `slice(str, start)` to get a substring from `start` to the end fails because Lambda only recognizes the 3-argument form `slice(str, start, end)`. The 2-arg call is treated as an entirely different (undefined) function.

```lambda
// FAILS
var tail = slice("hello", 2)  // error: call to undefined function 'slice'

// WORKS
var tail = slice("hello", 2, len("hello"))  // "llo"
```

**Workaround**: Always provide the third argument, using `len(str)` for slice-to-end.

**Suggestion**: Support the common 2-argument form `slice(str, start)` as shorthand for `slice(str, start, len(str))`. This is standard across JS, Python, and most languages.

---

## 15. No Case-Insensitive Mode for String Patterns

**Severity**: Missing feature (affects correctness)  
**Affected benchmarks**: regex_dna

Lambda's string patterns (which compile to RE2 regex) are always case-sensitive. There is no way to perform case-insensitive matching — neither via an inline flag like `(?i:...)` nor via a pattern modifier.

JavaScript's regex-dna benchmark uses `/agggtaaa|tttaccct/ig` — the `i` flag for case-insensitive matching is essential because the DNA data contains mixed-case characters.

```lambda
string Pat = "abc" | "def"

// This pattern only matches lowercase — no way to make it case-insensitive
find("ABCdefABC", Pat)  // finds only "def", misses "ABC"
```

**Workaround**: Pre-lowercase the input data and use lowercase patterns.

```lambda
// Precompute lowercase DNA (outside the benchmark loop)
var dna_lower = lower(dna)

// Then match with lowercase patterns
string Pat = "agggtaaa" | "tttaccct"
find(dna_lower, Pat)
```

**Suggestion**: Add case-insensitive support, either:
- A pattern modifier: `string Pat = "abc" | "def" /i`
- Or an optional flag on `find()`/`replace()`: `find(str, Pat, {case_insensitive: true})`

---

## 16. No Built-in Replace-First Function

**Severity**: Missing feature  
**Affected benchmarks**: regex_dna

Lambda's `replace()` always replaces **all** occurrences (global replacement), with both plain strings and patterns. There is no way to replace only the first occurrence.

This differs from JavaScript, where `str.replace(string, string)` only replaces the first match (global requires `str.replace(/regex/g, repl)` or `str.replaceAll()`).

```lambda
replace("aBcBdB", "B", "X")  // "aXcXdX" — all replaced

// No way to get "aXcBdB" (first only) without manual implementation
```

**Workaround**: Implement `replace_first()` manually using `find()` and `slice()`:

```lambda
pn replace_first(s, search, repl) {
    var matches = find(s, search)
    if (len(matches) == 0) {
        return s
    }
    var pos = matches[0].index
    var slen = len(search)
    return slice(s, 0, pos) ++ repl ++ slice(s, pos + slen, len(s))
}
```

**Note**: This workaround is inefficient for large strings because `find()` locates *all* matches when we only need the first. A built-in `replace_first()` or an optional `count` parameter on `replace()` would be more efficient.

**Suggestion**: Add either `replace_first(str, pattern, repl)` or extend `replace()` with an optional count: `replace(str, pattern, repl, 1)`.

---

## 17. Decimal Values Become Zero When Passed as Function Arguments

**Severity**: Compilation/runtime bug
**Affected benchmarks**: bigdenary

When a `decimal` value (with either `n` or `N` suffix) is passed as an argument to a `pn` function, the received value is `0` instead of the original decimal. This happens with both global `let` constants and local variables.

```lambda
let BD1 = 3.14159N

pn show(d) {
    print(string(d) ++ "\n")  // prints "0" instead of "3.14159"
}

pn main() {
    show(BD1)         // 0
    show(3.14159N)    // 0
    let x = 2.71828N
    show(x)           // 0
}
```

**Workaround**: Access global decimal constants directly inside functions instead of passing them as parameters.

```lambda
let BD1 = 3.14159N

pn show() {
    print(string(BD1) ++ "\n")  // prints "3.14159" — works correctly
}
```

**Suggestion**: Fix the transpiler or calling convention to correctly pass decimal values through function arguments.

---

## Summary Table

| # | Issue | Type | Impact |
|---|-------|------|--------|
| 1 | `float[]` rejects list literals | Type system | 3 benchmarks, 50+ errors |
| 2 | Can't assign through parenthesized access | Syntax | 4 benchmarks, core pattern |
| 3 | `list` field name silent failure | Reserved word | 1 benchmark, hours of debugging |
| 4 | No hex literals | Missing feature | 2 benchmarks, tedious manual conversion |
| 5 | Integer overflow errors | Arithmetic | 2 benchmarks, wrong results |
| 6 | No unsigned right shift | Missing feature | 1 benchmark, wrong hash output |
| 7 | `fill(0, x)` returns null | Edge case | 1 benchmark, 120k runtime errors |
| 8 | Missing substr/charcode | Missing feature | 1 benchmark, minor |
| 9 | `div` reserved word | Reserved word | 1 benchmark, minor |
| 10 | Immutable parameters | Language design | 1 benchmark, minor |
| 11 | Integer division returns float | Type inference | 1 benchmark, silent wrong results |
| 12 | `shl` 64-bit overflow | Bitwise ops | 1 benchmark, wrong hash output |
| 13 | `@` path literals don't parse | Missing feature | 1 benchmark, doc mismatch |
| 14 | `slice()` no 2-argument form | Missing feature | 1 benchmark, minor |
| 15 | No case-insensitive patterns | Missing feature | 1 benchmark, workaround needed |
| 16 | No replace-first function | Missing feature | 1 benchmark, manual workaround |
| 17 | Decimal args become zero | Runtime bug | 1 benchmark, workaround needed |
