# Lambda Scalar Return Lane — Landed Inline Classification and Slot Donation

> **Phase 7 successor note (2026-07-20):** this landed issue record predates the
> full-width scalar realignment. Its inline-`INT64` classifications and
> `DTIME` scalar-lane cases are historical. Current storage/return rules are in
> `vibe/Lambda_Design_Stack_API.md` Phase 7: `INT64`/`UINT64` use caller-donated
> homes without an inline form, while `DTIME` uses the ordinary owner-backed
> Item ABI (dynamic GC or static Mark Input arena).

**Status:** IMPLEMENTED 2026-07-16 — core correctness gates green; one
parallel-load Node timeout and comparative release profiling remain follow-up
evidence.
Decisions: the helper's decision logic is emitted **inline as MIR
instructions** in the generated epilogue through one shared emitter (§9.3 —
C++ `inline`/LTO cannot cross the JIT import boundary, §4.3); the cold arm is
**frame-base slot donation** (§9.4), not a caller-side helper rebuild; float
self-tagging is LANDED (`Lambda_Type_Double_Boxing.md`), so the §3
inline/residue float split is the end-state encoding. The lane-usage matrix
(§10) shows Today vs the decided Final state  
**Date:** 2026-07-16  
**Scope:** Lambda MIR-Direct and LambdaJS JIT-to-JIT return paths  
**Related:** `vibe/Lambda_Design_Stack_Frame.md` (SF14),
`vibe/Lambda_Impl_Stack_Frame.md`, `vibe/Lambda_Impl_Stack_Frame_JS.md`,
`vibe/Lambda_Type_Double_Boxing.md` (LANDED inline-double self-tagging — the
source of the inline/residue float split in §3),
`doc/dev/lambda/LR_07_MIR_Transpiler_JIT.md`, and
`doc/dev/js/JS_04_MIR_Lowering.md`

---

## 0. Landed implementation checkpoint (2026-07-16)

The decided end state in §§9–10 is implemented in both transpilers:

- `MirScalarReturnMode` and `em_scalar_return_mode_for_type()` live in the
  shared `mir_emitter_shared.hpp` layer.
- LambdaJS ordinary boxed functions select a mode from
  `JsFuncCollected::return_type`; generator, async, and `js_main` remain
  `DYNAMIC`, while native-specialized functions use `NONE`.
- `em_rehome_scalar_return()` emits representation checks and frame-base
  donation directly in MIR. Its ownership guard is the complete callee extent
  `[frame_base, current_top)`, so heap-backed and ancestor-frame scalar pointers
  pass through unchanged.
- Lambda MIR-Direct boxed returns use the same `DYNAMIC` emitter and expose a
  single caller-owned Item. Callers and boxed wrappers are pass-through.
- `lambda_item_scalar_lane()` and `lambda_item_from_scalar_lane()`, their
  declarations, and their import-registry entries were deleted because no
  consumer remains.
- `Context::mir_return_lane` now carries only the native `T^E` error Item. The
  scalar transport and its ARM64 staging workaround are retired.
- `js_throw_value()` already calls `lambda_item_heap_rehome()`, so thrown wide
  scalars enter traced heap storage before generated unwind restores a number
  watermark.

Permanent JS coverage includes direct, recursive/chained, mixed, closure,
generator, async, throw, signed-zero, inline-double, and
intervening-allocation cases. Lambda's existing `proc_stack_frame` regression
covers wide int64, DateTime, closure, async, map, and native error-lane paths.

Sections 4–7 retain the pre-change implementation/cost analysis that motivated
the patch. Sections 9–10 are the authoritative landed design.

---

## 1. Executive summary

The former scalar return lane was a correctness mechanism for an `Item` whose payload
lives in a callee's raw number side-stack frame. A callee must not return that
pointer after restoring `Context::side_number_top`, because the pointer then
refers to reclaimed storage. The pre-change implementation therefore:

1. extracts the scalar's raw 64-bit value before restoring the callee watermark;
2. restores the callee's number frame; and
3. rebuilds the `Item` in the caller's number-frame extent.

That mechanism is necessary only for a small representation subset:

- pointer-backed `int64` values that do not use the inline int64 encoding;
- pointer-backed `float`/`float64` values that cannot use the inline double or
  packed `+0`/`-0` encoding; and
- pointer-backed `DateTime` values.

Most returned values do not need it: undefined, null, bool, compact int,
inline double, strings, symbols, binaries, decimal/BigInt, functions, objects,
arrays, maps, promises, errors, and other GC-heap/container values are already
self-contained or live outside the callee number extent.

Before this patch, Lambda MIR-Direct used a scalar lane only for boxed/dynamic
return ABIs, while LambdaJS treated every boxed generated entry as potentially
wide. Consequently, every boxed JS invocation made two imported runtime calls:
`lambda_item_scalar_lane()` before number-frame restore and
`lambda_item_from_scalar_lane()` after restore. Both calls are no-ops for the
common non-wide return cases. Native-specialized JS functions already avoid
both calls.

The landed resolution deletes scalar transport while preserving the lifetime
operation through mode-aware inline classification and donation:

- prove `NONE` at compile time for return types that cannot point into the
  number stack;
- use a type-specialized `FLOAT`, `INT64`, or `DTIME` path where possible;
- retain a `DYNAMIC` path for `ANY` and unresolved unions;
- inline the cheap representation test in generated MIR; and
- re-home the rare pointer-backed scalar arm by donating the callee's
  frame-base number slot to the caller (§9.4) — no imported call and no
  allocation on any return arm.

The lifetime rule is non-negotiable: raw payload capture must happen while the
callee number extent is live, and the surviving value must be caller-owned
after cleanup — either rebuilt in the caller's extent after restore, or
donated by adjusting the restore point to `frame_base + 1` (§9.4).

---

## 2. The lifetime problem the lane solves

Each execution context has a raw number side stack:

```text
caller extent | callee extent
              ^ callee saved watermark
```

`push_l`, `push_d`, and `push_k` bump `Context::side_number_top`, write a raw
64-bit payload, and return a tagged `Item` that points at that slot. A generated
function saves the number watermark on entry and restores it at its unified
epilogue.

