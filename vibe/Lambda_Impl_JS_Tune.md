# LambdaJS Performance — Implementation Plan, Tune 6

**Status:** draft v2 (v2 2026-07-16: PIC track withdrawn to OI-6; Tier-2 tracks D/E added)
**Date:** 2026-07-16
**Scope:** the design-ready performance-critical items from `vibe/Lambda_Issues_Outstanding.md` (OI-6 slice / JO13):
- **Track A** — conservative ADD inference recovery (JS_15 §5.4; measured ~12 ns → ~208 ns/call on `ack`, ~17×)
- **Track B** — float boxing traffic / register residency (JO13, JS_15 §5.1) + the INT-arithmetic-through-doubles rider (`+`/`-`/`*`; JS_04 §6)
- **Track D** — realm-scoped intrinsic-prototype cache + pristine-tamper flags (JS_15 §5.2; promoted from Tier 2 — its design direction is already specified and tamper-flag precedent exists in code)
- **Track E** — mechanical scaling fixes with no design dependency (JS_13 §5/§6 DOM wrapper/listener storage + per-access logging; JS_01 §9.1 per-compile transpiler struct; JS_15 KI-7 production gating)

**Withdrawn (2026-07-16):** the former Track C (shape-based PIC + duplicate-class-name deopt) — judged too complicated for this plan; its design record and open decision live in `vibe/Lambda_Issues_Outstanding.md` **OI-6**. Not promoted from Tier 2: destination-passing (deep codegen project), GC de-pinning (needs the JO Group-A doctrine design first), MIR artifact caching (design settled but a multi-stage project of its own, not a tuning item).

Tracks are independent — land in any order; A is the cheapest/highest-ROI and goes first.

**Design status per track** (see §6 for the open-items ledger):
- **A: settled.** The recovery shape is already specified in JS_15 §5.4; only the evidence predicate needs pinning down (A0).
- **B: committed work is B0/B1 only** — no representation change; B1 is deliberately the simple first double move. The unboxed-storage end-state (B2) is out of this plan's scope; its deferral decision, predecisions, and open design questions are recorded in `vibe/Lambda_Issues_Outstanding.md` **OI-9**.
- **D: settled.** JS_15 §5.2 records the fix shape (pristine-prototype tamper flag + realm-scoped cache) and the failure mode to avoid (the reverted process-global cache leaked prototypes across realms); in-code precedent for monotonic tamper flags already exists (`js_note_array_prototype_push_tamper`, `g_array_sym_iter_ever_set`).
- **E: nothing to design** — data-structure and build-hygiene work.

---

## 1. Baseline protocol (all tracks)

