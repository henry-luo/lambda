# Explicit Integer Type Audit

Date: 2026-06-30

Scope: Lambda scalar integer annotations and sized integer literals (`i8`, `i16`, `i32`, `u8`, `u16`, `u32`, `i64`, `u64`), with emphasis on Go-like fixed-width numeric behavior: storage conversion, deterministic wraparound, promotion, signed/unsigned handling, and explicit conversions.

## Post-Fix Status

Status after the implementation pass on 2026-06-30:

- Fixed scalar `let`/`var` coercion for explicit compact integer annotations. Unsuffixed values now store through the same fixed-width conversion boundary as compact literals/typed-array elements.
- Fixed procedural assignment into compact integer variables. Reassigning `i8`, `u8`, `i32`, `u32`, and `u64` variables now coerces instead of corrupting the MIR register type.
- Fixed function parameter and function return coercion for compact integer annotations.
- Implemented ordinary compact scalar arithmetic with a Go-like fixed-width model, not JS `Number`, C/C++ signed-overflow, or Java-only signed promotion. Same-width compact arithmetic preserves width and wraps deterministically; mixed compact widths use the promotion table below.
- Preserved integer semantics for `div` and `%` so integer-only operators still produce integral results in the promoted integer width.
- Fixed MIR Direct bitwise lowering for compact operands by routing boxed compact values through the runtime bitwise argument conversion path.
- Fixed `u64` literal parsing above `INT64_MAX` by using unsigned parsing for `u64` suffixes.
- Added regression coverage in `test/lambda/sized_numeric_annotation_edges.ls` and `test/lambda/proc/proc_sized_numeric_annotations.ls`, covering `i8`, `i16`, `i32`, `u8`, `u16`, `u32`, and `u64`.

Verified with:

```bash
make build
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/sized_numeric_annotation_edges:AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/proc_proc_sized_numeric_annotations'
make test-lambda-baseline
```

Remaining follow-up:

- Scalar constructor/cast syntax such as `i32(2147483648)` is still not implemented.
- A JS-style unsigned right shift operator/function is still not implemented.

## Historical Audit Snapshot

This section preserves the original audit findings before the fix pass and before the Go-like arithmetic decision. The high-severity runtime crashes, parameter coercion gaps, bitwise compact operand bug, `u64` parse bug, and fixed-width arithmetic gap have now been addressed by the implementation above. The remaining product work is explicit constructor/cast syntax and unsigned right shift support.

- Compact integer literals and compact typed arrays perform fixed-width storage coercion. Examples: `128i8` prints `-128`, `4294967296u32` prints `0`, and `let a: u8[] = [255, 256, -1]` prints `[255, 0, 255]`. This matches JS typed-array storage conversion.
- Original finding: scalar arithmetic on compact integer values did not wrap to the annotated width. It promoted through the generic numeric runtime and returned `int64` for integer arithmetic. Example: `127i8 + 1i8` returned `128` with type `int64`, not `-128`.
- Original finding: scalar `i32` arithmetic also did not match JS `Number` for large products, because Lambda computed exact `int64` while JS `Number` rounded in double precision. Example: `49734321i32 * 1103515245i32` returned `54882581423223645`; Node reports `54882581423223650` for `49734321 * 1103515245`.
- Scalar sized annotations are unsafe when initialized from unsuffixed integer values. `let a: i32 = 49734321` and a `pn`-local `var a: i32 = 49734321` both segfaulted.
- Function parameter annotations are not reliable coercion points. `fn f(a: i32) => type(a); f(1)` returns `int`, not `i32`.
- Bitwise builtins are broken for sized operands in MIR Direct. `band(4294967295u32, 255u32)` returns `0`; JS `4294967295 & 255` returns `255`.
- `u64` is not real full unsigned 64-bit support today. Values above `INT64_MAX` parse as `9223372036854775807`, and `u64` arithmetic crashes.
- Original finding: public docs claimed sized integer arithmetic wraps silently, but the implementation only wrapped/truncated at literal packing and compact array storage boundaries.

## Evidence

Focused probes were added under `./temp/int_audit/` and run with the current `./lambda.exe`:

```bash
./lambda.exe temp/int_audit/literals_and_types.ls
./lambda.exe temp/int_audit/suffixed_arithmetic.ls
./lambda.exe temp/int_audit/unsigned_and_division.ls
./lambda.exe temp/int_audit/annotation_suffixed_ok.ls
./lambda.exe temp/int_audit/annotation_unsuffixed_crash.ls
./lambda.exe temp/int_audit/function_annotations.ls
./lambda.exe temp/int_audit/function_param_type_unsuffixed.ls
./lambda.exe temp/int_audit/conversions.ls
./lambda.exe temp/int_audit/arrays.ls
./lambda.exe temp/int_audit/bitwise.ls
./lambda.exe temp/int_audit/u64_literals_only.ls
./lambda.exe temp/int_audit/u64_arith_only.ls
./lambda.exe run temp/int_audit/pn_var_annotation_unsuffixed.ls
```

