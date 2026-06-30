# Lambda Issues Encountered During JetStream Benchmark Porting

Issues discovered while porting JetStream JavaScript benchmarks to Lambda Script.

---

## 1. `float[]` Type Annotation Rejects List Literals (Type 17 vs 22) — Fixed

**Status**: ✅ **Fixed** in the AST call-site type checker.
**Severity**: Was a compilation error.
**Affected benchmarks**: cube3d, navier_stokes, raytrace3d.

When a function parameter was annotated with `float[]`, passing a list literal like `[1.0, 0.0, 0.0]` failed before runtime coercion could run:

```
error[E201]: argument N has incompatible type 17, expected 22
```

The exact enum numbers have shifted in current builds (`Array` literals report as type 18 and the `float[]` annotation wrapper reports as type 23), but the failing shape was the same: the AST checker rejected a generic list literal before MIR parameter binding could call `ensure_typed_array()`.

**Fix**: Function-call type validation now consults the parameter's full occurrence type (`float[]`, `int[]`, `int64[]`) and accepts list literals whose known element type is compatible with the existing `ensure_typed_array()` conversion rules. Mixed literals such as `[1.0, "bad"]` are still rejected.

```lambda
// now works: list literals are accepted and coerced for float[] params
pn vec_add(v1: float[], v2: float[]) { ... }
var r = vec_add([1.0, 0.0], [0.0, 1.0])
```

**MIR follow-up**: MIR Direct keeps `float[]`/numeric-array parameters on the boxed container ABI, runs `ensure_typed_array()` at function entry, and preserves `ArrayNum` float/int fast paths when the index expression is boxed/unknown.

**Regression coverage**: `test/lambda/proc/proc_typed_array_param.ls` includes a direct list-literal call to a `float[]` parameter function. The affected JetStream Lambda benchmark ports have had the old untyped-parameter workaround removed for numeric vector/matrix/grid helpers.

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

## 3. Type Keywords as Map Field Names Were Treated as Reserved in Assignment

**Status**: ✅ **Fixed** in the AST member-assignment builder.  
**Severity**: Was a runtime bug (assignment silently ignored after a logged runtime error).  
**Affected benchmarks**: richards

Using `list` as a map field name did not produce a compile error, but assignment through dot-member syntax failed: the value stayed `null` when read back. The same shape also affected other type keywords such as `map`, `string`, and `int` when used as field names.

```lambda
// now works
var sched = {list: null, count: 0}
sched.list = some_value
print(string(sched.list))  // prints some_value
```

**Design decision**: Lambda aligns with JavaScript property semantics here. A type keyword is still reserved where the grammar expects a type or variable binding, but in map keys and dot-member field positions it is a field/property name. Therefore `{list: ...}` and `sched.list` are legal and should not require quoting or renaming.

**Fix**: Member assignment now normalizes identifier-like field tokens, including `base_type` tokens such as `list`, into `AST_NODE_IDENT`, matching the existing read-side member access path. This lets the MIR assignment lowering emit the field name as a string key for `fn_map_set()`.

**Regression coverage**: `test/lambda/proc/proc_member_keyword_field.ls` covers `list` and other type-keyword field names in map literals, dot-member reads, and dot-member assignments.

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

**Status**: ✅ **FIXED**
**Severity**: Runtime error  
**Affected benchmarks**: splay, crypto_sha1

Lambda now supports explicit fixed-width integer annotations with deterministic wraparound. Compact integer arithmetic follows the Go-like model documented in `vibe/Lambda_Type_Int.md`: `i8/i16/i32/i64` wrap in signed two's-complement width, and `u8/u16/u32/u64` wrap modulo their width.

The original issue was that Lambda's default `int` type overflowed on multiplication of large values, producing `<error>` / `nan` instead of wrapping. JavaScript uses floats for all numbers, but benchmark PRNG/hash code often depends on explicit 32-bit wrap semantics.

```
[ERR!] integer overflow in multiplication
```

This previously made it impossible to directly port standard LCG pseudo-random number generators or 32-bit hash computations.

**Benchmark cleanup**:

