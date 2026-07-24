# Lambda Impl Plan: Tune 7 — Dynamic-Call Path Slimming (R4)

**Status: PLANNED — 2026-07-24.**
**Successor of:** `vibe/Lambda_Impl_Tune6.md` (Track J kills the *lookup* half of
`obj.m()`; this plan owns the *invoke* half that remains). Implements
`Lambda_Tuning_Proposal.md` **R4** ("Reduce dynamic-call overhead", slices
R4.1–R4.4) as a phased plan.

**Baseline assumptions (verify at T0, do not re-litigate):**

1. **`vibe/Lambda_Impl_Merge_Stack.md` Merges A/B/C are COMPLETE.** In
   particular Merge A: call-argument frames live on the **side-root region**
   (marks are `side_root_top` values), the private `js_args_stack` is deleted,
   and above-top slots stay zeroed. This plan *depends* on that lifetime fact
   (Phase C2). Merge A's Phase 4 (inline save/restore of the two `js_args_*`
   C calls) is deferred-by-default there; if it did not land, **C2.4 adopts it
   verbatim**.
2. **`vibe/Lambda_Impl_Online_Exception (done).md` is landed** — the per-call
   exception-poll tax (R5's emission side) is owned there and is **out of
   scope** here; only residual poll counts appear in the exit measurement.
3. **Tune6 has landed and Result13 exists.** Post-J1, a method call resolves
   its `JsFunction` via the PIC; what is left of `obj.m(a,b)` is exactly the
   dispatcher/trampoline sequence this plan slims. All Tune7 gates are ratios
   against a same-day Result13-state baseline; absolute thresholds are set
   when Result13's numbers are in hand (§7).

**Diagnosis provenance:** source verification of `js_call_function_impl` /
`js_invoke_fn` / call-site emission, 2026-07-24. `file:line` refs verified same
day; they drift — confirm against symbol names before editing.

**Governing invariant.** Every phase is a pure performance change: no
observable semantic change to JS programs (timing, memory, GC counts
excepted). Two hard rules inherited from R4 and CLAUDE rule 15:

- **The generic `js_call_function` path remains the semantic authority.**
  Every slice is a *guarded shortcut in front of it*; unsupported shapes fall
  through unchanged. No slice may fork call semantics.
- **Exact rooting is never weakened.** A value live across a MAY_GC boundary
  is GC-visible via canonical slots, the side-root region, or a rooted global
  — proven, not assumed. Forced-GC stress runs on every phase.

Gates at every phase boundary: `test-lambda-baseline` 100%, test262 baseline
(40261) zero regressions, `make node-baseline` (3528) zero regressions, MT7
emission budgets lifted only deliberately with dump diffs quoted.

---

## 0. What one dynamic call pays today, and who owns it

Verified sequence for `f(a, b)` / post-J1 `obj.m(a, b)` reaching the dynamic
path (`js_call_function_impl`, `js_runtime.cpp:13294`):

| # | Cost item | Mechanism (verified) | Owner |
|---|---|---|---|
| 1 | Exception poll after the call | inlined / elided by online-exception tracker | **Done (R5)** |
| 2 | Method/property lookup | PIC (Tune6 J1) or IC hit | **Tune6** |
| 3 | Args save/restore marks | 2 C calls per argument-carrying call | **Merge A P4 → C2.4** |
| 4 | Depth guard + debug name stack | thread-local 64-deep name push/pop **maintained in release** (`:13298–13307`) | **C1.3** |
| 5 | `RootFrame(2 + argc)` + per-arg copy loop | every arg re-copied into fresh root slots (`:13314–13323`) even when args already live in the (merged) side-root region | **C2** |
| 6 | Non-callable / proxy / `.call`-polyfill checks | 3 branches + map probes on the miss paths (`:13328–13353`) | stays (cold) |
| 7 | Scalar-home borrow logic | `LAMBDA_SCALAR_HOME` + ABI flag test (`:13393–13402`) | stays (cheap) |
| 8 | Bound-function branch, `OrdinaryCallBindThis` coercion (duplicated in bound + plain blocks, `:13623`/`:13723`) | branchy, mostly-false tests | **C1** (folded into shape test) |
| 9 | ~10 save/install/restore pairs: `this`, `new.target` + pending handshake, module vars, realm swap check, eval-initializer flag, generator proto (`js_property_get`!), derived-ctor TDZ, private-home class, vm-stack source | the bulk of the function | **C1** |
| 10 | `js_function_home_class` per call | **hash lookup** on `fn->properties_map` (`js_runtime.cpp:513`); early-out only when the map is empty | **C1.1** |
| 11 | `with`-stack save + set per call | `js_with_save_stack` + `js_with_set_stack` (`js_globals.cpp:16379/:16390`) run on **every** call (`fn->func_ptr || with_env_depth > 0` — `func_ptr` is non-null for all compiled functions); set re-registers roots + clears + invalidates the binding cache | **C1** |
| 12 | `this`/`new.target` global round-trips | 4+ global loads/stores even for callees that never observe them | **C3** |
| 13 | Arg adaptation (pad/clamp/rest) + 16-case arity switch × {env} × {ABI} | `js_invoke_fn_raw` (`js_runtime.cpp:9425–9616`) | **C4** (measured) |
| 14 | Calls that shouldn't be dynamic at all | native eligibility requires INT/FLOAT *return* (phase 1.75, `js_mir_module_batch_lowering.cpp:5823`) — numeric-param/boxed-return functions are excluded | **C4.1 (R4.1)** |

Owned elsewhere / not this plan: builtin dispatch internals (`js_dispatch_builtin`),
GC frequency × live set (R7), unboxed slot storage (OI-9), interpreter link mode.

---

## 1. Evidence appendix (verified code facts)

- **`js_call_function_impl`** (`js_runtime.cpp:13294`): rows 4–12 above, in
  that order, on every dynamic call. The full state save/restore block is
  duplicated once for the bound path (`:13644–13714`) and once for the plain
  path (`:13717–13795`).
- **`JsFunction`** (`js_function.hpp:9`): `uint16_t flags` uses bits 1..512
  (`:54–63`) — **6 bits free**; the struct already carries every field the
  fast-lane predicate needs (`with_env_depth`, `home_global`,
  `vm_stack_filename/source`, `eval_initializer_context`, `bound_args`,
  `builtin_id`, `module_vars`) *except* home-class, which hides behind a
  `properties_map` hash probe.
- **Home-class single writer:** the only writer funnels through
  `js_property_set(fn_item, home_key=__home_class__, home)`
  (`js_runtime.cpp:611`) — one choke point for a `HAS_HOME_CLASS` flag.
- **`with` save/set:** `js_with_save_stack` copies `min(depth,16)` Items and
  returns depth (`js_globals.cpp:16379`); `js_with_set_stack` re-registers the
  root range, clears deeper entries, and invalidates the with-binding cache
  (`:16390`). Both run unconditionally per call (row 11's gate).
- **Trampoline:** `js_invoke_fn_raw` pads/clamps/rest-builds
  (`js_runtime.cpp:9461–9496`), then dispatches through typed casts `P0..P16H`
  (`:9425–9458`, switch bodies `:9526–9615`). `JS_FUNC_FLAG_MIR_PUBLIC_ABI`
  appends the trailing `uint64_t* scalar_result_home` (SG2 ABI).
- **No `uses_this` analysis bit exists.** `JsFuncCollected` has
  `uses_arguments` only (`js_mir_context.hpp:188`); `this` in a non-arrow body
  lowers to `js_get_this()` calls at each use, and arrows capture `_js_this`
  at closure creation (JS_05 §6). C3.0 must *add* the analysis bit.
- **Merged args stack (assumption 1):** post-Merge-A, an args frame is a
  side-root sub-range below `Context.side_root_top`; a caller's mark outlives
  its call by the watermark discipline, and above-top words are zeroed. Hence
  "args pointer within the bound side-root extent" ⟹ "args are GC-visible for
  the duration of the call".
- **Census hook exists:** `JS_EXEC_PROFILE_SCOPE(JS_EXEC_PROF_CALL_FUNCTION)`
  already brackets the dispatcher (`js_runtime.cpp:13296`).
- **Callsite-widening history:** the reverted fast-path widening
  ("regressions … `Object.defineProperty`, `Object.seal`" note at
  `js_mir_expression_lowering.cpp:9621`) is a standing warning for C4.1: widen
  *eligibility*, never skip the semantic wrapper.

---

## 2. Phase T0 — call-shape census and probes (½ day)

Reuse `temp/tune4_probes.sh` protocol (3-run medians, timestamped JSON, never
overwrite), extended with a `t7` phase.

- **T0.1 Call-shape census.** Add release-safe counters (`JS_CALL_STATS` env +
  atexit dump, the Tune6 T0.2 style) classifying every
  `js_call_function_impl` entry: plain / bound / builtin / with-active /
  realm-switch / home-class / generator / async / derived-ctor / vm-source /
  non-callable, plus `argc` histogram and args-pointer-in-side-root-extent
  hit rate. **This decides C1's coverage claim**: if the plain fraction on the
  OO probes is below ~85%, find which condition disqualifies before building
  the lane. Do not skip.
- **T0.2 Per-call microbench.** `temp/tune7_call_bench.js` (+ `.ls` control):
  tight loops over — plain 0-arg call, plain 2-arg, closure (env) call,
  method call via warmed PIC, bound call, `new` call — reporting ns/call.
  Benchmark rows alone cannot attribute per-slice wins; this harness is the
  primary gate instrument for C1–C4.
- **T0.3 Probe rows.**

| Phase | Primary probes | Guard probes (no regression >3%) |
|---|---|---|
| C1 | LJS richards, deltablue, crypto_sha1, hashmap (post-Tune6 residuals) | LJS json, sieve; all Tune6 J-gates must hold |
| C2 | same + cd, havlak (arg-heavy) | jetstream/splay |
| C3.0 | richards, deltablue + T0.2 microbench | full test262 `this`-semantics families |
| C4.1 | rows whose hot callees are numeric-param/boxed-return (census-picked) | kostya/matmul, larceny/ray (Tune4 M1 wins hold) |

---

## 3. Phase C1 — call-shape flag + the plain-call fast lane (R4.2, 2–3 days; the centerpiece)

**C1.1 Precompute the shape, kill the per-call probes.** Two new flag bits in
the free `uint16_t` space:

- `JS_FUNC_FLAG_HAS_HOME_CLASS` (1024): set at the single writer
  (`js_runtime.cpp:611` funnel; audit for any second writer by grepping
  `JS_HOME_CLASS_KEY` — currently none). Row 10's per-call hash lookup
  becomes a flag test; the lookup runs only when the flag is set.
- `JS_FUNC_FLAG_PLAIN_CALL` (2048): derived at every `JsFunction`
  construction/mutation choke point (`js_new_function`, `js_new_closure`,
  `js_new_method_function`, bound-function creation, the home-class writer,
  `with`-capture wrapper creation, vm-source stamping). Definition —
  ALL of: `builtin_id == 0`, no
  generator/async/async-gen/derived-ctor/bound flags, `with_env_depth == 0`,
  `home_global.item == 0`, `!HAS_HOME_CLASS`, no vm-stack source,
  `!eval_initializer_context`. **The predicate lives in one function**
  (`js_function_call_shape_recompute(fn)`) called by every stamping site, so
  the condition list is auditable and single-sourced.

**C1.2 The lane.** In `js_call_function_impl`, after the callee resolves to a
`JsFunction*`: if `PLAIN_CALL` **and** the global `with` stack is empty
(`js_with_stack_depth == 0` — caller-side state, cannot be precomputed):

```
save this/new.target (2 locals) → OrdinaryCallBindThis (shared helper, not a
third copy — CLAUDE rule 13: extract the existing duplicated block first) →
install this; consume-or-clear pending new.target; set js_pending_args_callee
→ module-vars switch (2-field compare, stays) → js_invoke_fn → restore the two
globals → js_finish_borrowed_scalar_result
```

Everything else — with save/set, realm swap, generator proto, TDZ push,
private-home install, vm-source push, eval-initializer flag — is *provably
dead* under the predicate and is skipped, not conditionally executed. The
existing full body remains untouched below the lane as the fallback and
semantic authority.

**C1.3 Debug bookkeeping out of the hot path.** The thread-local call-name
stack (`:13298–13307`) moves behind `js_runtime_trace_enabled()` (checked
once, cached). Accepted cost: release-build "not a function" diagnostics lose
the name backtrace unless the trace env is set — the error itself, callee
`FuncDebugInfo`, and arg dump remain. The depth guard (stack-limit RangeError)
**stays unconditionally** — it is semantics, not diagnostics.

**C1.4 Consistency harness (the risk mitigation, mandatory).** A debug-build
mode (`JS_CALL_LANE_CHECK=1`) runs the fast lane *and* shadows the skipped
saves: before the lane returns it asserts the skipped state (with depth,
realm, private-home, generator proto, eval flags) is bit-identical to entry.
Runs on the full test262 + node baselines once per phase landing.

**Gates.** T0.1: ≥85% of dynamic calls on OO probes take the lane. T0.2:
plain 2-arg ns/call **≥1.5x** down. richards/deltablue/crypto_sha1
measurable; zero-regression gates; C1.4 harness clean.

---

## 4. Phase C2 — argument rooting dedup on the merged stack (1–2 days)

Premise (assumption 1): a JIT-edge caller's args frame already lives in the
side-root region and its mark outlives the call. The `RootFrame` arg-copy loop
(`:13318–13323`) is then a second, redundant rooting of the same values. C
helpers passing `LAMBDA_ALLOCA` arrays still need it.

- **C2.1 Range-check discriminator.** Add
  `lambda_side_root_contains(Context*, void*)` to `side_stack.h` (bound base ≤
  p < committed top; O(1)). In `js_call_function_impl`: when
  `arg_count > 0 && lambda_side_root_contains(context, args)`, skip the
  per-arg slot loop. **Keep the 2-slot func/this `RootFrame` in this stage** —
  callee and receiver arrive in C parameters and are not covered by the
  caller's arg frame.
- **C2.2 No caller audit needed by construction.** C-helper arrays fail the
  range check and keep the copy path; helper code that *forwards* a rooted
  args frame (e.g. trap/dispatch re-entries) legitimately passes the check —
  the frame is live for the outer call's duration, which encloses the inner
  one. Document this invariant at the check site.
- **C2.3 (stage 2, measured) Pre-rooted JIT entry.** Emit
  `js_call_function_prerooted` at generic call sites: skips even the
  func/this frame because the call-site safepoint write-back already
  published every live value — *verify* by asserting at emission that the
  callee/this temporaries are registered GC candidates
  (`em_root_note_candidate` coverage) before selecting the import. MT7
  budgets change (new import) — deliberate lift. Land only if T0.2 shows the
  2-slot frame is material after C2.1.
- **C2.4 Merge-A Phase-4 adoption (conditional).** If Merge A deferred its
  inline save/restore of the two `js_args_*` C calls, implement it here
  exactly per that plan's Phase 4 text (mark = MIR load of `side_root_top`;
  restore keeps the validity/order check — a plain store is forbidden). Same
  MT7 discipline. Skip if already landed.

**Gates.** T0.1's args-in-extent rate confirms coverage (expect ~100% of
JIT-edge calls); T0.2 arg-carrying ns/call drops; cd/havlak measurable;
**forced-GC stress green with the arg-copy loop disabled** — this is the
phase's correctness cliff and its real gate.

