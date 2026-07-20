# Lambda Sized Integer Types — Design Record

- **Status:** design SETTLED and implemented. The Go-like fixed-width model,
  coercion boundaries, bitwise model, and full-unsigned `u64` landed
  2026-06-30; callable conversions (`i32(...)`, `u8(...)`, …) landed later and
  are verified working 2026-07-16. §5 (runtime representation of `int64` /
  `uint64`) added 2026-07-16. Open items in §7 — item 1 **decided 2026-07-20
  (D1-B, §1 decision 8), implementation pending**
  (`vibe/Lambda_Impl_Sized_Int.md`). §5.3 heap backing **reaffirmed
  2026-07-20** post-Stack-API.
- **Scope:** Lambda scalar sized integer annotations, literals, and builtins
  (`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`), plus the runtime
  representation of the `int` / `int64` / `uint64` scalar family.
- **Related:** `vibe/Lambda_Type_Int.md` (compact-width promotion table),
  `vibe/Lambda_Semantics_Number_Model.md` (number model v2 — ranks,
  embeddings, egress), `vibe/Lambda_Type_Double_Boxing.md` (LANDED float
  self-tagging — the sibling representation decision),
  `vibe/Lambda_Issue_Scalar_Lane.md` (return-lane consequences of
  pointer-backed scalars), `doc/dev/lambda/LR_04_Numbers_Decimal_DateTime.md`
  (arithmetic ladders).
- This file was previously `Lambda_Type_Int2.md`, an audit document. The
  audit's findings are all fixed; this revision keeps only the decided design.
  Consult git history for the original issue list and evidence matrix.

---

## 1. Design summary (ADR)

**Lambda's sized integers use Go-like fixed-width arithmetic, not JS `Number`
semantics, C/C++ undefined signed overflow, or Java signed-only promotion.**

Decisions:

1. **Storage conversion wraps/truncates.** Literal packing (`128i8` → `-128`,
   `4294967296u32` → `0`) and compact typed-array stores coerce by C cast,
   matching JS typed-array storage conversion.
2. **Arithmetic preserves width and wraps deterministically.** Same-width
   compact arithmetic produces the same width (`i8 + i8 → i8`, wrapping in
   8 bits). Mixed compact widths promote through the signed/unsigned table in
   `vibe/Lambda_Type_Int.md`. `div` and `%` stay integral in the promoted
   integer width.
3. **Every annotation boundary is a coercion point.** Scalar `let`/`var`
   initialization, procedural reassignment, function parameters, and function
   returns all convert unsuffixed numeric values through the same fixed-width
   conversion used by literals and typed-array stores.
4. **`u64` is real unsigned 64-bit.** Literals above `INT64_MAX` parse with
   unsigned conversion; same-width `u64` arithmetic wraps modulo 2⁶⁴
   (`18446744073709551615u64 + 1u64` → `0`).
5. **Callable conversions exist and are two-mode.** `i8(x)`, `u8(x)`, …,
   `u64(x)` are callable. A *constant* argument that overflows the target
   width is a compile-time error (`error[E108]: constant conversion to i32
   overflows`); a *runtime* value converts with storage semantics, i.e. wraps
   (`u8(v)` where `v = 300` → `44u8`).
6. **Go is the primary reference model** because it preserves the
   participating integer type, defines unsigned modulo arithmetic, and keys
   right-shift behavior on operand signedness. Lambda deliberately differs
   from Go in one respect: mixed compact widths are allowed through the fixed
   promotion table instead of requiring explicit conversion.
7. **Plain `int` interop:** untyped `int` operands keep legacy behavior among
   themselves; when one operand is compact and the other is a plain `int`,
   the plain value converts into the compact result width (bitwise builtins;
   ordinary arithmetic with `u64` is decision 8).
