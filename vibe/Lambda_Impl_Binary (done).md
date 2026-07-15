# Lambda Binary Type — Implementation Plan: Decoded Bytes + JS/Node Buffer Unification

**Status:** Phases 1–5 implemented; Phase 6 remains the explicitly deferred PL5 follow-up
**Plan date:** 2026-07-14
**Implementation completed:** 2026-07-15
**Context:** executes the latent-defect fix recorded in `Lambda_Design_Pipeline.md` §4 / O-PL5, and scopes the JS unification it unlocks. Before this implementation, `LMD_TYPE_BINARY` had **two producers with inconsistent payloads**: literals stored the raw source text undecoded (`b'\xDEADBEEF'` held the 10 chars `\xDEADBEEF`), while file reads stored real bytes. Phases 1–5 make decoded bytes the only payload, fix every consumer, and bridge the type to the JS side — where a complete typed-array stack already exists and is already unified with `ArrayNum`.

## 0. Completion record (2026-07-15)

The inconsistency described above is fixed. Every Lambda binary producer now
creates a length-delimited `LMD_TYPE_BINARY` containing decoded bytes, every
textual representation is explicit, and the JS/Node bridge copies at the
immutability boundary as required by B8.

| Phase | Status | Landed behavior |
|---|---|---|
| 1 — decoder and producers | **complete** | one shared decoder in `lib/str_binary.c`; compiler literals and Mark input both decode hex/base64 and reject malformed payloads |
| 2 — consumers | **complete** | byte length, canonical NUL-safe printing/string conversion, real `binary()` values, and a string-like consumer audit |
| 3 — formatters and I/O | **complete** | native Mark literals, padded standard base64 for non-binary text formats, and byte-exact raw output |
| 4 — tests and goldens | **complete** | decoder unit tests, positive/negative scripts, Mark/JSON/output regressions, corrected goldens, and a repaired runnable fuzz fixture |
| 5 — JS/Node copy bridge | **complete** | `Buffer`, `Uint8Array`, `Uint8ClampedArray`, and `DataView` copy interop with mutation isolation |
| 6 — PL5 convergence | **deferred by design** | unchanged: refcounted flat storage and zero-copy views remain a separate representation project |

Two implementation details differ from the original mechanical forecast while
preserving all B1–B8 decisions:

- The decoder lives in `lib/str_binary.c`, not `lib/str.c`, so small users of the
  foundational string library do not acquire the base64 allocator as a link-time
  dependency. `build_lambda_config.json` wires it into the full Lambda input/runtime.
- `pn_output` required a real fix: its old `it2s(source)` call accepts only text
  tags and discarded a correctly tagged binary. It now uses the binary-safe
  accessor before the existing length-delimited write.

### Verification snapshot

- `make build`: **pass**, 0 errors / 0 warnings.
- Decoder GTest and focused binary/Mark/JS/output regressions: **pass**.
- Direct MIR and legacy C2MIR binary regressions: **pass**.
- `test/fuzzy/lambda/corpus/valid/data_types.ls`: **pass**, including decoded binary construction.
- `make test-lambda-baseline`: **pass**, 3333/3333 script cases plus
  490/490 Lambda GTests, 311/311 JS GTests, and 110/110 preliminary Node
  GTests.
- `make node-baseline`: all binary/Buffer coverage passes and the run produced
  no semantic failures or crashes. The loaded official set reported 3524
  passes and four 60-second timeouts under parallel host load; isolated reruns
  passed three. The remaining `test-http-set-trailers.js` timeout is a verified
  pre-existing stale-baseline issue: both this implementation and an isolated
  executable built from untouched `HEAD` fail identically because
  `ServerResponse.addTrailers` is absent. The harness-generated slow-list edits
  were discarded; no unrelated baseline policy was changed to hide it.

---

## 1. Pre-implementation state (audited 2026-07-14)

### 1.1 The Lambda side — what's broken