- `test/benchmark/jetstream/splay.ls` and `test/benchmark/jetstream/splay2.ls` now use the direct `u32` LCG:

```lambda
pn next_random(state) {
    var s: u32 = state.seed
    s = s * 1103515245u32 + 12345u32
    state.seed = int(s)
    return float(s) / 4294967296.0
}
```

- `test/benchmark/jetstream/crypto_sha1.ls` now uses `u32` locals inside `safe_add()` so 32-bit word addition wraps by type instead of by overflow-prone default `int` arithmetic.

**Follow-up**: issue 6 now covers the typed unsigned shift fix. SHA-1 still keeps explicit masks around `bnot`/round helpers where default `int` bitwise semantics are intentionally used.

---

## 6. `shr` Performs Arithmetic (Sign-Extending) Right Shift

**Status**: ✅ **FIXED for explicit unsigned integer operands**
**Severity**: Wrong results  
**Affected benchmarks**: crypto_sha1

Lambda's `shr` now follows the Go-like explicit integer model for typed operands. Signed compact operands (`i8/i16/i32/i64`) still perform arithmetic right shift, preserving the sign bit. Unsigned compact operands (`u8/u16/u32/u64`) perform logical right shift, filling with zeros.

```lambda
shr(-1i32, 1)          // -1i32, sign bit preserved
shr(4294967295u32, 1)  // 2147483647u32, zero-filled
```

The original issue was that SHA-1-style code needed JS `>>>` semantics but only had default signed `int` shifting. The intended Lambda spelling is now to keep the word in `u32` and call `shr`:

```lambda
pn rol(num: int, cnt: int) {
    var n: u32 = num
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}
```

**Remaining optional convenience**: Lambda still has no JS-spelling `>>>` or `ushr` alias. That is now syntactic convenience rather than missing core behavior.

---

## 7. `fill(0, value)` Returns Null Instead of Empty Array

**Severity**: Runtime error (null concatenation)  
**Affected benchmarks**: deltablue

**Status**: ✅ **FIXED**.

`fill(0, value)` now returns a real empty array with `length = 0` and `capacity = 0`.
The result participates in normal collection operations:

```lambda
let cl = fill(0, 0)
cl ++ [idx]     // [idx]
len(cl)         // 0
```

**Fix notes**:
- `fn_fill()` keeps a dedicated zero-count branch that returns `[]`, not `null`.
- `test/lambda/proc/proc_fill.ls` now verifies `fill(0, int)`, `fill(0, null)`, typed `int[] = fill(0, 0)`, and concatenation from each empty result.
- `test/benchmark/jetstream/deltablue.ls` and `test/benchmark/jetstream/deltablue2.ls` now rely on `cl ++ [idx]` directly instead of guarding against `null`.

**Reference behavior from other languages/scripts**:
- Python has no direct `fill(n, value)` builtin, but equivalent construction idioms return empty lists for zero count: `[value] * 0`, `list(range(0))`, and `list(itertools.repeat(value, 0))` all produce `[]`.
- JavaScript follows the same collection rule: `new Array(0).fill(value)` returns `[]`, and `new Uint32Array(0).fill(value)` remains an empty typed array.
- Ruby and Rust mirror this behavior with `Array.new(0, value)` and `vec![value; 0]`, respectively.
- The design decision for Lambda is therefore: zero count constructs an empty collection; negative count is the error case.

**Remaining edge policy**: `fill(n, value)` rejects negative `n` as an error.

---

## 8. Missing `substr` and `charcode` Functions

**Status**: ✅ **No Fix**
**Severity**: Compilation error  
**Affected benchmarks**: crypto_sha1

Lambda does not expose JavaScript-style `substr(str, start, len)` or `charcode(str, idx)` aliases. This is intentional: Lambda already has native string indexing, range subscripting, and `ord()`/`chr()` Unicode code point helpers.

**Lambda spelling**:
- substring by explicit inclusive indices: `str[start to end]`
- substring by start plus length: `slice(str, start, start + len)`
- character code at index: `ord(str[idx])`

```lambda
"abcdef"[1 to 3]       // "bcd"
slice("abcdef", 1, 4)  // "bcd"
ord("ABC"[2])          // 67
ord("café"[3])         // 233
```

