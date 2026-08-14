# Lambda Impl Plan — Return-Value Convention v3 (Companion-Lane Returns)

**Date**: 2026-08-14  **Status**: NOT STARTED — plan for
[`Lambda_Design_Compiling_Return_Value.md`] (RV1–RV13 DECIDED 2026-08-14;
formal spec D5.2.1v2 / D2.7.2v2 / D8.4.2v2, v1.20.1). Open design residue:
RVO3 (interp bridge), RVO4 (async/yield), RVO8 (GVN multi-out), RVO9
(dummy lane-2) — each is resolved by a concrete step below.
**Tree anchor**: master `af254850f` (line refs are as of this commit;
anchor by symbol name when they drift).

**Goal**: retire the trailing-scalar-home ABI. Boxed returns become
`[item, scalar]` pairs discriminated by the pending-Item tag `0x1E`; typed
returns get native lanes with `^E` on an error-Item lane. Census baseline
to beat (2026-08-14, `utils/analyze_scalar_homes.py` over debug
`temp/mir_dump.txt`): home protocol = 9–39% of emitted MIR across AWFY
(deltablue 413 adopt sites / 38.7%), ~11–16 executed insns per boxed
return, 70–100% of functions carrying `_scalar_home`.

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

### P3 — Windows lowering *(RV12)*

- Add `Context::mir_companion_slot` beside `mir_return_lane` (`:1955`) —
  same non-GC-scanned, single-thread-owned contract as the existing
  double-bits scratch cell documented there.
- Emission under `_WIN32`: shape-2 wide arm stores payload to the slot,
  pending arm loads it; shape-4 error lane keeps the existing
  `mir_return_lane` mechanism (it already is this pattern). `nres` stays
  1 on Windows; everything else — descriptor, pending tag, resolution
  protocol — is byte-identical (RV12).
- Gate: Windows CI build + baseline; no mainline (SysV/aarch64) emission
  change at all.

### P4 — Formal/doc closure

- Already staged: D5.2.1v2 / D2.7.2v2 / D8.4.2v2 (spec 1.20.1). At this
  point flip the Appendix A footnotes from "decided, v1 ships" to landed
  status, and record the D5.2.2 discard-home/tail-forward clause lapse.
- Update `Lambda_Design_Stack_Frame.md` SF14 entry with the v3
  supersession note; refresh LR/JS overview docs' return-ABI sections.

### P5 — Delete v2 machinery *(gated on P1 + P2 + P2.5 all landed)*

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
| Debug/release divergence | the v2 emitter deliberately emitted identical MIR in all builds (`:971` comment); v3 keeps that rule — flag is the only axis |

---

## 4. Deliverable checklist (roll-up)

- [x] P0 encoding + asserts + tripwires *(gated 2026-08-14: 3718/3718)*
- [x] P1.1 `FnReturnShape` descriptor, single-source assert *(same gate)*
- [x] P1.2 callee pair returns; epilogue adopt deleted (flagged)
- [x] P1.3 caller resolution protocol + patch helper *(EAGER form; RV6 lazy
      `maybe_pending` propagation is the remaining refinement)*
- [ ] P1.4 dynamic/universal entries, wrapper + invoke metadata
      *(includes the non-`local_func` direct-call fallback — see log)*
- [ ] P1.5 interp/ff bridges (RVO3 closed)
- [ ] P1.6 async/yield audit + tests (RVO4 closed)
- [~] P1.7 gate: baseline green both ways + emission fixtures cover both
      conventions; STILL OPEN — census re-run, MT7 re-ratchet, AWFY stdout
      diff, release timing/R26 canaries, RVO8/RVO9 measurements
- [ ] P2 native lanes + error lane; can_raise un-deopt; RV9 assert
- [ ] P2.5 LambdaJS migration (node/js262 gates)
- [ ] P3 Windows slot lowering (RV12)
- [ ] P4 formal-doc closure (footnotes → landed; SF14 note)
- [ ] P5 delete v2 machinery; final census + ratchet
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
unreachable.