Baseline existing scripts also still run:

```bash
./lambda.exe test/lambda/sized_numeric_type_annot.ls
./lambda.exe test/lambda/sized_numeric_mixed_expr.ls
./lambda.exe test/lambda/compact_typed_arrays.ls
```

Node comparison command:

```bash
node -e "const a=49734321,b=1103515245; console.log(a*b); console.log(Math.imul(a,b)); console.log(Math.imul(a,b)>>>0); console.log(new Uint8Array([255,256,-1]).join(',')); console.log(4294967295 & 255);"
```

## Implementation Map

- `lambda/lambda.h`: `NUM_SIZED` stores only a subtype byte and low 32 raw bits. `i8/i16/i32/u8/u16/u32` packing uses C casts, so literal and storage coercion naturally wraps/truncates.
- `lambda/build_ast.cpp`: sized integer literals parse via `strtoll`, then pack by cast for compact widths. `u64` also uses `strtoll`, so unsigned values above `INT64_MAX` are not preserved.
- `lambda/lambda-eval-num.cpp`: `normalize_sized()` erases compact integer types before arithmetic by converting them to `INT64`; `u64` is cast to signed `int64_t`. Arithmetic ladders then run as `INT64`, `FLOAT`, or `DECIMAL`.
- `lambda/transpile-mir.cpp`: native arithmetic is only for `int`/`float`. `NUM_SIZED` arithmetic falls to boxed runtime calls (`fn_add`, `fn_mul`, etc.).
- `lambda/transpile-mir.cpp`: scalar `let` lowering honors a declared `LMD_TYPE_NUM_SIZED` storage type, but only converts `int <-> float`, not `int -> NUM_SIZED`.
- `lambda/transpile-mir.cpp`: bitwise builtins lower inline and unbox non-`int`/`int64` arguments as `int`, which loses packed `NUM_SIZED` values.
- `lambda/lambda-data-runtime.cpp`: compact typed arrays use C casts on store, so array storage conversion matches JS typed-array coercion for the tested widths.

## Observed Behavior Matrix

| Area | Probe | Observed |
| --- | --- | --- |
| `i8` literal packing | `128i8` | `-128` |
| `u8` literal packing | `256u8` | `0` |
| `i32` literal packing | `2147483648i32` | `-2147483648` |
| `u32` literal packing | `4294967296u32` | `0` |
| `u64` literal parse | `18446744073709551615u64` | `9223372036854775807` |
| scalar compact arithmetic | `127i8 + 1i8` | `128`, type `int64` |
| scalar compact arithmetic | `255u8 + 1u8` | `256`, type `int64` |
| scalar compact arithmetic | `2147483647i32 + 1i32` | `2147483648`, type `int64` |
| scalar compact multiply | `49734321i32 * 1103515245i32` | `54882581423223645`, type `int64` |
| JS `Number` comparison | Node `49734321 * 1103515245` | `54882581423223650` |
| JS 32-bit multiply comparison | Node `Math.imul(...)` | `-1038716067`, unsigned `3256251229` |
| compact array store | `let a: u8[] = [255, 256, -1]` | `[255, 0, 255]` |
| compact array read type | `type(a[0])` from `u8[]` | `u8` |
| compact array arithmetic | `b[0] + b[1]` from `[127i8, 1i8]` | `128`, type `int64` |
| annotated matching suffix | `let c: i32 = 2147483647i32; type(c)` | `i32` |
| annotated arithmetic | `c + 1i32` | `2147483648`, type `int64` |
| annotated unsuffixed let | `let a: i32 = 49734321` | SIGSEGV |
| annotated unsuffixed pn var | `var a: i32 = 49734321` inside `pn main` | SIGSEGV |
| annotated fn param | `fn f(a: i32) => type(a); f(1)` | `int` |
| annotated fn return | `fn f() i32 { 2147483648i32 }; type(f())` | `i32` |
| scalar sized constructors | `i32(2147483648)`, `u32(4294967295)` | runtime `cannot call non-function value`, output `error` |
| bitwise sized operands | `band(4294967295u32, 255u32)` | `0` |
| JS bitwise comparison | Node `4294967295 & 255` | `255` |
| signed shift | `shr(-1i32, 1)` | `-1` |

## Issues

