# Lambda Sized Integer Types — Design Record

- **Status:** representation and sized-lane semantics are settled and
  implemented. The 2026-07-20 Stack API realignment
  removes the old compact-inline `int64` and transient-heap `uint64` split:
  both full-width integer types use number homes while transient,
  caller-donated homes for generated Item returns, and destination-owned
  scalar storage when retained. `ushr` is implemented. The later
  type-directed sized/non-sized arithmetic decision supersedes the historical
  value-based `u64` fold and is implemented under `Lambda_Impl_Numbers.md`.
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
7. **Plain `int` interop:** ordinary arithmetic between a sized integer and a
   non-sized operand exits the fixed-width domain. `i8`…`u32` enter the
   non-sized tower as `int`; `i64/u64` enter as `integer`. Bitwise builtins
   retain their explicitly documented width-domain behavior.
8. **Mixed promotion is type-directed, never value-directed.** Every `u64`,
   including values at or below `INT64_MAX`, follows the same rule. With a
   non-sized operand it enters `integer`; with another sized integer it stays
   in the machine lane selected by decision 2. Thus `u64 + int → integer`,
   `u64 + float → decimal`, and `i64 + u64 → u64`. The earlier D1-B
   threshold fold was an implementation repair, not the final semantics, and
   is superseded.

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
| `ushr(a, n)` | unsigned counterpart of signed left operand; unchanged unsigned operand; `u32` for plain `int` | logical right shift after reinterpreting the left operand's width |

```lambda
bnot(0u32)                 // 4294967295u32
bnot(0i8)                  // -1i8
shl(1i8, 7)                // -128i8
shr(-1i32, 1)              // -1i32
shr(4294967295u32, 1)      // 2147483647u32
shr(18446744073709551615u64, 1)  // 9223372036854775807u64
ushr(-1i32, 1)             // 2147483647u32
ushr(-1i64, 1)             // 9223372036854775807u64
```

Mixed compact operands use the arithmetic promotion table:

```lambda
bor(256u32, 1u8)           // 257u32
band(-1i32, 4294967295u32) // 4294967295i64
```

Plain `int` operands keep legacy bitwise behavior; a plain `int` mixed with a
compact operand converts into the compact result width. `ushr` is the explicit
JS-style unsigned shift builtin; it has no `>>>` grammar token. A negative
shift count is an error, and a count at least the operand width returns zero.

---

## 4. NUM_SIZED packing

`i8/i16/i32/u8/u16/u32/f16/f32` scalars are **immediates**: the Item packs a
subtype byte plus the low 32 raw bits under the `LMD_TYPE_NUM_SIZED` tag
(`lambda/lambda.h`, `NUM_SIZED_PACK`). They are never heap- or
stack-allocated, never GC-visible, and never interact with the scalar return
lane. The shared numeric classifier keeps them in a selected sized lane for
sized integral operations. At a sized/non-sized boundary, compact integer
lanes convert to `int` and `f16`/`f32` convert to `float` before the selected
semantic-domain operation.

`i64` and `u64` do not fit the 32-bit NUM_SIZED payload and have their own
representations — the subject of §5. Ordinary arithmetic maps a compact sized
integer mixed with a non-sized value to `int`, never to `int64`; this is now
implemented consistently in the evaluator, AST, MIR, vectors, and reductions.

---

## 5. Runtime representation: `int`, `int64`, `uint64`

The scalar integer family deliberately separates compact immediates from the
two full-width ranks. Float's inline/residue split is a sibling decision in
`Lambda_Type_Double_Boxing.md`.

| Rank | Representation | Payload location | GC / lifetime | Scalar return lane |
|---|---|---|---|---|
| compact `int` | tagged immediate, value clamped to ±(2⁵³−1) | in the Item | none — immediate | never |
| `int64` | tagged pointer, no inline form | current activation number home, caller home, or destination-owned scalar storage | owner determines lifetime; ownerless Item-only escape uses interim GC cell | **yes** for generated Item returns |
| `uint64` | tagged pointer, no inline form | current activation number home, caller home, or destination-owned scalar storage | owner determines lifetime; ownerless Item-only escape uses interim GC cell | **yes** for generated Item returns |

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

### 5.2 `int64`: uniform full-width homes (ADR)