**Verification**: `test/lambda/string_indexable.ls` covers `str[idx]`, UTF-8-aware indexing, and `str[start to end]`. `test/lambda/string_ord_chr.ls` covers `ord()` and `chr()`.

---

## 9. `div` Is a Reserved Word

**Status**: ✅ **FIXED**.
**Severity**: Compilation error (syntax error near `=`)  
**Affected benchmarks**: navier_stokes

`div` is still the integer-division operator in expression context (`a div b`), but it is no longer treated as a reserved word for identifiers. It can be used as a local variable or parameter name, including as an indexed assignment target.

Because `div` remains an operator, a line starting with `div[...]` immediately after another expression can still be parsed as a continuation of the previous expression unless the previous statement is clearly terminated. Use a statement separator in that case, just as with other expression-continuation ambiguities.

**Former workaround**: Rename the variable (e.g., `dv`). This is no longer needed.

```lambda
pn project(u, v, p, div, iterations: int) {
    div[idx] = value  // now parses and assigns correctly
}

pn main() {
    var div = [1, 2, 3];
    project(null, null, null, div, 1)
}
```

**Verification**: `test/lambda/proc/proc_div_identifier.ls` covers `div` as a procedural parameter, local variable, and indexed assignment target.

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

**Status**:  ✅ **FIXED**.
**Severity**: Runtime bug (silent wrong results — returns 0 instead of correct value)  
**Affected benchmarks**: crypto_sha1

Lambda `/` correctly returns float, including for exact integer division (`0 / 8 = 0.0`). The bug was that `slice(str, start, end)` and array/vector `slice(vec, start, end)` rejected integral float indices such as `0.0` and `7.0`, which made `ord(slice(...))` collapse to `0` in ported benchmark code.

`slice()` now accepts integer-valued floats for start/end indices. `ord()` works with the resulting one-character string, and `ord(str[idx])` already supports integer-valued float indices through string indexing.

```lambda
var i: int = 0
let CHRSZ = 8
var char_idx = i / CHRSZ      // char_idx is 0.0 (float), not 0 (int)
ord(slice(s, char_idx, char_idx + 1))  // 97
ord(s[char_idx])                       // 97

slice("abcdefghi", 7.0, 8.0)           // "h"
```

**Remaining edge policy**: Fractional floats such as `1.5` are still rejected as index errors rather than truncated.

**Verification**: `test/lambda/slice_float_indices.ls` covers integral float indices for string slicing, UTF-8 strings, array slicing, 2-arg `slice`, and fractional-float rejection.

---

## 12. `shl` Operates on 64-bit Integers (No 32-bit Masking)

**Severity**: Wrong results in bitwise code  
**Affected benchmarks**: crypto_sha1, crypto_md5

**Status**: ✅ **FIXED for explicit compact integer operands**.

Lambda's default `int` `shl` still operates on the wider native integer domain, so left-shifting an untyped 32-bit value can still produce results exceeding 32 bits:

```lambda
shl(1732584193, 5)     // 55442694176, type int
```

The fixed Lambda spelling is to keep the word in an explicit compact type. With the Go-like bitwise model, `shl` preserves and truncates to the left operand width:

```lambda
shl(1732584193u32, 5)  // 3903086624, type u32
```

That gives SHA-1/MD5-style rotate code deterministic 32-bit wrap without a manual `band(..., 4294967295)` mask:

```lambda
pn rol(num: int, cnt: int) {
    var n: u32 = num
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}
```

**Fix notes**:
- `fn_shl_item()` routes compact integer operands through `sized_shift()`, preserving/truncating to the left operand width.
- `test/lambda/sized_numeric_bitwise_go.ls` covers compact shift truncation (`shl(128u8, 1) -> 0u8`, `shl(1i8, 7) -> -128i8`).
- `test/benchmark/jetstream/crypto_sha1.ls` and `test/benchmark/jetstream/crypto_md5.ls` now use `u32` rotate helpers instead of 32-bit mask workarounds.

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

**Status**: ✅ **FIXED**.

