# Lambda String Performance Tuning Proposal

## Profiling Summary

Profiling the AWFY JSON benchmark (`json2.ls`) with macOS `sample` revealed that **95.6% of CPU time** is spent in UTF-8 string operations. The benchmark parses ~10KB of JSON text character-by-character, and Lambda's string indexing makes this **O(n²)** vs Node.js's O(n).

### Profile Breakdown (debug build, 1409 samples)

| Function Group | Samples | % | Root Cause |
|---|---|---|---|
| `str_utf8*` functions total | 1347 | **95.6%** | All below feed into this |
| `fn_index` → `item_at` | 772 | 54.8% | `inp[idx]` — O(n) linear scan per char |
| `fn_len` | 261 | 18.5% | `len(inp)` — re-counts entire string each call |
| `fn_slice` → `fn_substring` | 141 | 10.0% | `slice(inp, cs, end)` — two O(n) char→byte scans |

### Why It's O(n²)

The hot function `p_read()` is called for every character in the input string:

```lambda
pn p_read(p: Parser) {
    var idx: int = (p.idx) + 1
    p.idx = idx
    var inp: string = (p.inp)
    var inplen: int = len(inp)        // ← O(n): str_utf8_count scans entire string
    if (idx < inplen) {
        var ch: string = inp[idx]     // ← O(n): str_utf8_char_to_byte linear scan from byte 0
        p.cur = ch                    //    + heap_strcpy allocates new 1-char String
    }
}
```

Each `p_read()` invocation triggers:
1. **`fn_len(inp)`** → `str_utf8_count()`: SWAR-scans entire string to count UTF-8 chars → **O(n)**
2. **`inp[idx]` bounds check** in `item_at` → `str_utf8_count()` again → **O(n)**
3. **`inp[idx]` char-to-byte** → `str_utf8_char_to_byte()`: linear scan from byte 0 → **O(n)**
4. **`inp[idx]` return** → `heap_strcpy()`: allocates new `String` struct for 1 character → **alloc**

For n characters: 3 × O(n) × n = **O(n²)** total + n heap allocations.

### Comparison with Node.js/V8

| Operation | Node.js/V8 | Lambda | Gap |
|---|---|---|---|
| `str.length` | O(1) — stored property | O(n) — rescan each time | n× |
| `str[idx]` | O(1) — flat array access | O(n) — linear scan from byte 0 | n× |
| `ch == "x"` | O(1) — integer comparison | `fn_eq` → string pointer deref + `strncmp` | ~5× |
| char return | no alloc — returns char code | `heap_strcpy` — 1-char String | alloc |

---

## Optimization 1: Cache `inplen` in Parser State (Script-Level)

**Impact: Eliminates 1 of 3 O(n) scans per character**
**Effort: Trivial — script change only**

Store `len(inp)` once in the Parser struct instead of calling it every `p_read()`:

```lambda
type Parser = {inp: string, idx: int, inplen: int, ln: int, col: int, cs: int, cb: string, cbf: int, cur: string}

pn p_new(input: string) {
    var il: int = len(input)
    var p: Parser = { inp: input, idx: 0, inplen: il, ln: 1, col: 0, cs: 0, cb: "_", cbf: 0, cur: "~" }
    ...
}

pn p_read(p: Parser) {
    ...
    var inplen: int = (p.inplen)   // O(1) field read instead of O(n) str_utf8_count
    ...
}
```

**Before**: `fn_len` called once per character (~10K calls × O(n) each)
**After**: `fn_len` called once total at parser init

---

## Optimization 2: Combine Bounds Check + Char-to-Byte in `item_at`

**Impact: Eliminates 1 of 3 O(n) scans per character**
**Effort: Small — modify `item_at` in lambda-data-runtime.cpp**

Current `item_at` for strings does two separate O(n) operations:

```cpp
// Current: TWO scans
size_t char_count = str_utf8_count(chars, byte_len);          // O(n) scan #1
if (index < 0 || (size_t)index >= char_count) { return ItemNull; }
size_t byte_offset = str_utf8_char_to_byte(chars, byte_len, (size_t)index);  // O(n) scan #2
```