`int64` has no compact-inline form. A local literal, conversion, evaluator
result, or typed-array read occupies one raw word in the current activation's
number home. A generated Item return copies that word into the caller-donated
home before the callee restores its watermark. Arrays, maps, environments, and
other retaining destinations copy the payload into storage they own.

This removes the former magnitude-dependent branch: zero, values within the
float64 safe-integer band, and full-width values all follow the same lifetime
protocol. `int` remains a separate compact rank, so `type()` and the promotion
lattice retain their existing distinction without an inline-int64 marker.

### 5.3 `uint64`: the same full-width ownership model (ADR)

`uint64` uses the same physical one-word homes as `int64`, with its unsigned
tag and analysis retaining signedness. There is no `push_u64` heap boxer and
no transient `uint64` GC allocation. Same-width operations retain modulo-2⁶⁴
semantics. Ordinary mixed arithmetic maps every `uint64` value to the
non-sized `integer` domain before operating; the current value never selects
an `int64` versus decimal path.

There is no standalone numeric GC representation. A bare-Item boundary carries
a caller-supplied scalar home, and persistent destinations use an owned payload
word. GC allocation rejects `DOUBLE`/`INT64`/`UINT64` scalar type tags.

### 5.4 Cross-rank and LambdaJS egress

Pure LambdaJS `number` remains float and JS `BigInt` is normally decimal, but
typed BigInt-array paths can carry an unsigned 64-bit value internally. Those
values use the same current-home/destination-owner rules rather than a JS-only
heap representation. At the polyglot boundary, `int64` egresses to JS as
BigInt, type-directed.

---

## 6. Implementation map

| Concern | Location |
| --- | --- |
| NUM_SIZED packing, `INT56_MAX`, full-width `i64`/`u64` Item tagging | `lambda/lambda.h` |
| Sized literal parsing (unsigned parse for `u64`) | `lambda/build_ast.cpp` |
| Callable conversions, constant-overflow diagnostic E108 | `lambda/build_ast.cpp` (sized type names), `lambda/transpile-mir.cpp` |
| Shared type-directed entry mapping; sized arithmetic/bitwise/`ushr` | `lambda/lambda-number.hpp`, `lambda/lambda-number-types.hpp`, `lambda/lambda-number-runtime.hpp`, `lambda/lambda-eval-num.cpp` |
| Shared scalar-home return classification and typed bitwise lowering | `lambda/transpile-mir.cpp`, `lambda/mir_emitter_shared.hpp` |
| Destination scalar storage and typed-array materialization | `lambda/lambda-data-runtime.cpp`, `lambda/lambda-data.cpp`, `lambda/vmap.cpp` |
| Full-domain number-home boxing, caller-home adoption, ownerless fallback counter | `lambda/lambda-mem.cpp` |

## 7. Resolved items

1. **Historical high-`u64` corruption repair — DONE 2026-07-20, semantic
   policy subsequently superseded.** The completed threshold fold fixed signed
   reinterpretation and the `INT64_MAX` sentinel crash. The latest number model
   removes that magnitude branch entirely: all `u64` mixed with non-sized
   values enter `integer`. The replacement is implemented and verified in
   `Lambda_Impl_Numbers.md`.
2. **JS-style `ushr` — DONE 2026-07-20.** The builtin performs a logical
   width-preserving shift, changes signed operands to their unsigned
   counterparts, and uses `u32` for plain `int`; there is still no `>>>`
   grammar token.
3. ~~`doc/Lambda_Reference.md` staleness check~~ — **DONE 2026-07-16**: both
   `Lambda_Reference.md` and `Lambda_Data.md` described `int` as "56-bit
   signed integer"; corrected to the float64 safe range ±(2⁵³−1). All other
   sized-numeric wording (type tables, wrap examples, `i64`/`f64` alias
   rows) verified accurate against the current build.

Historical implementation plan for items 1–2: `vibe/Lambda_Impl_Sized_Int.md`.
Current arithmetic realignment plan: `vibe/Lambda_Impl_Numbers.md`.

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
- `test/lambda/sized_numeric_u64_mixed.ls` — magnitude-independent `integer`
  promotion plus the historical sentinel, same-width wrap, negation,
  comparison, and Item-tag regressions.
- `test/lambda/sized_numeric_ushr.ls` — signed/unsigned and plain-`int`
  logical shift results, zero and width-edge counts, plus `u64` maximum.