Without a return lane, this sequence is invalid:

```text
callee: p = push_d(Number.MIN_VALUE)  // p points into callee extent
callee: return p
callee epilogue: side_number_top = saved_top
caller: use p                         // dangling/reusable slot
```

The correct sequence is:

```text
callee: p = push_d(Number.MIN_VALUE)
callee: raw = load payload(p)         // while p is still live
callee epilogue: side_number_top = saved_top
caller extent: q = push_d(bits(raw))  // new owner
return/use q
```

This rule applies only to upward flow. Arguments flow downward while the caller
frame remains live, so ordinary arguments, rest parameters, and spread values
do not need return-lane re-homing. C runtime helpers also establish no generated
number watermark of their own: a helper's `push_*` allocation already lands in
the calling JIT frame.

---

## 3. Item representations relevant to scalar lanes

The shared helpers in `lambda/lambda-mem.cpp` recognize four runtime type IDs.
The actual re-home requirement is narrower than the type ID alone.

The float split below is the **landed** self-tagging encoding
(`Lambda_Type_Double_Boxing.md`, S3 complete 2026-07-11, always on): a double
with `|d| ≥ ~1.5e-154` — including ±Inf and NaN — is stored as its raw IEEE
bits in the Item (`ITEM_DBL_MASK` hit), `±0` are packed immediates, and only
the subnormal/tiny residue boxes onto the number side stack (`flt2it` in
`lambda/lambda-mem.cpp`). This is the end state, not a transition: the residue
cannot be encoded away under the single-mask scheme.

| Runtime value | Item representation | Needs return-lane re-home? |
|---|---|---:|
| compact `int` | tagged immediate | no |
| inline `int64` | tagged immediate with `ITEM_INT64_INLINE_MARK` | no |
| pointer-backed `int64` | tagged pointer to number slot | yes |
| ordinary inline double | raw IEEE bits satisfying `ITEM_DBL_MASK` | no |
| `+0` / `-0` | packed `ITEM_FLOAT_P0` / `ITEM_FLOAT_N0` | no |
| out-of-band double residue | tagged pointer to number slot | yes |
| `float64` pointer form (legacy tag; `f64` canonicalizes to `FLOAT`) | tagged pointer to number slot | yes |
| `DateTime` | tagged pointer to number slot | yes |
| decimal / JS BigInt | GC-managed decimal object | no |
| string/symbol/binary | stable or GC-managed pointer | no |
| object/container/function | GC-managed pointer | no |
| null/undefined/bool/error | immediate | no |

The out-of-band double residue includes values whose raw IEEE bits collide with
the tagged-Item address/type space. `Number.MIN_VALUE` is the permanent JS
regression example. It is a valid JS Number but cannot be carried directly in
the normal inline-double representation.

---

## 4. Retired shared runtime helper implementation (pre-change)

### 4.1 `lambda_item_scalar_lane(Item item)`

Declared in `lambda/lambda.h`, defined in `lambda/lambda-mem.cpp`, and exported
to generated MIR through `lambda/sys_func_registry.c`.

Behavior:

- `LMD_TYPE_INT64`: decode the mathematical `int64` value and return its bits;
- `LMD_TYPE_FLOAT` / `LMD_TYPE_FLOAT64`: decode the double and bit-copy it to a
  `uint64_t`;
- `LMD_TYPE_DTIME`: decode the `DateTime` and bit-copy its 64-bit representation;
- all other types: return zero.

The helper deliberately returns raw bits rather than another `Item`; raw bits
survive number-watermark restoration without being interpreted by GC.

For inline int64 and inline double values, extraction is semantically harmless
but unnecessary: the original `Item` is already self-contained. This helper
does not distinguish “lane required” from “lane value happens to be zero.”

### 4.2 `lambda_item_from_scalar_lane(Item item, uint64_t scalar_lane)`

Behavior after the callee number watermark has been restored:

- `LMD_TYPE_INT64`: return the original Item when inline; otherwise allocate a
  new caller-owned box with `box_int64_value()`;
- `LMD_TYPE_FLOAT` / `LMD_TYPE_FLOAT64`: return the original Item when it is an
  inline double or packed signed zero; otherwise rebuild with `push_d()`;
- `LMD_TYPE_DTIME`: rebuild with `push_k()`;
- all other types: return the original Item unchanged.

The helper's allocation arms are correctness-critical slow paths. They should
not be copied into each transpiler. The common representation test can be
inlined, while the allocation/rebuild operations should remain shared.

### 4.3 Why a C/C++ `inline` annotation cannot solve the call overhead

Generated code reaches these functions as MIR imports registered in
`sys_func_registry.c`. The host compiler and release LTO do not see through a
dynamically generated MIR import call. Making the C++ definitions `inline`, or
moving them into a header, would not inline them into JIT code.

True inlining requires emitting the representation checks and raw payload loads
as MIR instructions, with a shared emitter/helper abstraction so Lambda and JS
do not acquire duplicate encodings of the Item layout. **This MIR-emission
approach is the adopted decision — see §9.3.**

---

## 5. Pre-change Lambda MIR-Direct return path

Lambda MIR-Direct has three internal return-lane modes in
`lambda/transpile-mir.cpp`:

```text
RETURN_LANE_NONE
RETURN_LANE_SCALAR
RETURN_LANE_ERROR
```

The selected mode is part of the generated function ABI:

| Lowered return | Current mode |
|---|---|
| native scalar, cannot raise | `NONE` |
| native scalar, can raise | `ERROR` |
| boxed/dynamic Item | `SCALAR` |

### 5.1 Callee epilogue

For `RETURN_LANE_SCALAR`, `finish_function_epilogue()` currently performs:

1. Call `lambda_item_scalar_lane(function_return_reg)` while the callee number
   frame is still live.
2. Keep the scalar bits in `function_return_second_reg`.
3. Stage the primary Item in the function's reserved raw scratch slot. This
   protects it across cleanup and works around MIR ARM64 multi-result loss.