---

## 5. Phase C3 — receiver binding without global round-trips (R4.3, staged)

**C3.0 `this`-oblivious callees (2 days, flag-only, no ABI change).** Add a
collection-phase analysis bit `body_observes_this`: true if the body (or any
default-param expression) references `this`, `new.target`, or contains a
**direct `eval`** (runtime `this` access is then possible); arrows are true
iff they *captured* `_js_this` (existing capture machinery already computes
this). Stamp `JS_FUNC_FLAG_READS_THIS` (4096) at function creation from the
collected bit. In both dispatcher paths: when clear, skip
`OrdinaryCallBindThis` and the `js_current_this` install/restore pair
entirely. **The pending-`new.target` handshake is consumed unconditionally**
— a pending bit must never survive into the next call (leak = wrongness, see
Risks). Accessors invoked via `js_call_function` are safe by construction:
a getter/setter body that uses `this` has the flag set.

**C3.1 Receiver as hidden ABI argument (3–5 days, DEFERRED BY DEFAULT).** The
full R4.3: pass `this` like `env` (ordering `env, this, args…, home`) for
flagged method-shaped functions, threaded from `js_invoke_fn`'s parameter and
from direct call sites; `js_get_this()` in those bodies becomes a parameter
read. Global `js_current_this` remains for arrows/lexical capture and the
dynamic edge. **Decision gate:** land only if the post-C3.0 T0.2 profile
shows the remaining global traffic (callees that *do* observe `this`) is
material on the probe set. This is the plan's only ABI-touching slice: MT7
major lifts, mir_dump fixture review, and the JS_05 §6 doc update travel with
it.