| Site | Today | Defect |
|---|---|---|
| `build_ast.cpp:2863–2901` `build_lit_string` SYM_BINARY | copies source chars between `b'`/`'` verbatim (whitespace-trimmed) into a `String`, appends to `const_list` | **no decode** — contradicts `doc/Lambda_Data.md` (`\x` hex, `\64` base64) |
| `input/input-mark.cpp:193–243` `parse_binary` | strips the `\x`/`\64` marker, accumulates the *encoded characters*, returns **`s2it` (a STRING)** | no decode **and** wrong type tag |
| `lambda-eval.cpp:3554–3568` `fn_len` | source-text length (`// todo: binary length`) | `len(b'\xDEAD')` = 6, should be 2 |
| `print.cpp:447–451` | `b'%s'` from chars | `%s` truncates at first NUL → breaks on *real* (file-read) binaries |
| `print.cpp:318–323` | `0x%s` prefix | disagrees with the other printer; same NUL bug |
| `lambda-eval-num.cpp:2199–2299` `fn_binary` | `binary(x)` returns **`s2it` STRING** | `binary("hello") is binary` == `false`; contradicts `doc/Lambda_Sys_Func.md:33` |
| `lambda/format/*` | **no binary case in any formatter** | binary falls into default/string handling; no egress story |
| `lib/str.c:1240` `str_hex_decode` | exists, **zero callers** | the decoder was built and never wired |

