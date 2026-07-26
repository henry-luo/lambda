# Lambda Impl Plan: Tune 7 — Dynamic-Call Path Slimming (R4)

**Status: PLANNED — revised 2026-07-26 after Tune6 closed.**
**Successor of:** `vibe/Lambda_Impl_Tune6.md`. Tune6's Track J did **not**
land: its census found that richards missed the existing load IC on only
0.01% of probes, so J1/J3 were dropped. Tune7 therefore does not assume a new
method PIC; it measures the invocation share that remains after the current
IC behavior. Implements
`Lambda_Tuning_Proposal.md` **R4** ("Reduce dynamic-call overhead", slices
R4.1–R4.4) as a phased plan.

**Baseline assumptions (verified 2026-07-26 against the landed merges and
Tune6 execution record):**

1. **`vibe/Lambda_Impl_Merge_Stack.md` Merges A/B/C are COMPLETE and
   performance-repaired.** Merge A's final form has **no per-call argument
   ABI at all**: each generated function reserves its maximum lexically
   overlapping argument arity as a **fixed suffix of its canonical side-root
   frame** (`JsMirArgStackScope` / `jm_arg_frame_base`,
   `js_mir_completion.cpp:210`); call scopes store args into stable
   frame-relative slots and clear them on completion. Consequences for this
   plan: the former C2.4 (adopting Merge A's inline-save/restore Phase 4) is
   **moot and deleted** — the `js_args_*` protocol no longer exists — and the
   C2 premise strengthens: a JIT-edge `args` pointer is *always* a
   caller-frame suffix address inside `[side_root_base, side_root_top)`.
   The repair also landed `JS_FUNC_FLAG_USES_WITH` (1024) and gated
   `js_with_set_stack` install/restore behind real `with` state — part of
   §0 row 11 is therefore already done.
2. **`vibe/Lambda_Impl_Online_Exception (done).md` is landed** — the per-call
   exception-poll tax (R5's emission side) is owned there and is **out of
   scope** here; only residual poll counts appear in the exit measurement.
3. **Tune6 is closed and Result13 is the current floor.** Result13 is
   `test/benchmark/benchmark_results_v13.json` at commit `22eefe3f1`:
   MIR/Node dedup geo 2.94x, LambdaJS/Node 15.4x, QuickJS/Node control 7.45x.
   Tune6's same-day rerun was stable within 0.4%, but its retrospective also
   proved that sequential build-then-measure can manufacture a false small
   win. Every Tune7 phase therefore builds both release binaries first and
   interleaves A/B runs row by row; the Result14 matrix records the new floor
   but is not the phase attribution instrument.
4. **The call lane is census-designed, not premise-designed.** T0 records
   orthogonal call requirements and their combinations before C1 chooses a
   lane shape. `PLAIN_CALL` means "eligible for the common dispatcher
   template", not "every optional field is zero". If one lane is too narrow,
   C1 groups the smallest number of semantically coherent common flows
   (ordinary, method-home, caller/callee-with, realm-switch) needed for useful
   coverage; it does not weaken guards merely to hit a target.

**Diagnosis provenance:** source verification of `js_call_function_impl` /
`js_invoke_fn` / call-site emission, 2026-07-24. `file:line` refs verified same
day; they drift — confirm against symbol names before editing.

**Governing invariant.** Every Tune7 optimization phase is a pure performance
change: no observable semantic change to JS programs (timing, memory, GC
counts excepted). C3.0a is explicitly separated because it repairs an
analysis-order correctness gap before the optimization consumes that fact.
Two hard rules inherited from R4 and CLAUDE rule 15:

- **The generic `js_call_function` path remains the semantic authority.**
  Every slice is a *guarded shortcut in front of it*; unsupported shapes fall
  through unchanged. No slice may fork call semantics.
- **Exact rooting is never weakened.** A value live across a MAY_GC boundary
  is GC-visible via canonical slots, the side-root region, or a rooted global
  — proven, not assumed. Forced-GC stress runs on every phase.

Gates at every phase boundary: `test-lambda-baseline` 100% (3,609 at the
post-merge baseline), test262 baseline (40,261) zero regressions,
`make node-baseline` **no new failures vs the current honest baseline** (the
absolute count is red with ~1,473 pre-Result12 compatibility defects exposed
by the beforeExit exception-preservation fix — see
`Lambda_Impl_Merge_Stack.md`; gate on the per-test pass/fail delta, not the
count), MT7 emission budgets lifted only deliberately with dump diffs quoted.

---

## 0. What one dynamic call pays today, and who owns it

Verified sequence for `f(a, b)` / `obj.m(a, b)` reaching the dynamic
path (`js_call_function_impl`, `js_runtime.cpp:13294`):

| # | Cost item | Mechanism (verified) | Owner |
|---|---|---|---|
| 1 | Exception poll after the call | inlined / elided by online-exception tracker | **Done (R5)** |
| 2 | Method/property lookup | existing load IC hit/miss behavior; Tune6 J1 was dropped after the miss census | **out of scope; T0 records attribution** |
| 3 | Args save/restore marks | eliminated — fixed frame-suffix slots, zero per-call ABI (Merge A repair, 2026-07-25) | **Done (Merge A)** |
| 4 | Depth guard + debug name stack | thread-local 64-deep name push/pop **maintained in release** (`:13298–13307`) | **C1.3** |
| 5 | `RootFrame(2 + argc)` + per-arg copy loop | every arg re-copied into fresh root slots (`:13314–13323`) even though a JIT-edge `args` pointer is now always a caller-frame suffix address in the side-root region | **C2** |
| 6 | Non-callable / proxy / `.call`-polyfill checks | 3 branches + map probes on the miss paths (`:13328–13353`) | stays (cold) |
| 7 | Scalar-home borrow logic | `LAMBDA_SCALAR_HOME` + ABI flag test (`:13393–13402`) | stays (cheap) |
| 8 | Bound-function branch, `OrdinaryCallBindThis` coercion (duplicated in bound + plain blocks, `:13623`/`:13723`) | branchy, mostly-false tests | **C1** (folded into shape test) |
| 9 | ~10 save/install/restore pairs: `this`, `new.target` + pending handshake, module vars, realm swap check, array-dispatch mode, eval-initializer flag, generator proto (`js_property_get`!), derived-ctor TDZ, private-home class, vm-stack source | the bulk of the function | **C1** |
| 10 | `js_function_home_class` per call | **hash lookup** on `fn->properties_map` (`js_runtime.cpp:513`); early-out only when the map is empty | **C1.1** |
| 11 | `with`-stack save + set per call | **install/restore now gated** by `saved_with_depth > 0 \|\| with_env_depth > 0 \|\| JS_FUNC_FLAG_USES_WITH` (Merge A repair); residual per call = the unconditional `js_with_save_stack` C call + no-op `JsSavedWithScopeRoots` RAII | **mostly done; C1 folds the residual save into the lane's depth test** |
| 12 | `this`/`new.target` global round-trips | 4+ global loads/stores even for callees that never observe them | **C3** |
| 13 | Arg adaptation (pad/clamp/rest) + 16-case arity switch × {env} × {ABI} | `js_invoke_fn_raw` (`js_runtime.cpp:9425–9616`) | **C4** (measured) |
| 14 | Calls that shouldn't be dynamic at all | native eligibility requires INT/FLOAT *return* (phase 1.75, `js_mir_module_batch_lowering.cpp:5823`) — numeric-param/boxed-return functions are excluded | **C4.1 (R4.1)** |

Owned elsewhere / not this plan: builtin dispatch internals (`js_dispatch_builtin`),
GC frequency × live set (R7), unboxed slot storage (OI-9), interpreter link mode.

### 0.1 Metadata ledger — which cached fact skips which per-call cost

Every conditional step in the dispatcher is a per-call *question*; this ledger
records the cached *answer* that lets the runtime skip it, where that answer
is stamped, and its status. The phases below implement the "planned" rows;
the table is the single place to check bit assignments against
`js_function.hpp` before adding one.

| Per-call question (§0 row) | Cached answer (metadata) | Stamped at | Status |
|---|---|---|---|
| builtin callee? | `builtin_id > 0` | creation | exists |
| bound function? | `JS_FUNC_FLAG_HAS_BOUND_THIS` / `bound_args` | `bind()` | exists |
| `this` coercion + binding needed? | `STRICT`/`ARROW` (exist) narrow coercion; existing `fc->observes_this` is stamped as **`READS_THIS`** | existing body/lexical-arrow analysis, repaired to include default params; direct eval defaults set | **C3.0** |
| `new.target` install needed? | existing `fc->observes_new_target` is stamped separately as **`READS_NEW_TARGET`**; the pending handshake is still consumed on every ordinary call | same analysis | **C3.0** |
| private home class installed? (row 10) | dedicated `JsFunction::home_class` slot; zero means absent and removes the backing-map hash probe | internal method-home stamping; one helper traces GC-owned functions or roots pool-owned slots | **C1.1** |
| `with` machinery needed? (row 11) | **`USES_WITH` (1024)** + caller `js_with_stack_depth` read inline | transpiler | **landed** (Merge A repair); residual save call folded into the C1.2 lane |
| realm swap? | `home_global` compared with the current global at the call edge | creation + dynamic pair check | exists; common lane keeps it |
| array-dispatch mode reset? | caller-dynamic state; no function bit can precompute it | common lane saves/clears/restores exactly like the generic path | mandatory in **C1.2** |
| generator callee proto? | `GENERATOR` flag | creation | exists, gated |
| derived-ctor `this` TDZ? | `DERIVED_CTOR` flag | creation | exists, gated |
| vm-source frame push? | `vm_stack_filename/source` field test | creation | exists, gated |
| `arguments`-callee stash needed? | `fc->uses_arguments` exists at compile time but is not stamped on `JsFunction` | transpiler | keep the one store unconditional in Tune7 unless T0 proves it material |
| argument adaptation kind? (row 13) | `param_count` and its sign already encode declared arity/rest; exact vs pad/clamp remains call-dynamic | exists | **C4.2 / C4.3**, measure-first |
| args already GC-rooted? (row 5) | **provenance, not function metadata**: an explicit prerooted-args entry is selected only at verified JIT emission sites | call-site emission | **C2** |
| common call-flow requirements | census-derived `uint8_t call_lane_kind` (ORDINARY / METHOD_HOME / optional measured variants / GENERIC); `PLAIN_CALL` is the ORDINARY eligibility concept, not a conjunction requiring null `home_global` | finalized after function metadata is stamped; mutation funnels update it | **C1** (centerpiece) |

Three facts stay dynamic by nature and are *not* folded into
`call_lane_kind`: the callee type check, the module-vars switch, and whether
`home_global` equals the caller's current global. The common lane retains
those pair checks. Discipline for every cached row: a stale classification is
wrongness, not slowness — writers funnel through one recompute helper, and
any uncertain or dynamically compiled function defaults to GENERIC. The C1.4
checks detect state leaks; separate forced-generic differential runs validate
observable behavior.

---

## 1. Evidence appendix (verified code facts)

- **`js_call_function_impl`** (`js_runtime.cpp:13294`): rows 4–12 above, in
  that order, on every dynamic call. The full state save/restore block is
  duplicated once for the bound path (`:13644–13714`) and once for the plain
  path (`:13717–13795`).
- **`JsFunction`** (`js_function.hpp:9`): `uint16_t flags` uses bits 1..1024
  (`:54–64`; 1024 = `JS_FUNC_FLAG_USES_WITH`, added by the Merge A repair) —
  **5 bits free** (2048/4096/8192/16384/32768); the struct already carries
  every field the fast-lane predicate needs (`with_env_depth`, `home_global`,
  `vm_stack_filename/source`, `eval_initializer_context`, `bound_args`,
  `builtin_id`, `module_vars`) *except* home-class, which hides behind a
  `properties_map` hash probe.
- **Merged argument frames** (post-Merge-A final form): a call/new expression
  owns a `JsMirArgStackScope` with a stable `base_slot` in the caller frame's
  argument suffix; `jm_arg_frame_base` is one ADD patched to
  `root_slot_count * 8` at finalization (`js_mir_hashmap_scope_utils.cpp:465`);
  scopes clear their slots on completion and exceptional routing clears all
  active scopes (`jm_clear_active_arg_frames`). The
  `test/mir/js/arg_frame_roots.mir-check` fixture forbids any per-call
  save/push/restore symbol from reappearing.
- **Home-class storage is not safely flaggable from a constant grep.** The
  internal writer currently funnels through
  `js_property_set(fn_item, "__home_class__", home)`, but arbitrary function
  properties and `Object.defineProperty` also write `properties_map` through
  dynamic keys. C1.1 moves the internal home object to a dedicated,
  ownership-correct `JsFunction` slot instead of maintaining a flag derived
  from a mutable public backing map.
- **`with` save/set:** `js_with_save_stack` copies `min(depth,16)` Items and
  returns depth; `js_with_set_stack` re-registers the root range, clears
  deeper entries, and invalidates the with-binding cache. Post-repair, the
  set/restore pair runs only under `switched_with_stack`
  (`js_runtime.cpp:13767`); the save C call still runs unconditionally —
  the row-11 residual. The `USES_WITH` flag exists because an early `return`
  from a `with` body bypasses its generated pop, so call dispatch must still
  restore an empty entry stack for such callees.
- **Trampoline:** `js_invoke_fn_raw` pads/clamps/rest-builds
  (`js_runtime.cpp:9461–9496`), then dispatches through typed casts `P0..P16H`
  (`:9425–9458`, switch bodies `:9526–9615`). `JS_FUNC_FLAG_MIR_PUBLIC_ABI`
  appends the trailing `uint64_t* scalar_result_home` (SG2 ABI).
- **Receiver analysis already exists.** `JsFuncCollected` carries
  `observes_this` and `observes_new_target`, and direct-call lowering already
  uses both. The current capture pass computes them before collecting
  default-parameter references, however. C3.0 first repairs that ordering,
  then stamps the existing facts on `JsFunction`; it does not invent a
  parallel analysis.
- **Merged args stack (assumption 1):** post-Merge-A, a generated args frame
  is a side-root sub-range below the logical `Context.side_root_top`; a
  caller's mark outlives its call by the watermark discipline, and above-top
  words are zeroed. Address containment of only `args[0]` is not sufficient
  provenance: C2 uses an explicit JIT entry, and any diagnostic containment
  helper checks the aligned, overflow-safe span `[args, args + argc)`.
- **Census hook exists:** `JS_EXEC_PROFILE_SCOPE(JS_EXEC_PROF_CALL_FUNCTION)`
  already brackets the dispatcher (`js_runtime.cpp:13296`).
- **Callsite-widening history:** the reverted fast-path widening
  ("regressions … `Object.defineProperty`, `Object.seal`" note at
  `js_mir_expression_lowering.cpp:9621`) is a standing warning for C4.1: widen
  *eligibility*, never skip the semantic wrapper.

---

## 2. Phase T0 — call-shape census and probes (½–1 day)

Reuse `temp/tune4_probes.sh` for timestamped, never-overwritten raw output,
but adopt Tune6's corrected attribution protocol: build baseline and candidate
release binaries first, then alternate them row by row
`A/B/A/B/A/B`. Three-run medians remain the reported statistic. The
four-engine matrix is an exit record, not evidence for an individual slice.

- **T0.1 Orthogonal call-requirement census.** Add release-safe counters
  (`JS_CALL_STATS` env + atexit dump, the Tune6 counter style) at
  `js_call_function_impl`. Do not force calls into one exclusive label.
  Record a bitmask for each entry: non-callable / special constructor /
  builtin / bound / generator / async / derived-ctor / caller-with-active /
  callee-with / same-realm / foreign-realm / home-class / vm-source /
  eval-initializer / closure-env, plus array-dispatch-mode-active,
  module-vars-same/different, `argc`, and generated-args provenance. Dump the
  top requirement combinations and cumulative coverage for each primary
  probe, not only one global aggregate.
- **T0.1a Simulated lane coverage.** Before implementing C1, compute coverage
  for candidate lane shapes from the census:
  ORDINARY, ORDINARY+realm-switch, METHOD_HOME, and WITH. Start with the
  smallest semantic shape. If it covers less than ~85% of executable
  non-builtin dynamic calls in aggregate or less than ~70% on any primary
  probe, add the next most common coherent flow and re-simulate. A low hit
  rate changes the design; it is not solved by deleting a required guard.
- **T0.1b Real-workload validation.** Collect the requirement-combination
  table on every T0.3 benchmark row, then on one full test262 baseline and one
  full Node baseline. Test suites contribute shape diversity, not timing
  claims. Publish benchmark-weighted, test-weighted, and per-workload
  coverage separately so a high-frequency microbench or one long test cannot
  make a narrow lane look universal. Freeze C1's initial lane kinds only
  after this table exists.

**Recorded T0 PLAIN_CALL coverage (2026-07-26).** `JS_CALL_STATS=1` on
`temp/tune7_call_bench.js` counted 36,120,001 non-builtin dynamic entries.
The strict `PLAIN_CALL` / ORDINARY classification hit 30,100,000 entries:
**83.33%**. The separate METHOD_HOME classification hit 3,010,001 entries;
the two semantically compatible common-flow classifications together covered
33,110,001 entries: **91.67%**. This is shape-mix coverage only, collected
from the debug census build and not a performance measurement. In particular,
it must not be presented as aggregate workload coverage until T0.1b records
the per-benchmark, test262, and Node tables. The adversarial
`test/js/tune7_call_lanes.js` fixture intentionally measures much lower:
1/4 (**25%**) strict ORDINARY and 2/4 (**50%**) when METHOD_HOME is included.

The current release decision is deliberately narrower than the classification
coverage: release A/B measurements found the ordinary lane slower than the
generic semantic dispatcher, so ordinary PLAIN_CALLs remain on that dispatcher.
Only METHOD_HOME selects the common lane. Thus these percentages describe
validated eligibility and future optimization headroom, not the fraction of
calls currently taking a new fast path.

- **T0.2 Per-call microbench.** `temp/tune7_call_bench.js` (+ `.ls` control):
  tight loops over — plain 0-arg call, plain 2-arg, closure (env) call,
  method call via the warmed existing IC, same-realm call, forced
  foreign-realm call, class method/home-object call, bound call, and `new`
  call — reporting ns/call.
  Benchmark rows alone cannot attribute per-slice wins; this harness is the
  primary gate instrument for C1–C4.
- **T0.3 Probe rows.**

| Phase | Primary probes | Guard probes (no regression >3%) |
|---|---|---|
| C1 | LJS richards, deltablue, crypto_sha1, hashmap | LJS json, sieve; current IC behavior must hold |
| C2 | same + cd, havlak (arg-heavy) | jetstream/splay |
| C3.0 | richards, deltablue + T0.2 microbench | full test262 `this`/`new.target` semantics families |
| C4.1 | rows whose hot callees are numeric-param/boxed-return (census-picked) | kostya/matmul, larceny/ray (Tune4 M1 wins hold) |

---

## 3. Phase C1 — census-derived common call lanes (R4.2, 2–4 days; the centerpiece)

**C1.1 Store stable function-local facts.**

- Replace the per-call `__home_class__` backing-map lookup with a dedicated
  `JsFunction::home_class` Item. The internal method-home stamping helper
  stores it through one ownership helper: GC-owned functions trace the edge,
  while pool-owned functions register the slot exactly as other function
  Item fields do. First pin whether ordinary JS property operations on
  `"__home_class__"` have observable compatibility behavior; if they do,
  their mutation/delete/defineProperty funnels must keep the dedicated slot
  coherent. The optimization may not silently change that behavior.
- Add `uint8_t call_lane_kind`, finalized after function flags and metadata
  are stamped. Initial kinds are `GENERIC`, `ORDINARY`, and `METHOD_HOME`.
  Add `WITH` or another variant only when T0.1a shows it materially raises
  coverage. `js_function_call_lane_recompute(fn)` is the one classifier and
  is called by every later metadata mutation site.
- `ORDINARY` deliberately permits closures, normal async functions,
  non-null `home_global`, and module ownership. Closure env handling already
  belongs to `js_invoke_fn`; realm and module identity are caller/callee pair
  facts handled cheaply inside the lane. Builtins, bound functions, special
  constructors, generators/async-generators, derived constructors,
  vm-source functions, eval-initializer functions, and uncertain dynamic
  wrappers default to `GENERIC`.

This is the plan's answer to an overly narrow `PLAIN_CALL`: the common lane
models the common *flow*, not an all-zero object. The exact kinds are frozen
only after the per-probe census table is attached to the phase record.

**C1.2 Common core and grouped variants.** Insert the lane only after
non-callable/proxy/Function-prototype handling, Date/Function/dynamic-function
special cases, invalid-wrapper checks, and builtin dispatch. It sits
immediately before the currently duplicated bound/plain state blocks.

The ORDINARY core is:

```
save/reset array-dispatch mode
→ save this/new.target
→ OrdinaryCallBindThis via one extracted helper
→ install this; consume-or-clear pending new.target; set pending callee
→ conditionally switch module vars
→ compare home_global with current global; swap only when different
→ invoke
→ restore realm/module/this/new.target/array-dispatch mode
→ finish borrowed scalar result
```

The array-dispatch save/reset/restore is mandatory caller-dynamic semantics,
not optional debugging state. `home_global.item == 0` is **not** an
eligibility condition: ordinary functions currently capture a non-null
global at creation. Same-realm calls avoid the swap; foreign-realm calls
either use the same common core's conditional swap or a grouped realm
variant, whichever T0 measures faster.

For `METHOD_HOME`, add only the dedicated private-home install/restore around
the same core. For a measured `WITH` variant, add the existing save/set/restore
protocol; otherwise caller-with-active or callee-with calls fall back to
GENERIC. No variant duplicates `OrdinaryCallBindThis` or the common restore
epilogue.

**C1.3 Debug bookkeeping out of the hot path.** Measure and then move the
thread-local call-name stack plus `_trace_last_fn` / `_trace_total_calls`
maintenance behind `js_runtime_trace_enabled()` (checked once, cached).
Accepted cost: release-build diagnostics lose the name backtrace/last-call
counter unless the trace env is set — the error itself, caller
`FuncDebugInfo`, and arg dump remain. The depth guard (stack-limit RangeError)
**stays unconditionally** — it is semantics, not diagnostics. Land C1.3
separately so a zero delta drops it without coupling it to the lane.

**C1.4 Validation modes (mandatory).**

- `JS_CALL_LANE_CHECK=1` asserts state that the selected lane promises not to
  touch and checks that all saved state is restored at exit. This is a
  state-leak detector, not semantic equivalence proof.
- `JS_CALL_FORCE_GENERIC=1` disables all lanes. Run full test262 and Node
  baselines once forced-generic and once lane-enabled in separate processes,
  comparing per-test status and outputs. Do not execute both paths for one
  call: arbitrary callees have side effects.
- Add focused fixtures for array builtin → user callback dispatch mode,
  same/foreign realm, method-home/private access, caller and callee `with`,
  Date/Function constructors, closures, async functions, and every lane-kind
  transition writer.

**Gates.** The implemented lane set must meet the T0.1a per-probe coverage
rule; report coverage by kind so one benchmark cannot hide another. T0.2
plain 2-arg ns/call targets **≥1.5x** down. At least one call-dense primary
row must improve beyond the interleaved-run noise floor and no guard row may
regress >3%; otherwise simplify or drop the lane regardless of microbench
success. All C1.4 modes must be clean.

---

## 4. Phase C2 — argument rooting dedup with explicit provenance (1–2 days)

Premise (assumption 1, strengthened by the Merge A final form): a JIT-edge
`args` pointer is a stable address inside the **caller's own frame argument
suffix** — within `[side_root_base, side_root_top)` and live for the whole
call by the frame's own lifetime, with slots cleared by the caller's scope
epilogue. The `RootFrame` arg-copy loop (`:13318–13323`) is then a second,
redundant rooting of the same values. C helpers passing `LAMBDA_ALLOCA`
arrays still need it.

- **C2.1 Pre-rooted-args JIT entry.** Add
  `js_call_function_prerooted_args` with the same semantics as the generic
  entry, except it skips the per-argument copy loop. Select it only at generic
  MIR call sites whose `JsMirArgStackScope` owns the complete argument span.
  The emitter assertion proves the base slot, count, and active-scope
  lifetime. **Keep the 2-slot func/this `RootFrame` in this stage** — the
  callee and receiver arrive in C parameters and are not proven by the args
  frame alone. C helpers and re-entry helpers continue to call the generic
  copying entry.
- **C2.2 Diagnostic span check, not authority.** If a containment helper is
  useful for assertions/census, name it
  `lambda_side_root_contains_span(Context*, const void*, size_t)`. It must
  reject unaligned pointers and overflow, and prove the complete half-open
  span is within the current context's logical live range
  `[side_root_base, side_root_top)`. `side_root_commit_limit` is not a live
  watermark and must never authorize skipping roots. Do not use address
  coincidence as the production provenance contract.
- **C2.3 (stage 2, measured) Fully pre-rooted JIT entry.** A later
  `js_call_function_prerooted` may skip even the func/this frame when
  safepoint write-back has published both values. Assert at emission that the
  callee/this temporaries are registered GC candidates
  (`em_root_note_candidate` coverage) before selecting the import. MT7
  budgets change (new import) — deliberate lift. Land only if T0.2 shows the
  2-slot frame is material after C2.1.

(The former C2.4 — adopting Merge A's deferred inline-save/restore phase —
was deleted 2026-07-25: the final Merge A form removed the `js_args_*` ABI
entirely, so there is nothing left to inline.)

**Gates.** T0.1 reports eligible-emission coverage rather than trusting an
address hit rate (expect near 100% of ordinary generated edges). T0.2
arg-carrying ns/call drops; cd/havlak measurable; **forced-GC stress green
with the arg-copy loop disabled only on the explicit entry** — this is the
phase's correctness cliff and its real gate. Add adversarial generic-entry
fixtures for stack/alloca args, partial side-root spans, forwarded rooted
spans, zero args, and an overflowing diagnostic span size.

---

## 5. Phase C3 — receiver binding without global round-trips (R4.3, staged)

**C3.0 Reuse and stamp receiver analysis (2 days, flag-only, no ABI
change).**

1. **Correctness prerequisite, landed and re-baselined separately:** move
   `jm_collect_param_default_refs` before the assignments to
   `fc->observes_this` / `fc->observes_new_target`. The existing analysis
   already propagates lexical-arrow references and treats direct eval
   conservatively; default expressions are currently collected too late for
   those two booleans. Add default-param `this`, `new.target`, direct-eval,
   and nested-arrow fixtures before using the facts more widely.
2. Stamp separate `JS_FUNC_FLAG_READS_THIS` and
   `JS_FUNC_FLAG_READS_NEW_TARGET` bits during function finalization. All
   generic/C-created wrappers default both bits **set**; compiler finalization
   may clear them only when the repaired analysis is available. Carry an
   explicit `ANALYSIS_KNOWN` init marker plus the two observed-value bits, so
   absence of a bit from an older/dynamic creator can never mean
   "unobserved". Bit numbers are assigned only after re-auditing
   `js_function.hpp`; do not create a second collection analysis.
3. In both dispatcher paths, when `READS_THIS` is clear, skip
   `OrdinaryCallBindThis` and the `js_current_this` install/restore pair.
   When `READS_NEW_TARGET` is clear, skip the `js_new_target`
   install/restore, but **always consume or clear the pending-new-target
   handshake** so it can never leak into the next call.

Accessors invoked via `js_call_function` are safe only through the same
stamped analysis; dynamically created or uncertain wrappers default both
flags to set. Keeping the two facts separate avoids making a
`new.target`-only function pay the receiver round-trip.

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

**Gates (C3.0).** T0.2 plain-call ns drops further; test262 `this`-binding and
`new.target` families (sloppy-mode coercion, arrows, accessors, default
params, `eval`) zero regressions; fixture set:
direct-`eval`-reads-`this`, default-param reads of both bindings, arrow
capturing `this` called dynamically, bound wrapper over an oblivious callee,
and pending-new-target consumed when calling an oblivious constructor via
`Reflect.construct`.

---

## 6. Phase C4 — fewer dynamic calls, thinner trampoline (R4.1 + R4.4, 2–4 days + measured tail)

- **C4.1 (R4.1) Audited mixed native ABI for boxed/void returns.** Treat this
  as a new native-return class, not a one-line eligibility widening.
  `has_native_version` currently implies numeric params **and** numeric return
  in eligibility, function definition, call lowering, TCO, and return
  lowering. First add an explicit `NativeReturnKind` (INT / FLOAT / BOXED /
  VOID) and grep-audit every `has_native_version`, `return_type`, and
  `native_func_item` consumer. Only then allow numeric-param functions with a
  boxed or discarded result to get a mixed-ABI native body or thin wrapper.
  Phase 1.76 parameter validation is reused only after that audit proves its
  assumptions remain valid. Widen which functions get a version; never widen
  which call sites may bypass semantic checks.
- **C4.2 Adaptation branch slimming (measure-first).** Do not stamp
  EXACT/PAD/REST as a per-function answer: exact versus pad/clamp depends on
  the dynamic `arg_count`, while `param_count` and its sign already encode
  declared arity and rest. Profile the adaptation block separately. If it is
  material, specialize the exact-arity hot edge at emission or fold it into
  the per-arity thunk; otherwise drop C4.2.
- **C4.3 Per-arity MIR invoke thunks (R4.4, DEFERRED BY DEFAULT).** Replace
  the 16-case switch with a per-function thunk pointer generated at JIT time.
  Only if C1–C3 leave the switch visible in profiles; otherwise skip — the
  branch predictor likely already owns it.

**Gates.** C4.1: a census names the concrete hot functions before
implementation; direct-call sites to widened functions are verified in
mir_dump (budget lift deliberate); numeric-edge golden
(NaN/±0/overflow), arbitrary boxed values, exceptions, and discarded returns
are identical boxed-vs-native. C4.2/C4.3: attributable interleaved T0.2 delta
or drop.

---

## 7. Sequencing, exit, and Result14

```
T0 → C1 → C2 → C3.0a fix/rebaseline → C3.0b → [C3.1?]
T0 → C4.1 ABI audit → [C4.1 implementation?]
C4.2 / C4.3 / C2.3                 (tail: only where T0.2 shows residual)
```

Prerequisites: Tune6 landed (Result13), Merge Stack complete, online-exception
landed. T0 freezes the minimum useful lane set before C1 code begins. C1 then
re-baselines every later ns/call measurement. The C3.0a analysis-order repair
is a correctness change and lands/re-baselines separately from the Tune7
performance slice. Each remaining phase lands independently green.

**Exit = Result14** (same protocol as Result12/13: clean release build,
four-engine matrix, 3-run medians, 180s, raw JSON preserved, QuickJS control).
Per-phase attribution comes from already-built, row-interleaved release
binaries. Result13 exists and supplies the matrix baselines below. Microbench
targets are ambitions for the combined track; landing decisions use measured
per-phase deltas and the >3% guard threshold rather than assuming every
call-dense row must move by the same factor:

| Metric | Baseline | Target |
|---|---|---|
| T0.2 plain 2-arg dynamic call (ns/call) | measured at T0 | ≥ 2x reduction (C1+C2+C3.0 combined) |
| T0.2 method call via warmed existing IC (ns/call) | measured at T0 | ≥ 1.5x reduction |
| LJS call-dense rows (richards, deltablue, crypto_sha1) | Result13 | ≥1 row improves beyond noise; report all, no assumed uniform 1.3x |
| LJS/Node geo (dedup) | 15.4x | measurable improvement recorded in Result14; phase claims come from A/B |
| MIR/Node geo + QuickJS/Node control | 2.94x / 7.45x | guard and calibration columns, not Tune7 attribution |

After Result14 the expected residuals are R7 (GC frequency × live set), OI-9
(unboxed slot storage), and whatever C3.1/C4.3 evidence says was rightly
deferred.

---

## 8. Risks

- **Lane-classification drift is the correctness cliff.** Any future state
  added to the full dispatcher body can silently break a lane. Mitigation is
  structural: one `js_function_call_lane_recompute` classifier, one shared
  common core, a comment contract at the generic save block ("adding a save
  here requires a lane review"), uncertain functions default GENERIC, and
  both C1.4 modes run at every phase landing. Caller-dynamic state such as
  array-dispatch mode, realm identity, and module identity stays in the lane;
  it is never hidden behind function-only metadata.
- **Home-class cache compatibility.** The current implementation stores the
  internal home object under a string key in the function property map, so
  arbitrary property APIs can collide with that representation. Before
  moving it to a dedicated slot, pin current observable behavior for direct
  assignment, `defineProperty`, delete, and private method access. If any
  compatibility behavior must remain, route the relevant mutation funnels to
  the slot; never let the map and slot disagree.
- **Pending-`new.target` leak (C3.0).** Skipping receiver binding must never
  skip handshake consumption; the `Reflect.construct`-on-oblivious-callee
  fixture pins it. Same for `js_pending_args_callee` when the callee uses
  `arguments` (`uses_arguments` functions are never `this`-oblivious *for the
  callee-stash*: keep the stash unconditional — it is one store).
- **C2 rooting soundness.** Only the explicit generated entry authorizes
  skipping the copy. A pointer merely landing inside a reserved or committed
  region is not provenance. Diagnostic checks use the current context's
  aligned complete span and logical `side_root_top`; generic C/re-entry calls
  retain their copies. Forced-GC stress with the explicit entry's copy loop
  disabled is the gate.
- **C3.0 dynamic receiver escape hatches.** Direct `eval`, lexical arrows,
  default parameters, accessors, and future debugger hooks can observe
  `this`/`new.target`. The existing analysis enumerates them; uncertain
  dynamic functions default both flags set. When in doubt the cost is only
  two global round-trips.
- **C4.1 semantic and ABI cliff.** `has_native_version` currently carries
  numeric-return assumptions across several files. The explicit return-kind
  audit is mandatory before BOXED/VOID exists. Native versions must keep
  exact JS numeric semantics at the boxing boundary (−0, NaN payloads
  irrelevant but sign bits are not), arbitrary boxed values, exceptions, and
  discard semantics.
- **Diagnostics regression (C1.3).** Losing the release-build call-name
  backtrace is accepted and documented; `JS_RUNTIME_TRACE=1` restores it.
  If node-baseline triage proves to need it, demote C1.3 to debug-only
  gating of the *push* (keep depth), not a revert.
- **Machine-state variance.** Same-day sequential runs were insufficient in
  Tune6. Build both release binaries first and interleave A/B row by row;
  preserve raw order and timestamps. Result14 absolutes carry the QuickJS
  control column, but do not replace the interleaved phase evidence.
