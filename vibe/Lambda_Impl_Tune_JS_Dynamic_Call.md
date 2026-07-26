# Lambda Impl Plan: JS Dynamic Call — Per-Callee Entry Specialization

**Status: P0–P2 IMPLEMENTED AND GREEN (2026-07-26). P3–P5 open.**
Successor analysis to `Lambda_Impl_Tune7.md` §8; implements the "genuinely
thin entry" follow-up (§8.4 items 2/3/5) as a design rather than another lane
variant.

## 0. Implementation record (2026-07-26)

P0, P1 and P2 landed together. Measured on this machine with both release
binaries built first and rows interleaved A/B (`temp/tune8_call_bench.js`);
"call cost" subtracts the 13.35 ns/iter empty-loop floor:

| Row | Baseline | P0 only | P0+P1+P2 | Call-cost speedup |
|---|---:|---:|---:|---:|
| plain 0-arg | 46.75 | 43.0 | 29.5 | **2.07x** |
| plain 2-arg | 51.1 | 48.1 | 31.45 | **2.09x** |
| closure 2-arg | 63.8 | 61.3 | 46.75 | **1.51x** |
| method 2-arg | 267.5 | ~273 | 244.4 | 1.09x |

The plain rows meet the ≥2x design target. The method row behaves exactly as
§2.3 predicted: its call share is ~21%, so thinning the call cannot move it
much — that row waits on property-hit work, not on more dispatcher work.

**Gates run.** `test-lambda-baseline` **3621/3621** (includes the forced-GC
`MirGcStressTest` corpus); differential over **341** JS scripts
(`test/js/*.js` + awfy) comparing baseline vs entries vs
`JS_CALL_FORCE_GENERIC=1` — **0 output differences**, 1 script skipped as
nondeterministic on the baseline itself; test262 baseline reports 5
regressions that were **verified to fail identically on the unmodified master
binary** (stale baseline file, tracked separately — two clusters: TypedArray
species-ctor on resizable buffers, and `super(...throwing)` losing the
exception to a derived-ctor TDZ ReferenceError); node baseline aggregate
2040/3550 passing vs the 2039 recorded for master (per-test delta not
completed).

**Deviation from the design as written.** DC1 specified that call sites load
`fn->invoke` and indirect-call it. The landed form keeps the existing imports
and turns them into a two-instruction trampoline (`js_call_via_entry`): type
check → load `fn->invoke` → tail call. This leaves MIR emission and MT7
budgets untouched while getting the same code-elimination benefit, and the
measurements above were taken with it. Moving the load to the call site
remains available if profiling later shows the hop matters.

**Entry family as landed.** `js_call_entry_ordinary<N, HasEnv, PublicAbi>`
for N = 0..4 (20 instantiations), with receiver-oblivious and method-home
handled as runtime branches rather than template axes. A call routes back to
the generic dispatcher when `arg_count != N`, a caller `with` scope is active,
the census is collecting, or the callee's cached special-constructor
classification is not NONE. `js_function_select_call_entry()` additionally
keeps async functions, rest parameters, arity > 4, and stub `func_ptr`s on the
generic entry.

**Classifier-drift repairs required by P1.** Five flag writers outside the
recompute funnel now reclassify: `bind()`, the promise resolve/reject naming
path, `AsyncResource.bind`, and the `Function`/`GeneratorFunction` dynamic
constructors. The three function-creation entry points
(`js_new_function`, `js_new_method_function`, `js_new_closure`) stamp the
generic entry at birth.

**Two supporting changes.** `js_with_stack_state` was promoted out of
`js_globals.cpp` into `js_runtime_state.hpp` so the caller's with-depth is a
load instead of a cross-module call on every call; `special_ctor_kind` /
`special_ctor_name` cache the name-identified special-constructor
classification against the name pointer, which self-corrects on rename and
needed no audit of the 57 name writers.

**Provenance.** All `file:line` refs verified 2026-07-26 against master
`e222487ee` (the Result14 rerun commit). New measurements in §2 were taken on
this machine, release binary of that commit, macOS `sample` at 1 ms, with the
nm-bisection recipe from the Result13 profiling session. Microbench sources:
`temp/tune8_call_bench.js`, `temp/tune8_prof_plain2.js`,
`temp/tune8_prof_method2.js` (P0 moves them under `test/benchmark/exe/` or
re-creates them; temp/ is not durable).