8. **`u64` in ordinary (untyped-mixed) arithmetic is value-preserving**
   (D1-B, decided 2026-07-20; **implementation pending** —
   `vibe/Lambda_Impl_Sized_Int.md`). `u64 ≤ INT64_MAX` folds to `int64`
   exactly; above that it promotes to **decimal** (the unlimited
   BigInt-family rank) — consistent with rank promotion
   `int → int64 → decimal` and with the comparison paths, which already
   uphold `u64` value semantics. Consequence: untyped arithmetic exits the
   fixed-width domain — `18446744073709551615u64 * 1` yields a decimal, not
   a `u64`; mixed float follows the ladder's decimal/float rule.

Why not the alternatives:

- **JS `Number` semantics** (double rounding for large products) would make
  sized annotations decorative rather than semantic, and diverge from every
  systems-language reader expectation of `i32 * i32`.
- **C/C++ signed overflow** is undefined behavior — unacceptable for a
  deterministic scripting semantics.
- **Java promotion** lacks primitive unsigned arithmetic and force-promotes
  `byte`/`short` to `int`, erasing the annotated width.

Secondary references retained for future work: **C#** checked/unchecked
blocks if an explicit overflow-checking mode is ever added; **Rust**
`checked_* / wrapping_* / saturating_* / overflowing_*` as the naming model
for optional overflow-behavior builtins; **Java** for deterministic
two's-complement wrap precedent.

---

## 2. Semantics

### 2.1 Literals and storage

Sized literals use a width suffix and pack by cast: `127i8`, `255u8`,
`2147483648i32` → `-2147483648`, `18446744073709551615u64` (full unsigned
parse). Compact typed arrays (`let a: u8[] = [255, 256, -1]` → `[255, 0,
255]`) coerce on store and read back their element type (`type(a[0])` →
`u8`).

### 2.2 Coercion boundaries

All of the following convert an unsuffixed numeric value into the annotated
compact type via the shared fixed-width conversion helpers:

```lambda
let a: i32 = 49734321        // stores as i32
var b: u8 = 300              // procedural var — stores 44u8; reassignment coerces too
fn f(a: i32) => type(a)      // f(1) sees i32
fn g() i32 { 2147483648i32 } // return coerces/preserves i32
```

### 2.3 Arithmetic

```lambda
127i8 + 1i8                 // -128i8   (wraps in 8 bits)
255u8 + 1u8                 // 0u8
2147483647i32 + 1i32        // -2147483648i32
49734321i32 * 1103515245i32 // wraps in 32 bits (Go/Math.imul-class result)
18446744073709551615u64 + 1u64  // 0u64
```

Mixed widths promote per the table in `vibe/Lambda_Type_Int.md` (e.g.
`i32 × u32 → i64`). Integer-only operators (`div`, `%`) produce integral
results in the promoted integer width.

### 2.4 Callable conversions

```lambda
i32(1000)        // 1000i32
i32(2147483648)  // compile-time error[E108]: constant conversion overflows
let v = 300
u8(v)            // 44u8 — runtime values wrap (storage conversion)
```

---

## 3. Bitwise design

Bitwise builtins use the same explicit typed integer model as arithmetic.
The left operand's signedness selects shift behavior; fixed-width results
truncate to their width.

| Builtin | Compact result type | Semantics |
| --- | --- | --- |
| `band(a, b)` | promoted compact integer type | bitwise AND in the promoted width |
| `bor(a, b)` | promoted compact integer type | bitwise OR in the promoted width |
| `bxor(a, b)` | promoted compact integer type | bitwise XOR in the promoted width |
| `bnot(a)` | operand type | invert only the operand width |
| `shl(a, n)` | left operand type | shift left and truncate to the left operand width |
| `shr(a, n)` on `i8/i16/i32/i64` | left operand type | arithmetic right shift, sign-extending |
| `shr(a, n)` on `u8/u16/u32/u64` | left operand type | logical right shift, zero-filling |

```lambda
bnot(0u32)                 // 4294967295u32
bnot(0i8)                  // -1i8
shl(1i8, 7)                // -128i8
shr(-1i32, 1)              // -1i32
shr(4294967295u32, 1)      // 2147483647u32
shr(18446744073709551615u64, 1)  // 9223372036854775807u64
```