4. Restore precise-root state and the number watermark.
5. Reload the primary result.
6. Store the scalar/error lane to `Context::mir_return_lane`.
7. Return the primary result through the normal machine return register.

The logical ABI is `[item, scalar]`, but its portable physical transport is one
normal return plus the per-context `mir_return_lane` handoff cell. The caller
must consume the handoff immediately; no unrelated nested call may overwrite
it between return and consumption.

### 5.2 Caller and boxed-wrapper path

After a scalar-lane call returns, generated caller code:

1. reads `Context::mir_return_lane`;
2. calls `lambda_item_from_scalar_lane(primary, lane)`; and
3. continues with the caller-owned Item.

Generated boxed ABI wrappers perform the same rebuild when adapting a raw
Lambda function to a single-Item external/JS-facing ABI.

Lambda therefore splits the two runtime helper calls across the call boundary:
capture in the callee, rebuild in the caller or boxed wrapper.

### 5.3 Existing strength and remaining conservatism

Lambda already avoids scalar lanes for native inferred returns. Boxed/dynamic
returns remain conservative because an `ANY` Item can contain a pointer-backed
wide scalar. A future shared specialization can improve Lambda too when its
signature/type analysis proves that a boxed return cannot be one of the
number-stack-backed types.

---

## 6. Pre-change LambdaJS return path

### 6.1 Unified JS epilogue

`jm_begin_function_frame()` records:

- whether the function returns a boxed Item (`side_frame_item_return`);
- the MIR return type;
- saved root and number watermarks; and
- one return register and return label.

While a side frame is active, `jm_emit()` rewrites every one-result `MIR_RET`
into a move to `side_frame_return_reg` plus a jump to the unified epilogue.
Normal returns, implicit undefined, exception propagation, generator/async
state-machine exits, and main completion therefore share cleanup ordering.

`jm_finish_function_frame()` first re-homes owned environment scalars. For an
Item-returning frame it then:

1. calls `lambda_item_scalar_lane(side_frame_return_reg)`;
2. restores `Context::side_number_top` to `side_number_frame_base`;
3. calls `lambda_item_from_scalar_lane(return_item, lane)`;
4. replaces the return register with the rebuilt Item;
5. restores `Context::side_root_top`; and
6. returns the Item.

Unlike Lambda's internal ABI, JS completes capture and rebuild inside the
callee and exposes a normal single-Item return to every caller.

### 6.2 Which generated JS entries currently enable it

The `item_return` argument is currently a boolean.

| Generated JS entry | `item_return` today | Scalar helper calls? |
|---|---:|---:|
| native-specialized numeric function | false | no |
| ordinary boxed function | true | yes, unconditionally |
| generator state machine | true | yes, unconditionally |
| async state machine | true | yes, unconditionally |
| generated `js_main` | true | yes, unconditionally |

For an ordinary boxed function, the compiler has `JsFuncCollected::return_type`
before lowering, but that type is not passed to the frame policy. The boxed path
calls `jm_begin_function_frame(..., true, ...)` even when return inference has
proved `BOOL`, `STRING`, compact `INT`, `NULL`/undefined, or decimal/BigInt.

### 6.3 Existing return inference

LambdaJS already collects a function-level `TypeId` and performs a deeper P6
reinference for some typed-parameter functions. The current analysis can prove,
among other cases:

- numeric/float expressions;
- boolean comparisons and boolean literals;
- string literals and some concatenations;
- no explicit return (`NULL` is used as the “returns undefined” result);
- decimal for a direct BigInt literal; and
- native `INT`/`FLOAT` functions when parameter and capture restrictions allow.

Conflicting return types, unresolved identifier/call returns, and many object or
container expressions collapse to `LMD_TYPE_ANY`. That is acceptable: sound
`ANY` cases must retain a dynamic representation check. The optimization must
never guess a narrow mode merely to make a benchmark faster.

---

## 7. Performance problems

### 7.1 Two imported calls on every boxed JS invocation

The common boxed JS epilogue pays two JIT-to-runtime calls even when both are
known no-ops. A trivial function returning undefined, a boolean, a compact int,
a string, an object, or an array still performs:

```text
call lambda_item_scalar_lane       // returns 0
restore side_number_top
call lambda_item_from_scalar_lane  // returns original Item
```

An imported call costs more than the switch body alone: it introduces a call
boundary, argument/result moves, caller-saved register pressure, and inhibits
optimization across the epilogue. The JIT cannot rely on host LTO to remove it.

### 7.2 Repeated runtime type decoding

Both helpers call `get_type_id()` independently. The second helper repeats type
classification after the first helper has already discovered the value class.
The lane itself carries no “live” bit, so a zero lane cannot distinguish a
non-wide return from a wide scalar whose raw payload is zero.

### 7.3 Inline float returns still pay the generic path

Most JS Numbers are inline doubles. Even for `LMD_TYPE_FLOAT`, only the rare
out-of-band residue needs re-homing. The current first helper decodes the double
and bit-copies it for every float Item, then the second helper recognizes an
inline representation and returns the original Item.

Packed `+0` and `-0` are also self-contained but still traverse both calls.

### 7.4 Function-level inference is discarded at the frame boundary

`JsFuncCollected::return_type` already controls native specialization and call
result typing. The side-frame policy reduces this richer fact to one boolean:
native return versus any boxed return. This loses safe compile-time proofs for
boxed functions that cannot produce a number-stack pointer.

### 7.5 Mixed-return functions lose per-path facts

A function such as the following must be `DYNAMIC` at function scope:

```js
function value(flag) {
    if (flag) return Number.MIN_VALUE;
    return "ready";
}
```

Only the first path can require re-homing. The unified return register currently
does not carry per-return lane metadata, so both paths use the generic helpers.
This is a second-stage optimization; function-level specialization should land
first.

### 7.6 Full switch inlining can trade call cost for code size

Blindly cloning both runtime switches into every boxed epilogue would increase
MIR size and instruction-cache pressure, particularly for Node/test262 modules
containing many small functions. The intended solution is a compact mode-based
fast path plus a shared cold path, not unconditional duplication of all boxing
logic.

### 7.7 Expected scope of the win