### 1. Scalar sized annotation from unsuffixed integers crashes

Severity: high

Repros:

```lambda
let a: i32 = 49734321
let b: i32 = 1103515245
a * b
```

```lambda
pn main() {
    var a: i32 = 49734321
    print(a)
}
```

Observed: both crash with SIGSEGV.

Likely cause: scalar binding lowering sets `var_tid` from the declared type (`LMD_TYPE_NUM_SIZED`) but does not emit a conversion from a raw `int` value into a packed `NUM_SIZED` Item. The only scalar conversion code there is `int -> float` and `float -> int`. Later code treats the register as a packed Item.

Expected: either reject unsuffixed integer initializers for scalar sized annotations, or coerce through the same fixed-width packing used by literals/typed-array storage.

### 2. Function parameter annotations do not coerce unsuffixed numeric arguments

Severity: high

Repro:

```lambda
fn param_type(a: i32) => type(a)
param_type(1)              // int
param_type(2147483648i32)  // i32
```

Observed: the unsuffixed argument reaches the function body as `int`, not `i32`.

This means `a: i32` is not a reliable runtime coercion contract at function boundaries. It accepts normal numeric types for compatibility, but does not convert them to the declared compact type.

Expected: if `i32` annotation is meant to be explicit integer type support, a parameter annotated `i32` should either store `a` as `i32` in the body or reject non-`i32` values. JS-aligned coercion would mirror `ToInt32`/typed-array storage conversion at the boundary.

### 3. Original: scalar compact arithmetic did not wrap or preserve compact type

Severity: medium/high depending on intended semantics

Repros:

```lambda
127i8 + 1i8                 // 128, int64
255u8 + 1u8                 // 256, int64
2147483647i32 + 1i32        // 2147483648, int64
49734321i32 * 1103515245i32 // 54882581423223645, int64
```

Original finding: this contradicted `doc/Lambda_Data.md`, which said sized integer arithmetic wraps silently. The implementation wrapped only when packing/storing a compact value; arithmetic promoted compact integers to `INT64`.

Original JS-alignment analysis depended on the intended model:

- If compact scalar values model JS typed-array elements after read, then non-bitwise arithmetic should produce a JS `Number`-like result, not necessarily a compact result. At original audit time, Lambda produced exact `int64`, which diverged from JS `Number` for large products.
- If compact scalar values model fixed-width arithmetic, then `i32 * i32` should wrap or provide an explicit wrapped result. At original audit time, Lambda did not.

Decision update: compact scalar arithmetic now uses Go-like fixed-width arithmetic instead of JS `Number` promotion or native `int64` promotion. Same-width compact arithmetic preserves width and wraps in that width; mixed-width arithmetic uses the wider result type and the signed/unsigned promotion table documented in `vibe/Lambda_Type_Int.md`.

### 4. Bitwise builtins are wrong for sized operands

Severity: high

Repros:

```lambda
let u = 4294967295u32
band(u, 255u32)  // 0, expected 255 for JS-style bitwise
bor(u, 0u32)     // 0, expected -1 or 4294967295 depending display contract
bxor(u, 255u32)  // 0, expected -256 for JS signed result
shr(u, 1)        // 0, expected implementation-defined by chosen signed/unsigned contract
```

Cause: MIR Direct special-cases bitwise builtins inline. For non-`int`/`int64` arguments it calls `emit_unbox(..., LMD_TYPE_INT)`, which does not understand packed `NUM_SIZED` values. The runtime `_barg()` helper does understand `NUM_SIZED`, but this path does not use it.

Expected: bitwise builtins should explicitly define JS-style coercion:

- `band`, `bor`, `bxor`, `bnot`, `shl`, `shr` should use ToInt32-like behavior for signed operations.
- An unsigned right shift (`ushr` or equivalent) is needed for JS `>>>`.
- If `u32` operands are supported, they must not collapse to zero before the operation.

### 5. `u64` is not real unsigned 64-bit support

Severity: high

Repros:

```lambda
9223372036854775807u64      // 9223372036854775807
9223372036854775808u64      // 9223372036854775807
18446744073709551615u64     // 9223372036854775807
9223372036854775807u64 + 1u64  // SIGSEGV
```

Causes:

- Literal parsing uses `strtoll`, so values above signed 64-bit max saturate/fail into `INT64_MAX` behavior rather than parsing as `uint64_t`.
- Arithmetic normalizes `UINT64` by casting to signed `int64_t`, which cannot preserve unsigned range.

Expected: either remove/disable scalar `u64` until implemented, or parse with unsigned conversion and define unsigned arithmetic/printing precisely. For JS alignment, `u64` is not a JS `Number` integer type; it is closer to BigInt/typed-array storage and should not silently pass through signed `int64`.