Consumers that are payload-agnostic and need **no code change** (results change, code doesn't): `fn_eq` memcmp (`lambda-eval.cpp:1523`), `total_cmp` byte compare (`:1778`), validator memcmp (`validate.cpp:118`), all shaped-slot/field plumbing (`lambda-data.cpp`, `input.cpp`, `js_runtime.cpp` store sites), GC scanning (`gc_heap.c` treats Binary in the String-like boxed range), `mark_builder.cpp:831` deep copy (`createString(chars, len)` is length-based), `pn_output` raw write (`lambda-proc.cpp:255` — becomes *correct* once payload is bytes). Grammar (`grammar.js:247`: `token(seq("b'", repeat1(/[^']/), "'"))`) is already encoding-agnostic — **no grammar change, no parser regen**.

Available lib machinery: `lib/hex.h` (`hex_decode`, `hex_decode_byte`), `lib/base64.{h,c}` (`base64_decode_variant` STD/URL, padded/unpadded, `base64_encoded_len`), `lib/str.c` `str_hex_encode`/`str_hex_decode` — all tested (`test/lib/test_base64_gtest.cpp`, `test_str_gtest.cpp`), all currently unused by the binary-literal path.

### 1.2 The JS side — what already exists (survey result)

The unification substrate is **already built**, and better than hoped:

- `lambda/js/js_typed_array.{h,cpp}`: full ES surface — `JsArrayBuffer` (owns `void* data`, `mem_calloc`'d, resizable/shared/detach), `JsTypedArray` (view descriptor: element type, backing buffer, `is_buffer` flag), `JsDataView`; 12 element kinds incl. BigInt64/Float16; Atomics.
- **Key architecture fact:** every `JsTypedArray` carries an **`ArrayNum* view`** created by `array_num_new_external_view(...)` (`js_typed_array.cpp:2107, 2172`) — a real `LMD_TYPE_ARRAY_NUM` container (`is_view=1, is_mutable_view=1`, `ArrayNumShape` offset/base side-table) aliasing the ArrayBuffer bytes. All element math delegates to Lambda's ArrayNum helpers. `ELEM_UINT8` exists (`lambda.h:198–227`). JS typed arrays and Lambda numeric arrays are *already one storage system*.
- `lambda/js/js_buffer.cpp`: complete Node `Buffer` module — and a Buffer **is** a `JS_TYPED_UINT8` typed array with `JsTypedArray::is_buffer = true` (`js_buffer.cpp:147–154`), methods on a parallel prototype. Not a separate representation.
- `LMD_TYPE_BINARY` is the **only orphan**: its 4 references in `lambda/js/` (`js_runtime.cpp:3030/5715/7409`, `js_props.cpp:618`) are generic slot writers treating it as `String*`. No bridge to ArrayBuffer/Uint8Array/Buffer exists.

### 1.3 Does ECMA-262 define a binary type? (question answered)

**Yes — the binary family is core ES262**, not host API: `ArrayBuffer` + the 11 TypedArray kinds + `DataView` since ES2015, `SharedArrayBuffer`/Atomics since ES2017, resizable ArrayBuffer ES2024, `Float16Array` ES2025. Directly relevant here: the **`Uint8Array.fromHex/fromBase64/toHex/toBase64`** proposal (TC39 Stage 4, ES2026-track, shipping in engines) standardizes *exactly* Lambda's two literal encodings — hex and base64 to/from a byte array. Node's `Buffer` is **not** in the spec; it is Node's Uint8Array subclass — which LambdaJS already models faithfully (§1.2). So the canonical unification target is **`binary` ⇄ `Uint8Array`** (spec type), with Buffer riding along for free via `is_buffer`.

---

## 2. Target semantics — decision ledger

- **B1 — Payload = decoded bytes; String-backed storage retained (this plan).** `typedef String Binary` stays: `{len, chars[]}` with `len` = byte count, `chars` = raw bytes (embedded NULs legal; still NUL-terminated at `chars[len]` for safety, but no consumer may use `strlen`/`%s`). `is_ascii` computed over the actual bytes. The PL5 flat-buffer representation (refcounted buffer + zero-copy sub-views, shared with `JsArrayBuffer`) is a **separate later phase** (Phase 6 sketch; `Lambda_Design_Pipeline.md` PL5) — this plan deliberately does not block on it.
- **B2 — Literal encodings.** Inside `b'...'`: `\x` prefix → hex; `\64` prefix → base64 (STD alphabet, padded or unpadded accepted); **no prefix → hex** (matches `input-mark.cpp`'s existing default and `test/lambda/value.ls:8` `b'A0FE'`). ASCII whitespace ignored *anywhere* inside the payload (doc example `b'\xA0FE af0d'`). Errors — non-hex char, odd nibble count, invalid base64 — are **compile-time errors** for literals (build_ast reports like other malformed literals) and parse errors for Mark input. Empty payload after the marker → empty binary is *absent* (existing Phase-3 rule, `build_ast.cpp:2864` comment — unchanged).
- **B3 — One canonical output form: `b'\x<UPPERCASE HEX>'`.** Used by `print.cpp` (both sites — the `0x%s` variant is retired), `format-mark`, REPL echo. Re-encoding, never stored text. Base64-authored literals print as hex too (canonical form is about the *value*, encoding is authoring sugar). One shared helper (CLAUDE.md rule 13): `format_binary_literal(StrBuf*, String* bin)`.
- **B4 — `len(binary)` = byte count.** `len(b'\xDEAD')` == 2.
- **B5 — `string(binary)` = the canonical literal text** (`"b'\xDEAD'"` form): lossless, round-trips through Mark parse, and makes `"x: " ++ bin` produce readable output. (UTF-8 *interpretation* of bytes is a different operation — that's a future `decode(bin, 'utf-8')`, PL6 rung territory, not `string()`.)
- **B6 — `binary(x)` returns real binary (`x2it`).** `binary(string|symbol)` = the UTF-8 bytes of the text (TextEncoder semantics); `binary(binary)` = identity; `binary(int|int64|float)` = bytes of its decimal text (preserves today's observable text, now correctly tagged — revisit toward `to_bytes(n, 'u32le')`-style explicit encodings when PL6 lands). Fixes the doc/impl mismatch at `Lambda_Sys_Func.md:33`.
- **B7 — Formatter egress.** `format-mark`: native `b'\x…'` literal (round-trips). JSON/YAML/XML and other text formats without a binary notion: **base64 string** (standard practice; matches `parse_data_uri` conventions already in lib). Input side unchanged beyond `input-mark`.
- **B8 — JS bridge v1 = copy at the boundary; zero-copy deferred to PL5.** `binary → Uint8Array`: allocate a `JsArrayBuffer`, memcpy, wrap (helper `js_typed_array_from_binary`). `Uint8Array/Buffer → binary`: memcpy the view's bytes into a new String-backed binary (`binary_from_typed_array`). Copying is *required* for correctness today, not laziness: Lambda binary is immutable while `JsArrayBuffer.data` is mutable, manually freed on detach/transfer (`js_typed_array.cpp:1864`) — aliasing would let JS mutate/dangle a Lambda value. The PL5 refc flat buffer is precisely what dissolves this (immutable refc pages + copy-on-detach), so zero-copy lands there by design, not by patch.

---

## 3. Phase 1 — the decoder, wired into both producers

**Implemented shared helper** (one decoder, two callers — never two copies):

```c
// lib/str.h — decode a Lambda binary-literal payload (content between b' and ').
// Handles \x / \64 markers, default hex, embedded ASCII whitespace.
// Returns bytes written to out, or -1 on invalid input (sets *err_off for diagnostics).
int str_binary_payload_decode(const char* content, int len, StrBuf* out, int* err_off);
```

Implementation in `lib/str_binary.c` on top of the existing `hex_decode_byte`
(`lib/hex.h`) and `base64_decode_variant` (`lib/base64.h`); the separate source
file avoids adding base64 linkage to small `str.c`-only targets. The
whitespace-skipping behavior is shared across both branches and is unit-tested
directly (GTest, Phase 4).

**Callers:**
1. `build_ast.cpp` `build_lit_string` SYM_BINARY branch: decode into a StrBuf, `pool_alloc` the `String` at decoded size, set `len`/`is_ascii` from bytes, keep the existing `const_list`/`const_index` flow (transpile/JIT const-load paths — `transpile.cpp:82` `const_x2it`, `transpile-mir.cpp:9000` — are index-based and untouched). Decode failure → compile error with `err_off` mapped to the source offset (follow the existing malformed-literal error pattern in build_ast; do not silently fall back).
2. `input/input-mark.cpp` `parse_binary`: replace the marker-strip-and-accumulate logic with the shared decoder; return **`x2it`**, not `s2it` (fixes the type-tag bug at the same time). Its private `mark_is_binary_space` helper becomes dead — delete it.

**Exit gate:** new decode unit tests green; `b'\xDEAD'` evaluates to a 2-byte value in a scratch script; baseline *not yet expected green* (goldens updated in Phase 4).

## 4. Phase 2 — consumers

1. **`fn_len`** (`lambda-eval.cpp:3554`): split `LMD_TYPE_BINARY` out of the string/symbol group → return `bin->len` (byte count; no `str_utf8_count`). Resolves the in-code `// todo: binary length`.
2. **Canonical printer**: add `format_binary_literal(StrBuf*, String*)` (uppercase hex via `str_hex_encode`, wrapped `b'\x…'`) in one place both `print.cpp:447` and `print.cpp:318` call; retires the `0x%s` divergence and the `%s`-NUL truncation in the same stroke. `emit_sexpr.cpp:984` prints AST-node *source* text at compile time — correct as-is, leave.
3. **`fn_string` / `++` join** (`lambda-eval.cpp:2379`, `:360`): route binary through the canonical printer per B5 (today it flows raw chars into `fn_strcat`, which would corrupt on NULs).
4. **`fn_binary`** (`lambda-eval-num.cpp:2199`): return `x2it` per B6; string/symbol input copies UTF-8 bytes; numeric input keeps text-bytes; add `binary→binary` identity fast path.
5. Audit sweep for `strlen`/`%s`/`str_utf8_*` on any path reachable with a binary payload (grep the string-like groupings listed in §1.1) — each such site either splits the binary case or is proven unreachable. Known-clean by construction: memcmp equality, `total_byte_cmp`, length-based copies.

**Exit gate:** `make build` clean; scratch-script checks: `len`, `==` across differently-authored equal literals (`b'\xA0FE'` == `b'\x A0 FE'` == `b'\64oP4='`), print round-trip, `binary("hello") is binary` == true.

## 5. Phase 3 — formatters and I/O egress

1. **`format-mark`**: add the `LMD_TYPE_BINARY` case emitting the canonical literal (shares `format_binary_literal`). Round-trip property: `parse(format(x, 'mark'), 'mark') == x` for binary values.
2. **`format-json` / `format-yaml` / other text formats**: binary → base64 string (`base64_encode` from lib) per B7. Document the choice in each formatter's header comment (asymmetric by design: JSON has no binary notion; ingress does not auto-detect base64).
3. **`pn_output`** (`lambda-proc.cpp:255`): changed the accessor from text-only
   `it2s` to `get_safe_binary`; the old accessor returned null after binaries
   became correctly tagged. The regression verifies that
   `output(b'\xDEADBEEF', path)` writes exactly 4 bytes `DE AD BE EF`.
4. `doc/Lambda_Data.md` (literal semantics — add the no-marker-default-hex rule and canonical form) and `doc/Lambda_Sys_Func.md` (`binary()` now truthful) updated in the same commit as the behavior.

**Exit gate:** mark round-trip test green; JSON egress test green; byte-exact file-output test green.

## 6. Phase 4 — tests and goldens

Golden updates (all currently encode the defect):
- `test/std/core/datatypes/binary_basic.expected` — `len` lines become byte counts (6→2 etc.), `binary("hello") is binary` flips to `true`, `len(binary("hello"))` stays 5 (UTF-8 bytes).
- `test/lambda/value.txt:6–7,30` — literals now print canonically: `b'A0FE'`/`b'\xA0FE'` → `b'\xA0FE'`; `b'\xA0FE af0d'` → `b'\xA0FEAF0D'`; the three base64 forms print as their hex equivalents.
- `test/lambda/expr.txt` — audited; it was already in the canonical form and
  required no content change.

New tests (per CLAUDE.md rule 8, each `.ls` with expected output):
- `binary_decode.ls`: hex default, `\x`, `\64` padded/unpadded, interior whitespace, case-insensitive hex, equality across encodings, `len`, embedded NUL (`b'\x00FF00'` — len 3, prints correctly), `string(bin)`, `binary()` conversions.
- Error cases (compile-error harness): odd nibble `b'\xDEA'`, non-hex `b'\xZZ'`, invalid base64 `b'\64!!'`.
- GTest: `str_binary_payload_decode` unit coverage incl. `err_off` positions; byte-exact `output()` file test.
- Fuzz corpus (`test/fuzzy/lambda/corpus/valid/data_types.ls`) already contains binary literals — re-run, no crashes.

**Exit gate:** `make test-lambda-baseline` **100%** (the non-negotiable); fuzz smoke clean.

## 7. Phase 5 — JS/Node unification (copy bridge, B8)

Now that both sides hold real bytes, the bridge is a pair of memcpys:

1. **`js_typed_array_from_binary(String* bin) → Item`** in `js_typed_array.cpp`: `js_arraybuffer_alloc(bin->len)` + memcpy + wrap as `JS_TYPED_UINT8` — a plain `Uint8Array` (spec type per §1.3; callers wanting Node flavor set `is_buffer` via the existing `create_buffer` path).
2. **`binary_from_typed_array(JsTypedArray* ta) → Item`**: refresh the view (`js_typed_array_refresh_arraynum_view`), memcpy `byte_length` bytes from the view window into a new String-backed binary, `x2it`. Accepts any `ELEM_UINT8`-compatible view; DataView handled via its buffer+offset+len.
3. **Wire-up (minimal, demand-driven):** (a) LambdaJS `Buffer.from(x)` / `Uint8Array.from(x)` / the `new Uint8Array(x)` constructor path accept a Lambda binary Item (one added type check where they already discriminate array-likes in `js_buffer.cpp` / `js_typed_array.cpp`); (b) a Lambda-side `binary(x)` overload accepting a typed-array Item (one case in `fn_binary`); (c) the four `lambda/js/` slot-writer sites keep String-slot semantics (they store *Lambda-shaped* fields — not a JS-value crossing; leave until a real consumer needs Uint8Array projection there).
4. Explicitly **not** in scope: auto-projection of binary as Uint8Array in general JS property access, and any zero-copy aliasing (B8 rationale — mutability + detach `mem_free` would dangle immutable Lambda values).

**Exit gate:** the interop regression (Lambda binary → JS `Buffer.from` →
mutate copy → Lambda value unchanged → round-trip bytes equal) passes. The
official Node run has no binary-related regression; its sole reproducible
timeout is unchanged in a clean-`HEAD` A/B executable, as recorded in the
verification snapshot above.

## 8. Phase 6 — convergence sketch (PL5 pointer, not this plan's scope)

> **Follow-up accepted and planned:** the storage-not-type unification is now
> authoritative in [Lambda_Type_Binary.md](Lambda_Type_Binary.md) §11, with the
> phased implementation and verification plan in
> [Lambda_Impl_Binary2.md](Lambda_Impl_Binary2.md). The paragraph below is kept
> as the original compatibility target for Phases 1–5.

End-state recorded so Phases 1–5 don't paint away from it: one **refcounted immutable flat byte buffer** (PL5) becomes the storage under *both* `binary` (immutable view: buffer+off+len) and `JsArrayBuffer.data` (mutable pages; detach = drop ref, not `mem_free`; transfer = ref move). Then `binary ⇄ Uint8Array` is view construction — zero-copy both directions — and `frames(...)` slicing (PL6) is free. The `ArrayNum` external-view machinery (`is_view`/`is_pinned`/`ArrayNumShape.base`, already GC-aware) is the template; the JS side already proves the view pattern at scale. Nothing in Phases 1–5 stores anything the flat buffer can't replace behind the same accessors (`chars`/`len` reads become view reads).

## 9. Sequencing and risk

Order: **P1 → P2 → P3 → P4** as one PR (the semantics flip must land atomically with its goldens — a green baseline mid-way is impossible by design), then **P5** as a follow-up PR against a green node baseline (K17 protect-what's-green).

| Risk | Mitigation |
|---|---|
| Hidden `strlen`-family consumer corrupts on NUL bytes | P2.5 audit sweep + embedded-NUL test in every phase's suite |
| Golden churn masks a real regression | goldens updated by hand from *predicted* values (this doc §6), never by blind `update-baseline` |
| `is_ascii` misuse downstream | set from decoded bytes; grep `is_ascii` consumers for binary reachability during P2.5 |
| Mark ingress/egress asymmetry (JSON base64) surprises | documented in formatter headers + doc updates (P3.4) |
| JS bridge detach/dangle | copies only (B8); zero-copy is PL5-gated |

**Effort:** P1–P4 ≈ one focused session (decoder ~80 lines + ~10 touchpoints + goldens); P5 ≈ half a session (two helpers + two constructor hooks + tests).

## 10. Follow-up action items (implemented 2026-07-15)

The pre-batch behavior was verified before implementation: `b[0]` returned
`null` unconditionally and `b1 ++ b2` returned the stringified literal texts.
The user-decided semantics in `Lambda_Type_Binary.md` §4.2–§4.4 are now
implemented through the shared runtime dispatch paths:

- **A1 — complete:** `item_at` returns `u8_to_item` for an in-range binary
  byte and `null` out of bounds. AST loop/index typing records `u8`, and all C
  and MIR index paths continue to converge on the shared accessor.
- **A2 — complete:** `binary ++ binary` calls the length-delimited
  `heap_binary_concat` allocator and remains binary even with embedded NULs.
  Mixed binary/string concatenation still follows B5 text conversion.
- **Companions — complete:** range indexing uses `fn_slice` to make an owned
  binary copy; `fn_in` compares each u8 byte with Lambda numeric equality; and
  binary iteration uses the unified iterator while preserving u8 values.

**Verification:** `test/lambda/binary_elements.ls` covers indexing, u8 identity,
OOB nulls, slices, membership, iteration, binary/text concatenation,
associativity, and embedded NULs. `make test-lambda-baseline` passes 3334/3334
(2105 input + 1229 runtime, including 491/491 Lambda GTests).