---

## 1. The question this document answers

Tune7 established (§8.2) that the attempted ORDINARY common lane was slower
than the generic dispatcher, and left the trade-off unresolved:

> If "ordinary/plain call" is too narrow, hit-rate is low and the lane is
> ineffective. If it is too broad, the lane must carry special logic and is
> itself slow — also ineffective. Find the sweet spot.

**Claim of this design: the sweet spot does not exist on that axis, and does
not need to.** The narrow-vs-broad dilemma is an artifact of making the
specialization decision *per call, inside one shared dispatcher body*. Any
lane selected there pays (a) the classification itself, (b) the branchy
shared body it threads through, and (c) a hit-rate penalty for every excluded
shape. Tune7 measured exactly that: predictable mostly-false branches are
nearly free, so skipping them buys nothing, while the added classification
costs something.

Move the decision to **function finalization time** and the axis disappears:

- Every `JsFunction` carries a **call-entry pointer** chosen once, by the
  existing `js_function_call_lane_recompute()` funnel
  (`js_runtime_function.cpp:32`), from a small family of specialized entries.
- A function with any rare/complex requirement gets the **generic dispatcher
  as its entry** — unchanged, still the semantic authority.
- The call site does: type-check → load `fn->invoke` → indirect call.

Coverage is 100% *by construction* — there is no "miss" path, only callees
whose own entry is the generic one. Breadth stops being a knob: each callee
pays exactly the protocol its own statically-known shape requires, compiled
straight-line with the irrelevant steps *absent*, not branch-skipped. The
hit-rate question devolves to "what fraction of dynamic calls have callees
whose shape earns a thin entry" — which the Tune7 census already answered:
**83.3% ORDINARY + 8.3% METHOD_HOME** on the call-shape mix (shape-mix
figures from the debug census; T0.1b per-workload tables still owed).

---

## 2. Continued analysis — measured evidence (new, 2026-07-26)

### 2.1 Microbench (release `e222487ee`, this machine)

`temp/tune8_call_bench.js`, callees fetched from arrays so the MIR lowering
cannot devirtualize; verified below that the calls take the dynamic path.

| Section | ns/iter | Note |
|---|---:|---|
| empty loop (`acc = acc + i`) | 13.35 | boxed loop arithmetic floor |
| plain 0-arg dynamic call | 46.80 | |
| plain 2-arg dynamic call | 51.20 | reference row |
| closure (env) 2-arg | 64.20 | |
| method 2-arg via warmed IC | 269.80 | property lookup + call |

These are not comparable to Tune7's 134.87/316.68 ns probe absolutes — that
bench file was not preserved and evidently had a different per-iteration
shape (harness-callback wrapping). The *structure* below, not the absolutes,
is the evidence. This file is the reference harness going forward.

### 2.2 Where the plain 2-arg call spends its time

`sample` leaf attribution, idle threads excluded, static symbols resolved by
nm bisection (8,577 work samples). The `js_call_function_impl_mode` body is
inlined into `js_call_function_prerooted_args_into`; the second blob resolves
to `js_invoke_fn_raw` (confirmed by its call targets: rest-array build,
corrupt-func_ptr logging, typed-array stubs).

| Share | Component | What it is |
|---:|---|---|
| 33.6% | dispatcher blob | `js_call_function_impl_mode` (rows 4–12 of Tune7 §0) |
| 13.1% | `js_invoke_fn_raw` | pad/clamp/rest + env/ABI select + 16-way switch |
| 2.3% | `_tlv_get_addr` | TLS: `__thread context` (`runtime-state.h:11`) + call-name-stack TLS address materialization |
| 2.6% | `memset`/`bzero` | RootFrame slot zeroing — clang converts the zero loop in `lambda_side_root_alloc_n` (`side_stack.c:193`) into `memset` for the 2-slot func/this frame |
| 1.7% | `strncmp` (+stub) | the **"Date"/"Function" name checks** (`js_runtime.cpp:13717,13733`) — the bench callee has a length-4 name, so `strncmp(name,"Date",4)` runs on every call |
| 1.6% | `js_check_exception` | call-site exception poll residual |
| 1.4% | `lambda_item_adopt_scalar_home` | result-home finish |
| 1.1% | `js_get_global_this` | realm compare + sloppy-this fallback |
| 0.5% | `js_debug_check_callee` | emitted per call site even in release (`js_mir_expression_lowering.cpp:10120`) |
| **≈57.5%** | **total call protocol** | **≈29 ns of the 51.2 ns** |
| 25.6% | boxed-numeric helpers | `js_add`, `js_cmp_raw`, `it2d`, `flt2it`, `js_to_number`, `js_get_number`, `lambda_restore_number_frame_top`, … — the *callee body's* `a+b` and the loop arithmetic |
| 16.9% | JIT code | caller loop, callee prologue, arg-frame stores |