**Gates (C3.0).** T0.2 plain-call ns drops further; test262 `this`-binding
families (sloppy-mode coercion, arrows, accessors, `eval`) zero regressions;
fixture set: direct-`eval`-reads-`this`, arrow capturing `this` called
dynamically, bound wrapper over an oblivious callee, pending-new.target
consumed when calling an oblivious constructor via `Reflect.construct`.

---

## 6. Phase C4 — fewer dynamic calls, thinner trampoline (R4.1 + R4.4, 1–2 days + measured tail)

- **C4.1 (R4.1) Widen native eligibility to boxed/void returns.** At the
  phase-1.75 eligibility computation (`js_mir_module_batch_lowering.cpp:5823`):
  allow numeric-param functions whose return is boxed or unused; the native
  body computes unboxed and **boxes at the return boundary inside `<fname>_n`**
  (or a thin `_nb` wrapper), so direct call sites escape the dispatcher
  entirely. The callsite-widening history (`:9621` note) is the guardrail:
  widen which functions get a native *version*; never widen which *call
  sites* may skip semantic checks. Callsite-propagation (phase 1.76) applies
  to the new shape unchanged.
- **C4.2 Adaptation precompute (measure-first).** Stamp an adapt-kind byte
  (EXACT / PAD / REST) + effective count on `JsFunction` at creation;
  `js_invoke_fn_raw` switches on it instead of re-deriving. Land only if
  T0.2 attributes measurable cost to adaptation — may be noise.