- **Correctness gates** (every landing): `make test262-baseline` (currently 40261 fully passed / 0 regressions), `make test-lambda-baseline` byte-identical, JS gtest suite green, `make editor-4c-js` 1931/1931. ASan run on the touched suites. `make node-baseline` on final candidates (long runtime — per-track landings may defer it to the track's last commit).
- **Perf measurement**: release build only (CLAUDE.md rule 10). Record a before/after table per track in this file's completion record, on the fixed benchmark set: `ack` (Track A), nbody/matmul/mandelbrot/spectralnorm (Track B), push-/array-method-heavy loops + kostya array benchmarks (Track D), DOM/editor suite wall-time + batch compile time (Track E), plus the 6 timing-out macro-benchmarks (havlak, cd, earley-boyer, navier-stokes, hashmap, raytrace3d) and richards/deltablue as the composite tracker — the tracks interact, so re-run the full set after each track lands.
- **The prior-revert lessons are gates, not history**: Track A's original regression was string-concat unsoundness; Track D's predecessor (a process-global prototype cache) leaked one realm's prototype into another and was reverted. Each gets a dedicated regression fixture before the optimization lands (A1c, D1c).

---

## 2. Track A — ADD inference recovery

**Problem.** The correctness fix that made `+` inference conservative (a param used in `x + y` is no longer inferred numeric, because `+` is overloaded add/concat) boxed arithmetic in additive/recursive numeric functions.

- **A0 — pin the evidence predicate.** Define "provably non-string" for each operand: numeric literal; result of an arithmetic op that cannot produce a string (`-`, `*`, `/`, `%`, unary minus, bitwise); a param whose *every* call site passes a provably-numeric argument (reuse the existing `JsParamEvidence` machinery and `jm_callsite_propagate`); a variable whose reaching definitions are all provably numeric. Anything else — including `+` results themselves until proven — is not evidence. Record the predicate here before coding.
- **A1 — binary ADD numeric inference** when *both* operands pass A0. Includes: (a) the emit-side change in `jm_transpile_binary`/native-version eligibility; (b) fixed-point **return-type inference for self-recursion** (`ack`'s shape: `return f(...) + f(...)` — seed return type unknown, iterate to fixpoint, cap iterations); (c) **string-concat soundness fixtures first**: params that are sometimes-string across call sites, `+` under type-unstable loops, string-returning self-recursion — all must stay on the boxed path.
- **A2 — measure and close.** `ack` back to the ~12 ns/call class; 0 test262 regressions; note which other benchmarks move (deltablue/richards have additive helpers too).

Exit: measured table recorded; JS_15 §5.4 and the OI-6 row updated.

## 3. Track B — integer multiply + float register residency

- **B0 — native INT arithmetic (`+`, `-`, `*`) under exactness guards.** Today *all three* lower through doubles even when the static types are int: `JS_OP_ADD`/`SUB`/`MUL` compute `both_int` (`js_mir_expression_lowering.cpp:2112`) but never use it — both operands convert via `jm_transpile_as_native(…, LMD_TYPE_FLOAT)` (`I2D` for the int side) and emit `DADD`/`DSUB`/`DMUL` (`:2116`/`:2124`/`:2132`). Replace with native int64 ops guarded so the integer result is provably bit-identical to the JS-mandated double result:
  - **multiply**: both magnitudes < 2²⁶ → product < 2⁵² (< 2⁵³ double-exact, < 2⁵⁵ int56-boxable);
  - **add/sub**: both magnitudes < 2⁵² → result magnitude < 2⁵³, exact.
  Emit as compare-and-branch, no helper call; guard failure falls to the existing double path (correct for all inputs, just slower). The result stays int-classified — downstream consumers keep the int lane, and boxing on escape is the inline int56 tag. `DIV`/`MOD` stay on the double path by design (`7/2 === 3.5`; `x % 0 → NaN`). *(Impl option if profiling justifies: a helper-based full-range overflow check for the wide case — but the bounded fast path covers loop counters/indices/hash loops and keeps zero C calls.)*
- **B1 — loop-scoped scalar replacement (no representation change).** Keep a shaped float field in a `MIR_T_D` register across loop iterations when eligibility holds within the loop body: the object reference is loop-invariant; every access to the field goes through the shaped-slot path; **no escaping call** (any helper/user call that could observe or mutate the object flushes the register back to the slot — helper calls are the common case, so eligibility is per-region between calls, not per-loop); no aliasing store through another reference (conservative: any store to the same shape's field via a different base flushes). Structure: mark eligible field-access chains during lowering, allocate a D-reg per (object, field) pair, spill at region boundaries/loop exits/suspension points. Start with read-residency (loads eliminated), add write-coalescing (store once per region) second — nbody's inner loop is read-heavy. **Shape-validity guard: hoist one shape check (a `TypeMap*` compare) per (object, region)**; when the PIC is later designed (OI-6), it must share this guard mechanism — no second guard scheme.
- **B1c — fixtures**: aliased-object mutation mid-loop, field written via a second reference, generator `yield` inside the loop (residency must not survive a suspension point — SF20 barrier), forced GC mid-loop (D-regs hold raw doubles, never Items — no rooting interaction, but assert the flush discipline).
- **B2 — out of scope (deferred by decision, 2026-07-16).** The unboxed-storage end-state generalizes beyond doubles to all scalar kinds in maps and arrays and will be designed once, much later. The decision record, predecisions (no in-band tombstones; ArrayNum discipline; fields-vs-elements scope question), and open design questions live in `vibe/Lambda_Issues_Outstanding.md` **OI-9**. B1's measurements are its go/no-go input.

Exit: nbody/matmul/mandelbrot/spectralnorm table (B0 additionally re-measures the integer-heavy kernels: hashmap, havlak, cd); the class-vs-object-literal nbody gap should close from the object-literal side.

## 4. Track D — realm-scoped intrinsic-prototype cache

**Problem** (JS_15 §5.2): hot builtins pay a per-call pristine-prototype check that **re-interns strings like `"Array"`/`"prototype"` on every call** (`arr.push` is the measured case) to detect user overrides.

**Named conversion targets** (verified in code; D0 may extend the list, never silently shrink it):
1. **`Array.prototype.push`** — the measured case; tamper noted via `js_note_array_prototype_push_tamper` (called from generic property-set `js_runtime.cpp:6673` and delete `js_globals.cpp:13843`).
2. **`Array.prototype[Symbol.iterator]`** — `js_check_array_sym_iterator()` (`js_runtime.cpp:28875`) re-interns `"Array"`, resolves the constructor, and probes the prototype shape **per call** once `g_array_sym_iter_ever_set` has ever fired; consumed on both the `__sym_1` property-get path (`:4325`) and the fast array-iterator creation path (`:28949`) — i.e. every `for-of`/spread over an array pays it. Note: `g_array_sym_iter_ever_set` is a **process-global**, not realm-scoped — folding it into the realm registry fixes a latent cross-realm leak of exactly the class that got the old cache reverted.
3. **The generic Array-prototype writable-method probe** — "honor materialized prototype own properties before virtual builtin fallback" (`js_runtime.cpp:4331`): a per-lookup `js_get_prototype_of` + own-property probe in front of the builtin table, paid by **every Array builtin dispatched through property-get** (`map`/`filter`/`forEach`/`join`/`slice`/`indexOf`/`pop`/`shift`/…). Converting this one site covers the whole method family at once.
4. **Census seed for the remaining sites**: the **45 `heap_create_name("prototype")` per-call interning sites** (32 in `js_runtime.cpp`, 13 in `js_globals.cpp` — e.g. the `Date.prototype` resolution at `js_globals.cpp:1824`, Function-`prototype` walks, ctor lookups) — D0 classifies each as convert / already-cold / leave.

- **D0 — inventory + realm state.** Complete the census from seed (4) above and record the final convert list here. Add realm-owned state: cached intrinsic-prototype Items (Array/String/Object/Function/Date prototypes + well-known-symbol slots) + **monotonic pristine-tamper flags**, set on any mutation path that can touch an intrinsic prototype (`js_property_set`/`defineProperty`/`delete`/`Reflect`/descriptor paths targeting them). This generalizes the existing ad-hoc precedent (`js_note_array_prototype_push_tamper`, `g_array_sym_iter_ever_set`) into one registry — and retires those globals.
- **D1 — replace the per-call checks** for targets (1)–(3) and the census-confirmed sites: fast path reads the realm's tamper flag (one load + branch, no interning, no ctor resolution, no prototype probe); tampered → today's slow path unchanged. **D1c fixtures**: two-realm isolation (the reverted process-global cache's failure mode — one realm's tamper or prototype must never leak into another; now also covers the migrated `g_array_sym_iter_ever_set`), tamper-then-call correctness for every converted builtin (test262's prototype-tampering suites are the backstop), tamper-flag coverage from *all* mutation entry points.
- **D2 — measure**: push-heavy loops, `for-of`/spread-heavy loops (target 2), method-dispatch loops over arrays (target 3), kostya array benchmarks, and the composite; record which builtins were converted.

Exit: measured table; zero test262 regressions; the ad-hoc tamper globals folded into the registry.

## 5. Track E — mechanical scaling fixes (no design dependency)

- **E0 — DOM wrapper identity cache → hash map.** `lookup_dom_wrapper` is a linear scan over 4096-entry chunks (`js_dom.cpp:820`); key by `DomNode*` in a `lib/hashmap.h` table. Same treatment for the flat per-target **event-listener storage** (JS_13 §6). Gate: DOM/editor suites, plus a many-nodes/many-listeners microbench showing linear→constant lookup.
- **E1 — per-access DOM logging + dispatch ladder.** Remove or compile-gate the ~50 `log_debug` sites on DOM property access and reorder/table-ize the linear `strcmp` property ladder (JS_13 §5). Pure overhead on every property touch.
- **E2 — per-compile transpiler struct** (JS_01 §9.1): 45–55 MB allocated and zeroed per compile, with `func_entries[32768]` silently truncating. Lazy-zero / reuse across batch compiles / grow dynamically (fixes the silent cap as a side effect). Measured on batch compile throughput (test262 wall time).
- **E3 — production gating** (JS_15 KI-7): make `JS_TEST262_FAST_PATHS=0` actually compile the test262-only fast-path sites out of release builds.

Exit: DOM suite + batch-compile before/after; no behavior change anywhere (E is invisible to test outputs by construction).

---

## 6. Sequencing and interaction notes

A → B0 → D → B1, with E landing anytime (it's independent and invisible to outputs). A/B0/D are small and de-risk the measurement baseline for B1, whose benchmarks overlap theirs. Re-run the composite (6 timeouts + richards/deltablue) after each track: A and D both feed array/call-heavy workloads; declaring victory on any composite benchmark requires knowing which track moved it.

## 7. Open-items ledger for this plan

| Item | Status |
|---|---|
| A0 evidence predicate | to pin in-plan before A1 (detail-level; no separate doc) |
| B1 eligibility rules (escape/alias/suspension) | specified above; refine during impl, fixtures are the contract |
| B2 unboxed storage | out of scope — decision record + predecisions + open questions at `vibe/Lambda_Issues_Outstanding.md` OI-9 |
| PIC + dup-class-name deopt | withdrawn from this plan — design record + open invalidation-granularity decision at `vibe/Lambda_Issues_Outstanding.md` OI-6 |
| D0 override-check site inventory | to record at D0 exit (which builtins converted) |