**Solution A — ASCII fast path**: When `byte_len == char_count` (or string is known ASCII), char index = byte index, skip both scans:

```cpp
case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: {
    const char* chars = data.get_chars();
    uint32_t byte_len = data.get_len();
    if (index < 0) { return ItemNull; }

    // fast path: ASCII string — char index == byte index
    if (str_is_ascii(chars, byte_len)) {
        if ((size_t)index >= byte_len) { return ItemNull; }
        // return single ASCII char (no UTF-8 decoding needed)
        ...
    }

    // slow path: UTF-8 — combined bounds check + char-to-byte
    size_t byte_offset = str_utf8_char_to_byte(chars, byte_len, (size_t)index);
    if (byte_offset == STR_NPOS) { return ItemNull; }
    ...
}
```

**Solution B — Combined function**: New `str_utf8_char_to_byte_checked()` that scans once, returning STR_NPOS for out-of-bounds:

```c
size_t str_utf8_char_to_byte_checked(const char* s, size_t len, size_t char_index) {
    // single pass: advance by UTF-8 sequences, return byte offset or STR_NPOS
    size_t ci = 0, bi = 0;
    while (bi < len && ci < char_index) {
        unsigned char b = (unsigned char)s[bi];
        size_t seq = (b < 0x80) ? 1 : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
        if (bi + seq > len) seq = 1;
        bi += seq;
        ci++;
    }
    return (ci == char_index && bi <= len) ? bi : STR_NPOS;
}
```

This is essentially what `str_utf8_char_to_byte` already does — the key insight is that we can **skip the separate `str_utf8_count` bounds check** entirely and just use the combined function's `STR_NPOS` return for out-of-bounds.

**Before**: `str_utf8_count` O(n) + `str_utf8_char_to_byte` O(index) = O(n) + O(index)
**After**: Single `str_utf8_char_to_byte` O(index) = O(index) (which naturally bounds-checks)

---

## Optimization 3: Add `is_ascii` Flag to String Struct

**Impact: Makes ALL string operations O(1) for ASCII strings (the common case)**
**Effort: Medium — modify String struct, all String creation sites, and string operation functions**

### Current String Struct

```c
typedef struct String {
    uint32_t len;    // byte length
    char chars[];    // UTF-8 data
} String;
```

### Proposed String Struct

```c
typedef struct String {
    uint32_t len;       // byte length (= char count when is_ascii=1)
    uint8_t is_ascii;   // 1 if all bytes < 0x80, 0 otherwise
    char chars[];       // UTF-8 data
} String;
```

**Note**: Adding 1 byte between `len` and `chars[]` will increase struct size by up to 3 bytes due to alignment of the flexible array member. Consider using a bit from `len` instead if size is critical (e.g., top bit of len — strings up to 2GB are fine with 31-bit length).

Alternative compact layout using bitfield:
```c
typedef struct String {
    uint32_t len: 31;      // byte length (up to 2GB)
    uint32_t is_ascii: 1;  // ASCII flag, no extra bytes
    char chars[];
} String;
```

### Impact on Operations

| Operation | Current (UTF-8) | With `is_ascii` flag |
|---|---|---|
| `len(str)` | O(n) `str_utf8_count` | O(1): `is_ascii ? len : str_utf8_count(...)` |
| `str[idx]` | O(n) char→byte scan | O(1): `is_ascii ? chars[idx] : ...` |
| `slice(str, a, b)` | 2×O(n) char→byte | O(1): `is_ascii ? memcpy(chars+a, b-a) : ...` |
| `str == "x"` | `strncmp` | Same (already fast) |

### Creation Sites to Modify

All functions that create `String` objects must set `is_ascii`:

1. **`heap_strcpy`** (`lambda-mem.cpp:123`) — general string creation
2. **`heap_create_name`** (`lambda-mem.cpp:135`) — interned name pool strings
3. **`name_pool_create_len`** — name pool internal
4. **Arena-allocated strings** in input parsers (`mark_builder.hpp`)
5. **`fn_substring`** (`lambda-eval.cpp:2234`) — can inherit parent's `is_ascii`
6. **String concatenation** (`fn_add` for strings) — `is_ascii = a.is_ascii && b.is_ascii`
7. **`fn_join`, `fn_replace`** etc. — propagate flag

### Checking Cost

`str_is_ascii()` in `lib/str.c` uses SWAR (8 bytes at a time with `_swar_has_highbit`), so the check is very fast — essentially free compared to the string copy that already happens at creation time.

---

## Optimization 4: Intern Single-Char ASCII Strings (Recommended)

**Impact: Eliminates heap allocation for ASCII characters**
**Effort: Small — pre-allocate 128 strings at init, modify 1-2 functions**

### Problem

Every `str[idx]` call allocates a new `String` on the GC heap:
```cpp
String *ch_str = heap_strcpy((char*)(chars + byte_offset), (int)ch_len);
return {.item = s2it(ch_str)};
```

For the JSON parser, this means ~10K heap allocations per parse — one per character read. These are all single-character strings that could be shared.

### Solution: Pre-allocated ASCII Character Table

Pre-allocate a permanent table of 128 `String*` objects (one per ASCII code point) at runtime init. All single-ASCII-char returns share these immutable strings:

```cpp
// in lambda-mem.cpp or a new string_intern.cpp
static String* ascii_char_table[128];

void init_ascii_char_table(Pool* permanent_pool) {
    for (int i = 0; i < 128; i++) {
        // allocate from permanent pool (never GC'd)
        String* s = (String*)pool_calloc(permanent_pool, 1, sizeof(String) + 2);
        s->len = 1;
        s->is_ascii = 1;
        s->chars[0] = (char)i;
        s->chars[1] = '\0';
        ascii_char_table[i] = s;
    }
}

// fast lookup — returns pre-allocated String* for any ASCII byte
static inline String* get_ascii_char_string(unsigned char ch) {
    return ascii_char_table[ch];
}
```

### Changes Required

#### 1. Init at Startup

Call `init_ascii_char_table()` during `EvalContext` initialization (in `lambda.cpp` or `lambda-mem.cpp`).

#### 2. Modify `item_at` for Strings (`lambda-data-runtime.cpp`)

```cpp
case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: {
    const char* chars = data.get_chars();
    uint32_t byte_len = data.get_len();
    if (index < 0) return ItemNull;

    // ASCII fast path (with is_ascii flag from Optimization 3)
    if (data.get_string()->is_ascii) {
        if ((uint32_t)index >= byte_len) return ItemNull;
        return {.item = s2it(get_ascii_char_string((unsigned char)chars[index]))};
    }

    // UTF-8 path
    size_t byte_offset = str_utf8_char_to_byte(chars, byte_len, (size_t)index);
    if (byte_offset == STR_NPOS) return ItemNull;
    size_t ch_len = str_utf8_char_len((unsigned char)chars[byte_offset]);
    if (ch_len == 0) ch_len = 1;
    if (byte_offset + ch_len > byte_len) return ItemNull;

    // single ASCII byte in a UTF-8 string (e.g. ASCII char in mixed string)
    if (ch_len == 1 && (unsigned char)chars[byte_offset] < 128) {
        return {.item = s2it(get_ascii_char_string((unsigned char)chars[byte_offset]))};
    }

    // multi-byte UTF-8 character — must heap-allocate
    String *ch_str = heap_strcpy((char*)(chars + byte_offset), (int)ch_len);
    return {.item = s2it(ch_str)};
}
```

### Why This is Better Than `LMD_TYPE_CHAR`