### 6. Scalar constructors/casts for compact integer types are missing

Severity: medium

Repro:

```lambda
i32(2147483648)
u32(4294967295)
u8(256)
i8(128)
```

Observed: runtime errors: `fn_call1: cannot call non-function value`; output is `error`.

Expected: if explicit integer annotations are intended as JS-compatible conversion tools, there should be callable conversions or named builtins for `ToInt32`, `ToUint32`, `ToInt8`, `ToUint8`, etc. Annotation-only conversion is currently unsafe for scalar bindings and incomplete for parameters.

### 7. Documentation is stale

Severity: medium

`doc/Lambda_Data.md` states:

```lambda
127i8 + 1i8    // -128 (wraps)
255u8 + 1u8    // 0 (wraps)
```

Observed:

```lambda
127i8 + 1i8    // 128
255u8 + 1u8    // 256
```

Decision update: the implemented semantics are now Go-like fixed-width arithmetic, with storage annotations continuing to wrap/truncate at storage boundaries.

## JS Alignment Notes

JS has several different integer-like behaviors:

1. `Number` arithmetic: double precision, no integer-width wrap for `+`, `-`, `*`, `/`.
2. Bitwise operators: operands are coerced to signed 32-bit integers; result is signed 32-bit except `>>>`, which returns an unsigned 32-bit number represented as `Number`.
3. `Math.imul`: signed 32-bit multiplication with wraparound.
4. Typed arrays: assignment coerces to element storage width; reading returns a `Number`.
5. BigInt: separate arbitrary-precision integer domain; cannot mix freely with Number.

Lambda currently mixes these models:

- Compact literal/array storage behaves like JS typed-array storage.
- Compact scalar arithmetic behaves like exact signed `int64`, not JS `Number` and not fixed-width wrap.
- Bitwise compact operands are broken before JS-style coercion can happen.
- `u64` is neither JS `Number` nor BigInt-correct.

## Additional Language References

Go remains the primary model for Lambda's compact integer arithmetic because it preserves the participating integer type, defines unsigned modulo arithmetic, and requires explicit conversion between distinct integer types. Lambda differs intentionally by allowing mixed compact widths through a fixed promotion table instead of rejecting them.

Keep these secondary references for future design work:

- **C#**: useful reference if Lambda later adds explicit overflow checking modes. C# distinguishes checked and unchecked integer arithmetic, which maps well to a possible future `checked` block/operator design without changing Lambda's default fixed-width wrap semantics.
- **Rust**: useful reference for named arithmetic helpers. Rust exposes `checked_*`, `wrapping_*`, `saturating_*`, and `overflowing_*` operations, which are a good model if Lambda adds standard-library functions for non-default overflow behavior.
- **Java**: useful reference for deterministic signed two's-complement wrap, especially `long`/`int` behavior. Java is not enough as Lambda's primary model because it lacks primitive unsigned arithmetic and promotes `byte`/`short` arithmetic to `int`.

## Recommended Fix Plan

1. Done: shared scalar compact conversion helpers for `NUM_SIZED` and `UINT64` targets are used in scalar bindings, assignment, function parameters, and function returns.
2. Done: ordinary compact scalar integer arithmetic uses Go-like fixed-width arithmetic. `i8 + i8 -> i8`, `u32 + u32 -> u32`, signed and unsigned operations wrap deterministically in the result width.
3. Done: MIR Direct bitwise lowering now handles compact operands via runtime bitwise argument conversion.
4. Done: `u64` literals now parse with unsigned conversion.
5. Remaining: add callable conversions (`i8`, `u8`, `i16`, `u16`, `i32`, `u32`, maybe `to_i32`/`to_u32` to avoid type-name call ambiguity).
6. Remaining: add unsigned right shift support for JS `>>>`.
7. Done: update `doc/Lambda_Data.md` for compact scalar integer arithmetic. `doc/Lambda_Reference.md` should still be checked for any duplicate sized-numeric wording.

## Regression Tests To Add

- Done in `test/lambda/sized_numeric_annotation_edges.ls`: scalar `i32` unsuffixed annotation, parameter coercion, return coercion, fixed-width `+`/`*`/`div`/`%`, signed/unsigned promotion, unary wrap, compact bitwise operands, `u64` boundary literal, and compact array storage.
- Done in `test/lambda/proc/proc_sized_numeric_annotations.ls`: procedural `var` initialization and reassignment coercion for `i8`, `u8`, `i32`, `u32`, and `u64`.
- Remaining when implemented: constructor/cast syntax and unsigned right shift coverage.