Native-specialized numeric functions already pass `item_return=false`, so this
work should not be credited with eliminating calls they do not make. The main
benefit is boxed/dynamic JS-heavy code: callbacks, closures, functions with
untyped or non-simple parameters, captured functions, generator/async entries,
and polymorphic Node/DOM code.

No performance claim should be closed from source inspection alone. Release
benchmarks and dynamic lane counters are required by the implementation plan.

---

## 8. Correctness constraints for any optimization

1. **Capture before restore.** A pointer-backed scalar must be dereferenced
   while the callee number extent is still live.
2. **Rebuild after restore — or donate the slot.** Rebuilding before restore
   only creates another allocation in the callee extent and returns a
   different dangling pointer. The capture/rebuild split is however not the
   only safe shape: transferring ownership of the callee's frame-base slot to
   the caller by restoring to `frame_base + 1` (§9.4) satisfies the same
   lifetime rule with no rebuild at all.
3. **Preserve signed zero and exact bits.** Float transport is a bit copy, not a
   numeric conversion. NaN payloads and `-0` must not be canonicalized.
4. **Do not scan raw lanes as Items.** Raw scalar bits remain outside the GC root
   stack and outside precise Item ranges.
5. **Keep `ANY` sound.** Unknown or conflicting return analysis must retain a
   runtime representation discriminator.
6. **Keep exception and suspension exits unified.** Generator/async and
   exception landing pads must restore both watermarks exactly once.
7. **Do not duplicate Item encoding.** Lambda and JS must use one shared
   MIR-emission abstraction or promoted module helper for representation tests.
8. **Consume `mir_return_lane` immediately.** Lambda's portable handoff cell is
   per context, not a stack; a nested call before consumption can overwrite it.
9. **Keep helper-return semantics distinct.** C helpers do not establish a JIT
   number frame and must not be forced through the JIT-to-JIT lane protocol.
10. **Preserve Windows side-stack commitment.** Rare reboxing still uses the
    checked number-stack allocation path and its Windows commit slow path.

---

## 9. Proposed design

### 9.1 Replace the JS boolean with a return-lane mode

Replace `side_frame_item_return` as the policy switch with an explicit mode.
The name should describe representation/lifetime, not merely whether the MIR
return register is `I64`.

```cpp
enum JsReturnLaneMode {
    JS_RETURN_LANE_NONE,
    JS_RETURN_LANE_FLOAT,
    JS_RETURN_LANE_INT64,
    JS_RETURN_LANE_DTIME,
    JS_RETURN_LANE_DYNAMIC,
};
```

The exact enum home should be shared if Lambda adopts the same classifier. Do
not create parallel Lambda and JS tables that can drift.

Suggested type mapping:

| Proven return type | Mode |
|---|---|
| native MIR `I64` / `D` | `NONE` |
| `NULL`, undefined, bool, compact int | `NONE` |
| string, symbol, binary, decimal/BigInt | `NONE` |
| object, array, map, function, promise, error | `NONE` |
| `FLOAT` / `FLOAT64` boxed Item | `FLOAT` |
| boxed `INT64` | `INT64` |
| `DTIME` | `DTIME` |
| `ANY`, unresolved union, unknown completion | `DYNAMIC` |

Only select `NONE` from a sound compiler fact. `js_main` should remain
`DYNAMIC` because its completion value can be arbitrary. Generator/async state
machine return protocols require a dedicated audit before narrowing; retain
`DYNAMIC` initially.

### 9.2 Stage-one fast path: eliminate calls for `NONE`

For `NONE`:

1. restore `side_number_top`;
2. restore `side_root_top`; and
3. return the original Item.

Neither scalar helper is imported or called. This is a low-risk first slice
because it changes no representation handling; it only uses proof that no
number-stack-backed return is possible.

### 9.3 Inline representation classification for scalar-capable modes (DECIDED)

**Decision (2026-07-16): the helper's decision logic is emitted as MIR
instructions in the generated epilogue.** C++ `inline` and host LTO cannot
cross the JIT import boundary (§4.3), and MIR's inliner cannot see into a
native import, so MIR emission is the only true inlining. The tests come from
one shared emitter (the `em_*` layer is the natural home — constraint 7);
each epilogue emits only the one or two tests its mode needs, never the full
four-way type switch (§7.6). Bit reinterpretation goes through the
`lambda_mir_double_bits` / `lambda_mir_bits_double` leaf helpers — no MIR
dynamic `alloca` (§9.5).

Introduce one shared MIR emitter that produces:

```text
needs_rehome : I64 boolean
scalar_bits  : I64 raw payload, valid iff needs_rehome
```

It runs before number-watermark restore.

- `FLOAT`: test inline-double bits and packed signed zero first. Only the tagged
  pointer residue needs a payload load.
- `INT64`: test `ITEM_INT64_INLINE_MARK`. Only pointer-backed int64 needs a
  payload load.
- `DTIME`: capture the pointed 64-bit value.
- `DYNAMIC`: classify the Item and enter one of the above capture arms only for
  pointer-backed wide scalars.

On the common arm (`needs_rehome == false`): restore the watermark and return
the original Item. On the rare arm: take the donation path (§9.4). An
intermediate slice may instead call `lambda_item_from_scalar_lane()` after
restore on the cold branch; donation supersedes it as the end state.

This removes `lambda_item_scalar_lane()` from the hot path entirely; with
donation the cold arm needs no imported call either.

### 9.4 Cold arm: frame-base slot donation (adopted)

When `needs_rehome` is true, do not extract-and-rebuild at all. Transfer
ownership of one number slot to the caller instead:

```text
// returned Item's payload lives in my own extent (payload_addr >= frame_base)
frame_base[0]   = *payload_addr           // copy raw payload down to my first slot
item            = retag(item, frame_base) // same tag bits, new slot address
side_number_top = frame_base + 1          // restore, minus one donated slot
return item                               // already caller-owned
```