| Aspect | `LMD_TYPE_CHAR` (new type) | Interned Strings (table) |
|---|---|---|
| Type system changes | Add new TypeId, modify 10+ switch statements | **None** |
| `fn_eq` changes | Cross-type CHAR↔STRING comparison logic | **None** — same `String*` pointers |
| `fn_len` changes | New case handling | **None** — `len` field already 1 |
| Concatenation | Convert code point → UTF-8 in `fn_add` | **None** — already a `String*` |
| Print/format | Encode code point → UTF-8 | **None** — already a `String*` |
| Type reporting | Map CHAR→"string" in `type()`, `get_type_name` | **None** — already LMD_TYPE_STRING |
| Transpiler changes | Handle CHAR in `transpile.cpp` | **None** |
| GC integration | Ensure CHAR Items not scanned as pointers | **None** — normal String pointers |
| Serialization | JSON/YAML formatters handle CHAR | **None** |
| Allocation cost | Zero (packed in register) | Zero (pointer to pre-allocated) |
| Comparison speed | O(1) integer compare | O(1) pointer identity (interned!) + fallback `len==1 && chars[0]` |
| Memory per char return | 0 bytes | 0 bytes (shared) |
| Risk | **High** — touches entire runtime | **Low** — localized change |

**Verdict**: Interned strings give **~95% of the benefit at ~10% of the complexity**. The only theoretical advantage of `LMD_TYPE_CHAR` is avoiding a pointer dereference in `fn_eq`, but interned single-char strings enable **pointer identity** comparison (`a.string_ptr == b.string_ptr`) as a fast path, which is equally fast.

### Bonus: Pointer Identity Fast-Path in `fn_eq`

Since all `str[idx]` returns for ASCII now point to the same interned strings, and string literals like `"\n"`, `"~"`, `" "` in the source code can also be resolved to the same table at compile time, many comparisons become pointer-identity checks:

```cpp
// In fn_eq, STRING case — add at the top:
if (a_item.string_ptr == b_item.string_ptr) return BOOL_TRUE;  // identity check
```

This one-line addition makes `cur == "\n"` and `cur == "~"` comparisons essentially free when both sides are interned.

### Performance Impact

For the JSON parser hot path (`p_read`):
- **Before**: `inp[idx]` → `heap_strcpy` (1 GC alloc per char, ~10K allocs/parse)
- **After**: `inp[idx]` → table lookup (zero alloc, shared pointer)
- **Bonus**: `cur == "\n"` → pointer identity (no `strncmp`)

---

## Optimization 5: Optimize `fn_eq` for Single-Char Strings

**Impact: Speeds up character comparison in the parser hot loop**
**Effort: Small**

Currently `fn_eq` for two strings does:
```cpp
const char* chars_a = a_item.get_chars();  // pointer deref through tagged pointer
const char* chars_b = b_item.get_chars();  // pointer deref through tagged pointer
uint32_t len_a = a_item.get_len();         // pointer deref
uint32_t len_b = b_item.get_len();         // pointer deref
bool result = (len_a == len_b && strncmp(chars_a, chars_b, len_a) == 0);
```

For single-char comparisons (`cur == "\n"`, `cur == "~"`), this is 4 pointer dereferences + `strncmp`.

**With interned single-char strings** (Optimization 4), both `cur` and the literal `"\n"` resolve to the same `String*` pointer from `ascii_char_table`. So we can add a **pointer identity fast-path**:

```cpp
// at the top of fn_eq STRING case:
if (a_item.string_ptr == b_item.string_ptr) return BOOL_TRUE;  // interned identity!
```

This makes character comparison **zero memory access** — just a 64-bit integer compare on the tagged pointer.

Even without interning, if both strings are length-1 ASCII, we could fast-path:
```cpp
if (len_a == 1 && len_b == 1) {
    return (chars_a[0] == chars_b[0]) ? BOOL_TRUE : BOOL_FALSE;
}
```

---

## Optimization 6: Transpiler-Level String Cursor Optimization (Future)

**Impact: Eliminates random-access indexing entirely for sequential patterns**
**Effort: Large — requires pattern detection in transpiler**

The transpiler could detect sequential string access patterns and convert them to byte-cursor iteration:

```c
// Before (generated C for p_read):
int64_t _inplen = fn_len(s2it(_inp));
String* _ch = it2s(fn_index(s2it(_inp), i2it(_idx)));

// After (transpiler-optimized):
// _inp_bytes and _inp_byte_len cached once
char _ch_byte = _inp->chars[_idx];  // direct byte access for ASCII
```