Mixed compact operands use the arithmetic promotion table:

```lambda
bor(256u32, 1u8)           // 257u32
band(-1i32, 4294967295u32) // 4294967295i64
```

Plain `int` operands keep legacy bitwise behavior; a plain `int` mixed with a
compact operand converts into the compact result width. There is no `>>>`
spelling: JS-style unsigned shift is expressed by shifting an unsigned-typed
value (`shr(x_u32, n)`); see §7.

---

## 4. NUM_SIZED packing

`i8/i16/i32/u8/u16/u32/f16/f32` scalars are **immediates**: the Item packs a
subtype byte plus the low 32 raw bits under the `LMD_TYPE_NUM_SIZED` tag
(`lambda/lambda.h`, `NUM_SIZED_PACK`). They are never heap- or
stack-allocated, never GC-visible, and never interact with the scalar return
lane. `normalize_sized()` (`lambda/lambda-eval-num.cpp`) promotes them to
`INT64` (or `FLOAT` for `f16`/`f32`) before the *ordinary* arithmetic ladders
run; the *sized* ladders operate on them directly, preserving width and
signedness.

`i64` and `u64` do not fit the 32-bit NUM_SIZED payload and have their own
representations — the subject of §5.

---

## 5. Runtime representation: `int`, `int64`, `uint64`

The scalar integer family uses three distinct representation strategies,
each an explicit decision. (Float's inline/residue split is the sibling
decision, settled in `Lambda_Type_Double_Boxing.md`.)

| Rank | Representation | Payload location | GC / lifetime | Scalar return lane |
|---|---|---|---|---|
| compact `int` | tagged immediate, value clamped to ±(2⁵³−1) | in the Item | none — immediate | never |
| `int64`, \|v\| ≤ 2⁵³−1 | tagged immediate with `ITEM_INT64_INLINE_MARK` | in the Item | none — immediate | never (mark test only) |
| `int64`, wider | tagged pointer | number side stack (`push_l`) | frame watermark | **yes** — re-home on return (donation) |
| `uint64` | tagged pointer, always | **GC heap** (`heap_calloc`) | GC-managed | never (heap survives frames) |

### 5.1 Compact `int`: the 53-bit safe-integer band (ADR)

The Item payload for compact `int` is 56 bits wide, but the range check
deliberately clamps to `INT56_MAX = 2⁵³−1` — the IEEE float64 safe-integer
band (`lambda/lambda.h`, comment at the `INT56_MAX` definition). **Every
compact int is exactly representable as a double**, so `int → float`
promotion is lossless and the number model's "int embeds into float/decimal"
rule holds exactly. Plain-`int` literals outside the band are a compile-time
error (`error[E108]: integer literal is outside compact int range`), and
plain-`int` arithmetic that leaves the band **crosses into `float`**
(`9007199254740991 + 1` → `9007199254740992.0`, type `float` — verified
2026-07-16; `pack_compact_int_or_float`, `lambda/lambda-eval-num.cpp`).
Plain `int` never wraps — wrapping belongs to the sized family above; the
`int64` rank is entered explicitly (suffix, annotation, or cast), not by
overflow.

### 5.2 `int64`: two-tier, inline within the safe band (ADR)

`int64` is a genuine full-64-bit rank with two representations:

- **Inline** when `|v| ≤ 2⁵³−1`: tagged immediate with
  `ITEM_INT64_INLINE_MARK` (bit 55) and the value in the low bits
  (`lambda_int64_fits_inline` / `lambda_inline_int64_to_item_bits`,
  `lambda/lambda.h`). The payload field could physically hold 55 bits, but
  the fits-check clamps to the same safe band as compact `int` for
  cross-rank consistency.