- **C4.3 Per-arity MIR invoke thunks (R4.4, DEFERRED BY DEFAULT).** Replace
  the 16-case switch with a per-function thunk pointer generated at JIT time.
  Only if C1–C3 leave the switch visible in profiles; otherwise skip — the
  branch predictor likely already owns it.

**Gates.** C4.1: census-picked rows improve; direct-call sites to widened
functions verified in mir_dump (budget lift deliberate); numeric-edge golden
(NaN/±0/overflow) identical boxed-vs-native. C4.2/C4.3: T0.2 delta or drop.

---

## 7. Sequencing, exit, and Result14

```
T0 → C1 → C2 → C3.0 → [C3.1?]     (dispatcher track, serial)
T0 → C4.1                          (emitter track, independent, may interleave)
C4.2 / C4.3 / C2.3                 (tail: only where T0.2 shows residual)
```

Prerequisites: Tune6 landed (Result13), Merge Stack complete, online-exception
landed. C1 first — it re-baselines every later ns/call measurement and its
census instrumentation is what gates the rest. Each phase lands independently
green.

**Exit = Result14** (same protocol as Result12/13: clean release build,
four-engine matrix, 3-run medians, 180s, raw JSON preserved, QuickJS control).
Thresholds are finalized against Result13 when it exists; the *shape* of the
targets, honest about what a per-call constant tax can buy:

| Metric | Baseline | Target |
|---|---|---|
| T0.2 plain 2-arg dynamic call (ns/call) | measured at T0 | ≥ 2x reduction (C1+C2+C3.0 combined) |
| T0.2 method call via PIC (ns/call) | measured at T0 | ≥ 1.5x reduction |
| LJS call-dense rows (richards, deltablue, crypto_sha1) | Result13 | additional ≥ 1.3x each |
| LJS/Node geo (dedup) | Result13 (≤8x targeted) | measurable improvement; no row regresses |

After Result14 the expected residuals are R7 (GC frequency × live set), OI-9
(unboxed slot storage), and whatever C3.1/C4.3 evidence says was rightly
deferred.

---

## 8. Risks

- **Fast-lane predicate drift is the correctness cliff.** Any *future* state
  added to the full dispatcher body (a new realm mechanism, a new per-call
  binding) silently breaks the lane if the predicate isn't updated. Mitigation
  is structural: one `js_function_call_shape_recompute` predicate, one lane,
  a comment contract at the full body's save block ("adding a save here
  requires a predicate review"), and the C1.4 dual-run harness on both
  baselines at every phase landing.
- **Pending-`new.target` leak (C3.0).** Skipping receiver binding must never
  skip handshake consumption; the `Reflect.construct`-on-oblivious-callee
  fixture pins it. Same for `js_pending_args_callee` when the callee uses
  `arguments` (`uses_arguments` functions are never `this`-oblivious *for the
  callee-stash*: keep the stash unconditional — it is one store).