This requires the transpiler to prove:
1. The string is only accessed sequentially (monotonic index)
2. The string is ASCII (or the index is tracking byte positions)

This is a longer-term optimization that builds on top of the `is_ascii` flag.

---

## Optimization 7: Cache `char_count` in String Struct (Optional)

**Impact: Makes `fn_len` always O(1) for all strings, not just ASCII**
**Effort: Medium — same sites as `is_ascii`, slightly more computation at creation**

```c
typedef struct String {
    uint32_t len;          // byte length
    uint32_t char_count;   // UTF-8 character count (= len for ASCII)
    char chars[];
} String;
```

When `char_count == len`, the string is ASCII (implicit `is_ascii` flag).

**Trade-off**: Every String takes 4 extra bytes. Since Lambda creates many strings (map keys, element names, content), this could be significant memory overhead. The `is_ascii` flag (1 bit) is more space-efficient for the same benefit in the common ASCII case.

**Recommendation**: Start with `is_ascii` flag (1 bit). Only add `char_count` if non-ASCII string length becomes a hot path.

---

## Implementation Priority

| # | Optimization | Impact | Effort | Status |
|---|---|---|---|---|
| 1 | Cache `inplen` in Parser | -33% calls | Trivial | **Done** |
| 2 | Combined bounds+char-to-byte | -33% calls | Small | **Done** |
| 3 | `is_ascii` flag on String | O(n)→O(1) for ASCII | Medium | **Done** |
| 4 | Intern ASCII char strings | Zero alloc per char | Small | **Done** |
| 5 | Optimize `fn_eq` pointer identity | Faster comparisons | Small | **Done** |
| 6 | Transpiler string cursor | Eliminate indexing | Large | Future |
| 7 | Cache `char_count` | O(1) len for all strings | Medium | Future — if non-ASCII hot |

### Estimated Combined Impact on JSON Benchmark

| Stage | `p_read` cost per char | Improvement |
|---|---|---|
| Current | 3×O(n) + alloc + string eq | baseline |
| After #1 (cache inplen) | 2×O(n) + alloc + string eq | ~33% fewer scans |
| After #2 (combined bounds) | 1×O(n) + alloc + string eq | ~66% fewer scans |
| After #3 (is_ascii) | O(1) + alloc + string eq | **O(n²) → O(n)** |
| After #4 (intern chars) | O(1) + no alloc + pointer eq | **Near-optimal** |

---

## Benchmark Results

Measured on Apple Silicon Mac (M4), `make build-release`, using `json2.ls` with 100-iteration timing loop via `clock()`.

### Json2 Benchmark: Before vs After (Release Build)

