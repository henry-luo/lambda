# Lambda Impl Plan — Return-Value Convention v3 (Companion-Lane Returns)

**Date**: 2026-08-14  **Status**: **v3 IS THE SHIPPING DEFAULT since
2026-08-15** (`LAMBDA_RETURN_V3` default **1**; v2 still buildable with
`-DLAMBDA_RETURN_V3=0` until P5). P0 / P1.1–P1.6 / P2 / P2.6 / P2.7.0 / P3
LANDED; P1.7 partial; P2.5, P2.7 (protocol), P4, P5 open.
Plan for [`Lambda_Design_Compiling_Return_Value.md`] (RV1–RV18 + addenda
RV3a / RV10a / RV14a / RV17a; formal spec D5.2.1v2 / **D5.2.2v2** /
**D5.2.3** / D2.7.2v2 / D8.4.2v2, **v1.22.0**). Open design residue: RVO8
(GVN multi-out), RVO9 (dummy lane-2), **RVO11 / DO24** (unnamed wide
temporaries across a loop back edge — gates P2.7's reclaim). RVO3 and RVO4
closed 2026-08-14; RVO10 superseded by RV14/RV15; **RVO12 opened and closed
2026-08-15** (shape 4 on all three lanes).
**Tree anchor**: master `af254850f`, plus `7187898f1` for the landed phases
(line refs are as of those commits; anchor by symbol name when they drift).

**Goal**: retire the trailing-scalar-home ABI. Boxed returns become
`[item, scalar]` pairs discriminated by the pending-Item tag `0x1E`; typed
returns get native lanes with `^E` on an error-Item lane. Census baseline
to beat (2026-08-14, `utils/analyze_scalar_homes.py` over debug
`temp/mir_dump.txt`): home protocol = 9–39% of emitted MIR across AWFY
(deltablue 413 adopt sites / 38.7%), ~11–16 executed insns per boxed
return, 70–100% of functions carrying `_scalar_home`.

**Scope correction (2026-08-14, measured).** The pair convention alone reaches
**14%** of the adopt census, not all of it. The other two populations need
mechanisms outside §2–§6 of the design, each matched to *who owns a watermark*
(design §1.4, §4a):

| population | share | phase |
|---|---|---|
| return-side, internal bodies | 14% | **P1** — landed |
| helper-call adopts | 68% | **P2.6** (free subset) + **P2.7** (RV14/RV15/RV16) |
| return-side, C-reachable `_b` wrappers + dynamic dispatch | 18% | **P3** (RV12 slot; not Windows-specific) |

P5's deletion is therefore gated on P2.7 and P3 as well — the home apparatus
keeps real callers until both land.

---

## 0. Context — verified code facts (2026-08-14)

**Encoding side** (`lambda/lambda.h`):
- Tag space: TypeIds `0x00–0x1B`, `LMD_TYPE_COUNT` 0x1C,
  `LMD_CONTAINER_HEAP_START` 0x1D, `ITEM_SENTINEL_TAG` `0x1F` (`:1266`).
  **`0x1E` is the last free byte** — claimed by the pending tag.
- Static-assert farm at `:1360–1375` (sentinel/tag placement) is the
  pattern to extend; `lambda_item_debug_trap()` (`:1378`) is the tripwire
  primitive.
- `INT64_ERROR == INT64_MAX == INT_LANE_INF` (`:1230`, the open v5 §5.8
  collision); producers `fn_mod_i`/`fn_idiv_i` (`:2327`),
  `int2it_i64_or_error` (`:2094`).
- `Context::mir_return_lane` (`:1955`) — the native `^E` error flag is
  **already a context-slot transport**, wired at `transpile-mir.cpp:1464`,
  `:1492`, `:16296`. This is the in-tree precedent for the RV12 Windows
  slot.

**Descriptor side** (already exists — v3 extends, does not invent):
- `lambda/runtime/value_rep.h` (56 lines): `ValueRep`, `ScalarReturnClass`
  {NONE, I64, U64, F64, DYNAMIC}, `FnEntryKind` {PUBLIC_WRAPPER,
  BOXED_BODY, NATIVE_BODY, RESUME}, `FnErrorLane` {NONE, CONTEXT_ITEM},
  `FN_RETURN_HOME_NORMAL|ERROR` masks.
- `lambda/runtime/ast-core.hpp:790–801`: `FnReturnAnalysis` { normal/error
  `FnReturnLaneAnalysis` (semantic_type, abi_rep, scalar_class,
  `may_need_caller_scalar_home`), `error_lane`, `scalar_home_lane_mask` }.
  This struct is where the v3 shape descriptor lives.
- `sys_func_registry.h:141` mirrors `scalar_home_lane_mask` for helpers.

**Emitter side** (`lambda/runtime/mir_emitter_shared.hpp` — shared by
Lambda AND LambdaJS; every deletion here gates on both lanes migrating):
- `MirScalarReturnMode` {NONE, FLOAT, INT64, UINT64, DYNAMIC} `:275`;
  type mapping `em_scalar_return_mode_for_type` `:743` — FLOAT/FLOAT64→
  FLOAT, INT64/UINT64→native ints, **ANY→DYNAMIC, everything else
  (incl. DTIME) → NONE**. RV8's DTIME ruling is already satisfied
  representationally; there is no DTIME mode to retire.
- Adopt cluster: `em_adopt_scalar_item_value` `:966–1057` (the measured
  20-insn sequence; publishes to the async spill tracker via
  `after_call_result` `:1053`), wrapper `em_adopt_scalar_item` `:1060`.
- Home machinery: `em_scalar_home_new/bind/for_reg/ref/materialize`
  `:2443–2534`, liveness/coloring/interference in
  `em_finalize_scalar_homes` `:2583+`, frame-plan fields
  (`scalar_home_lane_mask` `:379`, entry call metadata `:201`, variant
  contract checks `:3239/:3281–3293/:3343–3367`).
- Runtime helper: `lambda_item_adopt_scalar_home` (`lambda.h:1041`) +
  rehome counters (`:1046`).

**Transpiler call/return sites** (`lambda/runtime/transpile-mir.cpp`):
- Function epilogue adopt-into-incoming-home: `:1407–1478`.
- Context-ABI export wrappers (trailing caller-owned home): `:15371–15416`.
- Direct-call lowering (home bind + `mir_return_lane` read):
  `:16218–16330`.
- Dynamic dispatch (`dyn_scalar_home` materialize + bind): `:16492–16587`.
- Public wrapper creation — appends `p:_scalar_home` param and sets
  `plan.accepts_caller_scalar_home`: `:21364–21398`; entry result masks
  `:21162`, `:21605–21642`.
- `MIR_new_func_arr`/`MIR_new_proto_arr` sites (nres=1 today): `:15418`,
  `:16273`, `:21379`, `:21954`, `:23934`, `:24085`, `:24475`.

**LambdaJS sites** (RV13 — adopts the convention):
`js_mir_function_class_lowering.cpp:822/:959/:1356/:1970/:2480`,
`js_mir_module_batch_lowering.cpp:5814`; LJS already drives `MirValue` /
`em_require_rep` (D2.4 footnote), which v3's pending tracking plugs into.

**MIR facts** (vendored, never patched — rule 16): multi-results are core
(`MIR_new_func_arr` nres, `MIR_CALL` result output operands, interp
`nres=2` native); x86-64 SysV 2 int + 2 fp results, aarch64 8+8;
**Windows x86-64 hard-errors on nres>1** (`mir-x86_64.c:547/:660`) — hence
RV12's slot lowering; GVN combine skips multi-out defs
(`mir-gen.c:3594/:3635`) — RVO8 measures this.

**Test infrastructure**: MT7 0%-slack MIR budgets
(`test/mir/mir_budgets.json`); forced-GC sweep (MIR-emission test design);
⚠ the benchmark runner does NOT diff outputs (2026-07-25 audit) — gates
below add explicit diffs; ⚠ `make test-lambda-baseline` clobbers
`lambda.exe` with a debug build — re-`make release` before any timing.

---

## 1. Flag and compatibility strategy

- **One build-time flag, whole-module granularity**: `LAMBDA_RETURN_V3`
  (compile-time `#if` in emitters + runtime registry mirror). A module is
  compiled entirely v2 or entirely v3 — no mixed conventions inside a
  module, ever. Cross-module and dynamic calls always go through entries
  whose shape descriptor says what they speak (P1.1), so the flag flips
  emission, not dispatch correctness.
- **MIR cache L1**: the convention revision joins the cache key the moment
  P1 lands (a v2-cached module must never be linked against v3 callers).
  Cheapest correct form: bump the existing cache version constant under
  the flag.
- **C2MIR is frozen** (rule 14): keeps v2 single-Item returns forever;
  the C2MIR path never sets the flag. Not a gate for anything below.
- **Differential validation harness** (used by every phase gate): run the
  full Lambda baseline + AWFY (untyped and `--typed`) + JS suites twice
  (flag off/on), diff stdout byte-for-byte, then forced-GC sweep on the
  flag-on build. This is the same discipline that validated the online
  exception-poll replacement.

---

## 2. Phases

### P0 — Pending-Item encoding + tripwires *(no behavior change)*

1. `lambda/lambda.h`: define
   `ITEM_PENDING_TAG` (`0x1E`), `ITEM_PENDING = tag<<56`,
   `PENDING_KIND_INT64/UINT64/FLOAT` (0/1/2; 3 reserved — RV8 rules DTIME
   out), and inline helpers
   `lambda_item_is_pending(bits)` (`(bits & ITEM_HIGH_BYTE_MASK) ==
   ITEM_PENDING` — the 2-insn shape the JIT will mirror) and
   `lambda_item_pending_kind(bits)` (`bits & 3`).
2. Static asserts beside `:1360–1375`: pending tag is non-double, not
   inline-int, `!= ITEM_SENTINEL_TAG`, `>= LMD_CONTAINER_HEAP_START`,
   `< LAMBDA_TAG_SPACE_SIZE`, and distinct from both JS internal
   sentinels.
3. Debug tripwires at the accessor choke points (`it2d` cold arm,
   `it2l`-family, `get_type_id` container path, formatter/equality/hash
   entry): `assert(!lambda_item_is_pending(x))` →
   `lambda_item_debug_trap()`. Debug builds only; the tag's
   out-of-TypeId-range placement already makes release leaks fail loudly
   at table bounds.
4. Comment block pointing at the design doc; note the protocol invariants
   (never-in-memory, single-live) verbatim.

**Gate**: debug + release build clean; `make test-lambda-baseline` 100%;
zero emitted-code change (MT7 budgets untouched).
**Size**: small — one header, no emitter edits.

### P1 — Shape 2 for boxed returns *(flag-gated; the big phase)*

**P1.1 — Shape descriptor (single source of truth).**
- `value_rep.h`: add
  `typedef enum FnReturnShape { RETURN_SHAPE_ITEM, RETURN_SHAPE_ITEM_SCALAR,
  RETURN_SHAPE_NATIVE, RETURN_SHAPE_NATIVE_ERROR }` and add
  `FN_ERROR_LANE_PAIR` to `FnErrorLane` (used in P2).
- `ast-core.hpp` `FnReturnAnalysis`: add `FnReturnShape shape`, populated
  in the fn-analysis pass **from the declared signature only** (RV2):
  wide-free provable → `ITEM`; else → `ITEM_SCALAR`. (`NATIVE*` filled in
  P2.) `scalar_home_lane_mask` / `may_need_caller_scalar_home` stay
  authoritative for the v2 path until P5 deletes them.
- Mirror into entry metadata (`mir_emitter_shared.hpp:201/:3366`) and the
  registry row; add a debug assert that **no emitter site computes a shape
  locally** — everything reads the descriptor (the havlak-class-mismatch
  defense, RV10).

**P1.2 — Callee side.**
- All boxed Lambda entry creations get `nres=2` under the flag
  (`transpile-mir.cpp` func/proto sites listed in §0; both `_f` and `_b`
  variants).
- Return lowering replaces the epilogue adopt (`:1407–1478`):
  - statically non-wide value → `ret item, 0` (dummy lane; RVO9 check in
    P1.7);
  - statically wide value (INT64/UINT64/out-of-band-double return
    expression) → build `[ITEM_PENDING|kind, payload]` directly — **birth
    site classification, no cluster** (RV7);
  - ANY-provenance value (`MIR_SCALAR_RETURN_DYNAMIC` residue) → one
    reduced classify: `is it a frame-backed wide Item?` → if yes, load
    payload once and emit the pending pair; else pass through. This is the
    only surviving dynamic classify and it is strictly smaller than
    today's cluster (no home store, no helper call, no ±0 arms);
  - tail-forward of a shape-2 call result → forward both lanes unchanged
    (RV5), including through the context-ABI export wrappers
    (`:15371–15416`, which lose their wrapper home).
- Delete-under-flag: incoming-home adopt, `_scalar_home` param appending
  (`:21364`), `plan.accepts_caller_scalar_home`.

**P1.3 — Caller side.**
- Direct-call lowering (`:16218–16330`): call emits 2 result regs;
  result's `MirValue`/`FnValueAnalysis` carries a new `maybe_pending`
  bit (set iff callee shape == `ITEM_SCALAR`).
- Resolution emission per RV5:
  - **consume** sites (arith/compare/truthiness unbox paths): add the
    pending arm to the existing tag dispatch — reads lane-2 reg, no
    marginal cost on non-pending paths;
  - **escape** sites (store to binding/container/root, argument
    position, next-call boundary): if `maybe_pending`, emit the 2-insn
    test + out-of-line patch (allocate destination-owned storage per
    D5.2.2, retag, write back); clear `maybe_pending` on the resolved
    edge;
  - patch helper: one new runtime function
    `lambda_item_resolve_pending(Item, uint64_t payload)` → boxed Item
    (number-stack transient or destination-owned per call-site kind), the
    v3 successor of `lambda_item_adopt_scalar_home` with the opposite
    calling direction (caller pulls, callee never stores).
- **Single-live enforcement**: emitter asserts at most one `maybe_pending`
  value crosses any point; a second call while one is live forces an
  eager patch first (RV4.2).

**P1.4 — Dynamic calls, wrappers, `fn->invoke`.**
- Dynamic dispatch (`:16492–16587`): drop `dyn_scalar_home`
  materialize/bind; dynamic results are shape-2 pairs by definition
  (universal entry, §6 of the design).
- Public wrappers: `nres=2`, forward the body's pair or wrap a shape-1
  body with a dummy lane. Per-callee `invoke` entry metadata gains the
  shape field; the entry-equivalence check (`:3281–3293` region) compares
  shapes, not home masks, under the flag.

**P1.5 — Bridges** *(resolves RVO3)*.
- MIR interpreter: `nres=2` is native — verify the ff-thunk path passes
  2 result slots for shape-2 protos (mir-interp handles it; the audit is
  on Lambda's proto construction only).
- `lambda-eval.cpp` (AST interpreter) ⇄ JIT crossings: the interpreter
  speaks resolved Items only; the crossing wrapper patches pending pairs
  on JIT→interp return and never produces pending on interp→JIT return.
  Audit every `FnEntryKind` for which side constructs the call.
- C helpers: unchanged (SF6 — helper `push_l` homes into the calling JIT
  frame; helper returns are caller-homed valid Items, never pending).

**P1.6 — Async/generator** *(resolves RVO4)*.
- `after_call_result` spill hook: assert it only ever sees resolved
  Items; the P1.3 next-call rule already forces resolution before any
  suspending call, so this is an audit + assert, not new emission. Add a
  forced-GC + async test that returns wide scalars across `await` and
  `yield` boundaries.

**P1.7 — Gate + measurement.**
- Differential harness (§1) green: baseline 100%, AWFY untyped+typed
  stdout byte-identical, JS suites unchanged, forced-GC sweep clean.
- Census re-run (`utils/analyze_scalar_homes.py`): adopt sites = **0**,
  `_scalar_home` params = **0** on the flag-on build; record the new
  home-share number (expected: low single digits — residual escape
  tests).
- MT7 budgets re-ratcheted (expect large drops; deltablue-class ~-35%).
- RVO9 check: inspect dumps for surviving `mov rX, 0` dummy-lane traffic;
  if not move-eliminated and measurable, note for P2's shape refinement.
- Release timing (fresh `make release`, never post-test binary): AWFY
  geo-mean must improve or hold; R26 canaries (tak, cpstak, fannkuch,
  primes, pnpoly) must not regress — any per-call check growth is a P1.3
  elision bug, not an acceptable cost.

### P2 — Shapes 3/4: native typed returns, error lane

1. Shape assignment: native-return-eligible signatures (existing
   `generate_native` admission) get `RETURN_SHAPE_NATIVE` when provably
   infallible, else `RETURN_SHAPE_NATIVE_ERROR`. **Admission stays
   conservative**: TE-17's `may_defect` fixed point does not exist yet
   (D6.1.3 footnote) — use the existing can-raise summary, default to
   shape 4, and leave admission widening as follow-up gated on D6.1.3.
2. Shape 4 lowering: `[native_value, error_item]`, lane 2 `ItemNull` = no
   error (RV9); caller check = one compare + branch; error arm feeds the
   TE-15 skip machinery exactly where the boxed ItemError arm does today.
   `FnErrorLane` moves `CONTEXT_ITEM → PAIR` for these entries;
   `Context::mir_return_lane` reads (`:16296`) drop out on register
   platforms and remain the Windows lowering (P3).
3. Un-deopt `can_raise`: `^E`-annotated typed functions stop forcing
   boxed-ANY returns. This is the F2 boxed-cross-call-math lever
   (spectralnorm ~48%) — measure it explicitly.
4. `INT64_ERROR` retirement *(scoped)*: JIT-emitted div/mod lowerings gain
   inline zero-divisor checks feeding the error lane; the C helper
   boundary (`fn_idiv_i`/`fn_mod_i`) keeps its sentinel contract for now
   (helpers can't return pairs portably) — full retirement is a
   follow-up tracked against v5 §5.8, not a P2 gate.
5. Debug assert per RV9: shape-4 lane-2 payload ∈ {ItemNull, ERROR-tag}.

**Gate**: differential harness green; typed AWFY suite: spectralnorm /
nbody-typed / pnpoly-typed improve (pnpoly typed-vs-untyped inversion from
R26 is the named canary — typed must win again); no untyped regression.

### P2.5 — LambdaJS adoption *(RV13; blocks P5 deletion)*

- Same descriptor + shape-2 pair emission through the shared emitter for
  the LJS function sites (§0 list); LJS `MirValue` tracking gains the same
  `maybe_pending` bit. JS numbers make shape 2 the dominant JS shape, so
  the census win is larger here; ICs are unaffected (LC1 — caches sit on
  property access, not the return ABI).
- Node/js262 gates: `make node-baseline` no regression (1492/3517
  anchor), js262 suite stable — rule 18 applies: failures are runtime
  bugs to fix, never test edits.

### P2.6 — Read the watermark effect *(RV14a; independent, land first)*

Smallest available win and the only new phase that needs no protocol change,
no ABI change and no design question answered. Sound under the **v1** protocol
as it ships, so it is not gated on `LAMBDA_RETURN_V3` at all.

1. `em_call_import` (`mir_emitter_shared.hpp`) currently decides the adopt on
   `scalar_mode != MIR_SCALAR_RETURN_NONE`, i.e. on `scalar_class` alone. Add
   the second condition: skip the whole snapshot/home/classify/restore
   sequence when `resolved.call.effects.number_stack ==
   JIT_NUMBER_STACK_PRESERVES`. The field is already written at `:180` and has
   **no reader today** — this is the reader.
2. Debug assert in `lambda_side_number_alloc` (`side_stack.h` / its definition)
   that fires when it is reached from an import declaring `PRESERVES`.
   Reading the flag turns an audited claim into a correctness dependency, so
   an untruthful row must fail loudly rather than silently dangle. Needs a
   per-call "current import declares PRESERVES" marker in the Context, set
   around audited calls in debug builds only — keep it out of release
   emission so the MIR stays build-identical (§3 last row).
3. Fix the ordering hazard while here: `vibe/Lambda_Design_Stack_API.md` §7.1
   declared `JIT_NUMBER_STACK_PRESERVES` as enum value 0 while the shipping
   header has `MAY_ALLOCATE = 0`. Corrected in the doc 2026-08-14; add a
   `LAMBDA_STATIC_ASSERT(JIT_NUMBER_STACK_MAY_ALLOCATE == 0, …)` beside the
   enum so silence can never come to mean "preserves".
4. Optional, same shape: direct Lambda→Lambda calls hard-code
   `JIT_NUMBER_STACK_MAY_ALLOCATE` (`:3562`) although a `fn`/`pn` restores its
   own extent at its epilogue and therefore does preserve the watermark from
   the caller's view. Harmless while unread; correct it with the reader.

**Gate**: baseline 100%; MIR emission fixtures; MT7 ratchet should *drop* —
re-ratchet. Expected: 30 registry rows declare the flag, 3 of them return
boxed Items and pay the ritual today; `lambda_name_id_to_item` alone is 78
havlak sites = **1,560 instructions, 7.6% of that module**.
**Size**: small — one condition, one assert, one static assert.

### P2.7 — C-helper convention *(RV14 / RV15 / RV16)*

Retires the largest census population (68% of adopt sites, §1.4 of the design
doc). Order matters inside this phase: **RV16 first — the reclaim is unsound
without it.**

**P2.7.0 — `fn_member` is 80% of what P2.6 left; it may be reachable without
any of the below.** *(measured 2026-08-14, post-P2.6)* Remaining helper-call
adopts: havlak 78 of 99, deltablue 148 of 182, richards 49 of 58, json 43 of
58 — **318 of 397 across the four, all `fn_member`**. Its entire
number-stack exposure is two lines, `path.size` and `datetime.unix`, both
`box_int64_value`. Both values sit comfortably inside the v5 int53 band (a
file size in bytes reaches 9 PB; unix-ms reaches year 285,000), so returning
`int` — which boxes **inline**, no number stack — rather than `int64` looks
like the semantically correct answer and a pre-v5 leftover from when `int`
was 32-bit. That change would make `fn_member` genuinely non-pushing, letting
it be marked `RESULT_SCALAR_STABLE`, with **no protocol change and no RVO11
dependency**. It is held pending a ruling because it moves an observable
type: `type(dt.unix)` is `int64` today (verified: `1745663445000` / `int64`).
Not in the RV14/RV15/RV16 scope; recorded here because it dominates the
measured residue and is two lines.

**P2.7.0 LANDED 2026-08-14** *(ruling: return `int`, and audit the rest)*.

Changed six boxing sites from `box_int64_value` (number-stack home) to `i2it`
/ `lambda_int_box_lane` (inline, total, saturating per v5 §4.9):

| site | value | why in-band |
|---|---|---|
| `fn_member` `path.size` | file size in bytes | int53 reaches 9 PB |
| `fn_member` `datetime.unix` | unix ms | int53 reaches year 285,000 |
| `lambda-data-runtime` `meta->size` | same, second path | — |
| `fn_shape` `dims[i]` | axis length | — |
| `fn_ndim` ×4 | literal `0`/`1`, `shape->ndim` | boxing the constant 1 as int64 took a number home |

Then marked `fn_member` `RESULT_SCALAR_STABLE | NUMBER_STACK_PRESERVES`,
which is what actually pays. Verified non-pushing before marking it, and the
decisive fact is not in `fn_member` at all: an `int64`/`uint64` **map field
comes back as `l2it(field_ptr)`**, pointing at the map's own persistent
storage — *"the map field is the persistent scalar owner; preserve its
payload address rather than copying wide values into a transient number
home"* — and native lanes box inline via `lambda_int_box_lane`. So member
reads never left a payload above the caller's watermark once those two
metadata arms stopped doing it.

**Cumulative effect, v1 baseline → now, with `LAMBDA_RETURN_V3` still off:**

| bench | adopt sites | module insns | home share |
|---|---|---|---|
| deltablue | 413 → **117** | 22,639 → **15,776** (−30.3%) | 38.7% → **16.1%** |
| richards | 156 → **58** | 10,209 → **7,935** (−22.3%) | 32.2% → **15.5%** |
| havlak | 292 → **136** | 20,470 → **16,815** (−17.9%) | 30.5% → **17.7%** |
| json | 183 → **97** | 11,852 → **9,780** (−17.5%) | 32.6% → **21.0%** |

**Gate: PASS** — 3719/3719, forced-GC 67/67, JS 347/347, and **no golden
file needed updating** despite the type change (`type(dt.unix)` is now `int`;
the corpus compares values, not types). MT7 ratchet unchanged at 16/16 with
zero tightenings: its probes don't exercise map member access, which is
exactly where this win lands — worth knowing that the ratchet is blind to
this class of improvement.

**Found during the audit, NOT changed — needs its own ruling.** `fn_int()`
(`lambda-eval-num.cpp`) still thresholds on **INT32**, not the v5 band: an
INT64 input above `INT32_MAX` returns `push_d(dval)` — a *float* — while
NUM_SIZED / UINT64 / DECIMAL inputs above `INT32_MAX` return
`box_int64_value`. Under v5 all of these are `int` when inside ±(2^53−1) and
`±inf` outside it, which is exactly `i2it`'s contract. That is a change to
`int()`'s observable result for large inputs, owned by
`Lambda_Semantics_Int_Type.md` §5 rather than by this doc, so it is recorded
here and left alone.

Other `box_int64_value` sites were checked and are correct as they stand:
array `ELEM_INT64`/`ELEM_UINT64` element reads, `scalar_storage_read`,
bitwise `band`/`bor`/`bxor`/`bnot`/`shl`/`shr` (which deliberately preserve
the wide lane — `bitwise_lane_preservation.ls` pins it), `type_contract`
conversions to declared int64/uint64, `fn_abs` on int64,
`lambda_item_resolve_pending`, and the PRNG state in `fn_random` (a full
64-bit mix — nearly changed it, and it would have been wrong).

### 2026-08-14 — P2.7.0b: the audit generalized, and it obviates most of P2.7

Attempting P2.7.1 started with a measurement, which redirected the work. After
P2.6 + P2.7.0 the split had **inverted** — return-side 340 (77%), helper-call
101 (23%), against v1's 351 / 757 (32% / 68%). So the population RV14/RV15/RV16
was designed to attack had already mostly gone.

Attributing the remaining 101 needed care: a "nearest preceding call" heuristic
blamed `pn_print` for 78, and spot-checking one site showed the adopted
register was actually defined by `fn_fill`. Re-doing it by **tracing the
adopted register's defining instruction** confirmed `pn_print` at 78 anyway —
but the near-miss is the lesson: attribute by definition, never by proximity.
(That is the third heuristic in this effort to mislead, after the `if`-node
call graph and the `get -> Item -> push_d` paths.)

`pn_print` returns `ItemNull` unconditionally and is declared `&TYPE_NULL`, so
its 78 adopts were pure waste. The cause was general, in
`jit_import_get_metadata` (`mir.c`): when an import has no audited `ret_class`,
it falls back to the sys-func table and maps `C_RET_ITEM` → `BOXED_ITEM` —
"a boxed Item comes back" — while **the declared Lambda return type sits in the
same row, unread**.

The fix is type-driven and general: a declared return type that cannot be a
wide scalar sets `RESULT_SCALAR_STABLE`. This narrows by *proof*, not by an
audit promise, and wide declarations fall through and keep their adopt. To
avoid two copies of the "which types are wide" rule (rule 13), the decision now
lives in `lambda_type_id_may_be_wide_scalar()` in `lambda.h`, which
`em_scalar_return_mode_for_type()` defers to and `mir.c` uses directly.

**Helper-call adopts: 101 → 23.** Cumulative from v1: **757 → 23, −97%.**

| bench | module insns (v1 → now) | home share |
|---|---|---|
| deltablue | 22,639 → **15,063** (−33.5%) | 38.7% → **12.6%** |
| richards | 10,209 → **7,751** (−24.1%) | 32.2% → **13.7%** |
| havlak | 20,470 → **16,419** (−19.8%) | 30.5% → **15.9%** |
| json | 11,852 → **9,504** (−19.8%) | 32.6% → **19.0%** |
| storage | 1,334 → **1,081** (−19.0%) | 34.8% → **21.6%** |
| sieve | 699 → **584** (−16.5%) | 29.9% → **17.8%** |

Gate: 3719/3719, forced-GC 67/67, ratchet re-baselined (2 further tightenings).

**The 23 survivors are legitimate**: `fn_fill` 12, `fn_abs` 4, `pn_push` 4,
`fn_slice3` 2, `pn_splice` 2, `fn_floor` 1 — helpers that really can return a
wide scalar (`abs` of an int64, `floor` of a large float).

**Consequence for P2.7.1 and the rest of P2.7 — read before starting it.**
The residue is now **340 return-side (94%) vs 23 helper-call (6%)**, and the
340 splits **190 `_b` wrapper / 150 internal body**. So:

- **RV14/RV15/RV16 now address 23 sites.** Per-binding slots, retiring the
  eager restore, the back-edge reclaim and the RVO11 liveness obligation were
  designed against 757. The cost/benefit has inverted with the population.
- **The 150 internal-body adopts are already implemented** — they are what P1
  deletes; they persist only because `LAMBDA_RETURN_V3` ships off.
- **The 190 `_b` wrapper adopts are P3's** (RV12 slot transport), now the
  single largest remaining block.

P2.7.1 is therefore left unstarted deliberately, not abandoned: it is a large
change to shared var/assign emission, with no standalone payoff, gated behind
liveness machinery that does not exist, in service of 6% of the residue. P3 and
flipping the flag are both worth more. RV14/RV15/RV16 stay ruled and recorded;
what changed is their priority, and the evidence for that is above.

### 2026-08-14 — P3 landed (RV12 slot transport); the home protocol is gone

`Context::mir_companion_slot` beside the existing `mir_return_lane` and
`mir_bitcast_scratch`, same contract (never GC-scanned, single-thread-owned,
dead outside its window). The public `_b` wrapper drops its trailing
`_scalar_home` parameter entirely, builds the pending pair as the register form
does, stores lane 2 into the slot, restores the watermark, and returns one
result. Callers resolve: generated code loads the slot inline; C callers go
through the new `lambda_item_resolve_pending_slot()`, which passes a resolved
Item straight through so no call site needs a test of its own.

Both C bridges converted: `lambda_dynamic_invoke_by_count`'s function-pointer
cast loses its trailing operand and resolves, and the
`fn_call_boxed_N_into` trampoline family does the same through its macro. The
trampolines keep their own `result_home` parameter (now unused) so generated
call sites needed no change.

**Descriptor change: transport is now a first-class field.** `FnReturnShape`
says whether a companion exists; the new `FnCompanionTransport`
{`NONE`, `HOME`, `RESULT_REG`, `CONTEXT_SLOT`} says where it lives — RV10a's
"separate axes" and RV12's "companion location, not companion register" made
explicit. `em_companion_transport()` derives it once from shape plus
C-reachability.

**Two bugs the conversion surfaced, both caught by machinery added earlier:**

1. `em_returns_result_pair()` was mask-based (`shape is pair && mask == 0`).
   A slot-transport entry also has a zero mask, so the wrapper epilogue emitted
   a **two-operand `ret` from an nres=1 function** — a `MIR_finish_func` crash.
   Fixed by making the predicate transport-based, which is what it should
   always have been.
2. Shape 4 (`NATIVE_ERROR`) counts as a pair under RV1, but **P2 is not
   implemented** — native `^E` bodies still signal through
   `Context::mir_return_lane` with one MIR result. The descriptor therefore
   promised a companion the emitter never produced. This surfaced as
   `em_assert_callee_result_count` aborting with *"call site expects 2
   result(s), callee declares 1"* on six error-path tests, rather than as a
   wrong answer — which is precisely why P1.4 added that check. Shape 4 now
   stays on the v2 transport until P2 lands.

**Effect — the design's goal, reached.** Under v3, `_scalar_home` parameters
across the AWFY set: **0**. Return-side adopts: **0**.

| bench | v1 | flag-off today | **v3 + P3** |
|---|---|---|---|
| deltablue | 22,639 / 38.7% | 15,063 / 12.6% | **15,227 / 0.9%** |
| havlak | 20,470 / 30.5% | 16,419 / 15.9% | **16,276 / 0.7%** |
| json | 11,852 / 32.6% | 9,504 / 19.0% | **9,129 / 0.7%** |
| richards | 10,209 / 32.2% | 7,751 / 13.7% | **7,544 / 0.3%** |

(Home-protocol share of emitted MIR. The 9–39% the census opened with is now
under 1% everywhere.)

**Gates: 3719/3719 in BOTH configurations.** Forced-GC sweep 67/67, JS
347/347, MIR emission 38/38, ratchet 16/16.

**Test infrastructure gained a convention axis, mirroring the sidecar work.**
`mir_budgets.json` lookup now tries `<platform>-<config>-v3` before the v2
chain: a probe that shrinks under v3 passes and reports its tightening, one
that grows fails loudly and asks for an explicit entry — the same "no silent
slack" rule the file already applied to platforms. Two probes grew and got
explicit `darwin-debug-v3` entries (`lambda_scalar_home_donation` main +6, as
the caller now resolves instead of donating; `lambda_cow_nested_store` +1), and
`closure_scalar_rehome` gained a v3 check group asserting the wrapper has no
home and reaches `ret` through the pending classify.

### 2026-08-15 — P2 attempted; shape 4 PARKED with a reproducible defect

Shape 4 as a register pair is written end to end — `em_companion_transport`
un-parked for `NATIVE_ERROR`, `mir_body_returns_pair` extended to
`RETURN_LANE_ERROR`, a pair epilogue and overflow exit, `em_call_direct`
handing lane 2 back as `call_result.error`, and both the direct caller and the
`_b` wrapper reading a register instead of `Context::mir_return_lane`. RV9's
`ItemNull` encoding is implemented for the pair (the v1 context lane keeps its
`0`/`BT` form; both are one instruction, so the two encodings coexist per
transport rather than being unified for its own sake).

**It does not work, and the failure is sharp: `int^` and `float^` return
correctly, `int64^` does not.** For an `int64^` signature the caller's value
lane comes back as `ITEM_NULL` bits. Diagnostics run, in order:

| diagnostic | result | conclusion |
|---|---|---|
| dump the callee | `func i64, i64, …` / `ret %r17, %r6`, %r6 = ItemNull on success | callee emission is correct |
| dump both protos | `proto i64, i64, p:a, i64:a` | nres agrees at every call site |
| `ret first, first` | caller takes the error branch | **lane 2 is read correctly** |
| `ret first, ITEM_NULL` (const) | value still `ITEM_NULL` | lane 1 is not observed |
| `ret 777, ITEM_NULL` (both const) | value still `ITEM_NULL` | lane 1 is not observed *even as a constant* |
| `ret 111, 222` | error branch taken | lane 2 correct again |
| flag-off control | correct value | regression is P2's, not pre-existing |

So the caller observes lane 2 faithfully and never observes lane 1.

**Follow-up round (2026-08-15), four more hypotheses eliminated:**

1. **`lambda_value_rep(LMD_TYPE_INT64)` — dead.** It is `VALUE_REP_I64`,
   *identical* to `LMD_TYPE_INT`, so `em_mir_type_for_rep` gives `MIR_T_I64`
   and `em_value_class_for_rep` gives `NON_GC_SCALAR` for both. No rooting, no
   representational difference. The original suspicion was wrong.
2. **The `int^` comparison was FALSE, and this is the important correction.**
   Dumping the three signatures with shape 4 parked:

   | signature | emitted body | native? |
   |---|---|---|
   | `int^` | `func i64, i64, p:runtime, i64:%p1` | **no** — a boxed body on shape 2 |
   | `i64^` | `func i64, p:runtime, i64:%p1, p:_scalar_home` | yes |
   | `float^` | `func d, p:runtime, i64:%p1, p:_scalar_home` | yes |

   `int^` never exercised shape 4 at all — it takes the shape-2 pair, which
   works. So the real evidence is narrower than "int works, int64 doesn't":
   **the only shape-4 case that ever worked is `float^`, whose two lanes sit
   in different register classes (`d` + `i64`); the failing case is the one
   with two same-class integer lanes.**
3. **Operand order — dead.** Swapping the callee's `ret` operands moves the
   value to lane 2 and the error to lane 1, exactly as written: the caller
   reads operand 0 as result 0. There is no ordering mismatch.
4. **Result liveness across the following helper call — dead.** Copying both
   result registers into fresh registers as the first thing after the call
   changes nothing, so the value is already wrong *at* the call, not clobbered
   by the `box_int64_value` that follows.

What survives: shape 2 also returns two same-class `i64` results and works, so
"two integer results" is not broken in general. The difference between the
working shape-2 pair and the failing shape-4 pair is not in the descriptor,
the rep, the operand order, or downstream liveness. The next step is
machine-level rather than MIR-level — disassemble the generated code for an
`i64^` callee/caller pair and compare the actual result-register handoff
against the shape-2 case; MIR-level inspection has been exhausted.

### 2026-08-15 — ROOT CAUSE FOUND AND FIXED; shape 4 is LIVE

The machine-level step was never needed — the missing observation layer was
**post-simplify MIR**, not disassembly. Diagnosis chain, with what each step
eliminated:

1. **Two cheap discriminators first.** The failure reproduces under
   `--mir-interp` — so it is NOT a mir-gen lowering/regalloc/inlining bug;
   interp and gen share only the MIR data structures and `MIR_simplify_func`.
2. **Runtime probes beat inference.** A temporary `lambda_dbg_lanes` helper
   emitted on both sides of the boundary gave ground truth in one run: the
   callee holds `(123456789012345, ItemNull)` at the `ret`; the caller
   receives `(ItemNull, ItemNull)`. With `ret 111, 222` the caller receives
   `(222, 222)` — **both result slots get the LAST ret operand**. (This also
   explained every ambiguous earlier diagnostic: `ret 777, ITEM_NULL` showed
   value=2⁵⁶ *and* e=null only under "both ← op2".)
3. **A faithful standalone vendor repro works.** Forward-routed call, two ret
   sites, helper call before ret, scratch dance, both engines — all correct.
   So the trigger was not in the mechanics but in something this function's
   *content* feeds them.
4. **The missing layer: `LAMBDA_MIR_GEN_DEBUG`.** The finalized-MIR artifact
   is PRE-simplify; both engines execute POST-simplify code. A new env-gated
   hook in `jit_init` (kept permanently — this gap cost the whole outage)
   dumps gen-internal stages. It showed the executed callee epilogue as:

   ```
   mov  t26, %r24     ; value  -> t26
   mov  t26, %r6      ; error  -> t26   (clobbers the value)
   ret  t26, t26
   ```

   against the working shape-2 case's `ret t28, t11`.

**Root cause — a two-stage vendor interaction, Lambda-triggered.** In
`MIR_simplify_func` (shared by interp and gen, hence both engines):

- `simplify_op` passes constant operands through **value numbering**
  (`vn_add_val`), which returns the SAME temp for the same (type, value) —
  so a ret whose operands are the *identical constant twice* becomes
  `ret t, t`.
- `make_one_ret` then merges all rets by using the **last** ret's operands as
  the return-value homes for every other ret: each earlier ret is rewritten
  to `mov home[j], op[j]` + jmp. With `home[0] == home[1] == t`, the last
  move wins and every return in the function collapses to its lane-2 value.

The last ret in every generated function is the stack-overflow exit, and
P2's shape-4 overflow exit emitted `ret ITEM_ERROR, ITEM_ERROR` — the
identical-constant pair. Shape 2's overflow exit is `ret ITEM_ERROR, 0` —
distinct constants, distinct temps — which is the entire reason shape 2
worked and shape 4 failed. Registers are never value-numbered, so
register-operand rets are immune; the hazard is exactly **a multi-result
`ret` whose constant operands repeat, in a function with more than one ret**.

**Fix (Lambda-side, one operand):** the overflow exit's value lane is dead
once lane 2 carries the error, so it now returns `0` (or `0.0` for a double
lane) — `ret 0, ITEM_ERROR`. A comment at the emission site records the
invariant: *never emit identical constant pairs in any multi-result ret*.
Shape 4 is un-parked; the earlier `lambda_value_rep` suspicion and the
"machine-level next" recommendation are both superseded.

**Vendor note (rule 16).** `make_one_ret`'s assumption that the last ret's
operands are distinct locations is arguably an upstream defect (value
numbering legitimately produces `ret t, t`). Not patched here per rule 16;
if it recurs in another guise the upstream conversation should cite this
analysis. The Lambda-side rule above is sufficient and principled — the
dead lane has no business carrying a meaningful constant anyway.

### 2026-08-15 — P2 residue measured: the `can_raise` deopt is a *raise-arm* deopt

P2's third item ("un-deopt `can_raise`: `^E`-annotated typed functions stop
forcing boxed-ANY returns") turns out to be **half already delivered and half
mis-stated**. Measured on emitted signatures, `LAMBDA_RETURN_V3=1`:

| body | emitted | native? |
|---|---|---|
| `fn a2(x: float) float^ { x * 2.0 }` | `func d, i64, …` | **yes** — native value lane + error lane |
| `fn b2(x: float) float^ { if (x<0.0) { 1.0 } else { x*2.0 } }` | `func d, i64, …` | **yes** |
| `fn b1(x: float) float^ { if (x<0.0) { raise error("n") } else { x*2.0 } }` | `func i64, i64, …` | **no** — boxed |
| `fn a3(x: float) float { … }` (control, no `^E`) | `func d, …` | yes |

So **the `^E` annotation alone does not deopt anything** — `a2`/`b2` already
return a native `d` with the error on lane 2. What deopts is a **`raise` in
the body**: `b1` and `b2` are the same control shape and differ only in one
arm, and only `b1` goes boxed. `generate_native` never consults `can_raise` at
all; the rejection happens upstream of the MIR proof, because a raising branch
widens the expression's inferred type to ANY, after which
`function_return_may_defer` cannot prove a native lane
(`mir_proc_return_values_prove` has the matching explicit
`case AST_NODE_RAISE_STAM: return false`).

Under shape 4 that conservatism is now provably unnecessary: a raise arm exits
through **lane 2** and contributes nothing to the value lane, and the
admission is self-consistent — if `can_raise` holds and a native return is
admitted, `return_lane_kind` becomes `RETURN_LANE_ERROR`, so the error lane
that makes it safe is exactly the one that exists. (A raising body without
`^E` is rejected earlier by the type checker, so the case cannot arise.)

**AST-level fix attempted 2026-08-15 — implemented, tested, REVERTED. The
conservatism protects a second thing, and the code already said so.**

The narrowing itself is easy and works: `build_if_expr` joins branch types,
and a `raise` arm's type is the *raised value's* type, so a raising branch
makes `then_type_id != else_type_id` and the join falls to `TYPE_ANY`. Adding
an `ast_branch_diverges()` test (raise directly, through `PRIMARY`, or as the
last item of a `LIST`/`CONTENT` block) and taking the surviving arm's type
removed the deopt exactly as predicted — `b1` went from `func i64, i64, …`
to `func d, …` under v2 and `func d, i64, …` under v3, alongside `b2`/`b3`.

**But it silently loses errors.** With `fn b1(x: float) float^ { if (x<0.0)
{ raise error("n") } else { x*2.0 } }`, `let r^e = b1(-1.0)` returned
`[nan, null]` instead of `[null, error]` — and `nan` is the tell, because
`it2d()` maps an ERROR Item to `NaN`. Verified against a pre-change control on
the same binary, and it fails **identically under v2 and v3**, so it is not a
transport problem.

The callee is correct in both conventions: the raise arm emits
`dmov %r5, 0.0; mov %r6, <error>; jmp L2` — `emit_function_error_return`
setting the error lane and branching to the return label — and the `_b`
wrapper's merge (`bne L26, %r3a, ItemNull`) reads it properly. What breaks is
the **consumer**: `let r^err = …` expects the Item-valued success/failure join
that a boxed return provides. Give it a native value lane plus a separate
error lane and it takes the value lane and converts, so the error becomes
`NaN` and `^err` sees nothing.

That is precisely the invariant already written at the top of
`mir_expr_proves_native_return_lane`:

> *"A can-raise call is an Item-valued merge at every expression boundary:
> success and failure share one register until `^`/`^err` consumes it. Its
> declared payload type must not by itself certify a raw return lane."*

**So the un-deopt is not an AST-only change.** It needs both halves,
sequenced:

1. teach `^` / `^err` consumption (and any other error-join boundary) to read
   a two-lane native result — value from lane 1, error from lane 2 — instead
   of destructuring a boxed join; *then*
2. narrow the type join so the admission proof stops seeing ANY.

Doing (2) without (1) is a correctness regression, which is what this attempt
demonstrated. The `ast_branch_diverges` helper and the narrowing are ~35 lines
and can be re-applied verbatim once (1) exists; the revert is clean.

**Second attempt 2026-08-15 — (1) tried, ALSO reverted; the consumer surface
is wider than the two obvious guards.**

The infrastructure for (1) looked already present. `transpile-mir.cpp` has a
matched pair of witnesses — `mir_direct_native_scalar_can_unbox` (non-raising:
consume the lane directly) and `mir_direct_native_scalar_item_can_unbox`
(can-raising: *"publishes a boxed join … only the ordinary non-propagating
scalar consumer may consume that join as an Item and reopen the successful
lane"*). The second already excludes `call->propagate` and
`mt->in_handler_operand`, so `^err` looked like a missing third exclusion.

Implemented exactly that: an `in_error_destructure` flag on `MirTranspiler`
beside `in_handler_operand`, scoped across both RHS arms of
`transpile_error_destructure`, and added to that witness plus the native-unbox
site in the call emitter. With the narrowing re-applied on top, results got
**worse, not better**:

| case | expected | got |
|---|---|---|
| `b1(1.0)` success | `[2, null]` | `[2, null]` ✓ |
| `b1(-1.0)` raises | `[null, error]` | `[nan, null]` |
| `i1(5)` success, `int^` | `[10, null]` | `[inf, null]` |

The `int^` **success** path breaking is the decisive signal: `inf` is the
int-lane saturation sentinel, i.e. a raw i64 lane read as an Item (or the
reverse). So the failure is not confined to the error join at all — narrowing
the type changes how the *value* is carried at consumer boundaries that the
two guards never see.

Why the guards did not bite: the destructure reads the RHS's **AST node type**,
which the narrowing already rewrote at build time. A transpile-time flag can
gate the unbox *witnesses*, but it cannot retract a type the consumer has
already been handed. Any real (1) has to make the *call expression's* type at
non-`^`/`^err` boundaries remain the value-or-error join — which is a typing
change, not a guard — or thread a two-lane value through the consumers rather
than a single register.

### 2026-08-15 — USER RULING: the if-join types as `T | error`, LANDED

The user ruled the structural answer to both failed attempts: `build_if_expr`
must not collapse differing arms to ANY — the join is the **union of the
branch contributions**, and for a raise arm that is **`T | error`**. This
keeps the error-ness signal that ANY carried implicitly (whose loss broke
attempt 1) while recording the value lane's exact type (which ANY erased) —
the type-level foundation the eventual two-lane consumer work keys on.

**Implementation.**

- `lambda_type_is_union()` — shared predicate in `lambda-data.hpp` beside
  `TypeBinary` (15 sites previously open-coded the kind check; rule 13).
- `build_if_expr`: each arm CONTRIBUTES a type; a diverging (raise) arm
  contributes `&TYPE_ERROR` — detected structurally via `ast_branch_diverges`
  (raise directly, through PRIMARY, or as a block's last item), not via the
  raise node's own type, which is the raised *value's* type and can be
  non-error. Substitution happens BEFORE the numeric join, so `raise 1.0` can
  never numeric-join into a bare float. When contributions differ and one is
  the error contribution, the join is a `TypeBinary` UNION of the two; ANY
  still absorbs.
- Two admission sites hardened so the union is treated like ANY, not like a
  concrete carrier (`mir_decl_type_id(union)` is `LMD_TYPE_TYPE ≠ ANY`, which
  would otherwise *accidentally admit* a native return — attempt 1's failure
  reachable by a new road):
  - `function_return_may_defer`'s early-exit skips union body types, falling
    through to the expression proof (which correctly fails on the raise arm);
  - `infer_boxed_return_mode` maps `LMD_TYPE_TYPE` → ANY → DYNAMIC, because a
    `float | error` boxed return can carry a frame-backed double — mode NONE
    would skip the adoption and leak a dangling payload past the watermark
    restore.

Doctrinal support found in-tree: the shape-storage code already rules
*"unions retain their runtime Item tag"* (structured kinds → `LMD_TYPE_ANY`
lane), so union-in-expression-position = boxed is established practice, not a
new convention.

**The general `T1 | T2` join was implemented, measured, and narrowed.** With
unions for ALL differing arms, the corpus failed **95 tests** — not for
representation reasons, but because the **E208 containment checker** could
suddenly see error-capable branches that ANY had hidden
(`function 'child_width' may return error from call to 'float'; contain it
with 'or'…` — `graph_layout` et al.). That is the union being *more correct*:
those programs genuinely let a `float()` error escape uncontained, and ANY was
concealing it. But which programs compile is a language-surface decision, so
the join now unions **only when a diverging arm is present**; plain differing
arms keep ANY, with the evidence recorded in a comment at the join. Widening
to the general union join is a ready follow-up ruling whenever the stricter
E208 surface is wanted — the mechanism is in place and one condition gates it.

**Gates: 3720/3720 in BOTH flag states.** The motivating cases verified:
`float^`/`int^`/`i64^` raise-arm functions — success and error paths all
correct, and the raising bodies deliberately STILL boxed (the deopt remains by
design until the consumer work lands; what changed is that the type now says
`float | error` instead of ANY, so that work has an honest signal to key on).

### 2026-08-15 — RV9 un-deopt LANDED on the float lane

With the union in place the un-deopt became tractable, and it is now done for
`float^`: a `^E` fn whose body types as `float | error` **returns natively**.
Under v3 those functions are exactly **shape 4** — `func d, i64` — the first
`NATIVE_ERROR` bodies produced from ordinary user source rather than from the
convention's own tests. Under v2 the same functions return `d` with a scalar
home. The gate golden is byte-identical in both states.

**Admission** (`function_return_may_defer`): a can-raise signature whose body
is a `T | error` union with `T = float` returns false — i.e. is admitted. The
raise arms exit through `emit_function_error_return` on the error lane and
contribute nothing to the value lane, whose type the union records precisely.
This is the same reasoning that already admits a tail-raise proc.

**FLOAT only, deliberately.** The `d` return lane has a universal `it2d` fixup
for a boxed value arm (the if-merge is boxed — the union's `type_id` is
`LMD_TYPE_TYPE`), so a boxed success arm still lands correctly on the lane.
The INT-family native lanes have **no** boxed-to-lane conversion at the return
boundary: a boxed Item would flow onto the i64 lane as tag bits and saturate
to the inf sentinel — the exact havlak failure mode. Extending to int/i64
requires that conversion first; `twice(x: int) int^` stays boxed and the
regression test pins that as intended, not as an oversight.

**The consumer bug this exposed — the join carrier lie.** Admission alone
produced `nan` on every error path. Cause: `call_error_lane` merges the value
and error arms into one **boxed** register, but the call's declared type still
names the success scalar (`float`), so generic consumers re-boxed the join
through the scalar lane and sent the ERROR arm through `it2d` — NaN. This is
the same class of carrier lie that `routed_to_inferred_slow_body` already
publishes via `last_call_returned_boxed_item`, so the fix extends that
existing publication rather than adding a parallel mechanism: an error-lane
merge publishes too, and `transpile_box_item` passes a published join through
untouched. `f(...)^` is excluded — propagate consumes the error at the call
site, so the declared scalar type is truthful there — mirroring how every
other reopen witness excludes explicit propagation.

Regression test: `test/lambda/raise_arm_native_return.ls` — both arm orders,
destructure / `or` / arithmetic-on-recovered consumers, plus the int case that
must stay boxed. A regression shows up as `nan`, not as a crash, which is why
the test asserts values rather than merely running.

**Gates: both flag states green.** What remains of the original deopt is the
INT-family lane, blocked on the return-boundary conversion above — that part
of the earlier "not a small change" conclusion still stands. It touches AST typing, the native-admission proof, and
every consumer boundary that reopens a scalar lane, and its acceptance gate is
typed-AWFY timing with the `pnpoly` canary. Both reverts are clean and the
tree is green; the two attempt records above are the value delivered — they
convert "un-deopt `can_raise`" from a one-line-sounding plan item into a
scoped piece of work with its blast radius mapped.

**Deliberately not implemented in this session.** The remaining change lives in the
native-admission proof, which is precisely where R26's per-call check taxes
came from, and the plan gates it on typed-AWFY timing with `pnpoly` as the
named canary plus a fresh `make release` (test runs clobber `lambda.exe` with
a debug build). Starting a proof change at the end of a long session and
leaving it unmeasured is the wrong trade; the measurement above is the
expensive part and is now done. **Correction to the earlier "two candidate shapes" note above:** (a) and (b)
are not alternatives — both are downstream of the consumer change. Either way
of narrowing the admission is a regression until `^`/`^err` can read two
lanes. See the attempt record for the evidence.

**Also still open (both explicitly follow-ups in the plan):** the RV9 debug
assert (lane-2 payload ∈ {`ItemNull`, ERROR-tagged}) — note the emitter must
keep MIR build-identical, so this cannot be debug-only *emission*; and the
scoped `INT64_ERROR` retirement, which the plan already records as tracked
against v5 §5.8 rather than as a P2 gate.

**Verified:** `i64^` and `float^`, success and error paths, all four correct;
the six error-path tests that caught the original regression pass; both
configurations gate at **3720/3720** (flag-on with shape 4 live, and the
shipping default). The diagnostic probes are removed; the
`LAMBDA_MIR_GEN_DEBUG` hook stays.

One ordering defect was found and fixed along the way and is worth keeping
regardless: P2 initially deferred shape 4's result publication (as shape 2
must, for RV4.1), which moved it after `after_may_gc_call` — where the root
machinery reloads live values. Shape 4's value lane is a native scalar that is
never pending, so it must publish at the original point; `callee_defers_publication`
now excludes it. This did not fix the `int64^` defect, so it was not the cause.

**Shape 4 is therefore parked back on the v1 context-lane transport** (a
single `em_companion_transport` exclusion), which restores v3 to its
post-P3 state. The pair code is left in place, dormant and commented, since it
is correct as far as the dump shows and the remaining question is narrow.
Next step for whoever resumes: instrument the generated code around an
`int64^` call to see what the first result register actually receives, and
compare the register allocation against the working `int^` case — the
difference must be in how an `int64` value lane is classified
(`lambda_value_rep(LMD_TYPE_INT64)`), not in the two-result convention.

### 2026-08-15 — js_tune6 emission growth: NOT ours (incoming merge)

*(Resolved after the note below was written; kept because the reasoning is
the useful part.)* The growth is from an **in-progress merge of 11 commits
from `origin/master`** that landed in the working tree mid-session, touching
`js_mir_analysis.cpp`, `js_mir_expression_lowering.cpp`,
`js_mir_function_class_lowering.cpp`, `js_mir_statement_lowering.cpp`,
`js_runtime*.cpp` and more. That is exactly why it is flag-independent,
deterministic, and unattributable to any P2 edit — it is not a P2 edit. The
same merge explains the baseline count moving 3719 → 3720 (a Test262 runner
preflight test appeared).

Two consequences worth recording. First, the instinct that saved this from
becoming a wrong conclusion was refusing to lift a 0%-slack budget for growth
nobody had explained — had it been raised, the merge's emission change would
have been silently absorbed into a Return_Value commit. Second, **the gates
run after the merge appeared were measuring a partially-merged tree**, so the
final P2/P3 numbers in this log should be re-taken once the merge is
resolved. The pre-merge gates (through P3, commit `c565f1e7d`) stand.

**Closed once the merge landed.** With the merge committed the growth became
attributable, and to the incoming work's own stated change: Tune9 **P2.1**,
*"emit the existing guarded named load/store feedback slots in normal release
builds"* — IC feedback slots are precisely what adds instructions and scalar
homes to `js_main` (+176 insns, `scalar_homes` 6 → 15), and that doc's
checklist already records a measured `nbody` diagnostic recovery for it. So
the growth is intended by the commit that caused it; the ratchet simply was
not re-baselined there.

`darwin-debug` for that probe is now re-baselined to the post-merge numbers
(module 14,587; `js_main` 5,686 insns, roots 11, root_stores 773,
scalar_homes 15, safepoints 2,102), and the `darwin-debug-v3` entry added
earlier was **removed** — it held identical values, and the lookup falls back
to `darwin-debug`, so keeping it would have been a duplicate to maintain.
Post-merge gate: **3720/3720**, ratchet 16/16 with zero slack.

### (original note, superseded above) unattributed js_tune6 emission growth

`js_tune6_exact_collection` grew **+176 module instructions** (14,411 →
14,587), with `scalar_homes` 6 → 15, `root_stores` 806 → 773, `safepoints`
2,111 → 2,102. Deterministic across runs and **identical in both flag
states**, so it is not a v3 effect. It was green at the P3 gate, so it
appeared during the P2 edits — but every P2 edit is either `#if
LAMBDA_RETURN_V3`-guarded or gated on a `companion` value that is
`HOME`/`NONE` at flag-off, and the one unguarded change
(`mir_error_lane_no_error_op`) is behaviourally identical to the constant it
replaced. The `+176 ≈ 9 × 20` reading that suggested nine new adopt clusters
is wrong: the whole dump contains exactly **one** adopt.

**The budget was deliberately NOT lifted.** MT7 is a 0%-slack ratchet whose
stated contract is that growth is reviewed and justified in the same commit;
raising it for growth nobody has explained would convert the one mechanism
that noticed into noise. Recorded here as the open item instead.

**P2.7.1 — Per-binding slots for wide-capable mutable locals (RV16).**
- No per-source-binding scalar storage exists today. `MirScalarHomeBinding`
  (`mir_emitter_shared.hpp:459`) maps a MIR *register* to a colored home —
  transient-value coloring, not binding ownership — and `BindingStorage`
  (`value_rep.h`: `REGISTER`/`SCOPE_ENV`/`MODULE`/`PERSISTENT`) has no
  consumer in the emitter at all. Both need extending, not just reading.
- A `var` binding whose type is wide-capable gets one number slot at its
  declaration, held for its scope. Assignment copies the payload into that
  slot and retags — the same operation `lambda_item_adopt_scalar_home`
  performs, but statically typed and therefore a plain store when the
  binding's inferred type is wide (`var acc = 0i64`), with the 2-instruction
  test only for ANY-typed mutable bindings.
- Note for the census at this phase: the adopt does not disappear so much as
  **move** — from once per helper call (dynamic, 20 instructions) to once per
  wide assignment (usually static, one store). Expect the site count to fall
  far more than the instruction count.

**P2.7.2 — Retire the eager per-call restore (RV14/RV15).**
- Delete the snapshot/home/adopt/restore sequence in `em_call_import`
  (`:3194–3219` region). A C helper owns no watermark, so its push lands in
  the calling frame's extent and the returned Item is already caller-homed.
- Add the back-edge reclaim: for a loop whose body emitted a call with
  `scalar_class != SCALAR_RETURN_NONE`, restore `side_number_top` to the
  loop-entry watermark at the back edge.

**P2.7.3 — Close RVO11 before emitting the restore.**
- RV16 settles loop-carried *source bindings*. What remains is whether a
  compiler-generated wide temporary can be live across a back edge. Establish
  this positively in the emitter; do not assume it. A wrong answer here is a
  use-after-free, not a slowdown, so P2.7.2's reclaim must not ship until
  P2.7.3 answers.
- **Survey done 2026-08-14; there is nothing existing to hang it on.**
  `MirFrameState.root_backedge_reloads` is declared with no writer and no
  reader — the third declared-but-unconsumed field found in this area, after
  `JitCallEffects.number_stack` (which P2.6 gave a reader) and
  `BindingStorage` (which P2.7.1 must give one). So the emitter does not
  track loop structure today. Emitting the restore is the easy half — the
  back edge is a `MIR_JMP` the loop lowering already writes.
- Recommended placement: **`em_finalize_scalar_homes`**, which already
  computes home live ranges and interference at function finalization. Emit
  the back-edge restore unconditionally during lowering, then delete any
  restore whose loop body has a home live range crossing its edge. This
  inverts the obligation from "prove nothing crosses" to "detect what crosses
  and back off" — the safe direction. **Sequencing consequence:** the
  coloring pass must survive long enough to validate these reclaims, so it
  cannot be deleted in the same step that RV16 makes it redundant.

**Gate**: baseline + forced-GC sweep, **plus a peak-side-stack measurement** —
correctness alone does not gate this phase, because the whole point of RV15 is
the space bound. Required probes: a million-iteration untyped loop over
`int64` array elements, and the same over `int64` map fields; record peak
`side_number_top` displacement, which must stay O(1) in the iteration count.
Census re-run: helper-call adopt sites → 0.

### P3 — Slot transport *(RV12; not Windows-specific)*

Re-scoped 2026-08-14: this is not a platform-coverage phase. Per the design
doc §7 it is what lets C-reachable entries speak the convention on *every*
platform — the public `_b` wrappers reached through the `fn_call_boxed_N_into`
trampolines and `fn->invoke`, plus the `fn_callN_into` dynamic dispatchers —
and per §1.4 that is **18% of all adopt sites**, the largest block shape 2
alone cannot reach. Windows CI is a consequence, not the motivation.

- Add `Context::mir_companion_slot` beside `mir_return_lane` (`:1955`) —
  same non-GC-scanned, single-thread-owned contract as the existing
  double-bits scratch cell documented there.
- Emission under `_WIN32`: shape-2 wide arm stores payload to the slot,
  pending arm loads it; shape-4 error lane keeps the existing
  `mir_return_lane` mechanism (it already is this pattern). `nres` stays
  1 on Windows; everything else — descriptor, pending tag, resolution
  protocol — is byte-identical (RV12).
- **C-reachable entries on every platform** (the re-scoped part): the `_b`
  wrapper returns a pending Item and writes the payload to the slot, and the
  C side resolves. This removes the trailing `uint64_t* result_home` from
  `fn_call_into` / `fn_call0..3_into` (`lambda.h:1141–1145`), from
  `lambda_dynamic_invoke_by_count`'s function-pointer cast — which validates
  and forwards it today — from `LAMBDA_SCALAR_HOME`, and from ~14 JS
  transpiler entry points (`js_transpiler.hpp`). Also drop
  `dyn_scalar_home` in the dynamic-dispatch lowering
  (`transpile-mir.cpp:16527`), which is only there because that path is
  C-mediated end to end.
- Single-live discipline crosses into hand-written C here: a converted entry
  must leave the slot correct on return, including across its own internal
  calls. Weaker than compiler enforcement — add a debug generation counter
  on the slot so a stale read traps.
- Gate: Windows CI build + baseline; no mainline (SysV/aarch64) emission
  change beyond the C-reachable entries above; census re-run showing the
  wrapper-side return adopts → 0.

### P4 — Formal/doc closure

- Already staged: D5.2.1v2 / D2.7.2v2 / D8.4.2v2, plus **D5.2.2v2** (locals
  as destination-owned storage, RV16) and **D5.2.3** (watermark ownership
  selects the transport, RV14/RV14a/RV15) — spec **1.22.0**. At this point
  flip the Appendix A footnotes from "decided, v1 ships" to landed status,
  record the D5.2.2 discard-home/tail-forward clause lapse, and close
  **DO24** if P2.7.3 settled it.
- Update `Lambda_Design_Stack_Frame.md` SF14 entry with the v3
  supersession note; refresh LR/JS overview docs' return-ABI sections.

### P5 — Delete v2 machinery *(gated on P1 + P2 + P2.5 + P2.7 + P3)*

Gating widened 2026-08-14. The original list assumed shape 2 alone emptied
the home machinery; §1.4 of the design doc measures that it empties 14% of
it. `em_scalar_home_*` and `em_finalize_scalar_homes` keep real callers until
**P2.7** (helper side, 68%) and **P3** (C-reachable entries, 18%) have both
landed — those are the phases that actually leave the apparatus with no
remaining caller, and RV16's per-binding slots subsume the coloring pass
rather than the deletion removing it.

- Remove the flag (v3 becomes the only convention) and delete:
  `em_adopt_scalar_item_value` / `em_adopt_scalar_item`, the
  `em_scalar_home_*` family + `em_finalize_scalar_homes` coloring,
  `lambda_item_adopt_scalar_home` + import + rehome counters,
  `scalar_home_lane_mask` / `may_need_caller_scalar_home` fields and every
  consumer, `_scalar_home` naming, dyn/dispatch home paths, and the v2
  arms of the return/call lowerings.
- Final census run: home instructions = 0; final MT7 ratchet; memory +
  ledger sync.

---

## 3. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Convention mismatch at any bridge (the v27 havlak bug class) | P1.1 single descriptor + no-local-computation assert; entry-equivalence check compares shapes; L1 cache key bump |
| Pending Item leaks to memory | 0x1E out-of-range tag ⇒ loud bounds failures in release; P0.3 debug tripwires at every accessor choke point; forced-GC sweep in every gate |
| Per-call check tax reappears (R26 pattern) | `maybe_pending` is descriptor-driven and cleared on dominating resolution; R26 canaries in P1.7/P2 gates are hard failures |
| Mixed-convention linking during rollout | whole-module flag; cache version bump; dynamic calls always via descriptor-checked entries |
| Shared-emitter deletion breaks LJS | P5 explicitly gated on P2.5; until then v2 code paths stay compiled |
| Async/generator suspension observes lane 2 | next-call rule resolves before any suspending call; P1.6 assert + dedicated tests |
| Dummy lane-2 traffic on shape-2 fast path (RVO9) | measured in P1.7 dumps; escalate to shape refinement only if visible in timing |
| GVN multi-out exclusion (RVO8) | measure in P1.7 by diffing optimized dumps of hot pair-returning loops; if real, adjust emission shape (e.g. copy result out of the call insn early), never patch MIR |
| Debug/release divergence | the v2 emitter deliberately emitted identical MIR in all builds (`:971` comment); v3 keeps that rule — flag is the only axis. P2.6's `PRESERVES` assert must therefore be debug-only *state*, never debug-only emission |
| An untruthful `NUMBER_STACK_PRESERVES` row (P2.6) | reading the flag makes an audited claim load-bearing; the `lambda_side_number_alloc` assert turns a silent dangling pointer into a loud failure. Also static-assert the enum ordering so unaudited rows can never decode as "preserves" |
| Unbounded number-stack growth once the eager restore goes (P2.7) | RV16 gives loop-carried bindings a declaration-time slot below the watermark; the back-edge reclaim bounds per-iteration transients. **The gate measures peak side-stack, not just correctness** — a million-iteration untyped loop over `int64` array elements and over `int64` map fields |
| RVO11: an unnamed wide temporary live across a back edge (P2.7) | the reclaim ships only once the emitter *positively establishes* the set is empty; assumption is not sufficient, because a wrong answer is a use-after-free rather than a regression |
| Slot single-live discipline crossing into hand-written C (P3) | debug generation counter on `mir_companion_slot` so a stale read traps rather than returning a plausible payload |

---

## 4. Deliverable checklist (roll-up)

- [x] P0 encoding + asserts + tripwires *(gated 2026-08-14: 3718/3718)*
- [x] P1.1 `FnReturnShape` descriptor, single-source assert *(same gate)*
- [x] P1.2 callee pair returns; epilogue adopt deleted (flagged)
- [x] P1.3 caller resolution protocol + patch helper *(EAGER form; RV6 lazy
      `maybe_pending` propagation is the remaining refinement)*
- [x] **P1.4 COMPLETE** *(2026-08-15)* — descriptor↔`nres` cross-check
      (`em_assert_callee_result_count`, keyed on `em_variant_returns_pair` =
      the descriptor's `result.companion`, i.e. shape-derived not home-mask
      derived — which is item 3 of the plan); dynamic dispatch now donates no
      home under v3. Item 2 (`nres=2` wrappers) is superseded by RV12: a
      C-reachable `_b` wrapper returns ONE result plus the context slot, which
      is the design that replaced it.
- [x] P1.5 interp/ff bridges — **RVO3 closed by audit**, guard already
      enforced the invariant; comment added so it stays that way
- [x] P1.6 async/yield audit + test — **RVO4 closed**; found and fixed a real
      spill/root publication defect in P1.3
- [~] P1.7 gate: baseline green both ways + emission fixtures cover both
      conventions; STILL OPEN — census re-run, MT7 re-ratchet, AWFY stdout
      diff, release timing/R26 canaries, RVO8/RVO9 measurements
- [x] **P2 native lanes + error lane (shape 4)** — *landed + gated
      2026-08-15: 3720/3720 both configurations.* The outage was a
      simplify-level interaction (value-numbered identical constants in the
      overflow exit's `ret` + `make_one_ret` merge homes); fixed by returning
      a dead 0 on the value lane there. Root-cause chain + the new
      `LAMBDA_MIR_GEN_DEBUG` observation layer in the 2026-08-15 log.
      **Raise-arm deopt CLOSED later the same day** (RV17 + RV18 + the
      operator gate; RVO12): a `T^E` body of the form
      `if (…) { raise … } else { <T> }` now returns natively on **all three
      lanes** — `func d, i64` / `func i64, i64` under v3, from ordinary
      source. **P2 residue closed 2026-08-15**: the RV9 check landed as an
      emission-time producer/consumer agreement assert (a runtime payload
      check is unavailable — MIR must stay build-identical), negative-tested;
      and §5.8's INT64_ERROR gate was RUN — **not retired** (4 live compares,
      1 emitted), with the finding that deleting them would unmask an
      `int64(<decimal>)` crash the surviving compare currently masks. See the
      log entry; §5.8's step order is corrected there.
- [ ] P2.5 LambdaJS migration (node/js262 gates)
- [x] **P2.6 read the watermark effect (RV14a)** — *landed + gated
      2026-08-14: 3719/3719, ratchet re-baselined (24 tightenings).
      deltablue −15.1%, richards −11.0%, havlak −8.8%, js_tune6 −33%. The
      `lambda_side_number_alloc` assert was dropped with reasons — the skip
      removes the restore too, so an untruthful row leaks space rather than
      dangling (see log).*
- [x] **P2.7.0 in-band boxing audit + `fn_member` stable** — *landed + gated
      2026-08-14: 3719/3719. Cumulative from v1: deltablue −30.3%, richards
      −22.3%, havlak −17.9%, json −17.5%; home share 38.7%→16.1% on
      deltablue. Achieved with the v3 flag OFF. `fn_int`'s pre-v5 INT32
      thresholds found and left for a semantics ruling.*
- [x] **P2.7.0b type-driven sys-func metadata** — *landed + gated 2026-08-14:
      helper-call adopts 101 → 23 (v1: 757, −97%). deltablue −33.5% total,
      home share 38.7%→12.6%.*
- [ ] **P2.7 C-helper convention (RV14/RV15/RV16)** — **MEASURED UNDER v3 AND
      DECLINED 2026-08-15**, not merely deprioritized. Prior costings were
      flag-off and counted a population v3 deletes; under v3 the whole target
      is **11 adopt sites across 4 AWFY benchmarks (0.3–0.9%, zero
      `_scalar_home` params)**. The rulings stand for the 757-site population
      they were written against.
  - [x] P2.7.1 (RV16) implemented, gated, then **REMOVED** — its publish
        adopt-classified a raw-lane binding as an Item (see the 2026-08-15
        crash entry). The narrow predicate leaves nothing to publish.
  - [ ] P2.7.2/P2.7.3 — **structurally blocked, not just unstarted**: the
        reclaim's soundness runs through RV16, so the sound configuration is
        broad-predicate RV16 (+600–800 insns/bench) to recover RV15's ~142.
        Net ≈ +600. Revisit only if int64-heavy code becomes a target.
  - [x] RVO11's container question **VERIFIED 2026-08-15**: `array_set` copies
        wide payloads into array-owned cells and re-tags (D5.2.2); containers
        are not a hole. Bindings remain the only population.
- [x] **P3 slot transport (RV12)** — *landed + gated 2026-08-14: 3719/3719
      both configurations. Under v3 the `_scalar_home` parameter and every
      return-side adopt are **gone** (home share 30–39% → under 1%).
      `FnCompanionTransport` added; both C bridges converted; budgets gained a
      `-v3` profile axis. Windows lowering itself still untested (no CI here) —
      the slot path it needs is now the shipping v3 mechanism, so the
      remaining Windows work is `nres` selection, already handled by
      `em_companion_transport`.*
- [ ] P4 formal-doc closure (footnotes → landed; SF14 note; D5.2.2v2 /
      D5.2.3 / DO24 status)
- [ ] P5 delete v2 machinery; final census + ratchet *(now gated on P2.7 and
      P3 as well — shape 2 alone empties only 14% of the apparatus)*
- [ ] RVO8/RVO9 measurements recorded in this doc's log section

---

## 5. Log

### 2026-08-14 — P0 landed (encoding + guards)

`lambda/lambda.h`: `ITEM_PENDING_TAG` = `0x1E`, `ITEM_PENDING`,
`PENDING_KIND_INT64/UINT64/FLOAT` (3 reserved), `lambda_item_is_pending()`
(the 2-insn `and`/`compare` shape the JIT mirrors) and
`lambda_item_pending_kind()`. Static asserts confirm the tag is non-double,
outside the inline-int octant, `>= LMD_CONTAINER_HEAP_START`,
`< LAMBDA_TAG_SPACE_SIZE`, `!= ITEM_SENTINEL_TAG`, and distinct from both
storable JS sentinels' high bytes. Verified layout at runtime:
`LMD_TYPE_COUNT` 0x1C, `LMD_CONTAINER_HEAP_START` 0x1D, pending 0x1E,
sentinel 0x1F — the octant is now full, exactly as §3 predicted.

Tripwire: `assert_item_not_pending()` sits in the tag-byte arm of
`Item::type_id()` (`lambda.hpp`). That is the single choke point — `it2d`,
`it2l`/`item_to_int64`, `it2u`, formatters, equality and hashing all funnel
through it — so one debug branch covers the whole accessor surface instead
of a scattered set.

**Gate: PASS.** Debug build clean; `make test-lambda-baseline` 3718/3718
(Lambda 1614 + Input 2104), MIR Emission Size Ratchet 16/16 (confirming
zero emitted-code change), forced-GC sweep 67/67.

### 2026-08-14 — P1.1 landed (shape descriptor)

- `value_rep.h`: `FnReturnShape` {ITEM, ITEM_SCALAR, NATIVE, NATIVE_ERROR},
  `FN_ERROR_LANE_PAIR`, plus `fn_return_shape_is_pair()` /
  `fn_return_shape_may_be_pending()`.
- `ast-core.hpp` `FnReturnAnalysis.shape`; mirrored into `JitCallMetadata`
  (`sys_func_registry.h`) and `MirFunctionPlan`.
- `em_return_shape()` (`mir_emitter_shared.hpp`) is the ONLY place a shape is
  computed. Every consumer reads the published descriptor; the body-emission
  site reads `body_variant->result.shape` rather than deriving a second
  answer from its own locals.
- Populated at both Lambda variant sites, both LJS variant sites (truthful
  now even though LJS emission migrates in P2.5 — a zeroed field would have
  claimed "provably wide-free"), and the Jube guest-body frame. Imports are
  pinned to `RETURN_SHAPE_ITEM`: C helpers never speak the pair protocol
  (RV12/SF6).
- Flag `LAMBDA_RETURN_V3` (default 0) + `LAMBDA_RETURN_CONVENTION_REVISION`.

**Gate: PASS** (same run as P0 above — both landed together).

**Deviation from the plan, recorded:** the plan modelled shape 2 as implying
the register pair. In-tree there are two possible transports for it, and the
existing descriptor already distinguishes them: `scalar_home_lane_mask != 0`
= v2 trailing caller-donated home, `== 0` = v3 register pair. That is what
`em_returns_result_pair()` reads. The distinction is load-bearing for the
staging below and costs no new field.

### 2026-08-14 — P1.2 + P1.3 (eager form) implemented

**Callee** (`transpile-mir.cpp`): boxed bodies with a live scalar-return mode
are created with `nres=2` and drop the trailing `_scalar_home` parameter and
their home mask. `finish_function_epilogue` replaces the adopt cluster with
`em_build_pending_pair` + watermark restore + `ret item, companion`; the
stack-overflow exit supplies the dummy lane. `mir_body_returns_pair()` is
shared by the body-emission site and the forward-declaration contract site,
so a forward contract can never promise the home while the body returns a
pair (the v27 havlak mismatch class).

**`em_build_pending_pair`** (`mir_emitter_shared.hpp`) came out considerably
cheaper than the plan's "reduced classify" estimate. The four wide tags are
CONTIGUOUS (`LMD_TYPE_INT64`=6 … `LMD_TYPE_FLOAT64`=9), so one `sub` plus one
unsigned `ubgt` rejects every packed int, bool, null, string, container and
inline double at once, and `tag - LMD_TYPE_INT64` *is* the pending kind for
the two integer tags. Tags 0x06–0x09 also have the double-discriminator bits
clear, so the wide arm needs no `ITEM_DBL_MASK` test at all. Measured on a
real dump: **12 static instructions, 5 on the fast path** (`mov`, `mov`,
`ursh`, `sub`, `ubgt`) versus the v2 cluster's 20 static / 11–16 dynamic plus
a home materialization, a home store and an ABI parameter. Both facts are now
pinned by static asserts in `lambda.h` — reordering `EnumTypeId` would
silently mis-classify returns otherwise.

**Caller** (`em_call_direct`): reads the callee descriptor (never a local
fact), emits a 2-result `MIR_CALL`, then resolves eagerly via
`em_resolve_pending_pair` → `lambda_item_resolve_pending` (new runtime helper
in `lambda-mem.cpp`, registered + audited in `sys_func_registry.c`). Eager
resolution is correct by construction and satisfies RV4.2 trivially; RV6's
lazy `maybe_pending` propagation is a strict refinement on top, not a
prerequisite. New `MirFrameState.number_extent_dirty` forces the number-frame
epilogue when a frame emits a resolution — the patch boxes into *this*
frame's extent (SF6), so without it a resolve inside a loop would grow the
number stack without bound.

**Staging decision (deviation, deliberate):** public `_b` wrappers stay on
the v2 home ABI in P1. They are the entries reached from C — the
`fn_call_boxed_N_into` trampolines and `fn->invoke` — and a C prototype
cannot receive two MIR results portably. Internal bodies move to pairs; the
wrapper's own `em_call_direct` resolves the pair, so its v2 epilogue sees an
ordinary Item and needs no change. This keeps one convention per *entry*
with the descriptor as the arbiter, and confines the C-bridge question to
P1.5/P3 (where RV12's context-slot transport is the natural answer).

Verified emission (`temp/mir_dump.txt`, flag on):
`_big_23: func i64, i64, p:runtime` — two results, no home parameter —
ending in `ret %r56, %r57`, with `_big_b_23` still carrying `p:_scalar_home`
as designed. Smoke test covering int64 returns, subnormal-float returns,
wide arithmetic across a call, pass-through of strings/arrays/null/floats,
and wide results stored into a container all produce correct values.

**Not yet done in P1:** RVO9/RVO8 measurements, MT7 re-ratchet, census
re-run, AWFY diff + release timing, P1.4 (dynamic dispatch still uses
`dyn_scalar_home`; wrappers unchanged by design above), P1.5 bridges, P1.6
async audit.

### 2026-08-14 — emission fixtures made convention-aware

The first flag-on baseline was 3716/3718: the only failures were
`scalar_home_donation` and `scalar_home_tail_forward`, both of which assert
the v2 protocol *by name* (`p:_scalar_home`, `call
lambda_item_adopt_scalar_home`) — i.e. exactly the machinery v3 replaces, not
regressions.

Rather than delete or weaken them, the sidecar schema gained an optional
per-check-group `return_convention` (2 or 3) and the harness evaluates only
the groups matching `LAMBDA_RETURN_CONVENTION_REVISION`. This is deliberately
*not* a fixture-level skip: both fixtures now carry a v2 group and a v3 group,
so each build asserts real emission shapes, and `run_fixture` fails loudly if
a sidecar's every group is filtered out — a fixture can never report green
while asserting nothing. The v3 groups pin the two facts that matter: the
two-result signature with no home parameter, and the `ursh`/`sub`/`ubgt`
pending classify reaching a `ret`.

The `main` group of `scalar_home_donation` is intentionally left
convention-independent: argument boxing still goes through a helper-call
adopt, which v3 does not touch (RV12/SF6).

### 2026-08-14 — gate results

| Configuration | Result |
|---|---|
| `LAMBDA_RETURN_V3=0` (shipping default) | **3718/3718** — Lambda 1614 + Input 2104; MIR emission 38/38, ratchet 16/16, forced-GC sweep 67/67, JS 347/347 |
| `LAMBDA_RETURN_V3=1` | **3717/3718 in the parallel run**; the single failure (`dom_bootstrap`) is the known heavy-test parallel-load flake — it passed in the earlier flag-on run, passes standalone, and the whole JS suite is 347/347 standalone. Every other suite green, including forced-GC 67/67 and the emission fixtures 38/38 + ratchet 16/16. The two v2-protocol fixtures that failed the first flag-on run now pass under both conventions. |

The tree is left at the shipping default (`LAMBDA_RETURN_V3 0`); v3 is
exercised by flipping that one constant.

Verified v3 emission, from the real dump:

```
_accumulate_371:  func  i64, i64, p:runtime, i64:%p1, i64:%p2   <- two results, no home param
    ...
    ursh  %r58, %r51, 56
    sub   %r59, %r58, 6
    ubgt  L32, %r59, 3          <- 3-insn reject of every non-wide Item
    ...
    ret   %r56, %r57            <- item + companion
_accumulate_b_371:  func  i64, p:runtime, ..., p:_scalar_home   <- C-callable wrapper stays v2
    call  _accumulate_371_dp1, _accumulate_371, %r69, %r6a, ...  <- two result regs
    and   %r6c, %r69, 18374686479671623680
    bne   L40, %r6c, 2161727821137838080                         <- the 2-insn pending test
    call  lambda_item_resolve_pending, %r6d, %r69, %r6a          <- rare arm only
```

**Residue found while implementing, for the next slice:** the non-`local_func`
fallback in the direct-call lowering (`transpile-mir.cpp`, the `else` arm that
builds a proto by hand and targets `MIR_new_import(fn_mangled)`) has no callee
descriptor at all, so it cannot honour a pair-returning callee. It fails
loudly rather than silently (a pending Item reaching an accessor trips the P0
tripwire), but P1.4 should either route it through the descriptor or assert it
unreachable. *(Closed below.)*

### 2026-08-14 — P1.4 (partial), P1.5, P1.6

**P1.4 — blocked on the C bridge, and now provably so.** The plan's P1.4 asks
for pair-returning dynamic entries and `nres=2` public wrappers. Both are
unreachable before the RV12 slot transport, for one verified reason: the whole
dynamic path is C-mediated. `transpile-mir.cpp` calls `fn_callN_into`, which
are ordinary C functions (`Item fn_call1_into(Function*, Item, uint64_t*)`)
that resolve the callable and invoke it through a C function-pointer cast in
`lambda_dynamic_invoke_by_count`. A C prototype has no portable spelling for
MIR's two-result convention, so `dyn_scalar_home` stays until lane 2 has a
memory transport. Recorded at the emission site and in design §7/§1.4; this is
what re-scopes P3 away from being a Windows-only phase.

What P1.4 *did* land is the anti-mismatch machinery it exists for:

- **`em_assert_callee_result_count()`** — a call site derives its result count
  from the callee's descriptor, and the callee derived its `nres` from the same
  descriptor at creation. When the target is a defined function in this module
  its real `MIR_func->nres` is available, so the two derivations are now
  cross-checked instead of trusted to stay in step. A disagreement aborts with
  both numbers named. This is the v27 havlak class made impossible to
  reintroduce silently, and it is exactly the check RV10 asks for — previously
  a comment, now enforced.
- Wired into `em_call_direct` **and** the hand-rolled non-`local_func`
  fallback, which closes the residue above: that path hard-codes `nres=1`, and
  now asserts its target really is single-result rather than assuming it.

**P1.6 — found and fixed a real defect in P1.3.** `em_after_resolved_call`
publishes a call's result to two consumers: the async spill tracker
(`after_call_result`) and the root machinery (`root_call_value`, which fires
for any `JIT_VALUE_BOXED_ITEM` result). P1.3 as first written handed it the
**raw** pair result — so a pending Item could be spilled across a suspension
and, worse, written into a GC root slot, where its `0x1E` tag is outside the
traceable TypeId range. Both are direct RV4.1 violations ("a pending Item
never lives in memory").

It survived the earlier forced-GC sweep only because eager resolution
overwrites the slot almost immediately and wide returns are rare in the
corpus — i.e. it was latent, not benign. Fixed by splitting
`em_publish_call_result()` out of `em_after_resolved_call()`: pair call sites
pass result operand `0` (keeping the call-site and exception bookkeeping) and
publish the **resolved** register afterwards, exactly once. The pair is GC-safe
in the window between, per RV5's safepoint row — lane 1 is tag bits, lane 2
raw non-pointer bits, so a collector has nothing to trace.

New test `test/lambda/proc/wide_scalar_across_await.ls` (+ `.txt`) carries an
int64 and a subnormal double across a `start`/`wait` suspension, as call
results and as operands derived from them, and checks value, arithmetic and
`type()` on both sides. Byte-identical output under v2 and v3.

**P1.5 — RVO3 closed by audit, no code change needed.** The interpreter/JIT
crossing casts `fn->ptr` to a single-Item C prototype with a trailing home
(`lambda_dynamic_invoke_by_count`). That cast is already gated: dispatch
rejects any `entry_abi` outside {`LAMBDA_BOXED_FUNCTION`,
`LAMBDA_BOXED_PROCEDURE`, `HOST_ADAPTER`}, and the boxed markers are applied
only when a function is published through its `_b` wrapper
(`emit_mir_function_abi_markers`, `uses_wrapper`). Pair-returning bodies never
carry those markers, so they cannot reach the cast — a pending Item cannot
cross into interpreter or host code by construction. The pre-existing guard
now carries a comment saying it also holds this invariant, so a future edit
does not relax it unknowingly. MIR-interpreter mode needs nothing: `nres=2` is
native there, and every C entry point (`main`, `_b` wrappers) is single-result.

**Gates:** `LAMBDA_RETURN_V3=0` **3719/3719**; `LAMBDA_RETURN_V3=1`
**3719/3719** (both counts include the new async test). Forced-GC sweep 67/67,
MIR emission 38/38, ratchet 16/16 under v3.

### 2026-08-14 — P2.6 landed (RV14a: read the watermark effect)

One condition in `em_call_import` — skip the snapshot / home / classify /
restore sequence when `resolved.call.effects.number_stack ==
JIT_NUMBER_STACK_PRESERVES` — plus a static assert pinning `MAY_ALLOCATE` as
the zero value. Flag-independent: this is the v1 path, so it ships on by
default.

**The runtime assert in the plan turned out to be unnecessary, and the reason
is worth keeping.** The skip drops the *restore* along with the adopt. So an
untruthful `PRESERVES` row degrades to a **space leak, not a dangling
pointer**: nothing reclaims the helper's push, the payload simply lives to the
frame epilogue, and the returned Item stays valid. The one residual hazard is
a helper that allocates, restores itself, *and* returns an Item pointing into
the region it released — but that is broken on its own terms regardless of
this change. Since the emitter must produce identical MIR in debug and release
(§3), buying a marginal assert with debug-only emission was the wrong trade.

**Effect — measured, same 8 benchmarks:**

| bench | adopt sites | module insns | |
|---|---|---|---|
| deltablue | 413 → 265 | 22,639 → 19,231 | **−15.1%** |
| richards | 156 → 107 | 10,209 → 9,082 | **−11.0%** |
| havlak | 292 → 214 | 20,470 → 18,670 | **−8.8%** |
| json | 183 → 140 | 11,852 → 10,856 | **−8.4%** |

havlak's drop is exactly the 78 `lambda_name_id_to_item` sites predicted from
the audit, and the module fell more than the 1,560 cluster instructions alone
because the snapshot and home materialization went with them.

**Only two helpers actually change**, confirmed by enumerating the registry:
`lambda_name_id_to_item` and `js_error_lane_payload`. Both audits verified
truthful by reading the sources — the first returns interned name-pool data
(`name_pool_resolve_id`: "immutable, outside GC, never individually
ref-counted"), the second returns a stored `thrown_value_item`. A third row,
`js_finalize_function`, declares `BOXED_ITEM` but the function returns `void`
and is emitted through `jm_call_void_6`, so it never adopted and is unaffected
— a stale audit field, harmless, worth cleaning up separately.

`lambda_name_id_to_item` is also why the LambdaJS probes move so much: LJS
materializes property keys through NameIds, making it the hottest helper in
emitted JS. `js_tune6_exact_collection` fell 21,474 → 14,411 module
instructions (**−33%**), with `js_main`'s scalar homes 26 → 6.

**Gate: PASS.** Baseline 3719/3719 at the shipping default, forced-GC sweep
67/67, MIR emission 38/38. MT7 re-ratcheted: 24 budget tightenings applied
across 8 probes (`test/mir/mir_budgets.json`), after which the ratchet reports
zero remaining slack. `js_tune6`'s `linux-debug` and `default` profiles are
left alone — they are separate platform measurements and this host cannot take
them; the file's own convention is that each platform re-baselines its own.

### 2026-08-15 — RVO12 CLOSED: shape 4 on ALL lanes (int/i64 join float)

*(Later same day; supersedes the "int stays boxed" scope notes in the RV17/
RV18 sections above.)*

The `or`-containment failure that forced the int-widening revert is diagnosed
and fixed at the root. The caller merge was correct; the abort came from
`transpile_binary_out`'s native-arithmetic block, which is entered on operand
types alone (`both_int || both_float || int_float`) with **no operator
check**. The now-native `int^` call made the operand witness say INT, so `or`
entered the block; the eager native operand fetch ran
`emit_return_if_item_error` on the merged join — propagating the error `or`
existed to contain — converted both operands to doubles, found no
`OPERATOR_OR` case in the switch, and fell through (`default: break`) to the
boxed `or`, re-evaluating both operands.

Fix: gate the block on the operator set it implements (ADD/SUB/MUL/DIV/POW +
the six comparisons). This also repairs a **latent master bug**: `f() or 9`
with an ordinary non-raising native-int `f` evaluated `f()` twice — side
effects duplicated, unnoticed because the duplicated work was usually pure
and its results dead.

Diagnosis correction worth recording: the first trace reading called the
fatal tag test a *null* check (`eq tag, 27`, assumed `LMD_TYPE_NULL`). 27 is
`LMD_TYPE_ERROR`; NULL is 1. One enum lookup rewrote the narrative from
"missing error case in a null path" to "error propagation emitted by the
operand fetch". Check tag constants against `EnumTypeId` before narrating a
dump.

With the gate in place the INT/INT64 admission is re-applied and holds:
`func d, i64` / `func i64, i64` under v3 on all three lanes; destructure,
`or` containment, and arithmetic-on-recovered all correct;
`raise_arm_native_return.ls` extended to pin the int/i64 `or` cases the
original test missed. Full chain that closed RVO12: **RV17** (union names the
value component) → **RV18** (producer publishes the carrier) → **operator
gate** (non-arith ops never enter the native-arith block) — each necessary,
only all three sufficient.

Gates: **both flag states green** — 3718/3721 and 3717/3721+lib_alpine-flake,
where the 3 persistent failures are forced-GC divergences a **clean tree
reproduces exactly** (verified by stashing the whole working set), and
`lib_alpine` passes standalone (known parallel-load flake).

### 2026-08-15 — P1.4 completed, P2.7 measured under v3, and the CUTOVER

**P1.4 — done, and two thirds of it turned out to be moot.**

The plan asked for three things. Only the first needed code:

1. *Dynamic dispatch drops the caller-donated home.* **Implemented.** The
   whole chain — `fn_callN_into` → `lambda_dynamic_call` →
   `lambda_dynamic_invoke_by_count` → `LambdaDynamicNativeInvoker` — guards
   every use of the trailing operand with `#if !LAMBDA_RETURN_V3`, so under v3
   the callee already ignored it while the emitter still allocated a frame
   slot and materialized its address on every dynamic call. That was the last
   return-side home in the emitter. The operand stays in the C signature (one
   call shape across both conventions) and is passed null.
2. *`nres=2` public wrappers.* **Moot — superseded by RV12.** The slot
   transport is the answer for C-reachable entries: one MIR result plus
   `Context::mir_companion_slot`. A 2-result wrapper is exactly what RV12
   ruled out, so this item was asking for the rejected design.
3. *Invoke metadata gains a shape field; entry-equivalence compares shapes.*
   **Moot — the resolver is shape-agnostic by construction.**
   `lambda_item_resolve_pending_slot()` is documented and implemented as
   "safe to call on any Item — a resolved one passes straight through", so the
   dynamic path is correct for every callee shape without consulting
   metadata. A shape field would be data nobody reads. (`function_eq` still
   compares `requires_scalar_result_home`, which is uniformly 1 under v3 and
   therefore harmless; retiring the bit belongs to P5's deletion sweep.)

**P2.7 — measured under v3 for the first time; protocol change NOT shipped.**

Every prior P2.7 costing was taken with the flag off, which counts a
population v3 deletes. Under v3:

| bench | `_scalar_home` params | adopt sites | home share |
|---|---|---|---|
| deltablue | 0 | 3 | 0.9% |
| richards | 0 | 1 | 0.3% |
| havlak | 0 | 4 | 0.7% |
| json | 0 | 3 | 0.7% |

**Eleven sites across four benchmarks**, and attribution by defining
instruction names them exactly: `pn_push` ×4, `fn_fill` ×3, `pn_splice` ×2,
`fn_slice3` ×2. RV14/RV15/RV16 — per-binding slots, retiring the eager
restore, the back-edge reclaim, plus closing RVO11's liveness obligation —
were designed against 757 sites. Against 11 they are not worth their risk,
which is a sharper version of the same conclusion the 2026-08-14 entry
reached at 23.

*Attribution warning, again.* A first pass blamed `int2it_lane` for 13 of
deltablue's v2-era adopts. It is already marked
`RESULT_SCALAR_STABLE | NUMBER_STACK_PRESERVES`; the register was a φ-merge
of the inline and cold int-boxing arms, one of which happens to be that call.
Attributing by defining instruction is necessary but **not sufficient** when
the definition is a merge — check whether the def is a join before believing
it. (Fourth heuristic in this effort to mislead.)

*Why the obvious cheap fix was not taken.* All four survivors are declared
`&TYPE_ANY`, so the type-driven rule correctly declines to narrow them. Every
return path of all four was read and none is number-home-backed (`pn_push` /
`pn_splice` return the array argument; `fn_fill` returns `{.array}` /
`{.array_num}` / `ItemError`; `fn_slice3` delegates to `fn_slice`, which
returns a substring, `x2it` binary pointer, `array_num_slice_result`,
`{.array}`, or `ItemError`). So marking them stable **would** be sound. It was
not done because both mechanisms cost more than 11 sites are worth: an
explicit `jit_runtime_imports` row means hand-writing GC/reentry/arg-class
bits that are conservative today, and setting `success_type` reaches type
inference. Recorded here so the next attempt starts from the audit rather
than repeating it.

**CUTOVER — `LAMBDA_RETURN_V3` default 0 → 1.**

Gated by running the **same tree both ways**:

| build | total | passed | failed |
|---|---|---|---|
| v2 (`LAMBDA_RETURN_V3=0`) | 3721 | 3715 | 6 |
| v3 (`LAMBDA_RETURN_V3=1`) | 3721 | 3715 | 6 |

**Identical counts and identical failure lists** — `result29_indexed_guards`,
three `MirGcStressTest` forced-GC divergences, `dom_module_props`, and `tco`.
All six were then reproduced on a **clean tree** (whole working set stashed,
rebuilt), so none was attributable to v3 or to this session.

*Resolved 2026-08-15 by the `LAMBDA_JS_EXEC_PROFILE` / test-link fix
(`81e5ecbb1`, `bf13385dd`): all six cleared. The post-fix gate at the v3
default is **3833/3833 clean**, which retires the parity argument in favour
of an absolute one.*

v2 remains buildable via `-DLAMBDA_RETURN_V3=0` until P5 deletes its
machinery; the `#ifndef` guard is unchanged, only the default moved.

### 2026-08-15 — P2.7.1 (RV16) LANDED

A wide-capable **mutable** binding now owns one number slot for its scope.

**Mechanism.** The slot must be an *uncolored* fixed scratch slot
(`em_binding_number_slot_new`, `MIR_FRAME_REF_FIXED_SCRATCH`), not a colored
scalar home: coloring shares homes between values whose live ranges do not
overlap, which is precisely wrong for a value written each iteration and read
on the next. Fixed slots also sit below the loop-entry watermark, which is the
soundness argument RV15's reclaim needs. Growing the count during body
emission is safe because `em_finalize_scalar_homes` — which resolves slot
offsets and the frame size — runs after the body and before the prologue.

The store side re-homes at `transpile_assign_stam`'s **single exit** rather
than in each arm of its type cascade (rule 13), reusing
`em_adopt_scalar_item_value`, which passes packed values straight through and
copies only a frame-backed payload.

**The predicate is `declared` wide, not `may_be_wide` — and that distinction
is the whole cost of the feature.** The first cut used
`lambda_type_id_may_be_wide_scalar`, which is correct at a *return* boundary
(any ANY may turn out wide) and far too broad for a binding, because it admits
every ANY-carriered `var`:

| bench | before | with broad predicate | delta |
|---|---|---|---|
| deltablue | 15,227 insns / 0.9% | 15,966 / **3.5%** | +739 |
| richards | 7,544 / 0.3% | 7,973 / **3.6%** | +429 |
| havlak | 16,276 / 0.7% | 17,105 / **3.2%** | +829 |
| json | 9,129 / 0.7% | 9,705 / **3.7%** | +576 |

~600–800 added instructions per benchmark to serve loop-carried `int64`
accumulators those programs do not have — against RV15's ~142-instruction
recovery in deltablue. Narrowing to declared `int64`/`uint64`/`float64` makes
all four **byte-identical to before**, while still firing for the accumulator
the rule exists for (`var total = 0i64` in a loop: `number_scratch=2`, one
slot per declared-wide var, none for the `int` accumulator; value correct).

**Gate: 3833/3833 clean** at the v3 default.

**What this says about RV15.** RV16's cost scales with declared-wide mutable
bindings; RV15's benefit is capped at the 11 adopt sites v3 leaves. On the
AWFY corpus RV16 is now free and RV15 would recover ~142 instructions in
deltablue. That is the real trade, and it is far narrower than the 757-site
population RV14/RV15/RV16 were designed against.

### 2026-08-15 — P2.7.2/P2.7.3 (RV15 + RVO11): NOT SHIPPED, and now we know why

RV16 is in and free (previous entry). RV15 was then designed against the
tree and the blocker is structural, not incidental.

**The reclaim's soundness argument runs through RV16, and narrowing RV16 cut
that link.** The design's chain is: RV16 gives a wide-capable mutable binding
a slot allocated at its declaration, hence *below* the loop-entry watermark,
hence unreachable by a reclaim — so the reclaim cannot invalidate anything a
binding still needs. That argument requires RV16 to cover **every** mutable
binding that can hold a wide payload. The narrowed RV16 covers only
*declared* `int64`/`uint64`/`float64`. An ANY-typed `var` assigned a wide
value — common — gets its payload allocated *during* the statement, above the
snapshot, with nothing below to hold it. The reclaim would invalidate it.

So the two are not independent knobs:

| RV16 predicate | cost | makes RV15 sound? |
|---|---|---|
| declared wide (shipped) | **free** — byte-identical AWFY | **no** |
| may-be-wide (measured) | **+600–800 insns/bench** | yes |

RV15 recovers ~142 instructions in deltablue. So the sound configuration is
**net ≈ +600 instructions per benchmark**, and the unsound one is free. That
is the complete answer to "should RV14/RV15/RV16 ship": **the prerequisite
that makes the reclaim sound costs more than the reclaim saves**, by roughly
4×, under v3. RV14/RV15/RV16 stay *ruled* — they were correct against the
757-site population they were written for — but they are now measurably the
wrong trade at 11 sites.

**RVO11 stays open and is now sharper.** The obligation is no longer the
open-ended "where do loop-carried wide values live?", nor even "does any
unnamed wide temporary cross a back edge?". With the statement-boundary form
it is: *does this statement leave a wide payload above the snapshot that
something outliving the statement still points at?* Two known sources, one
settled and one not:

- **Bindings** — settled: covered iff RV16 is broad (see table).
- **Container stores** — NOT verified. Whether `arr[i] = <wide>` or
  `push(arr, <wide>)` copies the payload into destination-owned storage
  (D5.2.2) or retains a pointer into the caller's number extent decides
  whether containers are a second hole. Today's eager per-call restore hides
  the question; removing it exposes it. **Verify this before any reclaim
  ships** — it is the one fact standing between the current analysis and a
  complete RVO11 answer.

What is left implemented: `em_binding_number_slot_new` and the
`wide_number_slot`/`wide_number_addr` binding fields, both live and gated.
Re-broadening the predicate is a one-line change if the trade ever inverts —
e.g. if int64-heavy code becomes a target, where RV16's cost is paid anyway
and RV15's saving is proportionally larger.

### 2026-08-15 — the "int64 accumulator crash": TWO defects, one mine (fixed by removal), one pre-existing (root-caused, open)

The RVO11 churn harness exposed a segfault. Investigation separated it into
two independent defects that happened to share a repro shape.

**Defect A — RV16's publish classified a raw lane as an Item. MINE; fixed by
removing RV16's wiring.** `mir_publish_wide_number_slot` ran
`em_adopt_scalar_item_value` on `var->reg` assuming a boxed Item, but the
assignment cascade leaves a declared-`int64` binding's register holding a RAW
lane (`it2l` result). The guard (`mir_type != MIR_T_I64`) is the degenerate
register-class proxy — raw i64 and Item are both `MIR_T_I64` — i.e. I
reintroduced the exact bug class RVO12's closure documents, hours later, in
new code. Latent until the accumulated VALUE'S own high byte reached a
scalar-pointer tag (`value ≥ 6·2^56` ⇒ byte 0x06 = `LMD_TYPE_INT64` ⇒ the
adopter dereferenced the value's own bits — measured threshold n≈433 at
step 999999999999999 matches 6·2^56 exactly). The 3833/3833 gate missed it
because no baseline test accumulates an int64 past 4.3e17; my own behavioral
check used small magnitudes. **Removal, not repair**, because the narrow-
predicate RV16 has nothing to publish — every qualifying binding is
raw-lane-carriered ("static type ⇒ representation"), so the wiring was
dead-and-dangerous. `em_binding_number_slot_new` and the VarEntry fields went
with it; the RV16/RV15 cost analysis in the previous entry stands unchanged.

**Defect B — loop-carried representation widening; PRE-EXISTING. FIXED
2026-08-15 (see the follow-up entry below).** Minimal repro (crashed at the
SECOND iteration):

```lambda
pn churn(n: int) {
  var s = 0
  for (i in 1 to n) { s = s + 999999999999999i64 }
  return s
}
```

Mechanism, each step verified in the dump or by instrumentation:

1. `int + int64` classifies into the semantic **INTEGER** domain on both
   sides (static and runtime agree): `apply_decimal_numeric` returns a
   **decimal**. The values are correct — an apparent "10× wrong result" was
   twice misread from `print` output concatenating main's return value
   without a newline; there is no value bug.
2. The assignment's type-mismatch arm correctly boxes and **widens the
   binding to ANY** — but the loop body was emitted in ONE pass, and the
   read of `s` at the top of the body was emitted earlier, against the
   declaration's INT-lane representation. The back edge re-enters that stale
   code: `int2it_lane(decimal_pointer_bits)` → out-of-band → **inf**.
3. Iteration 2 computes `inf + int64` (observed via fn_add instrumentation:
   `a=0x7ff0000000000000`), and the eventual native-INT return converts the
   corrupted accumulator via `lambda_item_to_int_lane_c` →
   `decimal_to_int64_exact` dereferences junk (crash report:
   KERN_INVALID_ADDRESS, the read value itself a recycled-slot bit pattern).

The root cause is therefore: **an assignment inside a loop that widens a
binding's representation cannot retroactively fix the loop's already-emitted
reads.** Everything downstream (inf, the decimal deref) is consequence.
Component ages: the INTEGER-domain classification landed 2026-08-02 ("int
total impl"), the widening arm predates 2026-07-24 — both predate this
session. (A direct pre-session binary comparison was attempted twice and
failed for build-system reasons — worktree needs node_modules/premake state;
partial checkout doesn't compile against current config — so attribution
rests on component dating, stated as such.)

Candidate fixes, unranked, all needing a prepass or two-pass shape: pre-scan
a loop body for assignments whose RHS static type widens a read-before-
written binding and widen the DECLARATION's carrier; or emit loop-top reads
through the binding's FINAL type (requires the assignment cascade to stop
mutating `var->type_id` mid-body); or reject the widening at compile time
per TE-17-style enforcement. Each touches shared var/loop emission —
substantial, and not attempted here.

**Also verified this round (RVO11 container question, separate message):**
`array_set` copies wide payloads into array-owned cells
(`arr->items[capacity-extra-1]`, the `extra` counter) and re-tags — D5.2.2
destination-owned storage implemented in the store itself; native-lane and
map stores copy by construction. Verified empirically with runtime-computed
values after producer-frame teardown plus 600-push extent churn (an earlier
pass used wide LITERALS, which the const pool keeps alive — an invalid test,
redone). RVO11's container hole does not exist; bindings remain the only
population, covered iff RV16 is broad — the trade recorded above.

### 2026-08-15 — Defect B FIXED: loop-carried representation widening

Two independent blind spots, both "a scan that does not know Lambda's own AST
shapes". Fixed together; gate **3834/3834**, AWFY emission byte-identical
(deltablue 15,227 insns / 0.9%, richards 7,544 / 0.3% — unchanged), so the fix
is free on code that does not widen.

**B1 — the emitter's carrier (the SIGSEGV).** A loop body is emitted in one
pass, so a read of a binding at the loop top is emitted against whatever
carrier it has *at that moment*. An assignment later in the same body can
widen the binding to a boxed ANY (the cascade's final arm), and the back edge
then re-enters the already-emitted read, decoding a boxed Item through the old
raw lane.

Fix: `mir_prewiden_loop_bindings` walks the body **before** it is emitted and
widens any binding the body will widen, so read and write agree for the whole
loop. Called from `transpile_for` (before `push_scope` — the binding lives in
the enclosing scope) and `transpile_while_core`.

The widening predicate is the subtle part. `mir_is_native_scalar_value_type`
is TRUE for decimal/string/symbol — they are *pointer-lane* scalars — so it
cannot answer "does this value fit the binding's register?". The predicate
tests **lane compatibility** instead, mirroring the cascade's non-widening
arms: same integer family, float binding taking int/float, int binding taking
float (C16). Anything else crosses lanes and widens. Using "is native" as the
test made the pass silently do nothing on the exact case it was written for.

**B2 — the return-lane proof (the wrong ANSWER).** With B1 fixed the loop body
was exact, but sums past 2^53 still returned `int.inf`:
`mir_nested_control_writes_name` handled `AST_NODE_FOR_STAM` — the **JS**-shaped
`for(;;)` node — and had no case for `AST_NODE_FOR_EXPR`, which is Lambda's
own `for (x in xs)`. Every assignment inside a Lambda for-loop was therefore
invisible to it, so `mir_binding_has_reassignment` answered "never reassigned"
and the return proof fell through to the DECLARATION's initializer as its lane
witness: `var s = 0` published a native int return even after the loop had
widened the binding to a boxed decimal, and the decimal saturated on that lane.

*Method note:* the same blind spot bit twice. My first version of B1's walker
used `ast_visit_core_children`, which likewise has no `AST_NODE_ASSIGN_STAM`
or `AST_NODE_CONTENT` case — it is built for the JS node set — so the walk
visited nothing and the "fix" changed no behaviour. **A shared AST visitor
that silently visits nothing is worse than no visitor**; B1's walker now
switches over the Lambda shapes explicitly.

**Test:** `test/lambda/proc/int64_accumulator_widening.ls` — for-loop,
while-loop, nested loops, and a guarded (`if`-armed) widening, all at
magnitudes far past int53 so a lane saturation shows as a wrong value; plus
two CONTROLS that must NOT widen (a plain `int` accumulator, and a declared
`int64` one, which types as int64 throughout).

*Parser limitation found in passing, unrelated and not fixed:* two consecutive
assignments to outer bindings inside a `for` body fail to parse
(`s = s + 1` then `last = s` → "Unexpected syntax near '= s'"), with or
without semicolons; the working idiom in `for_expr_content_proc.ls` declares a
`var` first. The test was reformulated around it.

### 2026-08-15 — P2 residue: RV9 assert LANDED; INT64_ERROR gate RUN (and it reordered §5.8)

**RV9 debug assert — done, and it is not the assert the ruling imagined.**

RV9 asks for "a debug assert that lane-2 payloads are either `ItemNull` or
ERROR-tagged". A *runtime* payload check is unavailable here: emitted MIR must
stay byte-identical across build configurations (the MT7 ratchet and the
`.mir-check` fixtures compare it), so the check cannot be debug-only emission —
and emitting it always would tax every `^E` return in release.

So the check runs **in the compiler**, and it pins something stronger than the
payload's shape: **producer and consumer must agree on how lane 2 spells "no
error".** The two sides derived that independently — the callee wrote it from
`mt->em.frame.plan.companion`, the call site inferred it from "did
`em_call_direct` hand me an error register?". They agree today, and nothing
forced them to. `ItemNull` is **not** zero, so a producer writing the register
spelling into a consumer testing non-zero reports **every successful call as an
error** — silent and total. That is the RV10 two-derivations class exactly.

- `em_error_lane_in_register(companion)` is now the single definition; the
  producer (`mir_error_lane_no_error_op`) keys off it.
- `em_assert_error_lane_agreement()` cross-checks the call site's belief
  against the callee descriptor's, the same defence
  `em_assert_callee_result_count` gives the result *count*.
- **Negative-tested**: inverting one side makes it abort naming both
  ("call site reads the context-lane form, callee writes the register form"),
  so it has teeth rather than being decorative.

Gate **3859/3859**.

**INT64_ERROR — §5.8's gate was RUN, and its result changes the order of that
work.** §5.8 asks to "grep-verify zero surviving `INT64_ERROR` compares",
because the retired sentinel *is* `INT64_MAX`, which is now the `+inf` lane
value. Result: **not retired.** 24 mentions; 4 live compares, of which one is
on an emitted path (`box_int64_result_or_error`, reached from `int64()`).

Empirically the collision is dormant for ordinary values — a runtime `inf`
propagates correctly through `div`/`+` (`%` gives `nan`) and never reads as an
error, because those paths use the lane-aware helpers, not the legacy
`fn_idiv_i`/`fn_mod_i` (which have **no emitted callers at all** — dead code
carrying two of the sentinel returns).

**But running the gate surfaced a crash, and the surviving compare is what
hides it.** `int64(<decimal>)` SIGSEGVs — `Item::type_id()` dereferencing
`0x64`, i.e. the value `100` used as an Item pointer:

```
pn main() { print([int64(100m)]); return 0 }     // exit 139
pn main() { print(int64(100m));   return 0 }     // fine
pn main() { let x = int64(100m); print([x]); return 0 }   // exit 139
```

`int64()` returns a **raw** `int64_t` while its type is `int64 | error`, which
lowers to ANY; a consumer that trusts "ANY means already an Item" reads the raw
payload. `transpile_box_item` special-cases the direct call, but a binding
loses the raw-ness before any consumer sees it. Note `int64(9223372036854775807m)`
does **not** crash — it returns `INT64_ERROR`, the surviving compare converts it
to a proper `ItemError`, and the bug is masked. **Deleting the compare, as §5.8
asks, would unmask this.**

*A fix was implemented and reverted, which is the useful part.* Boxing the raw
return at its own ABI boundary (a `POST_PROCESS_INT64_UNION` beside the existing
DTIME/BOOL post-processing) fixed every crashing form — but the condition
`call_expr_tid == ANY` is too narrow, and widening it collides with native-lane
consumers. With the narrow form, `scalar_home_donation`'s emission switched from
`box_int64_result_or_error` to plain `box_int64_value`, i.e. **the error channel
was silently dropped on that path** — caught by the emission fixture, not by any
value test. Reverted; the fixture is the reason to trust the revert.

**Consequence for §5.8: its gate cannot be satisfied by deleting compares.**
The order must be (1) give `int64()`-class sysfuncs a carrier that represents
"raw int64 OR error" without stealing a domain value — which is precisely what
shape 4's error lane exists for, so the answer is to route them through it —
then (2) delete the compares. Recorded here because §5.8 currently reads as if
step 2 were a standalone grep.

### 2026-08-15 — `fn_fill` removed from the helper-call adopts (17 → 9)

The v3 adopt residue was 17 sites across 7 AWFY benchmarks, and attribution by
defining call put **8 of them on `fn_fill` alone** — the largest single source.
Reading every return path settles it: `fn_fill` returns an **Array** (the
`n == 0` case, and the spreadable non-numeric case), an **ArrayNum**
(`ELEM_INT`/`UINT64`/`FLOAT`/`BOOL`, chosen from the fill value's type), its two
explicit `ItemError`s, or **the caller's own error Item propagated by
`GUARD_ERROR2`** — that last one is easy to miss, since the returns live inside
the macro. A container pointer or an error tag; never a number-home-backed wide
scalar. Its adopts were pure waste.

The type-driven rule in `mir.c` cannot reach this: `fill` is declared
`&TYPE_ANY`, which *is* wide-capable, so it correctly declines to narrow. The
proof is in the body, so it takes an explicit `jit_runtime_imports` row — the
mechanism the 2026-08-15 census entry declined to use because it means
hand-writing effect bits. For `fn_fill` those are answerable from the body:
allocates (`heap_calloc`/`array_int_new`/`array_num_new`) so **MAY_GC**, calls
no user code so **no re-entry**, both args are Items.

`NUMBER_STACK_PRESERVES` is deliberately **not** claimed. `fn_fill` pushes
nothing today (verified: no `box_int64_value`/`push_*`/`lambda_side_number_alloc`
in its body, and `lambda_item_to_int64_exact` is pure), but that is a stronger
promise than this fix needs — `RESULT_SCALAR_STABLE` alone sets
`scalar_class = SCALAR_RETURN_NONE`, which skips the whole adopt block.

| | before | after |
|---|---|---|
| adopt sites (7 benches) | 17 | **9** |
| richards / sieve / storage | 1 / 1 / 4 | **0 / 0 / 0** |
| havlak | 0.7% | **0.5%** |

Three benchmarks now emit **zero** adopts. Remaining: `pn_push` ×4,
`pn_splice` ×2, `fn_slice3` ×2, `fn_floor` ×1 — of which push/splice/slice3
were already read and are likewise container-only, so the same row treatment
applies; `fn_floor` genuinely can return a wide double.

**Value-neutrality was A/B tested, not assumed**: `fill` exercised across every
return path (int, float, bool, string, empty, negative-count error, indexed
reads) gives byte-identical output with and without the row. Gate **3869/3869**.

*Found while testing, unrelated and NOT fixed:* an `int64` literal past int53
in an array literal reads back as garbage — `[9007199254740993i64, 1i64]` then
`[0]` gives `432345568797327376`, while the bare literal prints correctly.
`fill(3, <same value>)` gives `inf` (a defined lane sentinel) on the same input,
so the two numeric-array paths disagree. Own repro; separate from this change.

### 2026-08-15 — `trunc`'s native lowering generalized to abs/floor/ceil/round

The numeric sys funcs split two ways, and the second family was
under-implemented.

**Always-float** (`mir_native_math_always_float`): sqrt, cbrt, hypot, log*,
exp*, trig, hyperbolic. Lowered to a direct C call returning a raw double —
no Item, so **no adopt is possible**. This is why they never appeared in the
adopt census.

**Type-preserving**: floor, ceil, round, trunc, abs, sign. Excluded from that
whitelist for a real reason — their result type follows the *argument's*, and
lowering to C `fabs`/`floor` (which return `double`) would turn `abs(-3)` into
`3.0`. But that reason evaporates once the argument type is known, and
**`trunc` already exploited it** via a hand-written special case sitting among
unrelated one-offs (`SYSFUNC_FLOAT`, a u32 conversion, bitwise inlining). Its
four siblings never got the same treatment.

*Correcting an earlier statement in this log:* I described the split as
principled and put `floor` "on the correct side" of it. The whitelist is
principled; the implementation was not — `trunc` disproved the semantic
objection for the whole family.

**Generalized** rather than copied a fourth time (rule 13):
`mir_native_math_type_preserving(fn, &int_is_identity)` names the family and
carries the one bit that differs. `fn_numeric_rounding` literally does
`return item;` for the int family (sentinels included), so floor/ceil/round/
trunc are the identity on integers; `abs` is not, and keeps the boxed helper
for integer arguments where the int lane's sentinels need real handling.
`sign` has no `native_c_name` at all — it would need inline emission
(compare + select), not a call — so it stays boxed everywhere.

The result-type fact was already there: `mir_type_preserving_sysfunc_result`
covers all five. Only the lowering was missing.

| arg | before | after |
|---|---|---|
| `float` — abs/floor/ceil/round | boxed helper, **3 adopts each** | `fabs`/`floor`/`ceil`/`round`, **0 adopts** |
| `int` — floor/ceil/round | boxed helper, 3 adopts | **no call at all** (identity) |
| `int` — abs | boxed | boxed *(correct: |x| ≠ identity)* |
| `sign` | boxed | boxed *(no native C function)* |

**Value-neutrality A/B tested**, not assumed: restricting the predicate back to
TRUNC alone and re-running gives byte-identical output. Semantics verified
against hand-computed expectations — half-away-from-zero survives
(`round(2.5)` = 3, `round(-2.5)` = -3), and type preservation holds
(`floor(int)` → int, `floor(float)` → float, `abs(-5)` → 5 while its siblings
stay -5). Gate **3869/3869**.

Test: `test/lambda/proc/native_math_type_preserving.ls` pins the semantics the
boxed path existed to protect — result-type-follows-argument and the rounding
mode — plus `sign` still working on the boxed path.

This also retires `fn_floor`'s adopt, the last one flagged as "genuinely
wide-capable" in the census, as a *consequence* rather than by special-casing
it. Remaining adopt sources are `pn_push` ×4, `pn_splice` ×2, `fn_slice3` ×2 —
all container-only, so all candidates for the `fn_fill` row treatment.

### 2026-08-15 — helper-call adopts: 417 → 335 (user rulings 1 & 2)

Ruling (1) *"change sys func signature, if possible, to eliminate helper
adopts"*; ruling (2) *"C helpers and sys funcs use the number stack for wide
returns"*. Ruling (2) is already the shipped behaviour (RV14) — what it
licenses is deleting the rehome wherever a helper's result **cannot** be wide.

**Census first, because the AWFY number was misleading.** Over 119 corpus
scripts there are **417 adopt sites from ~60 helpers**, not the 9 AWFY
exercises. Statically, **153 of 221** sys funcs have metadata permitting an
adopt (151 declared `TYPE_ANY`, 2 `TYPE_FLOAT`). The AWFY "under 1% of emitted
MIR" figure measures benchmark coverage, not the population.

**The signature route was tried first, and failed instructively.**
`SysFuncInfo.success_type` exists for exactly this — it describes the
*successful* result, so a non-wide success type proves the whole result
non-wide (the error arm is an error Item, never wide). `jit_import_get_metadata`
now consults it, which is a strict generalization: identical behaviour whenever
`success_type` is unset. That part is kept.

Populating it for the container builders was the failure. `&TYPE_LIST` is the
array singleton under a legacy name — `{.type_id = LMD_TYPE_ARRAY}` — but
consumers of an ARRAY-typed `Type*` **cast it to `TypeArray*` and read
`->nested`, past the end of the global**. ASan: global-buffer-overflow,
16 structurizr tests aborting. *An array success type needs a real `TypeArray`
instance, not the singleton* — recorded as a comment at the extern block so the
next attempt doesn't repeat it.

A second, separate lesson from the same attempt: setting `may_return_error =
true` alongside it made inference wrap the result as `array | error` — a
union, whose `type_id` **is** `LMD_TYPE_TYPE` — producing "if condition has
container type type". That field changes the language surface; it is not a
free annotation.

**Shipped instead: explicit `jit_runtime_imports` rows**, the `fn_fill`
mechanism — JIT-only, no inference impact, no `Type*` object involved. Ten
helpers whose every return path was read and is a container or error Item:
`fn_take`, `fn_drop`, `fn_unique`, `fn_reverse`, `fn_reshape`, `fn_sort1`,
`fn_sort2`, `fn_slice3`, `pn_push`, `pn_splice`. Effects deliberately
conservative — MAY_GC (they allocate), REENTRY_UNKNOWN (sort can call a user
comparator); only the `RESULT_SCALAR_STABLE` bit is claimed.

| | sites |
|---|---|
| session start | 417 |
| after `fn_fill` | 409 |
| after the ten container rows | 335 |
| after the thirteen string/binary rows | **309** |

Remaining population, by class: **~200 legitimate** — the explicit wide boxers
(`box_int64_value` 61, `box_int64_result_or_error` 25) and numeric aggregates
(`fn_avg1` 39, `fn_sum1` 37, `fn_max1` 21, `fn_min1` 18, `fn_math_*`), which
really can return a frame-backed float or int64; **38** `owned_item_slot_read`,
a runtime slot read that can legitimately hold anything; and the string-builder tail, since read and claimed (below).

Gate **3888/3890**; both failures pre-existing (`test_interp_gtest`
`ExcludedScriptsAreCountedNotInterpreted` fails on a clean tree;
`side_stack_frame_gc` passes standalone — parallel-load flake).

**Next, per the user's sequencing**: the loop- or statement-level reclaim,
which is what would let the *legitimate* ~200 drop their rehome too. RVO11's
container half is already verified clear (`array_set` copies into array-owned
cells); the bindings half remains.

### 2026-08-15 — string/binary builders claimed too: 335 → 309

The tail I had declined to claim on a name is now read. Thirteen more, each
verified by following every return path — several through one or two levels of
delegation, which is why the grep-level check earlier was inconclusive:

| helper | returns |
|---|---|
| `trim` / `trim_start` / `trim_end` / `lower` / `upper` | `ItemError` \| `ItemNull` \| **`str_item`** (the input Item) \| `s2it` \| `y2it` — string/symbol POINTERS |
| `join2` | `ItemError` \| `s2it` |
| `find3` | → `fn_find_impl`: `ItemError` \| `{.array}` |
| `replace3` / `replace4` | → `fn_replace_impl`: `ItemError` \| `ItemNull` \| `str_item` \| `s2it` \| `y2it` |
| `split2` | `ItemError` \| → `fn_array_split` (`ItemError` \| `{.array}`) \| `{.array}` |
| `binary` | the input when already BINARY \| `ItemError` \| `x2it(bin)` \| → `binary_from_typed_array` / `_dataview` (`ItemError` \| `ItemNull` \| `x2it`) |
| `argmin` / `argmax` | → `vector_arg_extreme`: `ItemError` \| `ItemNull` \| **`i2it`** — inline int boxing, an index, not a number home |

None can produce a number-home-backed value. Two details that only reading
caught: the trim/case family returns **its own input Item** unchanged on the
no-op path (a string pointer, so still fine), and `argmin`/`argmax` box through
`i2it`, which is inline — an index could plausibly have been a wide int64, and
isn't.

**Aliased JIT names matter here**: `fn_replace3` → `FPTR(fn_replace)` and
`fn_split2` → `FPTR(fn_split)`. The `jit_runtime_imports` key is the JIT-visible
name, not the C symbol; pairing them from the `sys_func_defs` rows rather than
guessing is what makes the row take effect.

**Deliberately NOT claimed: `fn_string_ascii_at`.** Its non-string path falls
back to `item_at()`, which can hand back any element — wide included. One
site; not worth a wrong claim.

Gate **3889/3890** — the single failure is `test_interp_gtest`
`ExcludedScriptsAreCountedNotInterpreted`, confirmed failing on a clean tree.

**Cumulative: 417 → 309 (−26%).** What remains is essentially all genuine:
`box_int64_value` 61, `fn_avg1` 39, `owned_item_slot_read` 38, `fn_sum1` 37,
`box_int64_result_or_error` 25, `fn_max1` 21, `fn_min1` 18, `fn_float` 12 —
wide boxers, numeric aggregates, and a runtime slot read. Metadata has taken
this as far as proof allows; the rest needs the reclaim.

### 2026-08-15 — RV14 implemented: helper-call adopts 309 → 0 (Lambda)

The caller no longer rehomes a C helper's wide result. Per RV14 the helper owns
no number-frame watermark, so the payload rides the number stack and the Item
pointing at it is already caller-homed; it stays valid until the caller's own
epilogue restores to `number_base`.

**Corpus-wide the adopt count is now 0** (was 417 at session start, 309 after
the metadata work). AWFY home share: deltablue 0.5%, havlak 0.2%, json and
richards 0.0%.

**One real issue, and it is not uniform — worth stating because it decides how
urgent the reclaim is.** Dropping the rehome drops the eager watermark restore
with it, so space goes from peak-liveness to total-allocations-per-activation:

- **float results leak essentially nothing.** `push_d` → `flt2it` boxes
  INLINE for zero and for any double carrying `ITEM_DBL_MASK`; only tiny and
  subnormal doubles reach `box_float_number_stack`. avg/sum/float/math_* are
  therefore near-free.
- **`box_int64_value` allocates on every call by construction** — its own
  comment: *"INT64 never uses an inline Item so every transient value follows
  the same return and ownership protocol regardless of magnitude."* 61 sites.

Measured, not extrapolated: an `int64` accumulator loop runs clean at 10k,
100k and 1M iterations (1M ≈ 8 MB of the 64 MB reserve) and at 9M returns a
clean `error` — `lambda_side_number_alloc` fails, `box_int64_value` calls
`lambda_stack_overflow_error` and returns `ItemError`. **The degradation is a
reported error, never a dangling pointer**, because the payload stays valid for
the whole activation either way.

**The change is scoped to Lambda, and finding out why is the useful part.**
The first cut edited the shared `em_call_import` unconditionally and broke
`test/js/regression_side_stack_frame_gc.js`. LambdaJS still emits the **v2
caller-donated-home return protocol** — stated in
`js_mir_module_batch_lowering.cpp`: *"LJS keeps emitting v2 until P2.5"* —
and that protocol's callee epilogue restores its own watermark, which frees a
payload the caller never rehomed. The test is precisely targeted: its
`Number.MIN_VALUE` is subnormal, the one float case that genuinely
number-stacks.

So the behaviour is gated per front-end by `MirEmitter.helper_results_skip_rehome`.
**Sense matters**: the emitter is zero-initialized, so FALSE is the safe value —
a front-end that never heard of the flag (Jube sets `call_owner` without it)
keeps the rehome. The first version had it inverted, which would have opted
unaudited front-ends into the risky path by default.

**Two emission fixtures updated, not silenced.** `sized_int_boxing` asserted
`lambda_item_adopt_scalar_home` ≥ 2 and `scalar_home_donation`'s `main` group
asserted the adopt in sequence — its description even claimed that group was
"convention-independent … which v3 does not change". RV14 falsifies that. Both
now pin the boxing calls that remain and **forbid** the adopt, so the new
invariant is enforced rather than merely unasserted.

Gate **3889/3890** — the one failure is the pre-existing `test_interp_gtest`
`ExcludedScriptsAreCountedNotInterpreted`, which fails on a clean tree.

**Next: the reclaim** (loop or statement level), which restores the space
bound. RVO11's container half is verified clear; the bindings half remains, and
the int64 population above is what makes it worth doing.

### 2026-08-15 — measured: does anything hit the space bound without the reclaim?

Asked directly, so measured directly rather than reasoned about. A temporary
peak probe in `lambda_side_number_alloc_for` (env-gated, ~15 lines, removed
afterwards) recorded the highest number-stack depth reached per run.

**All 31 benchmark scripts run clean** — no failures, no
`number-side-stack` overflow. And the peak tells a stronger story than that:

| corpus | result |
|---|---|
| AWFY, all 30 scripts | **peak = 0 slots.** Not "low" — none of them ever produces a wide scalar needing a number home |
| 140 `test/lambda` scripts | only **7** touch the number stack at all |

Deepest anywhere: `int64.ls` at **164 slots = 1,312 bytes**, which is
**0.002%** of the 64 MB reserve — about a 50,000× margin. The rest:
`math_random` 25 slots, `js_array_props_tail_bridge` 16, and four scripts in
single digits.

Why AWFY is exactly zero, rather than merely small: those benchmarks compute in
normal doubles and `int`, and `flt2it` boxes both inline. Only tiny/subnormal
doubles and `int64` reach `box_float_number_stack` / `box_int64_value`, and
AWFY uses neither.

**So the reclaim is not urgent for any workload in the tree.** It remains
correct to build — the bound is O(allocations-per-activation) and a program
that boxes int64 in a long loop *will* reach it (measured earlier: clean error
at ~9M) — but nothing shipping is near it, and the failure mode is a reported
error rather than corruption. That reprioritizes it from "needed to make RV14
safe" to "needed to close the complexity bound".