Two facts matter for the design:

1. **The dispatcher cost is smeared, not spiked.** No single PC inside the
   33.6% blob exceeds ~1.2% — it is dozens of unconditional loads/stores of
   globals, guard bookkeeping, and call boundaries. This is why Tune7's
   branch-threading lane could not win: the cost was never mispredicted
   branches; it is real work that only *compiling it out* removes.
2. **A JS→JS dynamic call today crosses ≥6 C-call boundaries**:
   `js_debug_check_callee` → `js_call_function_prerooted_args_into` →
   `js_with_save_stack` (unconditional, even at depth 0) →
   `js_invoke_fn` → `func_ptr` → `js_check_exception`. Target: 2
   (specialized entry → body) plus the poll where the exception tracker
   requires it.

### 2.3 Method calls are a different bottleneck

Method-2-arg attribution (7,591 samples): property-HIT chain
(`js_map_get_fast` 20.1%, `js_find_shape_entry` 12.4%,
`js_own_shape_slot_status` 8.1%, `js_property_get` 4.4%, `memcmp` 3.4%,
prototype walk ~4%) ≈ **57%**, versus call machinery
(dispatcher 6.7% + `js_invoke_fn_raw` 11.6% + `js_map_method` 3.0%) ≈ **21%**.

This reconfirms Result13 ("richards/deltablue ~85–90% in the property-HIT
helper chain") from the call side. **No dynamic-call redesign can move
method-dominated rows by more than ~1.2x**; the method-row lever is the
IC-hit C-call tax (proposal R2/J3-KIV, OI-6) and shape identity (R2b), plus
the call-target caching in §5. Expectations for richards/deltablue must be
set against the ~21%, not the 57%.

### 2.4 Cost ledger → what the thin entry must keep

Of Tune7 §0's rows, the per-call work that is *semantically load-bearing*
for an ordinary same-realm call reduces to:

| Keep | Cost | Why |
|---|---|---|
| depth guard | inc/cmp/dec on a static | stack-overflow RangeError is semantics |
| callee type check | already at call site | dynamic typing floor |
| pending-new-target consume | load+branch(+store) | one-shot handshake must never leak (Tune7 risk item) |
| module-vars pair check | load+cmp+branch | caller×callee fact |
| realm pair check | load+cmp+branch | caller×callee fact |
| array-dispatch-mode reset | 2 loads+2 stores | caller-dynamic semantics (`js_runtime.cpp:13894`) |
| exact-arity direct body call | 1 indirect call | the actual work |
| exception poll | where tracker demands | owned by online-exception |

Everything else in the 57.5% — receiver globals for oblivious callees, the
`with` save call at depth 0, name-based special-ctor checks, TLS name-stack
guard, private-home save/restore for non-methods, arity switch, adaptation
branches for exact-arity sites, `arguments` stores for callees without
`arguments`, debug callee checks, the 2-slot RootFrame *when emission proves
rooting* — is skippable **per callee shape**, which is exactly what a
per-function entry encodes. Estimated thin-entry budget: ~20–30 instructions
plus the body call ≈ 5–8 ns, versus ≈29 ns measured — the original R4 ≥2x
per-call ambition becomes arithmetic rather than hope.

---

## 3. Design

### DC1 — `fn->invoke` call-entry pointer (the centerpiece)

Add to `JsFunction` a call-entry pointer with the uniform signature of the
current prerooted entry:

```c
typedef Item (*JsCallEntry)(Item fn_item, Item this_val, Item* args,
                            int argc, uint64_t* result_home);
```

- Stamped **only** by `js_function_call_lane_recompute()` — the funnel
  already runs at every metadata mutation site (`js_runtime_function.cpp:57`
  and the ~10 sites at `:344–:425`). `call_lane_kind` stays as the
  census/diagnostic label; the entry pointer is the executable form of it.
- Default and fallback value: the generic dispatcher. Uncertain, dynamic,
  bound, generator, async-gen, derived-ctor, `with`-using, vm-source,
  eval-initializer, special-ctor, and builtin functions keep it forever.
- The classifier's conservative direction is unchanged: no
  `ANALYSIS_KNOWN` ⇒ generic.

Call sites (`jm_call_function_into`, `js_mir_calls_boxing_types.cpp:55`)
stop naming a fixed import for eligible edges and instead emit:
type-check (already present) → load `fn->invoke` → indirect call with the
same 5 operands. Non-JIT callers (`js_call_function`, helpers, re-entry)
keep calling the generic entry, which retains the arg-copying RootFrame —
provenance stays an emission-site fact exactly as C2 established.

### DC2 — one layer, not two: the entry is the whole call

Specialized entries do **not** call `js_invoke_fn`. Each entry template
performs, straight-line: depth guard → pair checks (module, realm) →
handshake consume → (conditionally) receiver install → **direct typed call
to `func_ptr`** → restores → result finish. The 16-way `P0..P16H` switch
(`js_runtime.cpp:9496–9687`) is not reached from specialized entries; the
env/ABI/arity choice is baked into the template instantiation.

Entry family = C++ template instantiations over the callee-static facts:

```
arity N ∈ {0,1,2,3,4}          (exact-arity hot case)
env     ∈ {none, closure}
ABI     ∈ {MIR_PUBLIC_ABI home, legacy}
receiver∈ {oblivious, observing}      (READS_THIS/READS_NEW_TARGET both clear vs any set)
```

≈ 5×2×2×2 = 40 small functions, plus two shared non-template slow paths:
`entry_pad` (argc < param_count: pad undefined into a stack buffer, then the
same direct call — param_count is a template constant, so still no switch)
and `entry_generic` (everything else). `arity > 4`, rest params, and
argc > param_count clamping (needs `arguments` interplay) go generic until
the census says otherwise. C++ templates, not MIR-generated stubs, are the
stage-1 vehicle: no codegen dependency, debuggable, testable in isolation;
MIR-generated per-function stubs remain a stage-3 option only if evidence
shows the residual indirect-call cost matters.

Method-home functions (`home_class != 0`) get the same templates with the
private-home install/restore around the core — this subsumes the landed
METHOD_HOME lane.

### DC3 — strip name-based and debug work from the hot path

- **Special constructors stop being name-checks.** `Date`, `Function`,
  `GeneratorFunction`, `AsyncGeneratorFunction`, `AsyncFunction`, and the
  proxy-revoke marker are stamped with a creation-time flag/builtin-id at
  globals setup, so the per-call `strncmp` chain
  (`js_runtime.cpp:13717–13761`) runs only inside the generic entry, and
  even there against a flag, not a name. User functions that merely share
  those name lengths stop paying 1.7% per call.
- **`js_debug_check_callee` becomes debug-emission-only** (all four sites in
  `js_mir_expression_lowering.cpp`); release call sites emit nothing.
- **The TLS call-name stack moves entirely into the generic entry.** Elegant
  consequence of DC1: `JS_RUNTIME_TRACE=1` simply makes the recompute funnel
  stamp every function generic — full name-stack diagnostics return, with
  zero cost when off. The depth guard itself (semantics) stays in every
  entry; it is a static int, not TLS.
- **`lambda_side_root_alloc_n` zeroes small counts inline**
  (`side_stack.c:193`): `slot_count <= 4` → unrolled stores, else memset.
  Independent micro-fix; benefits every RootFrame user immediately.
- **The C2 span containment check** (`js_runtime.cpp:13597`) moves behind
  `JS_CALL_LANE_CHECK` as §8.3 already recommended; emission-scope ownership
  is the production proof.

### DC4 — receiver protocol only where observed

Specialized "oblivious" entries (both READS_ bits clear, `ANALYSIS_KNOWN`
set) touch neither `js_current_this` nor `js_new_target` — no
`js_compute_callback_this` (`js_runtime.cpp:655`), no `js_get_global_this`
fallback, no save/restore pair — but **always** run the one-shot
pending-new-target consume. "Observing" entries keep the C3.0 semantics
verbatim. `js_pending_args_callee` and `js_pending_call_args/argc`
(`js_runtime.cpp:9493`) are written only by entries whose callee has the new
`JS_FUNC_FLAG_USES_ARGUMENTS` stamp (from the existing compile-time
`fc->uses_arguments`; bit 16384 of the 3 remaining — re-audit
`js_function.hpp:56` before assigning). Absent analysis ⇒ the function is
generic anyway, so the conservative default costs nothing new.

### DC5 — GC staging for the func/this RootFrame

Stage 1 keeps the 2-slot func/this `RootFrame` inside specialized entries
(with DC3's inline zeroing) — same rooting proof as today, still removes the
copy loop, both layers, and the peripherals. Stage 2 removes the frame from
entries whose flow provably cannot GC before the body call (oblivious,
same-module, same-realm: nothing between entry and body allocates), and from
observing entries only after the C2.3 emission audit proves callee/this are
safepoint-published at every eligible site (`em_root_note_candidate`
coverage). Sloppy-mode `this` coercion (`js_to_object` can allocate) keeps
the frame in its branch. Forced-GC sweeps gate each step; precise rooting
only, per CLAUDE rule 15.

### DC6 — stage 2: monomorphic call-target caching at JIT sites

After DC1–DC5, the residual per-call cost at a *monomorphic* site is the
entry's dynamic checks. A per-site cache cell {callee `JsFunction*`,
`func_ptr`} lets the JIT emit: compare callee against cell → on hit, call
the *body* directly (the caller's realm/module equal the callee's, proven
once at fill time; the pair checks vanish) → on miss, refill through
`fn->invoke`. Two hard lessons carry over: (a) cells baked into replayed MIR
modules need the **shape-epoch discipline** from Tune6 L2 — a cell must
store its install epoch (`lambda_shape_epoch()`-style) and revalidate across
script re-runs; (b) `bind()`/`defineProperty`/home-class mutations already
funnel through recompute, which must bump the function's identity for cells
(or cells key on `func_ptr` + flags word). This is the piece that closes the
gap to direct-call cost for hot dynamic sites; it is deliberately staged
after the entry work so its win is measured against the thin baseline.

### DC7 — fewer dynamic calls at all (unchanged priorities)

C4.1 BOXED/VOID native lowering (the audited `NativeReturnKind` is already
in) remains the largest single opportunity for numeric-parameter callees —
a native call is ~0 protocol. Method-row property-hit work (R2b shared root
shapes, IC-hit inlining) proceeds independently per §2.3. Neither blocks
DC1–DC5.

### What stays generic (unchanged semantics, one authority)

Bound functions, generators/async-generators, derived ctors, `with` users
(either side), vm-source, eval-initializer, proxies, `.call`-polyfill maps,
Function.prototype, builtins, rest params, arity > 4, argc-overflow clamping,
foreign-module + foreign-realm *switching* (the pair checks are in every
entry; the actual swap path can stay generic-only at first — census says
foreign pairs are rare). The generic dispatcher body is not forked; entries
are generated from one shared template core so a future protocol addition is
a compile error in one place, not a drift across 40 copies. The comment
contract at the generic save block ("adding a save here requires a lane
review") upgrades to "requires an entry-template review".

---

## 4. Correctness protocol (inherited, strengthened)

- **One classifier.** Only `js_function_call_lane_recompute` writes
  `fn->invoke`. Every mutation funnel already calls it; a new-metadata
  checklist item: any writer of `flags`, `home_class`, `module_vars`,
  `home_global`, `with_env*`, `vm_stack_*`, `builtin_id`, `bound_*`,
  `param_count`, or `env` must go through a funnel that recomputes.
- **`JS_CALL_FORCE_GENERIC=1`** = recompute stamps generic everywhere; the
  full-suite differential oracle (test262 + node per-test status diff) runs
  lane-enabled vs forced-generic at every landing, as C1.4 specified.
- **`JS_CALL_LANE_CHECK=1`** wraps each specialized entry in a checking
  variant (second entry table) asserting: state the entry promises not to
  touch is untouched; all saved state restored; handshake consumed.
- **Adversarial fixtures** carry over from Tune7 C1.4/C3.0 plus:
  entry-restamp on every mutation writer (bind after hot use, defineProperty
  on a hot method's home class, `Function` realm transplant), argc drift on
  a warmed site (exact → pad → overflow), `Reflect.construct` on an
  oblivious ctor, arguments-object on clamped extra args.
- **Gates per phase**: `test-lambda-baseline` 100%, test262 zero
  regressions, node-baseline per-test delta clean, forced-GC sweep green,
  MT7 budgets lifted only deliberately (the new entry import changes
  emission), A/B row-interleaved release binaries per the Tune6 protocol,
  and release-safe entry-kind hit counters so *active* coverage (not
  classifier eligibility) is finally reported per workload — the T0.1b debt.

---

## 5. Expected effect (set against §2, honestly)

| Row | Today | Mechanism | Expectation |
|---|---:|---|---|
| plain 2-arg microbench | 51.2 ns/iter | ~29 ns protocol → ~6–9 ns | ~28–32 ns/iter (≥1.6x); ≥2x on the call alone |
| plain 0-arg / closure | 46.8 / 64.2 | same | similar ratio |
| method microbench | 271 ns | 21% call share thinned; DC6 helps re-dispatch | ~10–20% until property-hit work lands |
| call-dense LJS rows (richards, deltablue, crypto_sha1) | Result14 floor | call share per row (census counters will quantify) | move by call-share × 2, i.e. real but sub-2x; report all rows |
| numeric rows | — | untouched (boxed-numeric tax dominates, §2.2's 25.6%) | ≈ flat; OI-9/R11 territory |
| hashmap / havlak | — | untouched (shapes/GC dominate) | ≈ flat; R2b/R7 territory |

If the specialized 2-arg entry does not beat the generic dispatcher on the
interleaved A/B microbench by ≥1.5x, the design is wrong at DC2 (the entry
is still too fat) — fix the entry; do not add a call-site classifier back.

---

## 6. Phased plan

- **P0 (½ day).** Land the independent micro-fixes with their own A/B:
  inline small-count zeroing in `lambda_side_root_alloc_n`; debug-gate
  `js_debug_check_callee` emission; creation-time special-ctor stamping
  (DC3). These are unconditional wins regardless of DC1's fate. Move the
  tune8 bench into the repo as the reference probe harness.
- **P1 (1 day).** `USES_ARGUMENTS` stamp + `fn->invoke` field + recompute
  stamping + generic-entry wiring + call-site emission of the indirect
  entry call behind `JS_CALL_ENTRY=0/1`. Semantics identical (every entry
  is generic); gates prove the plumbing is free.
- **P2 (2–3 days).** The template entry family for ORDINARY ± home-class,
  arity 0–4, stage-1 rooting (DC2/DC4/DC5-stage-1). Both check modes, full
  differential, forced-GC. Primary gate: microbench ≥1.5x on plain rows;
  guard rows within 3%.
- **P3 (1–2 days).** DC5 stage 2 (frame removal where proven) + entry-kind
  hit counters + per-workload census tables (benchmarks, test262, node) —
  the honest coverage report Tune7 never produced.
- **P4 (measured).** DC6 call-target cells with epoch discipline, richards/
  deltablue-driven. **P5 (parallel track).** C4.1 BOXED/VOID completion.
- **Exit = Result15** under the standard four-engine protocol, with
  per-phase attribution from interleaved A/B only.

---

## 7. Risks

- **Entry/metadata drift** is still the correctness cliff; the mitigation is
  now structural twice over: one recompute writer, one shared template core,
  force-generic oracle at every landing. Uncertain ⇒ generic remains the
  default direction of failure.
- **Indirect-call misprediction**: `fn->invoke` adds an indirect call where
  today a fixed import is called. Monomorphic sites predict perfectly; the
  16-way switch it replaces was itself an indirect dispatch. Net win
  expected; the microbench gate catches the alternative.
- **`arguments` interplay with clamping** (argc > param_count) is the
  subtlest semantic corner — kept generic until a dedicated fixture set
  exists.
- **Template bloat**: 40 small entries ≈ a few KB of cold-instantiated code;
  I-cache pressure is bounded because any given workload touches a handful.
  If instantiation count creeps, collapse the receiver axis first (measured
  cheapest to re-branch).
- **DC6 epoch discipline**: a stale target cell is silent wrongness — the
  Tune6 L2 lesson is the design input, and cells land only with the epoch
  check and a replay-across-runs fixture.