| | 100 iterations (ms) | Per iteration (ms) |
|---|---|---|
| **Baseline** (no optimizations #3–#5) | ~48,500 | ~485 |
| **Optimized** (is_ascii + interned chars + ptr identity eq) | ~430 | ~4.3 |
| **Speedup** | **~113×** | |

All 473/473 Lambda baseline tests pass after the changes.

### What Was Implemented (Optimizations #3–#5)

1. **`is_ascii` flag on String struct** — Added `uint8_t is_ascii` field. Set at all 20+ String creation sites (`heap_strcpy`, `build_lit_string`, `fn_strcat`, `fn_substring`, `fn_format`, `fn_trim`, `fn_split`, `mark_builder`, numeric-to-string, etc.). Fast-paths added to `fn_len` (O(1) for ASCII), `fn_substring` (direct `memcpy`), `fn_index_of`/`fn_last_index_of` (skip byte→char conversion), `item_at` (direct byte indexing).

2. **Interned ASCII char table** — 128 pre-allocated `String*` objects (one per ASCII code point), initialized once in `heap_init()`. Static storage (not GC-managed). `item_at` returns interned strings for single-byte ASCII characters, eliminating ~10K heap allocations per JSON parse.

3. **Pointer identity fast-path in `fn_eq`** — Single-line `if (a_item.string_ptr == b_item.string_ptr) return BOOL_TRUE` check before any string content comparison. Also changed `strncmp` → `memcmp` for known-length comparison.

### Analysis

The **113× speedup** significantly exceeds the initial estimate of "2–4× slower than Node.js". The dominant factor was the O(n²) → O(1) indexing for ASCII strings (`is_ascii` flag), which eliminated the quadratic `str_utf8_char_to_byte` scans that consumed 95.6% of CPU time. The interned char table eliminated all per-character heap allocations, and the pointer identity check made character comparisons (`cur == "\n"`, `cur == "~"`) effectively free.

Remaining gap vs Node.js (~34ms for 100 iterations):
- Lambda: ~430ms → **~12.6× slower** than Node.js (down from ~1,426× before)
- JIT dispatch overhead (function calls through MIR, type checks)
- GC overhead for non-char string allocations (parsed values, arrays, maps)
- Map field access overhead (shape lookup vs V8's inline caches)

---

## Additional String Tuning Opportunities

### A. String Comparison Identity Fast-Path

Name-pooled strings (from `heap_create_name`) are interned — same content always returns the same pointer. For these, `fn_eq` could check pointer identity first:

```cpp
// If both are name-pooled, pointer equality suffices
if (a_item.string_ptr == b_item.string_ptr) return BOOL_TRUE;
```

This helps map key comparisons but not general string equality.

### B. `fn_substring` ASCII Fast-Path

`fn_substring` (used by `slice()`) currently does `str_utf8_count` + two `str_utf8_char_to_byte` scans. With `is_ascii`, it can use direct `memcpy`:

```cpp
if (str->is_ascii) {
    // char index == byte index for ASCII — no scanning needed
    if (start < 0) start = str->len + start;
    if (end < 0) end = str->len + end;
    if (start < 0) start = 0;
    if (end > str->len) end = str->len;
    if (start >= end) return empty_string();
    long result_len = end - start;
    String* result = heap_strcpy(str->chars + start, result_len);
    result->is_ascii = 1;
    return {.item = s2it(result)};
}
```

### C. Avoid UTF-8 Re-encoding in String Concatenation

When concatenating two ASCII strings, the result is guaranteed ASCII — skip `str_is_ascii` check and just set the flag directly: `result->is_ascii = a->is_ascii && b->is_ascii`.

### D. Pre-compute `is_ascii` for Literal Strings at Compile Time

The transpiler knows string literal contents at compile time. For ASCII literals (which are the vast majority), the `is_ascii` flag can be set statically in the const table without any runtime check.

### E. `fn_len` ASCII Fast-Path

With `is_ascii`, `fn_len` for strings becomes O(1):
```cpp
case LMD_TYPE_STRING: {
    String* str = item.get_string();
    if (str->is_ascii) {
        size = str->len;  // byte_len == char_count for ASCII
    } else {
        size = str_utf8_count(str->chars, str->len);
    }
    break;
}
```

### F. `str_utf8_char_to_byte` Sequential Hint (Mid-term)

For sequential access patterns like `inp[0]`, `inp[1]`, `inp[2]`, ..., the runtime could cache the last `(char_index, byte_offset)` pair and start scanning from there instead of byte 0 each time. This turns O(n) per access into O(1) amortized:

```c
// thread-local or context-scoped cursor cache
typedef struct {
    const char* str;    // which string this cache is for
    size_t last_ci;     // last character index accessed
    size_t last_bi;     // corresponding byte offset
} Utf8Cursor;

size_t str_utf8_char_to_byte_cached(Utf8Cursor* cursor, const char* s, size_t len, size_t char_index) {
    // if accessing the same string and index >= last position, resume from there
    if (cursor->str == s && char_index >= cursor->last_ci) {
        size_t ci = cursor->last_ci, bi = cursor->last_bi;
        while (bi < len && ci < char_index) { /* advance */ }
        if (ci == char_index) { cursor->last_ci = ci; cursor->last_bi = bi; return bi; }
    }
    // fallback to full scan
    return str_utf8_char_to_byte(s, len, char_index);
}
```

This is useful for non-ASCII strings where `is_ascii` can't help. However, with interned char strings and the ASCII fast path, this becomes less critical.

### G. Transpiler: Hoist `len(str)` for Loop-Invariant Strings

When the transpiler detects `len(x)` inside a loop where `x` is not reassigned, it could hoist the length computation out of the loop. This is a general optimization not specific to strings.

### H. Small String Optimization (SSO) — Future

Strings up to ~6 bytes could be stored inline in the Item's 56-bit payload:

```
Item layout for small string:
┌──────────────────────────────────────────┬─────┬────────┐
│  chars[0..5] (up to 6 bytes of string)   │ len │ type   │
│  6 bytes                                 │ 2b  │ 8 bits │
└──────────────────────────────────────────┴─────┴────────┘
```

This would eliminate allocations for very short strings (keys like `"x"`, `"id"`, values like `"H"`, `"He"`). However, this adds significant complexity to every string operation (must detect inline vs heap-allocated) and conflicts with the current `String*` pointer assumption throughout the codebase. **Not recommended** for now — interned single-char strings cover the most impactful case with minimal changes.

---

## Appendix: Why Not `LMD_TYPE_CHAR`?

Adding a new runtime type `LMD_TYPE_CHAR` that packs a Unicode code point in the Item's 56-bit payload would eliminate heap allocation entirely (not even a pointer dereference). However, the complexity cost is disproportionate to the marginal benefit over interned strings:

### Blast Radius of a New Type ID

A new `LMD_TYPE_CHAR` type would require changes in **every** switch statement that handles types:

| Component | Changes Needed |
|---|---|
| `fn_eq` | CHAR×CHAR + CHAR×STRING cross-type comparison |
| `fn_lt`, `fn_le`, `fn_gt`, `fn_ge` | Ordered char comparison + cross-type |
| `fn_add` (concat) | CHAR+STRING, STRING+CHAR, CHAR+CHAR → String |
| `fn_len` | Return 1 for CHAR |
| `fn_index` | CHAR[0] → return self |
| `fn_slice` | CHAR slice → return self or empty |
| `get_type_name` | Map CHAR → "string" |
| `type()` system func | Map CHAR → string Type* |
| `fn_to_string`, `fn_format` | Encode code point → UTF-8 |
| `fn_contains`, `fn_starts_with`, etc. | Handle CHAR as pattern |
| All output formatters (JSON, YAML, HTML...) | Serialize CHAR → string |
| `transpile.cpp` | Emit `ch2it()` calls, handle CHAR in type inference |
| `transpile-mir.cpp` | Same |
| GC scanner | Recognize CHAR as non-pointer (unlike STRING) |
| Type inference / validator | Map CHAR → string type |
| `mark_reader.hpp` | Handle CHAR in data traversal |
| Pattern matching | CHAR in switch/match expressions |

**Estimate: 20-30 code locations**, each a potential source of bugs if missed.

### Interned Strings: Same Performance, Minimal Changes

| Component | Changes Needed |
|---|---|
| `item_at` | Return `ascii_char_table[ch]` instead of `heap_strcpy` |
| `fn_eq` | Add 1-line pointer identity check (optional optimization) |
| Init code | 10-line init function |
| **Total** | **~15 lines of new code** |

Everything else (fn_eq, fn_add, fn_len, formatters, transpiler, GC) works **unchanged** because the return is still a normal `String*`.

### When Would LMD_TYPE_CHAR Make Sense?

Only if profiling shows that the `String*` pointer dereference in `fn_eq` is a measurable bottleneck *after* interning is implemented. With interned strings + pointer identity fast-path, the comparison is already `a.string_ptr == b.string_ptr` (one 64-bit integer compare), which is equivalent in cost to `LMD_TYPE_CHAR`'s `a.item == b.item`.

**Conclusion**: Interned strings are the pragmatic choice. Reserve `LMD_TYPE_CHAR` as a future "last 5%" optimization if needed after measuring.
