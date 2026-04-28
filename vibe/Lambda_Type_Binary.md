# Lambda Binary Type — Enhancement Proposal

> **Status:** Proposal
> **Scope:** Lambda Script `binary` primitive type — runtime representation, language surface, and stdlib roadmap.
> **Related:**
> - [Lambda_Data.md](../doc/Lambda_Data.md#binary-literals) — binary literal syntax
> - [Lambda_Type.md](../doc/Lambda_Type.md) — type hierarchy
> - [Lamdba_Runtime.md](../doc/dev/Lamdba_Runtime.md) — current `String*` storage
> - [JS_Runtime_Detailed.md](../doc/dev/JS_Runtime_Detailed.md) — existing `ArrayBuffer`/`Uint8Array`/`DataView` machinery in the JS jube

---

## 1. Motivation

Lambda already has a `binary` primitive type with hex and base64 literal syntax (`b'\xDEADBEEF'`, `b'\64A0FE'`), but the surface stops there:

- The runtime stores `binary` as a `String*` holding the **raw, undecoded source text** (`build_lit_string` in `lambda/lambda/build_ast.cpp` copies the literal characters between `b'` and `'` verbatim, hex/base64 prefix included). Length and content reported by the runtime are therefore the encoded string, not the bytes.
- There are no operations: no indexing, slicing, iteration, concatenation, encoding round‑trips, struct packing, hashing, or I/O.
- The JS jube already implements `ArrayBuffer`/`Uint8Array`/`DataView` (see [JS_Runtime_Detailed.md](../doc/dev/JS_Runtime_Detailed.md)) but those buffers are not bridged to Lambda's `binary`.
- Python jube docs explicitly mark `bytes`/`bytearray` as **not supported** ([Python_Support.md](../doc/Python_Support.md)).

This proposal surveys prior art across scripting languages, identifies the highest‑leverage features to borrow, and lays out a tiered implementation plan starting with a representation refactor.

---

## 2. Prior Art Across Scripting Languages

| Language | Core type(s) | Mutability | Views / zero‑copy | Notable extras |
|---|---|---|---|---|
| **Python** | `bytes` (immutable), `bytearray` (mutable), `memoryview` | both | `memoryview` over any buffer‑protocol object | `struct.pack/unpack`, `int.from_bytes`/`to_bytes`, `array`, `mmap`, `pickle`, buffer protocol (PEP 3118) |
| **JavaScript** | `ArrayBuffer` + typed arrays (`Uint8Array`, `Int32Array`, …), `DataView`, `Blob`, Node `Buffer` | mutable views over fixed buffer | typed arrays / `subarray()` share storage | `TextEncoder/Decoder`, `BigInt64Array`, `SharedArrayBuffer`, streams, `crypto.subtle.digest` |
| **Ruby** | `String` with encoding `ASCII-8BIT` (a.k.a. binary), `IO` in binmode | mutable | slicing copies | `Array#pack`, `String#unpack` (Perl‑style format strings) |
| **Perl** | Strings are byte strings by default; `:utf8` flag | mutable | substr | `pack`/`unpack` (the gold standard for ad‑hoc binary parsing) |
| **Lua** | Plain `string` is 8‑bit clean | immutable | substring shares in LuaJIT | `string.pack/unpack/packsize` (5.3+), `ffi.cdata` (LuaJIT) for zero‑copy struct access |
| **Tcl** | "byte arrays" (dual‑typed) | copy‑on‑write | `binary scan` / `binary format` | Channel `-translation binary` |
| **Erlang/Elixir** | `binary` / `bitstring` — first‑class | immutable + sub‑binary refs | **bitstring pattern matching** (zero‑copy slices) | Bit‑level patterns: `<<Header:16, Len:8, Body:Len/binary>>` |
| **Go** (often used as scripting) | `[]byte` | mutable slice | sharing via slicing | `encoding/binary`, `bytes.Buffer`, `io.Reader`/`Writer` |
| **PHP** | binary‑safe `string` | mutable | substr | `pack`/`unpack`, `hex2bin`, `base64_encode` |
| **Raku** | `Buf`/`Blob` (typed `Buf[uint8]` etc.) | `Buf` mutable, `Blob` immutable | yes | `pack`/`unpack`, native types |
| **Julia** | `Vector{UInt8}`, `IOBuffer`, `reinterpret` | mutable | `reinterpret`, `view` | `read(io, T)`, `Mmap`, `StaticArrays` |

### Best features worth borrowing

1. **Separate value from view** (JS / Python). One owning buffer + many cheap, typed views (`Uint8Array`, `DataView`, `memoryview`). Enables zero‑copy slicing, sub‑ranges, and reinterpretation.
2. **Mutable *and* immutable variants** (Python `bytes` vs `bytearray`, Raku `Blob` vs `Buf`). Immutable binaries are safe to share/intern; mutable ones support builders and I/O.
3. **Bit‑level pattern matching** (Erlang). `<<ver:4, type:4, len:16/big, payload:len/binary, rest/binary>>` is the most expressive binary parser in any language — declarative, exhaustive, and the compiler emits optimal extraction code.
4. **`pack`/`unpack` format mini‑language** (Perl/Ruby/PHP/Lua/Python `struct`). A string like `">IH4sB"` (big‑endian u32, u16, 4 bytes, u8) compactly describes a record. Cheap to add, instantly familiar.
5. **Endianness/width aware accessors** (`DataView.getUint32(offset, littleEndian)`). Avoids the "I forgot to byte‑swap" class of bugs and replaces ad‑hoc shifts.
6. **Buffer protocol / FFI interop** (Python PEP 3118, LuaJIT FFI). One ABI lets NumPy, Pillow, Pandas, and C libraries share data without copies.
7. **Builder / Rope semantics** (Go `bytes.Buffer`, Erlang `iolist`). Append‑heavy workloads (HTTP, codec output) benefit from a list‑of‑chunks that flattens lazily.
8. **First‑class encodings as functions, not types** (`TextEncoder`, `base64`, `hex`). Keep `binary` distinct from `string`; provide `encode/decode` round‑trips for utf‑8, latin‑1, hex, base64, base64url, base32, ascii85.
9. **Streaming over slurping**. `Reader`/`Writer` (Go), Node streams, Python iterators of chunks. Avoids loading multi‑GB files into RAM.
10. **Hash/crypto/compression in stdlib** (Node `crypto`, Python `hashlib`, Go `crypto`). Once a real `binary` type exists, expose `sha256(b)`, `gzip(b)`, `hmac(key, b)`.
11. **Hex/base64 literal syntax** (Lambda already has this, plus Elixir `~b""`, JS `Uint8Array.from([..])`). Keep it.
12. **Memory mapping** (`mmap`, Julia `Mmap`). Treat large files as binaries without reading them.
13. **Constant‑time comparison** for crypto (`hmac.compare_digest`, `crypto.timingSafeEqual`). Subtle but important.
14. **Sub‑binary reference counting** (Erlang's "match context" / "sub‑binary"). Slices are pointer + offset + length, never copies, and the GC understands shared ownership.

---

## 3. Current State in Lambda

| Aspect | Today | Source |
|---|---|---|
| Type id | `LMD_TYPE_BINARY` | `lambda/lambda/lambda-data.hpp` |
| Storage | `String*` (heap, GC‑managed, tagged pointer) | `lambda/doc/dev/Lamdba_Runtime.md` |
| Literal parsing | Stores **raw source text** between `b'…'`, including `\x` / `\64` prefix | `build_lit_string()` in `lambda/lambda/build_ast.cpp` (line ~1908) |
| Decoding | None — never converted to bytes at parse time | — |
| Operations | Only `binary(x)` constructor (no slicing, indexing, iteration, concat) | `lambda/doc/Lambda_Sys_Func.md` |
| JS bridge | JS `Uint8Array`/`DataView` exists but does not share storage with Lambda `binary` | `lambda/doc/dev/JS_Runtime_Detailed.md` |
| Python bridge | `bytes`/`bytearray` marked **not supported** | `lambda/doc/Python_Support.md` |

**Implication:** Today, `len(b'\xDEADBEEF')` and `b'\xDEADBEEF'[0]` (if it worked) would operate on the **encoded string** (`\xDEADBEEF` = 9 chars), not the 4 decoded bytes. This is a correctness bug waiting to surface as soon as anyone does anything with `binary`.

---

## 4. Tier 1 — Foundation (highest priority)

### 4.1 Refactor: decode binary literals into actual bytes at parse time

**The core fix.** Replace the "store raw source text in a `String*`" model with a real byte buffer.

#### New runtime representation

```c
// lambda/lambda/lambda-data.hpp
typedef struct Binary {
    uint32_t  len;        // byte length
    uint32_t  flags;      // bit 0: is_const, bit 1: is_literal, bit 2: is_subview
    uint8_t*  bytes;      // points into owner->bytes for sub‑views
    struct Binary* owner; // nullptr unless this is a sub‑view (Tier 3 will use this)
} Binary;
```

- Tagged pointer keeps `LMD_TYPE_BINARY` as today; the pointee changes from `String*` to `Binary*`.
- For Tier 1 we ship without `owner` semantics — every binary owns its bytes inline (`bytes` immediately follows the struct, as `String*` does today). The field is reserved so that Tier 3 sub‑views are an additive change.
- Allocation: `pool_alloc(pool, sizeof(Binary) + n)` with `bytes = (uint8_t*)(this+1)`. GC treats it identically to `String*`.
- Encoding round‑trip helpers (`bin_to_hex`, `bin_to_b64`) are used purely for printing; the in‑memory form is always raw bytes.

#### Parser changes

`build_lit_string()` already isolates the `SYM_BINARY` case. The new logic:

1. Strip leading whitespace. Inspect the prefix:
   - `\x…` → hex decode (ignore intra‑literal whitespace; require even hex digit count; `ERR_INVALID_BINARY` otherwise).
   - `\64…` or `\b64…` → base64 decode (RFC 4648, accept `=` padding, ignore whitespace, reject non‑alphabet).
   - Future: `\b16…`, `\b32…`, `\b85…`.
2. Allocate `Binary` of decoded size; emit decoded bytes.
3. Register in the const pool exactly as the current code does (so MIR JIT can reference literals by index).

#### Touch list (non‑exhaustive)

| File | Change |
|---|---|
| `lambda/lambda/lambda-data.hpp` | Add `Binary` struct, update `TypeString` → split to `TypeBinary` (or keep unified with a discriminator until Tier 3). |
| `lambda/lambda/build_ast.cpp` | Decode at parse time. |
| `lambda/lambda/transpile.cpp` | Update C type for `LMD_TYPE_BINARY` (currently `String*` at line 87) to `Binary*`. Update unbox/box helpers (`it2s`/`x2it` family) — likely add `it2b`/`b2it`. |
| `lambda/lambda/transpile-mir.cpp` | Update `BIN_TAG` paths and any binary literal emission. |
| `lambda/lambda/emit_sexpr.cpp` | Pretty‑print as `b'\x…'` (canonical hex, lowercase, grouped 2/byte). |
| `lambda/lambda/lambda-proc.cpp` | Update conversion paths (`source_type == LMD_TYPE_BINARY`). |
| `lambda/lambda/mark_editor.cpp` | Update editor read paths. |
| Stdlib | New `binary(x)` semantics: from `string` → utf‑8 encode; from `[u8]` → copy; from `int` → encode as min bytes (configurable endianness). |
| Tests | Add round‑trip: `decode(b'\xDE AD BE EF') == [0xDE,0xAD,0xBE,0xEF]`; `len(b'\xDEADBEEF') == 4`. |

#### Migration risk

Any code currently relying on `String*`‑shaped binaries (likely none, given the absent surface) will break. Search hits in `transpile.cpp`, `transpile-mir.cpp`, `mark_editor.cpp`, and `js_runtime.cpp` show the contact surface is small and almost entirely in switch dispatch tables — mechanical to update.

### 4.2 Indexing and slicing

Mirror the existing string surface ([Lambda_Data.md §String Indexing and Slicing](../doc/Lambda_Data.md#string-indexing-and-slicing)):

```lambda
let b = b'\xDE AD BE EF'

len(b)            // 4
b[0]              // 0xDEu8        — single byte returns u8
b[1 to 2]         // b'\xADBE'     — inclusive‑inclusive range slice → binary
b[0 to 0]         // b'\xDE'       — single‑byte slice
b[0 to len(b)-1]  // full copy
0xAD in b         // true          — byte membership
// KIV: support of negative index from end, b[-1]
```

Semantics:

- Element type at a single index is `u8`.
- Range slices return `binary`.
- Out‑of‑range indices raise `error` (consistent with array access).
- Tier 1 does an honest copy on slice; Tier 3 promotes slices to zero‑copy sub‑views.

### 4.3 Iteration

```lambda
for byte in b { print(byte) }     // prints each u8

let sum = for (byte in b) byte    // [0xDE, 0xAD, 0xBE, 0xEF] : u8[]
let xor = reduce(b, (a, x) => a ^ x)
```

- The iterator yields `u8` values (not single‑byte binaries).
- Comprehensions over `binary` build `u8[]` by default; an explicit `binary` cast collects back into a binary.
- Implementation: extend the existing iterable dispatch table (same place strings register) with a `binary` case that walks `bytes[0 .. len)`.

### 4.4 Concatenation operator `++`

```lambda
b'\xDE AD' ++ b'\xBE EF'         // b'\xDEADBEEF'
b1 ++ b2 ++ b3                    // associative
```

- Choose `++` (rather than overloading `+`) to keep numeric `+` unambiguous and to leave room for a future `+` doing element‑wise addition on `u8[]`.
- Type rule: `binary ++ binary → binary`. Mixed `binary ++ string` is a type error in Tier 1 (force an explicit `utf8.encode(s)` once Tier 2 lands).
- Implementation: allocate destination `Binary` of summed length, two `memcpy`s. Tier 3 may replace this with an `iolist`‑style rope.

### 4.5 Tier 1 deliverables checklist

- [ ] `Binary` struct + allocator + GC integration
- [ ] Parser decodes hex / base64 literals to raw bytes
- [ ] Pretty‑print canonical `b'\x…'` form
- [ ] `len(b)`, `b[i]`, `b[a to b]`, `x in b`
- [ ] `for byte in b { … }` and comprehension support
- [ ] `b1 ++ b2`
- [ ] Update transpile/MIR/emit/proc/editor switch tables
- [ ] Test suite: literal round‑trip, indexing, slicing, iteration, concat, error paths
- [ ] Doc updates: [Lambda_Data.md](../doc/Lambda_Data.md), [Lambda_Type.md](../doc/Lambda_Type.md), [Lambda_Sys_Func.md](../doc/Lambda_Sys_Func.md)

---

## 5. Tier 2 — Structured Access & Encoding Stdlib

### 5.1 Builder type for incremental construction

Append‑heavy code (codec output, HTTP body assembly) needs better than O(n²) repeated `++`.

```lambda
let bld = binary.builder()
bld.append(b'\xDE AD')
bld.append_u32_be(0xCAFEBABE)
bld.append_str_utf8("hello")
let out: binary = bld.freeze()        // immutable snapshot
```

- `binary.builder()` returns a mutable, growable buffer (doubling strategy).
- `freeze()` transfers ownership into an immutable `binary` (zero‑copy when capacity ≈ length, otherwise truncating copy).
- Internally implemented as a thin wrapper around the existing `StringBuf` machinery (see `strbuf_append_str_n` in [Lambda_Transpiler.md](../doc/dev/Lambda_Transpiler.md)).

### 5.2 Encoding stdlib

Keep encodings as **functions over `binary` and `string`**, not separate types. This matches JS `TextEncoder`/`atob`/`btoa` and keeps `binary` semantically pure.

```lambda
hex.encode(b)       -> string                      // "deadbeef"
hex.decode(s)       -> binary | error              // accepts whitespace, mixed case
hex.encode(b, sep: " ", upper: true)               // "DE AD BE EF"

base64.encode(b)    -> string
base64.decode(s)    -> binary | error
base64.url_encode(b) / base64.url_decode(s)        // URL‑safe alphabet, no padding

base32.encode(b)    -> string                      // RFC 4648
base32.decode(s)    -> binary | error

base85.encode(b)    -> string                      // ASCII‑85 / Z85
base85.decode(s)    -> binary | error

utf8.encode(s: string)  -> binary
utf8.decode(b: binary)  -> string | error
ascii.encode/decode, latin1.encode/decode
```

All decoders return `binary | error`; all encoders are total.

### 5.3 Endianness‑aware accessors (DataView equivalent)

```lambda
read_u32_be(b, offset)   write_u32_be(buf, offset, value)
read_u32_le(b, offset)   write_u32_le(buf, offset, value)
read_i16_be / read_i16_le
read_u64_be / read_u64_le
read_f32_be / read_f32_le
read_f64_be / read_f64_le
```

Naming follows Lambda's snake_case stdlib convention. Bounds checks raise `error`. Writers operate on a `binary.builder` (mutable) only — immutable `binary` cannot be mutated in place.

### 5.4 `pack` / `unpack` with format strings

Adopt Lua 5.3's syntax — the cleanest of the family — with extensions where useful.

```lambda
pack(">I4 H s1", 0xCAFEBABE, 16, "hi")       // -> binary
unpack(">I4 H s1", b)                          // -> [int, int, string]
pack_size(">I4 H")                             // -> int
```

Format directives (initial set):

| Code | Type | Notes |
|---|---|---|
| `<` `>` `=` | endian: little / big / native | applies to subsequent codes |
| `b` `B` | i8 / u8 | |
| `h` `H` | i16 / u16 | |
| `i4` `I4` | i32 / u32 | digit = byte width |
| `i8` `I8` | i64 / u64 | |
| `f` `d` | f32 / f64 | |
| `sN` | length‑prefixed string (N‑byte length) | UTF‑8 |
| `xN` | N bytes of raw binary | |
| `z` | zero‑terminated string | |

### 5.5 Bit‑pattern matching in `match` (Erlang‑style)

The single feature that would make Lambda *uniquely* good at document/binary formats — and it fits Lambda's "documents as data" thesis.

```lambda
match packet {
    case <bin u8: ver, u8: type, u16/be: len, binary[len]: body, binary: rest>:
        process(ver, type, body, rest)
    case <bin u32/be: 0x89504E47, binary: rest>:
        parse_png(rest)
    default:
        error("unknown format")
}
```

Tier 2 ships a restricted form (byte‑aligned widths only). Sub‑bit fields can come later if demand exists. Implementation builds on the existing pattern‑matching infrastructure in [Lambda_Expr_Match.md](Lambda_Expr_Match.md).

---

## 6. Tier 3 — Performance & Ecosystem

### 6.1 Sub‑binary slicing without copy

Promote the reserved `owner` field of `Binary` to active use:

```c
Binary {
    uint32_t  len;
    uint32_t  flags;        // bit 2: IS_SUBVIEW
    uint8_t*  bytes;        // points into owner->bytes
    Binary*   owner;        // GC keep‑alive root
}
```

- `b[a to b]` becomes O(1): allocates a new `Binary` header pointing into the source bytes.
- GC treats `owner` as a strong reference, so the parent buffer survives as long as any sub‑view does.
- A periodic compaction pass (or explicit `binary.copy(b)`) can detach long‑lived small sub‑views from giant parents to avoid memory bloat — same trade‑off Erlang documents.

### 6.2 File I/O

Promote the existing proposal in [vibe_legacy/Lambda_File (idea).md](../../vibe_legacy/Lambda_File%20(idea).md) to a real spec:

```lambda
io.read_bytes(path) -> binary | error
io.write_bytes(path, b: binary) -> null | error
io.append_bytes(path, b: binary) -> null | error

// Streaming
let f = io.open(path, 'rb')
for chunk in f.chunks(64 * 1024) { process(chunk) }
f.close()
```

### 6.3 Memory‑mapped binaries

```lambda
let big = io.mmap(path)            // -> binary, zero‑copy, read‑only
let header = big[0 to 511]         // O(1) sub‑view (Tier 3.1)
```

Pairs naturally with sub‑binary slicing; works for multi‑GB files without RAM pressure.

### 6.4 Crypto / hash stdlib

```lambda
crypto.sha256(b)              -> binary       // 32 bytes
crypto.sha1(b)                -> binary
crypto.md5(b)                 -> binary
crypto.hmac(key: binary, b: binary, alg: 'sha256') -> binary
crypto.crc32(b)               -> u32
crypto.random_bytes(n: int)   -> binary
crypto.constant_time_eq(a: binary, b: binary) -> bool
```

### 6.5 Compression

```lambda
gzip.compress(b, level: int? = 6)   -> binary
gzip.decompress(b)                   -> binary | error
zstd.compress(b)                     -> binary
zstd.decompress(b)                   -> binary | error
deflate.compress(b) / deflate.decompress(b)
```

Each entry takes `binary → binary` and is trivially streamable in Tier 3.

### 6.6 Cross‑runtime zero‑copy bridge

Make Lambda `binary` the **same physical buffer** as JS `Uint8Array` and (future) Python `bytes`/`bytearray`. Lambda's [Jube runtime](../doc/Lambda_Jube_Runtime.md) is the right place to define the contract:

- A shared `BufferDescriptor { ptr; len; owner; refcount; readonly }` ABI.
- `js.Uint8Array` constructed from a Lambda `binary` shares storage.
- Python `bytes` (when implemented) likewise.
- Eliminates the marshal cost that currently makes binary‑heavy cross‑language workflows untenable.

### 6.7 Buffer protocol / FFI

Expose a stable C ABI matching the descriptor above so MIR‑JIT‑emitted code and external C libraries can read/write binaries directly without going through `Item` boxing. Python's PEP 3118 and LuaJIT FFI both demonstrate the multiplier this gives to the ecosystem.

---

## 7. Tier 4 — Polish

### 7.1 Pretty printing & REPL display

- Default `print(b)` → `b'\xDE AD BE EF'` with two‑hex‑per‑byte grouping.
- `binary.hexdump(b)` → canonical 16‑bytes‑per‑row display with offset gutter and ASCII column:
  ```
  00000000  de ad be ef 48 65 6c 6c  6f 20 77 6f 72 6c 64 21  |....Hello world!|
  ```
- REPL truncation: show length + first/last N bytes for binaries over a threshold.

### 7.2 Validators

Tie into Lambda's [validator framework](../doc/Lambda_Validator_Guide.md):

```lambda
type PNGFile = binary where {
    len(self) >= 8,
    self[0 to 7] == b'\x89504E470D0A1A0A'
}

type Sha256Hash = binary where len(self) == 32
type MaxPayload = binary where len(self) <= 1_048_576
```

### 7.3 Constant‑time equality

`crypto.constant_time_eq(a, b)` from §6.4 is reiterated here as a polish item: required for any HMAC verification, password digests, and TLS‑like protocols. Easy to forget, dangerous to omit.

### 7.4 Round‑trip and fuzz tests

Lock down each codec with property tests:

- `hex.decode(hex.encode(b)) == b` for arbitrary `b`
- `base64.decode(base64.encode(b)) == b`
- `unpack(fmt, pack(fmt, ...args)) == args`
- Fuzz invalid inputs to every decoder; assert `error`, never crash.

### 7.5 Documentation deliverables

- Update [Lambda_Data.md](../doc/Lambda_Data.md) §Binary Literals with decoded semantics.
- New section in [Lambda_Sys_Func.md](../doc/Lambda_Sys_Func.md) for the encoding / pack / crypto / compression stdlib.
- New page `lambda/doc/Binary_Support.md` mirroring the structure of [Python_Support.md](../doc/Python_Support.md), enumerating what's supported and what isn't.
- Cheatsheet update in [Lambda_Cheatsheet.md](../doc/Lambda_Cheatsheet.md).

---

## 8. Phased Rollout Summary

| Tier | Theme | Ship‑gating items |
|---|---|---|
| **1** | Foundation | Decoded representation; indexing; slicing; iteration; `++` |
| **2** | Structured access | Builder; encoding stdlib (hex, base64, utf8, …); endian accessors; `pack`/`unpack`; bit‑pattern `match` |
| **3** | Performance & ecosystem | Sub‑binary zero‑copy slicing; file I/O; `mmap`; crypto; compression; cross‑runtime buffer bridge; FFI |
| **4** | Polish | Hexdump; validators; constant‑time eq; round‑trip / fuzz tests; full docs |

Tier 1 is the unblocker: every later tier assumes binaries are real bytes, not source‑text shadows. Once it ships, Tiers 2–4 can land independently and incrementally.