The slot at `frame_base` now lies inside the caller's extent and is reclaimed
by the caller's own epilogue like any of its slots. Compared with the
capture/rebuild scheme, this arm has zero imported calls, zero allocation —
hence no Windows commit slow path (the slot was already committed by the
callee's own first push) and no GC point inside the epilogue — and no second
return channel.

Guard on the extent range, not the representation alone: donate only when the
payload address is `>= frame_base`. Two cases fall out of that check:

- a pointer-backed scalar below the frame base (e.g. an argument returned
  unchanged, or a value produced in an ancestor frame) is already caller-safe
  and is returned as-is; and
- an extent-range hit proves the callee pushed at least one number slot, so
  `frame_base[0]` is committed and writable without a check.

Chained returns copy one payload per hop — the same hop count as the rebuild
scheme, but each hop is a load/store/retag instead of a helper call plus a
checked allocation.

The copy exists to *normalize the slot's position*, not to move the value into
scope: it pins per-call retention at exactly one slot. A zero-copy variant
(restore to `P + 1`, no copy/retag) is sound but retains the callee's entire
dead prefix `[frame_base, P)` — see Appendix A for why it is not the decided
design and the `P == frame_base` fast case kept as a future tuning option.

Consequences once adopted on both transpilers:

- the logical `[item, scalar]` ABI collapses to a single-Item return;
- `Context::mir_return_lane` is no longer used for scalar transport (the
  `ERROR` flag channel is unaffected), which retires correctness constraint
  8's consume-immediately hazard;
- the MIR ARM64 multi-result staging workaround in the Lambda epilogue
  (§5.1 step 3) is no longer needed for scalar mode; and
- Lambda's boxed-ABI wrappers become pass-through — the raw function already
  returns a caller-safe Item.

Audit obligations before landing:

- anything that asserts exact watermark restoration (debug leak checks,
  stack-depth accounting) must tolerate the `frame_base + 1` post-donation
  state;
- generator/async suspension re-homing keeps its own protocol and stays
  `DYNAMIC` until audited; and
- the throw path needs the same lifetime audit as returns: a thrown wide
  scalar (`throw Number.MIN_VALUE`) crosses the same watermark restores
  during unwind — prove the thrown value safe or re-home it in the throw
  transport (§12.1).

### 9.5 Reuse existing inline numeric lowering

LambdaJS already emits inline float representation branches in
`jm_box_float()` and `jm_emit_unbox_float()`. The return-lane implementation
should reuse or promote their shared representation primitives rather than
writing a third float classifier in the epilogue file.

The existing bit reinterpretation helpers (`lambda_mir_double_bits` and
`lambda_mir_bits_double`) are a separate leaf-call concern. Scalar-lane work
must not silently introduce MIR dynamic `alloca` for bitcasts; prior code notes
that repeated MIR `alloca` can grow the native stack until failure.

### 9.6 Add per-return refinement only after function-level modes are stable

For a `DYNAMIC` function, later lowering can attach a mode to each return edge:

```text
return Item register
return scalar bits
return needs_rehome flag
```

Known-safe edges write `needs_rehome = 0`. Known float-pointer edges capture the
payload directly. Unknown edges use the dynamic classifier. All edges still
jump to one cleanup label.

This preserves single-epilogue discipline while avoiding dynamic work for the
safe branch of a mixed-return function. It should not be the first patch: it
touches every return-lowering path and has a larger exception/generator surface.

Under donation this refinement is expected to be unnecessary: the `DYNAMIC`
epilogue's worst case is a short classify plus a load/store/retag, so
per-return metadata pays only if SL0/SL2 counters show classification itself
measurable on hot mixed-return functions. Default expectation is to drop it.

### 9.7 Apply the shared fast path to Lambda MIR-Direct

After JS proves the shared emitter, Lambda boxed return ABIs can use the same
classification:

- signatures proven not to return a wide scalar select `NONE`;
- `ANY` retains `DYNAMIC`;
- known wide scalar types use their specialized mode; and
- donation replaces the split capture-in-callee/rebuild-in-caller protocol:
  the callee returns a caller-safe Item directly, so the scalar-transport use
  of `Context::mir_return_lane`, the ARM64 staging workaround, and the boxed
  wrapper rebuilds are all retired (§9.4). The `ERROR` channel keeps the
  cell.

The JS optimization does not require changing Lambda's transport in the first
stage. Keeping the initial patch JS-local in policy, while sharing only
representation emission, limits regression risk.

---

## 10. Lane usage matrix — Today vs Final

Lane usage is decided at two levels: a **compile-time mode** baked into the
function's ABI (from the inferred return type, §9.1), and a **runtime
representation test** within that mode (one type can have several encodings).
The tables below enumerate the full combination space. **Today** is the
current unconditional two-helper path; **Final** is the decided end state with
§9 landed — mode-based lanes (§9.1/§9.2), inline MIR classification (§9.3),
and frame-base slot donation on the cold arm (§9.4). No Final cell contains
an imported helper call.

### 10.1 The two side channels

| Channel use | Today | Final |
|---|---|---|
| Scalar transport | Lambda `RETURN_LANE_SCALAR` via `Context::mir_return_lane`; JS boxed epilogue (callee-internal register) | **retired** — donation returns a caller-owned Item through the single normal return register |
| Error signal | Lambda `RETURN_LANE_ERROR` raise flag | unchanged |

### 10.2 Lane policy per generated entry kind

| Entry kind | Mode today | Calls today | Mode final | Calls final |
|---|---|---|---|---|
| Lambda native return, cannot raise | `NONE` | 0 | `NONE` | 0 |
| Lambda native return, can raise | `ERROR` | 0 (flag only) | `ERROR` | 0 (flag only) |
| Lambda boxed/dynamic return | `SCALAR` | 2 — capture in callee, rebuild in caller/wrapper | mode from signature: `NONE` where proven, else typed/`DYNAMIC` | 0 |
| JS native-specialized numeric | (no lane) | 0 | (no lane) | 0 |
| JS ordinary boxed function | boolean `true` | 2, unconditional | mode from return inference (§6.3) | 0 |
| JS generator / async / `js_main` | boolean `true` | 2, unconditional | `DYNAMIC` until the return protocol audit; narrowing candidates after | 0 |

### 10.3 Master matrix: inferred return type × runtime representation

| # | Inferred return type | Runtime representation | In callee number frame? | How reachable | Mode (final) | Today | Final |
|---|---|---|---|---|---|---|---|
| 1 | native `I64` / `D` (unboxed) | raw machine value | n/a | typed numeric fns | `NONE`/`ERROR` | 0 | 0 |
| 2 | `NULL` / undefined | immediate | no | everywhere | `NONE` | **2** | 0 — restore + `ret` |
| 3 | `BOOL` | immediate | no | everywhere | `NONE` | **2** | 0 — restore + `ret` |
| 4 | compact `INT` (int56) | tagged immediate | no | everywhere | `NONE` | **2** | 0 — restore + `ret` |
| 5 | string / symbol / binary | stable or GC pointer | no | everywhere | `NONE` | **2** | 0 — restore + `ret` |
| 6 | decimal / JS BigInt | GC-heap object | no | BigInt, `n` literals | `NONE` | **2** | 0 — restore + `ret` |
| 7 | object / array / map / element / function / promise / error object | GC-heap container | no | everywhere | `NONE` | **2** | 0 — restore + `ret` |
| 8 | `INT64` | inline (`ITEM_INT64_INLINE_MARK`, fits 56 bits) | no | polyglot int64 | `INT64` | **2** | inline-mark test → 0 |
| 9 | `INT64` | pointer-backed (>56-bit magnitude) | **yes** | polyglot int64 only | `INT64` | 2 | test → donate (~4 instr), 0 calls |
| 10 | `FLOAT` † | self-tagged raw IEEE bits (`\|d\| ≥ ~1.5e-154`, incl. ±Inf, NaN) | no | **the dominant Number case** | `FLOAT` | **2** | mask test → 0 |
| 11 | `FLOAT` † | packed `+0` / `-0` immediates | no | common | `FLOAT` | **2** | test → 0 |
| 12 | `FLOAT` † | residue: subnormal/tiny, side-stack boxed (e.g. `Number.MIN_VALUE`) | **yes** | rare-by-construction; plain JS can produce it | `FLOAT` | 2 | test → donate, 0 calls |
| 13 | `DTIME` | pointer-backed (always) | **yes** | polyglot DateTime only | `DTIME` | 2 | donate, 0 calls |
| 14 | `ANY` — runtime value is any of rows 2–7 | immediate or heap ptr | no | most dynamic returns | `DYNAMIC` | **2** | classify → 0 |
| 15 | `ANY` — runtime value is inline int64 / self-tagged double / `±0` | immediate | no | common | `DYNAMIC` | **2** | classify → 0 |
| 16 | `ANY` — runtime value is row 9 / 12 / 13 | number-slot pointer | **yes** | rare (residue doubles, polyglot wide scalars) | `DYNAMIC` | 2 | classify → donate, 0 calls |
| 17 | generator / async / `js_main` completion | any (protocol unaudited) | maybe | all such entries | `DYNAMIC` initially | **2** | as rows 14–16 |

† `LMD_TYPE_FLOAT64` is no longer a distinct runtime rank — it is a legacy
reserved tag and `f64` canonicalizes to `FLOAT` at production — so the old
"float64 pointer form" collapses into these three `FLOAT` rows.

### 10.4 Reading the matrix

- Bold **2** marks pure waste today: both helper calls are provable no-ops.
  That is rows 2–8, 10–11, and 14–15 — the overwhelming majority of real
  returns. In the Final column those rows cost exactly what a function with
  no lane costs.
- Re-homing is ever *required* only in rows 9, 12, 13, and 16 (and 17 when it
  hits those representations). Row 12 is the only one reachable from pure JS,
  and it takes a nonzero double below `~1.5e-154`; rows 9 and 13 need polyglot
  Lambda values. Even these rows are call-free in the Final state: donation
  is a load, a store, a retag, and a watermark adjustment.
- The inline classifier cost per mode is fixed by the landed encoding:
  `FLOAT` = one mask test plus one packed-zero compare; `INT64` = one
  inline-mark test; `DTIME` = unconditional (always pointer-backed);
  `DYNAMIC` = tag extract then the same tests (§9.3).
- Rows 14–16 are a runtime split inside one compiled `DYNAMIC` function; the
  compile-time modes exist precisely so rows 2–7 skip the classifier too.
- In the landed state `lambda_item_scalar_lane` /
  `lambda_item_from_scalar_lane` are deleted: the shared MIR emitter is their
  only replacement and no non-JIT consumer remained (SL4, §11).

---

## 11. Implementation stages

### SL0 — Structural baseline recorded; runtime profiling deferred

- Count compiled JS functions by inferred return type and proposed lane mode.
- Add opt-in profiling for dynamic return values: total boxed returns,
  pointer-backed wide returns, inline floats, and non-scalar Items.
- Measure runtime helper call counts, not just total wall time.
- Use release builds only for performance data.

Suggested opt-in prefix: `LAMBDA_MIR_LOG_SCALAR_LANES`. Logging must be
aggregate and disabled by default; per-call logging would dominate the result.

Implementation checkpoint: emitted-MIR inspection established the structural
baseline and proves that the landed path imports neither retired helper.
Permanent runtime counters were not added because they are not required for
correctness and would add dormant production machinery. Release wall-time and
code-size comparison remain follow-up performance evidence; no speedup is
claimed from the correctness gates.

### SL1 — Function-level `NONE` mode in LambdaJS

**LANDED 2026-07-16.**

- Replace the JS frame boolean policy with the explicit mode.
- Map sound inferred non-wide return types to `NONE`.
- Keep `FLOAT`, `INT64`, `DTIME`, and `ANY` on the existing helper path.
- Keep `js_main`, generator state machines, and async state machines dynamic
  until their return protocols are audited.

Exit condition: helper calls disappear from emitted MIR for proven-safe boxed
functions, with no behavior change for scalar-capable functions.

### SL2 — Inline scalar representation discriminator + donation cold arm

**LANDED 2026-07-16.**

- Add/promote one shared MIR representation classifier, reusing the landed
  self-tagging predicates (§9.5).
- Compute `needs_rehome` plus the payload address before watermark restore.
- Common arm: plain restore, return the original Item.
- Cold arm: frame-base slot donation (§9.4). An intermediate slice may call
  `lambda_item_from_scalar_lane()` here first, but the exit state is donation
  with zero imported calls.
- Land the donation audit items from §9.4 (watermark-exactness assertions,
  throw-path lifetime) alongside.

Exit condition: no boxed JS return path executes an imported scalar helper —
common or cold arm.

### SL3 — Per-return metadata (conditional; expected to be dropped)

**NOT IMPLEMENTED.** Keep this conditional until release profiling shows the
remaining `DYNAMIC` classifier is material; the current patch does not add
speculative per-return metadata.

Donation makes the dynamic cold arm a load/store/retag, so per-return
`needs_rehome` metadata for mixed `DYNAMIC` functions (§9.6) is justified
only if SL0/SL2 counters show classification itself measurable on hot
mixed-return functions. Decide from profiling; do not build it speculatively.

Exit condition: an explicit keep-or-drop decision recorded from SL0/SL2 data;
if kept, mixed-return safe edges avoid generic classification with unchanged
behavior.

### SL4 — Lambda MIR-Direct adoption and cleanup

**LANDED 2026-07-16.** No non-JIT helper consumer remained, so both runtime
helpers were deleted rather than retained as utilities.

- Reuse the proven shared classifier for Lambda boxed returns.
- Narrow lane ABIs from signatures where sound.
- Adopt donation on the Lambda side; retire the scalar-transport use of
  `Context::mir_return_lane`, the ARM64 multi-result staging workaround, and
  boxed-wrapper rebuilds (§9.4, §9.7). The `ERROR` channel keeps the cell.
- Remove dead unconditional helper imports/calls; `lambda_item_scalar_lane` /
  `lambda_item_from_scalar_lane` remain only if a non-JIT consumer still needs
  them.
- Update SF14 and LR/JS design docs to describe conditional lanes and
  donation.

---

## 12. Required tests

### 12.1 Permanent correctness cases

The test matrix must exercise direct, nested, recursive, closure, generator,
async, exception, and mixed-return paths for:

- undefined, null, bool, compact int, string, object, array, and function;
- an ordinary inline double;
- `+0` and `-0` with `Object.is` checks;
- `Number.MIN_VALUE` and `-Number.MIN_VALUE`;
- NaN and infinities;
- a polyglot pointer-backed int64 value;
- a polyglot `DateTime` value; and
- a function returning a wide scalar on one branch and an object/string on the
  other.

Every pointer-backed case must force GC and make at least one intervening
number allocation after return so a stale pointer cannot pass accidentally.

The exception row deserves emphasis: a thrown wide scalar
(`throw Number.MIN_VALUE`) crosses the same watermark restores during unwind
as a return does. Audit where the thrown value lives during unwind; prove it
safe or re-home it in the throw transport.

Donation-specific checks: any debug assertion or accounting that expects exact
watermark restoration must accept the `frame_base + 1` post-donation state;
a donated slot must survive intervening number allocations made by the caller
and by subsequent callees; and a chained donation (wide scalar returned up
several frames) must stay bit-exact at every hop.

Existing anchors:

- `test/js/regression_side_stack_frame_gc.js`;
- JS generator/async regressions;
- Lambda `proc_stack_frame` and wide return tests; and
- JS/Lambda bridge tests for int64 and DateTime.

### 12.2 Performance microbenchmarks

Use release builds and enough iterations to dominate startup/JIT cost:

```js
function retUndefined() { return undefined; }
function retBool() { return true; }
function retString() { return "ok"; }
function retObject() { return sharedObject; }
function retInlineDouble() { return 1.25; }
function retTinyDouble() { return Number.MIN_VALUE; }
function retMixed(flag) { return flag ? Number.MIN_VALUE : "ok"; }
```

Record:

- wall and CPU time;
- generated function count and MIR size;
- scalar helper calls per invocation;
- pointer-backed scalar slow-path frequency; and
- peak/steady RSS to ensure no lifetime regression.

The expected structural result is zero scalar-helper calls for proven `NONE`
functions and zero calls on the common safe arm after SL2. A wall-time target
must be set from SL0 data rather than invented in advance.

### 12.3 Regression gates

At minimum after every semantic stage:

- focused scalar-lane and side-stack regressions;
- full `test_js_gtest`;
- `make test-lambda-baseline` with zero failures;
- `make test262-baseline` with zero failures, zero non-fully-passing tests, and
  zero regressions; and
- focused Node callback/generator/async modules.

Before closure, run the full Node baseline when the runtime budget permits. A
timeout must count in the harness's explicit gate-failure total.

Verified implementation snapshot from 2026-07-16:

- focused side-stack scalar regression: **17/17** checks;
- full JS gtest: **332/332**;
- Lambda baseline: **3424/3424**;
- test262 baseline: **40261/40261**, 0 failed, 0 non-fully-passing, 0 regressions.
- Node preliminary gate inside the Lambda baseline: **110/110**;
- full Node baseline aggregate: **3527/3528** concurrent cases passed, with 0
  ordinary failures, 0 crashes, and one 60.1s timeout in
  `test-snapshot-umd.js`. The exact test then passed twice through the focused
  harness in about 25.4s and once directly in 26.1s, identifying a
  parallel-load timeout rather than a deterministic scalar-return failure.

Structural MIR inspection also shows zero imports/calls of either retired
helper and mode-specific omission of the classifier for proven-`NONE`
functions.

---

## 13. Rejected shortcuts

### 13.1 Delete scalar lanes because most values are inline

Rejected. A single out-of-band double or pointer-backed int64 returned across a
restored watermark becomes a dangling pointer. `Number.MIN_VALUE` proves this
is reachable in ordinary JavaScript.

### 13.2 Mark the C++ helpers `inline`

Rejected. MIR imports remain runtime calls; host compiler inlining cannot cross
the JIT boundary.

### 13.3 Rebuild before restoring the number watermark

Rejected. The new box would still belong to the callee and would be reclaimed
by the same restore. Slot donation (§9.4) is not this shortcut: it adjusts
the restore point to `frame_base + 1`, so the surviving slot is caller-owned
by construction rather than left dangling.

### 13.4 Disable number-watermark restoration for boxed functions

Rejected. This converts a local call overhead into unbounded scalar-stack growth
for callbacks, servers, and event loops, undoing the stack-frame design.

### 13.5 Treat every `FLOAT` as requiring a lane

Rejected as the final state. It is sound but misses the dominant inline-double
case. It is acceptable only as a temporary SL1 fallback while `NONE` types are
separated.

### 13.6 Clone the Item-layout switch into both transpilers

Rejected. Representation rules are sensitive to inline int64 markers, double
masking, signed zero, and future encoding changes. A shared emission boundary is
required.

### 13.7 Narrow `ANY` from optimistic profiling

Rejected. Profile-guided speculation would require guards and deoptimization.
This proposal is static proof plus a sound dynamic fallback, not unsound type
assertion.

---

## 14. Completion criteria

This issue is complete only when:

- proven non-wide boxed JS returns emit no scalar-lane helper calls;
- inline float returns take a call-free common return path;
- pointer-backed float/int64/DateTime returns are bit-exact and caller-owned
  via slot donation, with no imported helper call on any return arm;
- the scalar-transport use of `Context::mir_return_lane` and the ARM64
  multi-result staging workaround are retired (the `ERROR` channel remains);
- `ANY` and mixed-return functions remain sound under forced GC and subsequent
  number allocations;
- generator, async, exception, and main completion paths restore watermarks
  exactly once;
- the Lambda and JS transpilers share representation classification rather than
  duplicating it;
- release profiling demonstrates reduced helper calls and no material MIR/code
  size regression; and
- all gates in §12.3 are green.

The implementation items and core correctness gates are satisfied by the
2026-07-16 snapshot in §12.3. The full Node aggregate is not recorded as green
because of its single load-sensitive timeout, despite three clean isolated
runs. Comparative release wall-time/MIR-size profiling is also intentionally
open; the patch claims the directly verified structural result (zero imported
scalar helpers), not an unmeasured speedup.

---

## 15. Live code map

| Concern | Current location |
|---|---|
| Item encoding and inline scalar predicates | `lambda/lambda.h` |
| Shared return modes, representation classifier, and donation | `lambda/mir_emitter_shared.hpp` |
| Retired helper declarations/implementations/imports | deleted from `lambda/lambda.h`, `lambda/lambda-mem.cpp`, and `lambda/sys_func_registry.c` |
| Lambda boxed return ABI and error-only lane | `lambda/transpile-mir.cpp` |
| Error-only `Context::mir_return_lane` cell | `lambda/lambda.h` |
| JS side-frame mode state | `lambda/js/js_mir_context.hpp` |
| JS unified donation epilogue | `lambda/js/js_mir_hashmap_scope_utils.cpp` |
| JS function return inference | `lambda/js/js_mir_function_collection_class_inference.cpp` |
| JS native/boxed function selection | `lambda/js/js_mir_function_class_lowering.cpp` |
| JS inline float box/unbox lowering | `lambda/js/js_mir_calls_boxing_types.cpp` |
| JS main/state-machine frame policy | `lambda/js/js_mir_module_batch_lowering.cpp`, `lambda/js/js_mir_function_class_lowering.cpp` |


---

## Appendix A — Zero-copy retention (future tuning option)

A considered variant of §9.4 donation, recorded for possible future tuning.
Not part of the decided design; revisit only with SL0/SL2 profiling data.

### A.1 Mechanism

The returned wide scalar already lives on the number stack, at some slot `P`
inside the callee extent. Ownership can transfer with no copy and no retag at
all by restoring the watermark to just above it:

```text
side_number_top = P + 1   // instead of frame_base
return item               // pointer unchanged — still valid, now caller-owned
```

This is the zero-copy limit of donation: transfer purely by watermark
arithmetic. The Item pointer never moves, so even the retag disappears.

### A.2 Why it is not the decided design

Restoring to `P + 1` retains not just the returned slot but **every slot the
callee allocated before it** — `[frame_base, P)` are dead, and because the
number stack is a raw bump allocator with no per-slot metadata (constraint 4:
raw lanes are never scanned), nothing can identify or reclaim them before the
caller's own epilogue. Per-call retention becomes data-dependent and
unbounded: an int64-heavy Lambda callee can push thousands of intermediate
wide values before its result, so `P` can sit arbitrarily deep, and a loop
calling such a function grows the caller frame by the callee's entire
transient allocation per iteration. This is a data-dependent variant of
rejected shortcut 13.4 (skipping watermark restoration), with the same
unbounded-growth disease — triggered conditionally instead of always, and on
exactly the workloads (wide-int64 Lambda code) that hit the cold arm most.

Donation's single copy is the price of a hard bound: exactly one slot
retained per call regardless of callee behavior — which is also the floor,
since any scheme consumes one caller slot per returned wide scalar.

### A.3 The sound fast case: `P == frame_base`

When the returned value happens to sit at the frame base — plausibly the
common shape for the residue-float case, where the returned value is the
callee's only number allocation — zero-copy retention and donation coincide,
and donation's copy is a self-copy with an identity retag:

```text
if payload_addr == frame_base:      // already in position
    side_number_top = frame_base + 1
    return item                     // no copy, no retag
else:                               // normalize: 1 load+store caps retention at 1 slot
    frame_base[0]   = *payload_addr
    item            = retag(item, frame_base)
    side_number_top = frame_base + 1
    return item
```

This variant is always sound. It trades one compare-and-branch on an
already-cold arm for sometimes saving a load, a store, and a tag-OR —
below the noise floor, and unconditional donation degenerates gracefully
anyway (the self-copy store is harmless).

### A.4 When to revisit

Consider this appendix only if SL0/SL2 counters show, on real workloads,
that the donation arm is hot **and** dominated by `P == frame_base` cases
(making the branch predictable and the saved store worth it). Any adoption
must preserve the O(1)-per-call retention bound — i.e. only the A.3 hybrid
is admissible, never bare `P + 1` retention.
