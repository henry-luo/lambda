# Lambda Binary Type — Enhancement Proposal

> **Status:** Proposal — **Tier 1 core (decoded representation, element operations) + JS/Node copy bridge IMPLEMENTED 2026-07-15** via [Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) (decisions B1–B8 and follow-up A1/A2). Tiers 2–4 remain proposal. Design decisions recorded 2026-07-15: **§9** streams alignment (WHATWG/Node), **§10** Uint8Array type-unification **rejected**, **§11** byte-storage substrate unification **accepted** (separate value types over shared storage), **§5.2** encode/decode selector API **settled** (namespaced `hex.encode` form rejected), **§4.2/§4.4** element-op semantics settled and implemented (`b[i]` → u8, out-of-bounds → `null`, `++` → byte concat).
> **Scope:** Lambda Script `binary` primitive type — runtime representation, language surface, and stdlib roadmap.
> **Related:**
> - [Lambda_Data.md](../doc/Lambda_Data.md#binary-literals) — binary literal syntax
> - [Lambda_Type.md](../doc/Lambda_Type.md) — type hierarchy
> - [LR_00_Overview.md](../doc/dev/lambda/LR_00_Overview.md) — runtime value model (`String*`-backed storage)
> - [JS_00_Overview.md](../doc/dev/js/JS_00_Overview.md) — `ArrayBuffer`/`Uint8Array`/`DataView` machinery in the JS jube (`lambda/js/js_typed_array.{h,cpp}`, `js_buffer.cpp`)
> - [Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) — the executed Tier-1 implementation plan (B1–B8, Phases 1–5; its Phase-6 sketch is superseded by §11 and `Lambda_Impl_Binary2.md`)
> - [Lambda_Impl_Binary2.md](Lambda_Impl_Binary2.md) — detailed implementation plan for the accepted shared byte-storage substrate (§11)
> - [Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) — binary pipelines/streams (PL1–PL11); PL5 flat-buffer representation is this doc's Tier 3.1
> - [Lambda_Type_Binary_Schema.md](Lambda_Type_Binary_Schema.md) — schema-driven structured views (`Velmt`) over binary

---

## 1. Motivation

Lambda already has a `binary` primitive type with hex and base64 literal syntax (`b'\xDEADBEEF'`, `b'\64A0FE'`). **As of 2026-07-15 the foundation defect is fixed** ([Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) Phases 1–5): literals and Mark input decode to real bytes, `len` is the byte count, printing is canonical and NUL-safe, `binary()` returns real binary, and a copy bridge to JS `Uint8Array`/`Buffer`/`DataView` exists. What the surface still lacks:

- ~~The runtime stores the raw, undecoded source text~~ **(fixed — decoded bytes are the only payload; storage remains `String*`-shaped per B1).**
- ~~No element operations~~ **(fixed — indexing yields u8, slicing preserves binary, iteration yields u8, membership is byte-based, and `binary ++ binary` concatenates bytes).**
- No encoding stdlib surface (§5.2), struct packing, hashing, or binary-specific I/O helpers.
- The JS bridge is **copy-based** (B8); zero-copy sharing awaits the Tier-3.1 / PL5 flat-buffer representation.
- Python jube docs explicitly mark `bytes`/`bytearray` as **not supported** ([Python_Support.md](../doc/Python_Support.md)).

This proposal surveys prior art across scripting languages, identifies the highest‑leverage features to borrow, and lays out a tiered implementation plan starting with a representation refactor.

> **Notation note.** Lambda currently has **no hex integer literal syntax** (no `0xDE`). All numeric literals in this document use decimal; hex equivalents appear only in comments for readability. Adding `0x…` / `0o…` / `0b…` integer literals is a separate, orthogonal language change and is out of scope here.

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

## 3. Current State in Lambda (updated 2026-07-15, post-implementation)

| Aspect | Today | Source |
|---|---|---|
| Type id | `LMD_TYPE_BINARY` | `lambda/lambda-data.hpp` |
| Storage | `String*`-shaped (heap, GC‑managed, tagged pointer), payload = **decoded bytes**, `len` = byte count, embedded NULs legal (B1) | `lambda/lambda.h` (`typedef String Binary`) |
| Literal parsing | **Decodes** `\x` hex / `\64` base64 / bare-hex default, whitespace ignored; malformed literal = compile error | shared `str_binary_payload_decode` in `lib/str_binary.c`, called from `build_lit_string()` and Mark `parse_binary` |
| Printing | Canonical `b'\x<UPPERCASE HEX>'`, NUL-safe, one shared helper (B3) | `format_binary_literal` in `lambda/print.cpp:32` |
| `len(b)` | Byte count (`len(b'\xDEADBEEF')` == 4) (B4) | `fn_len`, `lambda-eval.cpp` |
| `binary(x)` | Returns real binary (`x2it`): UTF-8 bytes for string/symbol, identity for binary (B6) | `fn_binary`, `lambda-eval-num.cpp` |
| Formatters | Mark: native `b'\x…'` literal (round-trips); JSON/YAML/text formats: padded standard base64 (B7) | `lambda/format/` |
| File I/O | `output(bin, path)` writes exact bytes; file reads produce byte binaries | `lambda-proc.cpp` |
| Element ops | **Implemented:** scalar indexing yields u8, OOB yields `null`, slices remain binary, iteration yields u8, membership is byte-based, and `++` concatenates bytes | `lambda-data-runtime.cpp`, `lambda-vector.cpp`, `lambda-eval.cpp` |
| JS bridge | **Copy bridge** to `Uint8Array`/`Uint8ClampedArray`/`Buffer`/`DataView` with mutation isolation (B8); zero-copy = Tier 3.1/PL5 | `lambda/js/js_typed_array.cpp`, `js_buffer.cpp` |
| Python bridge | `bytes`/`bytearray` marked **not supported** | `doc/Python_Support.md` |

**The 2026-07-14 audit that triggered the fix** (kept for the record): literals stored the encoded source text (`len(b'\xDEADBEEF')` was 9-ish source chars, not 4 bytes) while file reads stored real bytes — two producers, inconsistent payloads; two printers disagreed (`b'%s'` vs `0x%s`, both NUL-unsafe); `binary()` and Mark's `parse_binary` returned strings. All fixed; full inventory and verification in [Lambda_Impl_Binary.md](Lambda_Impl_Binary.md).

---

## 4. Tier 1 — Foundation (highest priority)

### 4.1 Refactor: decode binary literals into actual bytes at parse time

> **✅ IMPLEMENTED 2026-07-15** ([Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) Phases 1–4), with two deliberate deltas from the original sketch: (1) **storage stays `String*`-shaped** in the landed Tier-1 implementation (B1); the accepted dedicated representation is now the refcounted storage/span design in §11 rather than the rejected parent-`Binary*` shape; (2) the canonical print form is **uppercase, ungrouped** `b'\xDEADBEEF'` (B3), not lowercase/grouped as originally sketched in §7.1. The decoder lives in `lib/str_binary.c` (`str_binary_payload_decode`), shared by the compiler and Mark input.

**The core fix.** Replace the "store raw source text in a `String*`" model with a real byte buffer.

#### Representation follow-up

The original sketch reserved an `owner: Binary*` field for subviews. That
shape is rejected for Tier 3 because it creates parent chains, remains tied to
one GC lifetime, and does not unify ownership with JS ArrayBuffer or ArrayNum
external storage. The accepted target is §11's direct
`(ByteStorage, offset, length)` span. The Item tag remains
`LMD_TYPE_BINARY`; only its pointee layout and accessors change.

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
| Stdlib | New `binary(x)` semantics — **as landed (B6):** from `string`/`symbol` → UTF-8 bytes; from `binary` → identity; from numbers → bytes of the decimal text (revisit toward explicit `to_bytes(n, 'u32le')`-style encodings with §5.3). |
| Tests | Add round‑trip: `decode(b'\xDE AD BE EF') == [222, 173, 190, 239]` (i.e. `0xDE 0xAD 0xBE 0xEF`); `len(b'\xDEADBEEF') == 4`. Note: Lambda has no hex integer literal syntax yet — tests must use decimal. |

#### Migration risk

Any code currently relying on `String*`‑shaped binaries (likely none, given the absent surface) will break. Search hits in `transpile.cpp`, `transpile-mir.cpp`, `mark_editor.cpp`, and `js_runtime.cpp` show the contact surface is small and almost entirely in switch dispatch tables — mechanical to update.

### 4.2 Indexing and slicing

Mirror the existing string surface ([Lambda_Data.md §String Indexing and Slicing](../doc/Lambda_Data.md#string-indexing-and-slicing)):

```lambda
let b = b'\xDE AD BE EF'

len(b)            // 4
b[0]              // 222u8         — single byte returns u8 (0xDE in the literal)
b[1 to 2]         // b'\xADBE'     — inclusive‑inclusive range slice → binary
b[0 to 0]         // b'\xDE'       — single‑byte slice
b[0 to len(b)-1]  // full copy
173 in b          // true          — byte membership (Lambda has no 0xAD literal yet; use decimal)
// KIV: hex integer literals (0xDE) — currently must use decimal
// KIV: support of negative index from end, b[-1]
```

Semantics **(decided 2026-07-15, user-confirmed)**:

- `b[i]` returns the byte as **u8** — a real runtime value, not an aspiration: Lambda's sized numerics already exist as scalars (`200u8` literals, `NUM_SIZED_PACK` representation via `u8_to_item`, `lambda.h:283`), and indexing a `ELEM_UINT8` typed array already returns a u8-tagged item (`array_num_read_item`, `lambda-data-runtime.cpp:263`). Binary indexing must match. Verified semantics: `x is u8` → `true`, arithmetic promotes to plain integer (`200u8 + 100` → `300`, no wrap-around), prints as the bare number. Returning `int` instead would be *inconsistent*: `5 is u8` is `false` (no implicit narrowing), so an int-returning `b[i]` would make `b[i] is u8` false — wrong for a byte.
- Range slices return `binary`.
- ~~Out‑of‑range indices raise `error`~~ **Out‑of‑bounds indices return `null` — aligned with array semantics** (verified: `[1,2,3][5]` → `null`).
- Tier 1 does an honest copy on slice; Tier 3 promotes slices to zero‑copy sub‑views.

> **Status 2026-07-15:** implemented. `item_at` returns a u8-tagged byte,
> range indexing returns an owned binary slice, and OOB scalar indexes return
> `null`. Covered by `test/lambda/binary_elements.ls`.

### 4.3 Iteration

```lambda
for byte in b { print(byte) }     // prints each u8

let sum = for (byte in b) byte    // [222, 173, 190, 239] : u8[]  (0xDE 0xAD 0xBE 0xEF)
let xor = reduce(b, (a, x) => a ^ x)
```

- The iterator yields `u8` values (not single‑byte binaries).
- Comprehensions over `binary` build `u8[]` by default; an explicit `binary` cast collects back into a binary.
- Implementation: extend the existing iterable dispatch table (same place strings register) with a `binary` case that walks `bytes[0 .. len)`.

> **Status 2026-07-15:** implemented through the unified iterator; loop values
> retain their u8 tags and indexed iteration exposes the normal integer key.

### 4.4 Concatenation operator `++`

```lambda
b'\xDE AD' ++ b'\xBE EF'         // b'\xDEADBEEF'
b1 ++ b2 ++ b3                    // associative
```

- Choose `++` (rather than overloading `+`) to keep numeric `+` unambiguous and to leave room for a future `+` doing element‑wise addition on `u8[]`.
- Type rule: `binary ++ binary → binary`. Mixed `binary ++ string` stays a *string* concat (the binary contributes its canonical `b'\x…'` text per B5) — explicit `encode(s, 'utf-8')` (§5.2) when byte concat with text content is meant.
- Implementation: allocate destination `Binary` of summed length, two `memcpy`s. Tier 3 may replace this with an `iolist`‑style rope.

> **Status 2026-07-15:** implemented with a checked, length-delimited allocation
> and two byte copies. Embedded NULs are preserved; mixed binary/string joins
> remain textual under B5.

### 4.5 Tier 1 deliverables checklist (status 2026-07-15)

- [x] ~~`Binary` struct~~ decoded-bytes payload on `String*` storage (B1; dedicated struct deferred to Tier 3.1/PL5)
- [x] Parser decodes hex / base64 literals to raw bytes (`lib/str_binary.c`, both producers)
- [x] Pretty‑print canonical `b'\x…'` form (uppercase, NUL-safe, shared helper)
- [x] `len(b)` = byte count
- [x] `b[i]`, `b[a to b]`, `x in b`
- [x] `for byte in b { … }` and comprehension support
- [x] `b1 ++ b2` byte concat
- [x] Update transpile/MIR/emit/proc/editor switch tables (incl. `pn_output` binary-safe accessor fix)
- [x] Test suite: literal round‑trip, decode errors, Mark/JSON egress, byte-exact output, fuzz fixture
- [x] Doc updates for landed semantics, including the element-ops batch

---

## 5. Tier 2 — Structured Access & Encoding Stdlib

### 5.1 Builder type for incremental construction

Append‑heavy code (codec output, HTTP body assembly) needs better than O(n²) repeated `++`.

```lambda
let bld = binary.builder()
bld.append(b'\xDE AD')
bld.append_u32_be(3405691582)        // 0xCAFEBABE — Lambda has no hex int literal yet
bld.append_str_utf8("hello")
let out: binary = bld.freeze()        // immutable snapshot
```

- `binary.builder()` returns a mutable, growable buffer (doubling strategy).
- `freeze()` transfers ownership into an immutable `binary` (zero‑copy when capacity ≈ length, otherwise truncating copy).
- Internally implemented as a thin wrapper around the existing `StringBuf` machinery (see `strbuf_append_str_n` in [Lambda_Transpiler.md](../doc/dev/Lambda_Transpiler.md)).

### 5.2 Encoding stdlib — `encode`/`decode` with a codec selector *(API SETTLED 2026-07-15, user-confirmed)*

Keep encodings as **functions over `binary` and `string`**, not separate types — and expose them as **two functions with a codec-symbol selector**:

```lambda
encode(x, 'hex')                 // codec as a symbol argument
decode(x, 'base64')
```

> **Rejected design (recorded):** the per-codec-namespace form this section originally proposed —
>
> ```lambda
> hex.encode(b) / hex.decode(s)              // REJECTED
> base64.encode(b) / base64.url_encode(b)    // REJECTED
> utf8.encode(s) / utf8.decode(b)            // REJECTED
> ```
>
> Rejected because the codec is code rather than data (not storable/computable/passable), each codec mints global names, N codecs × 2 directions = 2N functions to learn, and it diverges from the `input`/`format` selector family and Node's `Buffer` precedent. The rationale for the winner follows.

**Why selector beats namespace** (decision rationale):

1. **Consistency with the existing family.** Lambda already selects behavior by symbol everywhere at the data boundary: `input(path, 'json')`, `format(x, 'mark')`, `convert -t yaml` — and the pipeline design's stream stages (`decode('utf-8')`, PL6 rung table in [Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) §5) already use *these exact names*: the stream stage is simply the incremental form of the same codec. Node's `Buffer` is the direct precedent for the shape: `buf.toString('hex')` / `Buffer.from(s, 'base64')`.
2. **The codec is a value.** A symbol selector can live in config, arrive in a message, be computed by a pipeline (`encode(b, cfg.wire_encoding)`). A namespace (`hex.encode`) makes the codec *code* — selecting it at runtime needs reflection.
3. **One registry, like formatters.** Codecs become a name→vtable table mirroring the input/format dispatcher architecture — extensible (add `'base32'`, `'z85'`) without minting global names, and the stdlib namespace stays flat.
4. **Discoverability:** two functions to learn, not 2×N.

**The direction rule** (kills the classic encode/decode ambiguity): every codec has a *plain* side and an *encoded* side; **`encode` always maps plain→encoded, `decode` encoded→plain.**

- For byte-to-text codecs (`'hex'`, `'base64'`, …): plain = `binary`, encoded = `string`.
- For charsets (`'utf-8'`, `'latin1'`, …): plain = `string`, encoded = `binary` (Python 3's `str.encode`/`bytes.decode` convention).

```lambda
// byte ⇄ text codecs: plain side is binary
encode(b, 'hex')          // -> string   "DEADBEEF" (uppercase, matches canonical print)
decode(s, 'hex')          // -> binary^error   accepts whitespace + mixed case
encode(b, 'base64')       // -> string   RFC 4648, padded
decode(s, 'base64')       // -> binary^error   padded or unpadded accepted
encode(b, 'base64url')    // URL-safe alphabet, no padding — a codec NAME, not an option
decode(s, 'base64url')
encode(b, 'base32')  / decode(s, 'base32')     // RFC 4648
encode(b, 'base85')  / decode(s, 'base85')     // ASCII-85 / Z85 as separate names

// charset codecs: plain side is string
encode(s, 'utf-8')        // -> binary   (total — Lambda strings are UTF-8)
decode(b, 'utf-8')        // -> string^error
encode(s, 'latin1') / decode(b, 'latin1')
encode(s, 'ascii')  / decode(b, 'ascii')       // strict 7-bit; error on violation
```

Conventions: decoders return `T^E` (`binary^error` / `string^error` — the standard error-value model, not a union with bare `error`); encoders are total per codec (unknown codec name → error value from either function). Variant behavior is expressed as **distinct codec names** (`'base64url'`, not `url: true`) so the selector stays pure data; cosmetic formatting options (`encode(b, 'hex', sep: " ")`) may be added as named args later without disturbing the model. The same selector principle extends to the neighboring stdlib families: `hash(b, 'sha256')`, `compress(b, 'gzip')` (§6.4–6.5), and the streaming stages of PL6 — one vocabulary from scalar call to pipeline stage.

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
pack(">I4 H s1", 3405691582, 16, "hi")       // 3405691582 == 0xCAFEBABE -> binary
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
    case <bin u32/be: 2303741511, binary: rest>:    // 0x89504E47 — PNG magic; decimal until hex int literals land
        parse_png(rest)
    default:
        error("unknown format")
}
```

Tier 2 ships a restricted form (byte‑aligned widths only). Sub‑bit fields can come later if demand exists. Implementation builds on the existing pattern‑matching infrastructure in [Lambda_Expr_Match.md](Lambda_Expr_Match.md).

---

## 6. Tier 3 — Performance & Ecosystem

### 6.1 Sub‑binary slicing without copy

> **Superseded design:** the earlier `Binary* owner` chain is replaced by the
> accepted shared byte-storage substrate in §11. A binary slice is a direct
> `(storage, byte_offset, byte_length)` span, never a chain of parent binaries.

- `b[a to b]` becomes O(1): allocate only a binary view header and retain the
  shared storage.
- Every slice points directly at the root storage allocation, so slicing a
  slice does not create an ownership chain.
- `binary.copy(b)` remains the explicit escape hatch for detaching a tiny,
  long-lived view from a giant allocation.
- The same storage allocation can back a JS `ArrayBuffer` handle and an
  `ArrayNum` external view without merging their runtime types.

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

Same selector principle as §5.2 — the algorithm is a symbol argument, not a namespace:

```lambda
hash(b, 'sha256')             -> binary       // 32 bytes
hash(b, 'sha1') / hash(b, 'md5') / hash(b, 'crc32')
hmac(key: binary, b, 'sha256') -> binary
random_bytes(n: int)          -> binary
constant_time_eq(a: binary, b: binary) -> bool
```

### 6.5 Compression

```lambda
compress(b, 'gzip', level: 6)   -> binary
decompress(b, 'gzip')            -> binary^error
compress(b, 'zstd') / decompress(b, 'zstd')
compress(b, 'deflate') / decompress(b, 'deflate')
```

Each entry takes `binary → binary`; the streaming forms are exactly the PL6 transducer stages of [Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) §5 (`|> gunzip()` etc.), same names, incremental with flush.

### 6.6 Cross‑runtime zero‑copy bridge

> **Status 2026-07-15:** the **copy** bridge shipped ([Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) Phase 5, decision B8): `Buffer.from`/`Uint8Array`/`Uint8ClampedArray`/`DataView` interop with mutation isolation. Copying is *required* today, not provisional laziness — Lambda binary is immutable while `JsArrayBuffer.data` is mutable and manually freed on detach/transfer; aliasing would let JS mutate or dangle a Lambda value. The zero-copy end-state below is the PL5 flat-buffer work, where detach becomes drop-ref and sharing becomes sound. **Note the type stays distinct — see §10; only the buffer unifies.**

Make Lambda `binary` share the **same physical buffer** as JS `Uint8Array` and (future) Python `bytes`/`bytearray`. Lambda's [Jube runtime](../doc/Lambda_Jube_Runtime.md) is the right place to define the contract:

- A shared refcounted buffer ABI — this is exactly PL5's refcounted immutable flat buffer ([Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) §4); the `BufferDescriptor { ptr; len; owner; refcount; readonly }` sketched here and PL5 are one design, and the `ArrayNum` external-view machinery (`is_view`/`is_pinned`/`ArrayNumShape`, already GC-aware and already backing every JS typed array) is the implementation template.
- `js.Uint8Array` constructed from a Lambda `binary` shares storage (read-only pages or COW).
- Python `bytes` (when implemented) likewise.
- Eliminates the marshal cost that currently makes binary‑heavy cross‑language workflows untenable.

The authoritative ownership, copy-on-write, detach, resize, transfer, and
`SharedArrayBuffer` rules are in §11; the executable migration is
[Lambda_Impl_Binary2.md](Lambda_Impl_Binary2.md).

### 6.7 Buffer protocol / FFI

Expose a stable C ABI matching the descriptor above so MIR‑JIT‑emitted code and external C libraries can read/write binaries directly without going through `Item` boxing. Python's PEP 3118 and LuaJIT FFI both demonstrate the multiplier this gives to the ecosystem.

---

## 7. Tier 4 — Polish

### 7.1 Pretty printing & REPL display

- ~~Default `print(b)` → `b'\xDE AD BE EF'` with two‑hex‑per‑byte grouping.~~ **Decided & shipped as B3:** canonical form is `b'\xDEADBEEF'` — uppercase, **ungrouped** (round-trips as a literal since whitespace is ignored on decode; grouping remains available as a future `encode(b, 'hex', sep: " ")` cosmetic, §5.2).
- `hexdump(b)` → canonical 16‑bytes‑per‑row display with offset gutter and ASCII column:
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

| Tier | Theme | Ship‑gating items | Status |
|---|---|---|---|
| **1** | Foundation | Decoded representation ✅; canonical print ✅; `len` ✅; `binary()` ✅; formatter/I-O egress ✅; JS copy bridge ✅ (impl plan Phases 1–5); indexing ✅; slicing ✅; membership ✅; iteration ✅; `++` byte concat ✅ | **landed 2026-07-15** |
| **2** | Structured access | Builder; encoding stdlib (`encode`/`decode` selector API, §5.2); endian accessors; `pack`/`unpack`; bit‑pattern `match` | proposal |
| **3** | Performance & ecosystem | Shared byte-storage substrate and sub‑binary zero‑copy slicing (**= PL5 flat buffer**, §11); file I/O; `mmap`; crypto; compression (= PL6 transducers streaming); cross‑runtime zero-copy buffer; FFI | substrate design accepted; implementation planned in `Lambda_Impl_Binary2.md`; remaining items proposal |
| **4** | Polish | Hexdump; validators; constant‑time eq; round‑trip / fuzz tests; full docs | proposal |

Tier 1's decode core was the unblocker: every later tier assumes binaries are real bytes, not source‑text shadows. With the Tier-1 element operations now landed, Tiers 2–4 can proceed independently and incrementally.

---

## 9. Streams alignment — WHATWG byte streams and Node streams

*(Recorded 2026-07-15; authoritative design in [Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) Parts 2–3 and `Lambda_Design_Concurrency.md` §11 K27/K28. This section is the binary-type-eye view.)*

The `binary` type is the **chunk currency** of Lambda's binary pipelines: a binary stream is the K27 bounded Item chunk-queue carrying binary Items, with chunk boundaries *semantically invisible* (PL3 re-chunking license) and no `~` auto-mapping (PL4). What the type must provide for streaming — zero-copy sub-views (Tier 3.1/PL5), byte-length measurement (PL7 queues bound by `sum(len)`), and transducer stages with flush (PL6) — is exactly this doc's Tier-3 roadmap; the pipeline design and this proposal converge on the same representation work.

**WHATWG byte streams — the conformance target (PL10a).** Lambda commits to WHATWG Web Streams at 100% spec conformance (K28), *including readable byte streams*, and the binary type maps one-to-one onto the spec's concepts:

| WHATWG | Lambda binary |
|---|---|
| chunk = `ArrayBuffer`/`Uint8Array` view | `binary` value; sub-binary view (buffer, offset, len) under Tier 3.1/PL5 |
| `ByteLengthQueuingStrategy` / `desiredSize` | PL7 byte-metric queue bound / remaining byte capacity |
| `TransformStream` `transform`/`flush`/`cancel` | PL6 transducer contract (decompress, decrypt, charset decode, frame-split) |
| `TextDecoderStream` / `CompressionStream` | `decode(…, 'utf-8')` / `gunzip()` stages — same codec vocabulary as §5.2 |
| BYOB reader / `autoAllocateChunkSize` | PL11 pooled reads (deferred; Layer-0 API seam reserved) |

**Node streams — best-effort through the shim (PL10b).** Legacy Node streams re-base as a compat shim over the Web-Streams-shaped core (K27 Layer 2); they are *best-effort by commitment* (K28) — shim gaps are fixed when real packages need them, never chased to 100%. The binary-relevant mappings: a Node `Buffer` chunk and a Lambda binary chunk are the same bytes riding the same queue (Buffer ≡ Uint8Array ≡ `ELEM_UINT8` view — §1.2 of the impl plan; under PL5, literally the same flat buffer); `highWaterMark` in bytes ≡ PL7 byte mode, `objectMode` ≡ count mode; flowing/paused mode-switching and event-timing folklore stay shim-only, regression-gated by the node baseline.

---

## 10. Design decision: unification with `Uint8Array` — REJECTED

*(Discussed and decided 2026-07-15, user-confirmed. Context: after the Tier-1 fix, both `binary` and JS `Uint8Array` hold real bytes, and LambdaJS typed arrays are already `ArrayNum` external views — so "make `LMD_TYPE_BINARY` be a u8 array" was the obvious question.)*

**The idea.** Re-base `LMD_TYPE_BINARY` on `ArrayNum` `ELEM_UINT8` (or otherwise merge it with the `Uint8Array` representation), so Lambda binary *is* a byte array — one type, free element ops, free JS interop.

**Rejected — the fundamental reason: `binary` represents just the raw bytes, and raw bytes are interpretation-free.** A byte buffer can be read as a `Uint8Array`, but equally as a `Uint16Array`/`Float64Array`, a `DataView` with per-access endianness, a UTF-8 or Latin-1 text, a length-prefixed frame sequence — or as **structured, non-array data** through a binary schema: [Lambda_Type_Binary_Schema.md](Lambda_Type_Binary_Schema.md) builds typed layouts over `binary` where, quoting its §5.1, *"`Velmt` is to `Element` what `VMap` is to `Map`: a vtable-dispatched virtual node that presents the standard element API while storing nothing but a buffer, a schema, and an offset."* A PNG held in a `binary` is *presented as an element tree* — no array in sight. `Uint8Array` is therefore **one possible view among many**, and baking it into the value model would wrongly promote a particular interpretation into the identity of the uninterpreted value. The correct layering is: **`binary` = the interpretation-free bytes; every view — typed arrays, DataView, text decodings, `Velmt` schemas — is constructed on top.**

**Supporting reasons (each independently sufficient):**

1. **Value vs. container semantics.** `binary` is a compound *scalar*: immutable, `memcmp` value equality, a rank in the total order, validator-matchable, map-key-safe. `Uint8Array` is a mutable *container* with identity semantics, reference equality, and a detach/resize lifecycle. Merging would import `ArrayNum`'s representation-sensitive `==` (a known defect for numeric arrays) into a scalar where `b'\xDEAD' == b'\xDEAD'` must hold unconditionally, and would add a mutation surface to a value whose immutability is load-bearing — for K5 flat-sharing across isolates and for PL3's "streams equal on their byte concatenation."
2. **Precedent.** Python deliberately keeps `bytes` ≠ `bytearray` ≠ `memoryview` along exactly this axis; Rust's ecosystem converged on `bytes::Bytes` (refcounted immutable) vs `Vec<u8>`; JS itself demonstrates the gap from the other side — it *lacks* an immutable byte value, which is why "immutable ArrayBuffer" proposals keep appearing at TC39. Lambda already has the pair right: `binary` ≙ `bytes`, u8 `ArrayNum`/`Uint8Array` ≙ `bytearray`.
3. **The number-model contrast.** JS `number` ⇔ Lambda `float` were unified (N1) because their semantics are *identical*. `binary` vs `Uint8Array` differ on mutability, equality, and identity — the precondition for type unification fails.

**What IS unified instead — the storage, not the type** (the K27 "one core, separate faces" pattern): today a copy bridge at the immutability boundary (B8, shipped); under Tier 3.1/PL5, one refcounted flat byte buffer beneath both `binary` (immutable view) and `JsArrayBuffer.data` (mutable pages, detach = drop-ref), making conversion view-flipping — with the one permanent rule that handing JS a *mutable alias* of a Lambda binary is never allowed, or immutability is a lie.

---

## 11. Accepted design: shared byte-storage substrate, separate value types

*(Accepted 2026-07-15. Detailed implementation plan:
[Lambda_Impl_Binary2.md](Lambda_Impl_Binary2.md). This section supersedes the
older parent-`Binary*` sub-view sketch in §6.1 and makes the PL5 convergence
contract precise.)*

### 11.1 Decision

Unify the **byte-storage substrate** used by Lambda `binary`, JS
`ArrayBuffer`/typed arrays/Node `Buffer`, and eligible `ArrayNum` storage. Do
**not** merge `LMD_TYPE_BINARY`, `LMD_TYPE_ARRAY_NUM`, or JS buffer objects.

The shared layer owns bytes and byte ranges. Each semantic wrapper retains its
own rules:

- `binary` remains an immutable compound scalar with byte-value equality,
  canonical hex printing, and binary slicing/concatenation semantics.
- `ArrayNum` remains a typed numeric container with element kind, dimensions,
  strides, broadcasting, and functional/procedural mutability rules.
- `JsArrayBuffer` remains a mutable identity-bearing handle with detach,
  resize, transfer, and shared-buffer state.
- `JsTypedArray`, `DataView`, and Node `Buffer` remain views/faces over an
  ArrayBuffer handle; they do not become Lambda binaries.

This is the same architectural split already proven on the JS side: a
`JsTypedArray` carries an `ArrayNum*` descriptor over `JsArrayBuffer` bytes, so
the element-operation layer is shared without merging JS and Lambda value
identity. Phase 6 extends that principle downward to storage ownership.

### 11.2 Current implementations and the convergence point

| Value/view | Current byte ownership | Current view/lifetime rule | Target |
|---|---|---|---|
| Lambda `binary` | bytes inline in a String-shaped GC object | slices copy | immutable span over shared storage; small-inline optimization permitted |
| Owned `ArrayNum` | separate GC data-zone allocation | movable unless pinned | retain current fast path initially; large/shareable storage may use the common substrate |
| `ArrayNum` view | raw data pointer + `ArrayNumShape.base` | GC marks base; owner is permanently pinned once viewed | descriptor resolves through a stable storage owner/handle; no dangling raw alias |
| `JsArrayBuffer` | mutable `mem_alloc` allocation | manually freed on detach/finalize; resize replaces allocation | mutable handle over refcounted storage |
| JS typed array / Buffer | `ArrayNum` external view over ArrayBuffer bytes | wrapper refreshes raw pointer after detach/resize | same `ArrayNum` operation layer, backed by the stable handle/storage generation |

`ArrayNumShape` is the template for offsets, shapes, strides, base liveness, and
mutable/read-only views. It is **not** itself the common storage object:
N-dimensional metadata does not belong in a scalar binary, and a raw
`Container* base` is not sufficient for cross-isolate refcounted ownership.

### 11.3 Two storage layers: allocation and mutable handle

The design separates the physical allocation from any mutable language-level
handle:

```c
// conceptual C-compatible shape; exact field packing is an implementation task
typedef struct ByteStorage {
    atomic_int32 refs;
    uint8_t* data;
    size_t capacity;
    uint32_t flags;        // owned, readonly, external, mapped, shared-mutable
    void (*release_data)(void* data, size_t capacity, void* context);
    void* release_context;
} ByteStorage;

typedef struct ByteSpan {
    ByteStorage* storage;
    size_t offset;
    size_t length;
} ByteSpan;

typedef struct ByteBufferHandle {
    ByteStorage* storage;
    size_t storage_offset; // start of this handle's visible allocation
    size_t byte_length;
    size_t max_byte_length;
    uint64_t generation;
    uint32_t flags;        // detached, resizable, shared
} ByteBufferHandle;
```

`ByteStorage` is pointer-free with respect to Lambda Items and is owned by
atomic retain/release. `ByteSpan` is the common byte-range vocabulary.
`ByteBufferHandle` is stable while its current allocation may change. A JS
ArrayBuffer embeds or owns this handle; all of its typed-array/DataView faces
resolve through it.

The separation is load-bearing:

- A `binary` points directly to an immutable storage snapshot and never follows
  later JS resize/detach operations.
- Typed arrays follow the ArrayBuffer handle and therefore continue to observe
  its current allocation, length, detach state, and generation.
- Typed-array/DataView offsets are relative to `storage_offset`; checked
  resolution adds the handle and view offsets without losing the root pointer
  required by the storage release callback.
- A resize swaps the handle to a new storage allocation; existing binary
  snapshots retain the old allocation.
- A detach clears the handle and releases its reference; it does not free bytes
  still retained by a binary.

### 11.4 Binary representation and slicing

A binary becomes either a small inline value or a direct span over storage:

```c
typedef struct Binary {
    uint32_t len;
    uint8_t is_ascii;
    uint8_t flags;         // inline or storage-backed
    uint16_t reserved;
    ByteStorage* storage;  // NULL for inline values
    size_t offset;         // valid for storage-backed values
    uint8_t inline_bytes[];
} Binary;
```

All consumers use `binary_data()` and `binary_len()`; none inspect `chars`
directly. A storage-backed slice retains the same `ByteStorage` and adjusts
`offset`/`len`, so nested slices always point directly to root storage rather
than forming parent chains.

Small binaries may remain inline (the PL5/BEAM precedent suggests an initial
64-byte threshold, to be confirmed by measurement). Crossing an inline value
into a shareable runtime may perform one promotion copy; storage-backed values
then remain zero-copy. The threshold is an allocation optimization, never a
semantic distinction.

Retaining a tiny slice can retain a giant allocation. `binary.copy(value)` is
the explicit detachment operation; a later heuristic may copy automatically
only when profiling establishes a safe size/retention policy.

### 11.5 Immutability and copy-on-write

The physical allocation may be shared between an immutable binary snapshot
and a mutable JS ArrayBuffer handle only if mutation preserves the snapshot:

1. `binary → Uint8Array/Buffer`: retain the storage and create an ArrayBuffer
   handle over the same range.
2. JS read: access the shared storage directly.
3. First JS write while storage is shared: allocate a private storage block,
   copy the handle's visible bytes, swap the handle, increment its generation,
   and then write.
4. The original binary continues to observe the original bytes.

All views of the **same** ArrayBuffer follow the handle, so they remain coherent
after copy-on-write. A typed-array implementation must not cache a raw data
pointer across a generation change. MIR/JS optimized paths must guard the
handle generation or call the same ensure-current/ensure-writable helper as the
generic path.

The same rule applies in the reverse direction: `binary(typed_array)` may retain
an eligible ArrayBuffer storage snapshot. Later JS writes trigger copy-on-write;
the binary value never changes.

### 11.6 Detach, resize, transfer, and shared-buffer rules

| Operation | Required storage behavior |
|---|---|
| ArrayBuffer detach | Clear the handle, increment generation, release one storage reference. Binary snapshots remain valid. |
| Resize | Allocate the required new storage, copy the preserved prefix, atomically swap handle state, increment generation, release the old reference. |
| Transfer, unchanged length | Move the handle reference when legal; detach the source. Sharing with immutable binaries is allowed because subsequent writes remain COW. |
| Transfer with changed length | Allocate/copy according to JS semantics, then detach the source. |
| `ArrayBuffer.prototype.slice` | Remains a copying JS operation, as required by ECMAScript; storage unification does not change JS-visible semantics. |
| TypedArray `subarray` | Remains a view over the same ArrayBuffer handle. |
| `SharedArrayBuffer → binary` | Snapshot copy. Mutable concurrent storage must never masquerade as an immutable Lambda value. |
| `binary → SharedArrayBuffer` | Copy into new shared-mutable storage. |
| mmap/read-only external storage | Wrap with an appropriate release callback; JS mutation first materializes writable private storage. |

### 11.7 Relationship to `ArrayNum`

`ArrayNum` and binary share storage capabilities, not container semantics.
Convergence proceeds in two levels:

1. **Required convergence:** JS typed arrays continue using `ArrayNum` view
   descriptors, but those descriptors resolve bytes through the new
   ArrayBuffer handle/storage rather than treating a refreshable raw pointer as
   the owner. This completes the binary ↔ JS ↔ ArrayNum storage triangle.
2. **Profile-gated convergence:** large owned `ArrayNum` allocations may move
   from the GC data zone to `ByteStorage`, enabling cheap cross-isolate sharing
   and eliminating permanent pinning for those arrays. Small/local arrays may
   retain the current GC data-zone fast path.

The second level is not required to ship binary zero-copy interop. Forcing
every small numeric array through atomic refcounting would add header,
allocation, and retain/release costs without evidence that it helps. The
storage API must support ArrayNum, but allocation policy remains type- and
size-aware.

`ArrayNumShape` continues to own element-space geometry: element offset,
dimensions, and strides. The common substrate uses byte offsets and byte
lengths. Conversion between the two must be checked using
`ELEM_TYPE_SIZE[elem_type >> 4]`; byte/element units may never be mixed
implicitly.

### 11.8 Lifecycle and GC invariants

The shared substrate is outside the GC data zone and therefore requires real
finalization, not context-end cleanup only:

- Every GC-managed storage-backed `Binary` releases exactly one reference when
  swept or when the heap is destroyed.
- Every live ArrayBuffer handle owns exactly one reference; detach/transfer
  consume that ownership exactly once.
- ArrayNum views either keep a GC base handle alive or own an explicit storage
  reference, never an unowned raw pointer.
- Compiler/pool-owned binary constants have an explicit storage-owner registry
  or remain inline; pool destruction may not leak external refcounted blocks.
- Retain/release is atomic because PL5 storage may cross isolates.
- The release callback owns allocator-specific cleanup (`mem_free`, `munmap`,
  foreign-runtime release); storage clients never guess how bytes were created.
- Refcount underflow, offset+length overflow, stale generation, and a non-null
  data pointer on a detached handle are assertion failures in debug builds.

### 11.9 Non-goals and compatibility

- No change to binary literal syntax, canonical printing, equality, ordering,
  indexing, iteration, formatter output, or I/O behavior.
- No automatic equivalence between `binary` and `u8[]`/`Uint8Array`.
- No mutable Lambda binary type in this phase; builders remain a separate Tier
  2 surface.
- No zero-copy alias of `SharedArrayBuffer` as immutable binary.
- No requirement to migrate every owned ArrayNum allocation before shipping.
- No raw public pointer ABI without an accompanying retained owner/span token.

The implementation must preserve the Phase-1–5 tests unchanged while adding
new storage lifetime, COW, subview, detach/resize/transfer, GC-stress, and
cross-runtime regressions. The ordered work and acceptance gates are specified
in [Lambda_Impl_Binary2.md](Lambda_Impl_Binary2.md).