- **C2 rooting soundness.** The range check must test the *bound* extent of
  the current context's side-root region only; a foreign pointer that happens
  to fall in-range would be silently trusted. The region is a single
  reserve-committed block per context (Merge A), so containment is exact, but
  the predicate must use the context's own base/top — never a cached global.
  Forced-GC stress with the copy loop disabled is the gate, plus one
  adversarial fixture: C helper calling back into JS with stack args across a
  collection.
- **C3.0 dynamic `this` escape hatches.** Direct `eval`, `arguments.callee`
  re-entry, and accessors are the known routes to observing `this` at
  runtime; the analysis bit's definition enumerates them, and any new dynamic
  facility (e.g. a future debugger hook) must default the flag to set.
  When in doubt, `READS_THIS` stays set — over-conservatism costs two global
  stores, never wrongness.
- **C4.1 semantic cliff.** Native versions must keep exact JS numeric
  semantics at the boxing boundary (−0, NaN payloads irrelevant but sign
  bits are not); the boxed-vs-native golden across the numeric edge table is
  the gate, mirroring Tune6 L3's discipline.
- **Diagnostics regression (C1.3).** Losing the release-build call-name
  backtrace is accepted and documented; `JS_RUNTIME_TRACE=1` restores it.
  If node-baseline triage proves to need it, demote C1.3 to debug-only
  gating of the *push* (keep depth), not a revert.
- **Machine-state variance.** All phase gates are same-day before/after
  ratios on one host; Result14 absolutes carry the QuickJS control column.