Lambda now supports `slice(vec, start)` as shorthand for `slice(vec, start, len(vec))`.
This works for strings, arrays, method syntax, pipe syntax, negative starts, and `null`.

```lambda
slice("hello", 2)         // "llo"
"hello".slice(2)          // "llo"
"hello" | slice(2)        // "llo"
slice([1, 2, 3, 4], -2)   // [3, 4]
```

**Fix notes**:
- `slice/2` is registered as an overload and dispatches to `fn_slice2()`.
- `fn_slice2()` computes the end index with `fn_len()` and delegates to the existing `fn_slice()` implementation, preserving UTF-8 string slicing, array slicing, negative-index handling, clamping, and `null` behavior.
- `test/lambda/slice_two_arg.ls` covers function, method, and pipe forms.

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

**Status**: ✅ **FIXED**.
**Severity**: Compilation/runtime bug
**Affected benchmarks**: bigdenary

Decimal argument passing now preserves both fixed (`n`) and unlimited (`N`) decimal values through `pn` parameters and expression functions. The regression test `test/lambda/proc/proc_decimal_args.ls` covers global constants, literals, locals, forwarded procedural calls, and `fn` calls.

Previously, when a `decimal` value (with either `n` or `N` suffix) was passed as an argument to a `pn` function, the received value was `0` instead of the original decimal. This happened with both global `let` constants and local variables.

```lambda
let BD1 = 3.14159N

pn show(d) {
    print(string(d) ++ "\n")  // now prints "3.14159"
}

pn main() {
    show(BD1)         // 3.14159
    show(3.14159N)    // 3.14159
    let x = 2.71828N
    show(x)           // 2.71828
}
```

**Former workaround**: Access global decimal constants directly inside functions instead of passing them as parameters. This workaround is no longer needed.

```lambda
let BD1 = 3.14159N

pn show() {
    print(string(BD1) ++ "\n")  // prints "3.14159" — works correctly
}
```

**Verification**: `test/lambda/proc/proc_decimal_args.ls` passes under the Lambda procedural test runner.

---

## Summary Table

| # | Issue | Type | Impact |
|---|-------|------|--------|
| 1 | `float[]` rejects list literals | Type system | **Fixed**; benchmark workarounds removed |
| 2 | Can't assign through parenthesized access | Syntax | 4 benchmarks, core pattern |
| 3 | `list` field name silent failure | Reserved word | 1 benchmark, hours of debugging |
| 4 | No hex literals | Missing feature | 2 benchmarks, tedious manual conversion |
| 5 | Integer overflow errors | Arithmetic | **Fixed**; splay PRNG and SHA-1 word arithmetic now use `u32` |
| 6 | No unsigned right shift | Bitwise ops | **Fixed for typed unsigned ints**; `shr(u32, n)` zero-fills |
| 7 | `fill(0, x)` returns null | Edge case | **Fixed**; DeltaBlue now concatenates from empty fill arrays directly |
| 8 | Missing substr/charcode | Missing feature | **No Fix**; use `str[start to end]`, `slice(str,start,end)`, and `ord(str[idx])` |
| 9 | `div` reserved word | Reserved word | **Fixed**; `div` can be used as a parameter/local name and assignment target |
| 10 | Immutable parameters | Language design | 1 benchmark, minor |
| 11 | Integer division returns float | Runtime bug | **Fixed**; `/` stays float and `slice` accepts integer-valued float indices |
| 12 | `shl` 64-bit overflow | Bitwise ops | **Fixed for typed `u32` code**; SHA-1/MD5 rotate helpers use compact shifts |
| 13 | `@` path literals don't parse | Missing feature | 1 benchmark, doc mismatch |
| 14 | `slice()` no 2-argument form | Missing feature | **Fixed**; `slice(vec,start)` delegates to slice-to-end |
| 15 | No case-insensitive patterns | Missing feature | 1 benchmark, workaround needed |
| 16 | No replace-first function | Missing feature | 1 benchmark, manual workaround |
| 17 | Decimal args become zero | Runtime bug | **Fixed**; decimal values pass correctly through `pn` and `fn` arguments |