- **Pointer-backed** beyond: boxed onto the **number side stack** via
  `push_l`, lifetime managed by frame watermarks. This is the only integer
  representation that participates in the scalar return lane
  (`Lambda_Issue_Scalar_Lane.md` §10 matrix row 9 — re-homed by frame-base
  slot donation on return).

**Why two forms instead of always-pointer-backed:** the safe-band population
(loop counters, sizes, most arithmetic results) dominates real int64 code.
Inline makes those allocation-free (no number-stack bump per value),
dereference-free (shift, not load), and lifetime-free (immediates need no
rooting, re-homing, donation, or suspension handling). Always-boxing would
push the entire int64 population through the re-homing machinery that exists
for the rare wide tail, and reintroduce pointer-identity equality hazards
(equal values with distinct pointers). Structurally, dual representation adds
no new mechanism: float already has a permanent inline/residue split, so
every classifier must handle "immediate or number-stack pointer" regardless.
The strict rule *fits → inline, always* keeps the encoding canonical: each
value has exactly one bit pattern, so raw-bit equality works in the band.

**Why a separate inline-int64 tag instead of collapsing to compact `INT`:**
`int` and `int64` are distinct ranks in the number model — `type()` must
report `int64`, and the promotion lattice keys on rank. The inline mark
preserves rank without paying allocation for it.

### 5.3 `uint64`: single form, GC-heap, carrier tag (ADR)

`uint64` has **one representation**: a tagged pointer to a GC-heap-allocated
8-byte payload (`push_u64` → `heap_calloc(sizeof(uint64_t),
LMD_TYPE_UINT64)`, `lambda/lambda-eval-num.cpp`). No inline form, and —
deliberately — **not the number side stack**: the payload is GC-managed, so
a returned `u64` Item survives frame teardown and never needs the scalar
return lane (it is a `NONE` row in the lane matrix, like decimal/BigInt).

`uint64` is **not a first-class arithmetic rank**. It is a carrier tag for
the sized-numerics family (`u64` scalars, `ELEM_UINT64` typed-array
elements, FFI/interop). `normalize_sized()` folds it to `INT64` before the
ordinary arithmetic ladders; only the sized-integer ladders preserve
unsignedness (`SizedIntegerValue.is_unsigned`). The two-form optimization is
unjustified here: the population is cold and small, so an inline band would
add a classifier branch for essentially zero saved allocations — the same
reasoning that keeps decimal single-form.

**Heap backing reaffirmed 2026-07-20, post-Stack-API.** The paragraphs above
cited the donation-era return lane; that machinery is gone
(`Lambda_Design_Stack_API.md` — caller-donated canonical scalar homes), and
the new uniform copy-to-home protocol tolerates any payload provenance, so
moving `u64` to the number side stack is now mechanically *possible*. It
stays rejected. Heap backing is what keeps `UINT64` out of every
frame-lifetime mechanism — the scalar-home lane set, wrapper heap-rehoming,
import adoption, watermark brackets, GC-audit metadata, and C2MIR handling —
and keeps the hot generic-return classifier narrow (non-inline `INT64`,
out-of-band `FLOAT`, `DTIME`); the Stack API excludes `UINT64` from scalar
homes explicitly, on exactly these grounds. The escaping population stays
cold: mixed arithmetic exits the u64 domain (§1 decision 8), and JS never
carries u64 (BigInt ⇔ decimal). Note payload provenance is already mixed —
constant `u64` literals point into AST `TypeUint64` nodes
(`transpile-mir.cpp`), not the heap. If wide-u64 loops (PRNG/hash ports)
ever matter, the lever is representation-demand *native* u64 inside
generated bodies — boxing only at escape, per the Stack API's own
philosophy — with container storage under the OI-9 shaped-slots design
("all scalar kinds"); number-stack boxing per operation is the wrong tool
either way.

### 5.4 Cross-rank egress

Per number model v2: LambdaJS never sees these representations from pure JS
(JS `number` is uniformly float; JS BigInt is decimal). At the polyglot
boundary, `int64` egresses to JS as BigInt, type-directed, always.

---

## 6. Implementation map

| Concern | Location |
| --- | --- |
| NUM_SIZED packing, `INT56_MAX`, inline-int64 mark/encode/decode, `u2it` | `lambda/lambda.h` |
| Sized literal parsing (unsigned parse for `u64`) | `lambda/build_ast.cpp` |
| Callable conversions, constant-overflow diagnostic E108 | `lambda/build_ast.cpp` (sized type names), `lambda/transpile-mir.cpp` |
| `normalize_sized`, sized arithmetic/bitwise ladders, `push_u64` | `lambda/lambda-eval-num.cpp` |
| Scalar binding/assignment/param/return coercion; typed bitwise lowering | `lambda/transpile-mir.cpp` |
| Compact typed-array element storage coercion | `lambda/lambda-data-runtime.cpp` |
| `int64` number-stack boxing and the return lane | `lambda/lambda-mem.cpp`, `vibe/Lambda_Issue_Scalar_Lane.md` |

## 7. Open items

1. **`u64` above `INT64_MAX` in ordinary (untyped-mixed) arithmetic** —
   **DECIDED 2026-07-20 (D1-B, §1 decision 8): value-preserving promotion;
   implementation pending** (`vibe/Lambda_Impl_Sized_Int.md`). Same-width
   `u64` ops are correct, but `normalize_sized()` folds `UINT64` into the
   ordinary ladders by signed reinterpretation
   (`push_l((int64_t)get_uint64())`). Verified 2026-07-16, re-verified
   2026-07-20: `18446744073709551615u64 * 1` → `-1`; `… + 0.5` → `-0.5`.
   **Worse (found 2026-07-20): `u64` exactly `INT64_MAX` segfaults in any
   untyped-mixed arithmetic** — `push_l(INT64_MAX)` returns `ItemError`
   (`INT64_ERROR == INT64_MAX` sentinel, `lambda/lambda.h`) and
   `normalize_sized()` doesn't check, so the ladder consumes a poisoned
   operand. The fix folds via `box_int64_value` (full-domain) below the
   boundary and promotes to decimal above. (Bitwise mixing is unaffected —
   plain operands convert into the compact width.)
2. **JS-style `>>>` / `ushr` spelling.** Not implemented; the core behavior
   exists as `shr` on unsigned-typed operands. Optional convenience alias —
   add only if JS-porting demand materializes.
3. ~~`doc/Lambda_Reference.md` staleness check~~ — **DONE 2026-07-16**: both
   `Lambda_Reference.md` and `Lambda_Data.md` described `int` as "56-bit
   signed integer"; corrected to the float64 safe range ±(2⁵³−1). All other
   sized-numeric wording (type tables, wrap examples, `i64`/`f64` alias
   rows) verified accurate against the current build.

Implementation plan for items 1–2: `vibe/Lambda_Impl_Sized_Int.md`.

## 8. Test coverage

- `test/lambda/sized_numeric_annotation_edges.ls` — unsuffixed annotation
  coercion, parameter/return coercion, fixed-width `+`/`*`/`div`/`%`,
  signed/unsigned promotion, unary wrap, compact bitwise operands, `u64`
  boundary literal, compact array storage.
- `test/lambda/sized_numeric_bitwise_go.ls` — Go-like `band`/`bor`/`bxor`
  promotion, width-preserving `bnot`, arithmetic vs logical `shr`,
  shift-width edges, mixed compact/default operands, `u64` bitwise.
- `test/lambda/proc/proc_sized_numeric_annotations.ls` — procedural `var`
  initialization and reassignment coercion for `i8`, `u8`, `i32`, `u32`,
  `u64`.
- `test/lambda/sized_numeric_type_annot.ls`,
  `test/lambda/sized_numeric_mixed_expr.ls`,
  `test/lambda/compact_typed_arrays.ls` — pre-existing coverage.
- To add with §7 items: callable-conversion edge coverage (constant E108 vs
  runtime wrap) and, if adopted, `ushr` alias coverage.
