# Lambda / LambdaJS Performance Tuning Proposal

- **Date:** 2026-07-03
- **Co-Author:** Anthropic Fable
- **Scope:** LambdaJS engine (`lambda/js/`) primarily; the shared runtime (`lambda/lambda-mem.cpp`, `lib/gc/`) and the Lambda-script MIR transpiler (`lambda/transpile-mir.cpp`) where they share the cost.
- **Baseline:** `test/benchmark/Overall_Result9.md` (2026-06-27, release build, Node.js v22.13.0).
- **Convention:** `file:line` references drift; confirm against the symbol name.
- **Related docs:** [JS_15 — Performance & Optimization](../doc/dev/js/JS_15_Performance.md) (optimization catalog), [JS_03 — Value Model](../doc/dev/js/JS_03_Value_Model.md), [JS_04 — MIR Lowering](../doc/dev/js/JS_04_MIR_Lowering.md), [Lambda_Box_Unbox.md](Lambda_Box_Unbox.md) (Lambda-side dual-version proposal), `Lambda_GC_Root_Issue.md` (open JIT-local rooting issue).

This document has five parts:

- **Part 1** — a whole-engine analysis of where LambdaJS loses to Node.js, and a ranked set of tuning proposals.
- **Part 2** — a detailed design for one specific idea: **stack allocation of boxed 64-bit scalars** (int64 / float / datetime), replacing nursery heap boxing for frame-local values.
- **Part 3** — eliminating/reducing the **per-call JIT GC root-frame overhead** (`heap_jit_gc_root_frame_enter`/`_set`/`_exit`) in the Lambda-script MIR transpiler — the prime suspect in the T0 regression.
- **Part 4** — a design assessment: **Lambda's high-byte tagging vs. NaN-boxing** — which is faster, which is the better design for Lambda, and the recommended strategy.
- **Part 5** — a concrete candidate fix for the double-boxing problem: **inline doubles via high-byte float self-tagging** (a no-rotation adaptation of the "Float Self-Tagging" technique), with a single-bit-mask discrimination scheme and the TypeId re-alignment it requires.

---

# Part 1 — Engine-Wide Analysis & Tuning Proposals

## 1.1 What the benchmarks say

Result9 headline: **LambdaJS/Node dedup geometric mean = 13.1×** (56 benchmarks, 53 timed). The mean hides a bimodal distribution:

| Cluster | Benchmarks | Ratio vs Node | Shared trait |
|---|---|---:|---|
| At/near parity | pidigits 0.16×, fannkuch 0.49×, fasta 1.25×, tak 1.53×, array1 1.71×, sumfp 1.84× | < 2× | pure numeric arithmetic in locals; inference keeps values native |
| Moderate | ack 6.3×, puzzle 6.6×, fib 9.5×, binarytrees 11.5× | 3–15× | call-heavy or alloc-heavy but simple shapes |
| Disaster tail | splay **392×**, deltablue **304×**, cube3d **198×**, matmul **176×**, nbody **163×**, navier_stokes **155×**, richards **112×** | > 100× | boxed array elements, polymorphic dispatch, object-literal allocation churn |

The tail defines the geo mean. Three walls produce it, and each maps to a specific verified mechanism (§1.2). Fixing the walls — not shaving the parity cluster — is the whole game.

**A separate red flag from history:** comparing Result4 (2026-03-19) to Result9 (2026-06-27):

- Lambda/MIR overall went **1.21× → 4.62×** raw vs Node (Result9 itself flags this: "3.82× worse raw").
- LambdaJS's allocation-heavy rows regressed while most others improved: deltablue **935 ms → 4.17 s**, richards **3.31 s → 5.54 s**; MIR gcbench 19× → 54×, MIR fib ≈2 ms → 57 ms.

Something in the **shared runtime** (GC / allocator / rooting) got several times slower in that window. On the Lambda side, the per-call `heap_jit_gc_root_frame_enter`/`_set`/`_exit` emission (`transpile-mir.cpp:390`–`:423`) is the prime suspect — a recursive benchmark like fib regressing ~30× is the classic signature of per-frame overhead. The "js GC tuned" and "mem context implementation" commits are in the same window. **Any optimization measured against the regressed baseline will be mis-ranked; bisect this first** (§1.4, proposal T0).

## 1.2 Where the cycles go (verified mechanisms)

Each finding below was verified against current code, not just the design docs.

### Wall 1 — Boxed array elements (matmul 176×, navier_stokes 155×, cube3d, mandelbrot)

Every JS array element is a boxed `Item`. The inline array fast path (`jm_transpile_array_get_inline`, `js_mir_expression_lowering.cpp:10568`) emits ~20 MIR instructions of guards (tag check, null check, type check, content-array check, extra-fields check, 3-way bounds check) and then still loads a **boxed Item**, which numeric consumers must unbox via an `it2d` runtime call. A numeric element **write** must first box the value — `push_d`, an 8-byte nursery allocation per store. matmul does ~6 element accesses per inner-loop iteration; at N³ iterations this is the entire 176×.

Notably, **shaped object float fields are already solved**: `js_get_slot_f`/`js_set_slot_f` (`js_runtime.cpp:2941`, `:2971`) read/write raw doubles inline in the object's data buffer, no boxing. The array story just never got the same treatment — and Lambda core already ships the needed representation (`ArrayNum`, raw doubles).

Two adjacent codegen facts compound this wall:

- **INT×INT multiplies through doubles** — `I2D`/`DMUL`/`D2I`, 4 instructions instead of 1 (`js_mir_expression_lowering.cpp:2112`; deliberate, for 2^53 semantics, but avoidable when range analysis proves int32 operands).
- **The native dual-version gate requires a numeric *return* type** (`js_mir_module_batch_lowering.cpp:5260`): `return_type == LMD_TYPE_INT || LMD_TYPE_FLOAT` is required *in addition to* numeric params. nbody's `advance(dt)` returns undefined → the whole function is disqualified from the native path by its return type alone, despite fully numeric internals.

### Wall 2 — Property & method dispatch through the C runtime (richards 112×, deltablue 304×, splay, havlak)

Load and store inline caches **exist and are on by default** (`JsLoadIC` install at `js_mir_expression_lowering.cpp:11806`; `js_property_access_named_ic` with MONO/POLY/MEGAMORPHIC states at `js_runtime.cpp:7122`; store IC at `:7421`). But:

1. **An IC *hit* is still a full C call.** The emitted MIR calls `js_property_access_named_ic`, which runs profile counters, key matching (`js_load_ic_key_matches`), a receiver-map fetch, and an entry probe — ~30–40 instructions plus call overhead *on the hit path*. V8's equivalent monomorphic hit is 3–4 inlined instructions (compare shape pointer, load slot). The IC data is right; the fast path just lives on the wrong side of the call boundary.
2. **There is no method/prototype IC at all.** `obj.m()` re-runs `js_prototype_lookup_ex` (`js_runtime.cpp:27703`) — a prototype-chain walk with a per-level `js_ordinary_get_own` — on **every call**. The compile-time devirtualization (P3, `js_mir_expression_lowering.cpp:9086`) requires the method to be provably non-overridden across all classes, which is exactly what richards/deltablue violate; they take the full generic cascade (type-dispatch → `js_map_method` → property lookup → `js_call_function`) per call.
3. **Object-literal objects are shape-cold.** `jm_transpile_object` (`js_mir_expression_lowering.cpp:11991`) lowers a literal to `js_new_object()` (an `&EmptyMap` object) plus one generic runtime put per property. Each instance grows its own `TypeMap`/`ShapeEntry` chain. A transition tree exists (`TypeMapTransition`, `lambda-data.hpp:290`, with `is_transition_shared_shape`) but the literal path does not start from a per-callsite shared blueprint the way `js_constructor_create_object_shaped_cached` (`js_runtime.cpp:2566`) does for `new Klass()`. Splay's nodes are literals — they get per-instance shapes, ~512 bytes and 3 allocations each, and shape-keyed ICs cannot go monomorphic on them.

### Wall 3 — Dynamic call overhead

`js_call_function` (`js_runtime.cpp:12748`) carries ~60–80 instructions of fixed overhead per call: a validation cascade (builtin? generator? async? bound? Date/Function constructor special-cases?), then save/restore of **12+ globals** around the invoke (`js_current_this`, `js_new_target`, `js_active_module_vars` swap, global-this swap, with-env, private-class tracking — `:13091`–`:13170`), then a 16-way argc switch to the function pointer. Closure *variable access* is cheap (env array indexing — this is why cpstak is only 2.6×); it is the **call protocol** that is heavy. The const-bound direct-dispatch path proves the ceiling: ~31 ns/call when the dynamic path is skipped entirely.

### Wall 4 — Allocation & GC (splay 392×, gcbench 48×, storage 46×, binarytrees 11.5×)

- A small literal object = **3 allocations** (Map struct via `heap_calloc_class`; TypeMap + per-field ShapeEntries from the pool, ~72 B/field, never deduped for literals; data buffer) — ~624 bytes for a 2-field object.
- The GC (`lib/gc/gc_heap.c`) is a **non-generational, whole-heap mark-and-sweep**, triggered when the nursery data zone crosses ~3 MB (`GC_DATA_ZONE_THRESHOLD`, `gc_heap.c:604`). An allocation-heavy loop therefore re-marks the *entire live heap* every ~3 MB of allocation.
- Marking walks each Map's **ShapeEntry linked list** per object (`gc_heap.c:1006`) instead of the O(1) `slot_entries[]` array.
- Root scanning includes a **conservative stack scan** — O(stack size), every 8-byte-aligned word tested against slab ranges (`gc_heap.c:1570`) — plus re-registration of all active JIT root frames per collection (`lambda-mem.cpp:145`).
- Nursery survivors are **promoted (copied) to the tenured zone** at collection — which is why JIT-held pointers to nursery values need rooting, the open issue documented in `Lambda_GC_Root_Issue.md`.

Splay is the perfect storm: object literals (per-instance shapes, 3 allocs each), `Math.random()` keys (every key a boxed double), heavy churn (whole-heap marks), string concat in payloads.

## 1.3 What is *not* the problem

Worth stating to avoid wasted effort:

- **Closure variable access** — compiled to env array indexing; effectively free (cpstak 2.6×).
- **Shaped-constructor field access** — the `js_get_slot_f` path is already a direct slot load; nbody's residual is boxing at loop boundaries and the native-gate exclusion, not field access itself.
- **Compile/link time** — the benchmark harness times `__TIMING__` self-reported runtime, so the interpreter-selection and MIR-link work (JS_15 §3) is out of scope here.
- **The parity cluster** — pure-numeric-local code already matches or beats Node; the native arithmetic path works when inference lets it fire.

## 1.4 Tuning proposals (ranked)

### T0 — Bisect the March→June shared-runtime regression *(do first; days, possibly 2–4× globally)*

Re-run deltablue/richards/gcbench/splay/fib at the Result4 commit vs HEAD with today's harness; bisect the alloc-heavy deltas. Suspects: per-frame `heap_jit_gc_root_frame_*` emission (`transpile-mir.cpp:390`), "js GC tuned", "mem context implementation". The rooting is known-load-bearing (do not revert blindly — see `Lambda_Issue_GC_Root (fixed).md`); the remediation plan for the root-frame overhead specifically is **Part 3** of this document. Note that the blanket `MIR_T_I64` rooting rule was already replaced by honest type tracking *after* Result9 was captured, so re-measuring on current master comes first — the bisect target may already have shrunk.

### T1 — Inline the IC fast path into JIT code; add a shape-keyed method/call IC *(highest single lever)*

- At each named-access site, emit inline MIR: load `map->type`, compare against `ic->entries[0].shape`, on match load `map->data[offset]` (or through `slot_entries` for typed coercion); call `js_property_access_named_ic` only on guard failure. Hit cost drops from ~35 insns + C call to ~4 inline insns. Same treatment for the store IC.
- Extend the IC structure to **method resolution**: cache the resolved `JsFunction*` (or builtin id) keyed on receiver shape, 2–4 entries (a classic PIC), so `task.run()` skips `js_prototype_lookup_ex` + the `js_map_method` cascade. This is JS_15's unimplemented "P3b", and it is what richards/deltablue need — the compile-time devirtualizer correctly refuses them.
- Prerequisite for literals to benefit: T3 (shared literal shapes), otherwise literal receivers are polymorphic-by-construction.
- Substrate already present: per-site `JsLoadIC` structs, shape pointers, `slot_entries[]`, profile counters (`js_exec_profile.cpp:92`–`:96` already track IC hit/miss — use them to validate).

### T2 — Slim the call path; fix the native-version gate *(three independent sub-items)*

1. **Drop the numeric-return requirement** on native dual versions (`js_mir_module_batch_lowering.cpp:5260`): require numeric params only; box the return at the boundary. Smallest diff in this document; re-enables the native path for void hot loops (nbody `advance`, cube3d, raytrace3d).
2. **Fast lane in `js_call_function`** for plain user functions — one flag bit on `JsFunction` meaning "not generator/async/bound/proxy/ctor-special"; skip the validation cascade when set.
3. **Pass `this`/`new.target` as real ABI arguments** instead of saving/restoring globals per call. Combined with T1's method IC, a monomorphic method call becomes shape-guard → direct `MIR_CALL`.

### T3 — Per-callsite literal shapes + single-allocation objects

The transpiler statically knows the property set of `{left: …, right: …}`. Emit `js_new_object_with_shape(callsite_blueprint, n)` reusing the constructor-shape machinery (`js_constructor_create_object_shaped_cached` pattern) instead of `js_new_object()` + N generic puts. Cuts ~2 allocations and ~512 B per literal object, and makes literal objects **shape-shared so T1's ICs can go monomorphic** on them. Fold the data buffer into the Map allocation for small objects while at it. Direct wins: splay, gcbench, storage, json.

### T4 — Generational collection within the non-moving constraint

The heap cannot move objects (JIT holds raw pointers), but **sticky mark bits** work non-moving: minor collections mark only objects allocated since the last cycle, with a write barrier on old→young pointer stores (property/array/env writes are already centralized C entry points, so barrier emission sites are few). Add lazy sweeping; switch GC-time field tracing from the ShapeEntry linked list to `slot_entries[]`. Splay/gcbench/binarytrees are "allocate fast, die young" workloads that generational collection collapses. Part 2 (stack boxing) reduces what reaches the nursery at all and pairs with this.

### T5 — Dense-double / dense-int element kinds for JS arrays

Give JS arrays an elements-kind: an all-numeric array stores raw doubles (reuse Lambda's `ArrayNum` representation), transitioning to boxed on the first incompatible store. Element read = one `MIR_T_D` load, no `it2d`, no guards beyond the kind check; numeric write = raw store, no `push_d`. This is *the* fix for matmul/navier_stokes/cube3d/mandelbrot and removes the dominant nursery pressure in numeric-array code. Risk: Lambda core shares `Array`; the kind must be a JS-side flag or a distinct container reusing `ArrayNum`'s layout, with every JS array consumer (iteration, methods, JSON) taught the kind or funneled through accessors.

### T6 — Codegen-quality items (validated versions of JS_15's open list)

- **Destination-passing lowering** — emitted MIR is 66–88 % MOVs (Tune6 histogram); the structural fix, big and risky, gate on full test262 + Radiant.
- **Int-multiply fast path** — native `MUL` + overflow check when both operands provably int32; fall back to the double route on overflow.
- **`arr.push` pristine-prototype guard** (realm-scoped) — retires the per-call override check + name re-interning behind base64 (43×) and knucleotide (22×). Deferred design already sketched in JS_15 §5.2.
- **Non-string ADD inference + fixed-point return-type inference** — recovers native typing for additive recursion (ack ≈12 ns → ≈208 ns/call regression, Tune5 §6c) without resurrecting the concat unsoundness.
- **MIR-inlinable hot helpers** — MIR's `process_inlines` cannot inline native C imports; emit the smallest hot helpers (`js_get_slot_f`-class) as MIR functions so calls to them disappear.

### Sequencing & expected effect

**T0 → T2.1 (return gate) → T1 (ICs) → T5 (double arrays) → T3/T4 (alloc/GC) → T6 opportunistically.** Each stage is measurable with existing `js_exec_profile` counters and gated on test262 + `make test-lambda-baseline`, matching prior tuning rounds.

Calibrating against what comparable past changes achieved (shaped-slot path: nbody 741→288 ms; direct dispatch: AWFY 16.3×→4.5×): the 100–400× tail should land in the 10–30× range and the dedup geo mean plausibly reaches **3–5× vs Node** — QuickJS-class or better on this suite — **without** a NaN-boxing rewrite. NaN-boxing is explicitly not proposed: the top-byte `Item` tag is shared with all of Lambda core, GC scanning, and every container API; container-level inline doubles (T5 + existing typed slots) buy most of the benefit at a fraction of the blast radius.

---

# Part 2 — Stack Allocation of Boxed 64-bit Scalars

## 2.1 The idea

Today, `Item` values of type `LMD_TYPE_FLOAT`, `LMD_TYPE_INT64`, and `LMD_TYPE_DTIME` are tagged pointers to an 8-byte payload that must be heap-allocated — even when the value is a short-lived expression temporary or a frame-local variable. The proposal: **allocate the 8-byte payload in the JIT frame (C stack) via `MIR_ALLOCA`** for values that do not escape the frame, and **copy the payload by value** (promote to heap) at the moment a value is stored into anything that outlives the frame. No heap allocation, no GC pressure, no rooting for the frame-local case.

## 2.2 Current status analysis

### Where boxes come from

- `jm_box_float` (`js_mir_calls_boxing_types.cpp:406`) — emits a call to `push_d`. This is the boxing site for every float that must become an `Item`: stores to untyped locals, call arguments, returns, property/array stores, mixed-type flows.
- `jm_box_int_reg` (`:356`) — packs int56 inline (no allocation), **but** out-of-range integers promote to a boxed float via `MIR_I2D` + `push_d`.
- Runtime-internal boxing — `js_make_number` (`js_runtime_value.cpp:1071`) and the boxed arithmetic fallbacks (`js_add`, …) call `push_d` from inside C helpers. The Lambda-script engine boxes through the same `push_d`/`push_l`/`push_k` (`lambda-mem.cpp:491`–`:563`).

### What a box costs today

- `push_d` = C call + nursery bump (`gc_nursery_alloc_double`, `lib/gc/gc_nursery.c:78`): bounds check, slot store, pointer tag. Cheap per call — the damage is aggregate:
  1. **GC frequency.** The nursery data zone triggers a **whole-heap mark** every ~3 MB (`gc_heap.c:604`) — ~400 K boxed doubles. A float-heavy loop forces full-heap collections continuously.
  2. **Survivor promotion + rooting.** Nursery survivors are copied to the tenured zone at collection; any JIT-held pointer to a nursery payload must therefore be rooted so it stays valid — this is the open issue in `Lambda_GC_Root_Issue.md`, and the per-frame `heap_jit_gc_root_frame_enter`/`_set` calls the Lambda transpiler now emits (`transpile-mir.cpp:390`–`:423`) are its direct cost (see the fib regression, §1.1).
  3. **Cache behavior.** Nursery bumping spreads temporaries across 32 KB blocks; a loop's boxes never stay cache-resident the way a frame slot would.

### What already avoids the box

The native fast path (`jm_is_native_type`, dual `_n` function versions — JS_04 §2–4) keeps proven-numeric chains in `MIR_T_D`/`MIR_T_I64` registers with no box at all. Shaped object float fields store raw doubles inline (`js_set_slot_f`). **Stack boxing is for the residue**: untyped locals, values crossing typed/untyped boundaries, arguments/returns of non-native functions, and the interpreter path (which executes the same lowered MIR, so lowering-level changes apply there too).

### Mechanism availability

`MIR_ALLOCA` is supported and already used in this tree — the Python transpiler emits it routinely (`lambda/py/transpile_py_mir.cpp:1508` et al.). The JS and Lambda transpilers do not use it yet.

### GC compatibility (why this is safe at the representation level)

- An `Item`'s payload pointer is opaque; `d2it(ptr)` on a stack address is a well-formed Item, and every reader (`it2d`, comparisons) just dereferences.
- GC marking already **skips non-heap pointers**: both the conservative stack scan and root-range marking test candidates against slab ranges (`is_gc_object`, `gc_heap.c:1570`–`:1609`). A stack-boxed Item sitting in the args stack, a rooted range, or a scanned stack word is simply ignored — which is exactly correct, *provided it never outlives its frame*.
- These payloads are **immutable values with no observable identity** (JS numbers have no reference equality; Lambda int64/datetime likewise). Copy-by-value at escape is semantically invisible. This is the property that makes the whole scheme correct.

## 2.3 The two hazards that define the design

### Hazard A — escape (payload outlives the frame)

Escape points, exhaustively:

| Sink | Handling |
|---|---|
| property / array-element / `push` writes | promote (copy payload to heap) at the sink |
| closure-env writes, module-var writes | promote at the sink |
| `return` | **always promote** — the callee's slot dies at return; the caller cannot check. (Better still: return native doubles via dual versions where inference allows.) |
| `throw` | promote in `js_throw_value` (`throw 3.14` is legal; `js_exception_value` is a GC root) |
| generator / async spill (`jm_gen_spill_save`) | promote at spill; **simplest v1: disable stack boxing inside generators/async entirely** (their frames suspend) |
| call arguments | **no copy needed** — the caller's frame outlives the callee, so the callee may read stack-boxed args freely; only *persisting* them hits a sink above |
| C-runtime internal stashes (IC entries, regexp last-match, caches) | audit; any C path that stores an `Item` into a global/heap struct without going through a checked sink must promote (§2.5 audit list) |

**Detection at sinks:** a pointer-range test — `current_sp <= payload_ptr < stack_base` (stack bounds are already known to `lambda-stack.cpp`). Two compares. Correct even when the sink runs deep in callees, since any callee's SP is below the boxing frame's slot address. Every sink above is already a C runtime call costing dozens-to-hundreds of instructions, so the check is noise there. A distinct "stack-boxed" TypeId tag was considered and rejected: it would add a branch to every *reader* (`it2d` etc.), whereas the range check taxes only the sinks.

### Hazard B — aliasing under slot reuse

If a boxing site reuses its frame slot while an Item created by a previous execution of that site is still live, the value is corrupted. Example: `var prev = x; x = next(); use(prev)` in a loop — `prev` holds a pointer into the slot the next iteration overwrites. The discipline:

1. **Expression temporaries → per-site slots.** A temp is consumed within its own loop iteration and never crosses a back-edge; a site's slot is only re-written on the *next* execution of that same site. Recursion is automatically safe: `MIR_ALLOCA` slots are per-frame, so a recursive re-entry writes a different frame's slot.
2. **Untyped local variables → variable-owned slots, copy-on-assign.** Each untyped local gets a dedicated payload slot in the frame; *any* assignment of a float-tagged Item to a variable copies the payload into the variable's own slot and re-tags. The variable *is* the storage — reassignment overwrites exactly the slot whose old value the language semantics discard. Var-to-var assignment (`b = a`) copies `a`'s payload into `b`'s slot, so later `a = …` cannot alias `b`. The transpiler emits every assignment, so all copy points are visible to it.
3. **Captured variables** live in heap envs already — keep heap boxing for them (the env write is a sink).
4. **Loops need no special casing** beyond rules 1–2: temps don't cross back-edges; vars own their slots.

## 2.4 Proposal

### Design summary

- At function entry, emit one `MIR_ALLOCA` region sized for: one 8-byte slot per stack-boxable expression site + one per untyped local variable (bounded, known at lowering time; fall back to heap boxing if a function is pathologically large).
- Replace `jm_box_float`'s `push_d` **call** with two inline MIR instructions: store the `MIR_T_D` register to the site's slot; `OR` the tag onto the slot address. (Also applies to `jm_box_int_reg`'s overflow-promotion arm and, on the Lambda side, `push_l`/`push_k` sites.)
- Variable assignment of a possibly-stack-boxed Item lowers to: range-check (or statically known) → copy payload into the variable's slot → retag. Statically-known-float assignments skip the check.
- Sinks (property/array/env/module-var write, throw, return, spill): add the two-compare range check + payload clone (`push_d` of the payload) in the existing C entry points.
- Generators/async: excluded in v1 (no stack boxing emitted inside them).
- Runtime-internal boxing (`js_make_number` etc.) is untouched in v1 — C helpers cannot use the caller's frame without an ABI change (a caller-passed slot pointer is a possible v2, i.e. destination-passing for boxes).

### What it wins

1. **Box = 2 inline instructions instead of a C call** at every lowering-emitted boxing site.
2. **Nursery pressure from temporaries → zero**; whole-heap GC frequency drops proportionally (the ~3 MB trigger stops being hit by loop temporaries at all).
3. **No rooting, no evacuation** for stack payloads — nothing for the GC to move or track. This directly shrinks the surface of the open GC-root issue, and on the Lambda-script side removes `jit_gc_root_frame_set` traffic for these values (part of the suspected T0 regression).
4. **Cache-hot payloads** — frame slots instead of 32 KB nursery blocks.

### Honest sizing

- Helps most where **boxed temporaries and untyped-local churn** dominate: float loops with untyped vars, mixed-type flows, `js_make_number`-adjacent code, and GC-trigger-bound benchmarks (mandelbrot-, nbody-class residuals). Plausibly **1.5–3× on nursery-bound benchmarks**.
- Does **not** help matmul-class code: array element writes escape by definition and still promote — dense-double element kinds (T5) are the right fix there.
- Where inference already keeps values native, there is no box to remove — this is **complementary to T2.1/native-path widening**, not a substitute.
- The sink checks add two compares to already-heavy runtime paths: measurable-noise level, but verify with A/B.

### Phased plan

| Phase | Content | Risk | Gate |
|---|---|---|---|
| P0 | **Measure**: add `js_exec_profile` counters on `push_d`/`push_l` call rate per benchmark; size the prize before committing | none | — |
| P1 | Per-site frame slots for expression temporaries whose consumers are provably unbox/compare/condition **before any call or store** — these never reach a sink, so **zero runtime changes** | low | test262 + lambda baseline + benchmark A/B |
| P2 | Variable-owned slots + copy-on-assign; promotion checks in sink runtime functions (map/array/env/module-var write, throw, return, spill) | medium | full test262, Radiant baseline, ASan run of the benchmark suite |
| P3 | Port to the Lambda-script transpiler (`transpile-mir.cpp`) — same design; additionally retires root-frame traffic for these values | medium | lambda baseline 100%, benchmark A/B vs T0 baseline |
| P4 (optional) | Destination-passing box ABI so C helpers (`js_make_number`) can fill caller slots | high | defer until P1–P3 measured |

### Correctness audit list (the review checklist for P2)

Hunt for any C runtime path that stashes an `Item` into memory that outlives the frame **without** going through a checked sink:

- `js_exception_value` (`js_runtime_state.cpp:192`) — covered by the throw sink.
- IC entries caching values (load IC caches shape+offset, not values — verify no value caching).
- RegExp last-match statics, `JsRuntimeState` capsule fields (`current_this`, `pending call-arg state`).
- The transient args stack: values persist only until `js_args_restore` — caller-frame lifetime, safe — but verify the exception-in-argument-evaluation path (`js_mir_statement_lowering.cpp:5532`) restores before the frame dies.
- Interpreter mode (`MIR_set_interp_interface`): executes the same MIR including `MIR_ALLOCA` — confirm interp alloca semantics match gen (per-frame).
- Conservative stack scan: a stack word holding a stack-boxed Item is skipped (non-heap pointer) — desired; confirm `is_gc_object` never false-positives on stack addresses (slab ranges are heap-only — it can't).

### Interaction with Part 1 proposals

- **T5 (dense-double arrays)** removes the largest *escaping* box population; stack boxing removes the largest *non-escaping* one. Together they approach "no numeric boxing in hot loops" from both sides.
- **T4 (generational GC)** and stack boxing both cut GC frequency; stack boxing makes T4's minor collections even smaller (fewer nursery survivors to promote).
- **T0 (regression bisect)** should land first so this work is measured against the true baseline; P3 may itself recover part of the regression.
- **Part 3 (root-frame elimination)** — stack-boxed values need no rooting at all (nothing for the GC to move or track), so Part 2 shrinks the root-slot population that Part 3's shadow stack has to carry, and Part 3's L4 option (non-moving nursery) is most viable once Part 2 keeps frame-local payloads out of the nursery.

---

# Part 3 — Eliminating the Per-Call JIT GC Root-Frame Overhead

## 3.1 Scope

This part addresses the Lambda-script MIR transpiler's per-call GC rooting protocol — the `heap_jit_gc_root_frame_enter` / `_set` / `_get` / `_exit` calls emitted around every JIT'd function (`transpile-mir.cpp:389`–`:442`) and their runtime backing in `lambda-mem.cpp:337`–`:464`. It is the prime suspect behind the MIR-side share of the T0 regression (fib ≈2 ms → 57 ms between Result4 and Result9 — the classic per-frame-overhead signature). The LambdaJS engine does not emit these calls (its roots are the args stack, module vars, and env ranges), but it shares the GC-side range machinery and benefits from the GC-side cleanup.

## 3.2 Current status analysis

### What one JIT call pays today

Per JIT'd Lambda function call, the transpiler emits:

1. **`heap_jit_gc_root_frame_enter`** at the prologue (`transpile-mir.cpp:10842`) — a C call whose body only increments a thread-local depth counter (`lambda-mem.cpp:431`; frame creation is lazy — good).
2. **`heap_jit_gc_root_frame_set` per rooted-local assignment** (`store_gc_root_slot`, `transpile-mir.cpp:398`) — a C call which, on the *first* set in a frame, materializes the frame (pops a `JitGcRootFrame` from a small cache, `:375`) and then, per 64-slot block, does a **`mem_calloc`** plus a **`gc_register_root_range`** — which linearly scans the existing ranges array for dedup (`lambda-mem.cpp:356` → `gc_heap.c:685`).
3. **Rooted call results** go through `root_gc_result_if_needed` (`transpile-mir.cpp:437`): a `_set` **plus an immediate `_get`** — two C calls just to round-trip the value through protected memory.
4. **`heap_jit_gc_root_frame_exit`** at every return path (`:8020`, `:8032`, `:8091`, …) — a C call that, per block, runs **`gc_unregister_root_range`** (another linear scan **plus a memmove** of the ranges array, `gc_heap.c:709`) and `mem_free` (`lambda-mem.cpp:361`).

So a recursive function with even one rooted local pays **O(recursion depth)** in the range-array scans *per call* — **O(depth²) total**. In addition, every collection re-walks all registered ranges (`gc_heap.c:1649`) and re-registers active JIT frames (`lambda-mem.cpp:145`).

### What already got fixed (post-Result9)

The original BUG-001 fix rooted **every `MIR_T_I64` local** — including packed-scalar ints like fib's `n` — which multiplied the above by the local count of every frame and produced the StackOverflow-test hang. That blanket rule has been replaced by **honest type tracking** (`should_gc_root_var`, `transpile-mir.cpp:375`: root only `MIR_T_P` or genuinely boxed/container `TypeId`s; `var x = null` placeholders tracked as `ANY`; call-result types via `get_effective_type`). Full write-up: `Lambda_Issue_GC_Root (fixed).md`. **This landed after Result9 was captured**, so the first action is to re-measure on current master — fib-class functions no longer create any root slots.

### What remains after that fix

- The **enter/exit C-call pair on every call**, including frames that create zero slots.
- For functions with genuinely rootable locals (strings, containers, boxed int64/decimal, `ANY`): the per-assignment `_set` calls, the set+get round-trip for call results, and the first-set **block `mem_calloc`/`mem_free` + register/unregister linear scans** per frame — still O(depth) per call for rooted recursion.

### The constraint that shapes the design

The roots must stay **precise and writable**. The GC *relocates* data-zone payloads at collection — nursery numeric boxes and container backing buffers are copied to the tenured zone and pointers rewritten (`gc_data_zone_copy` sites, `gc_heap.c:1258`, `:1294`, `:1312`; embedded-pointer fixup `:1203`). A conservative stack scan can *mark* but cannot *rewrite* an ambiguous root, so it cannot substitute for the frame slots while promotion moves data. Any elimination strategy must either (a) keep giving the GC a precise, writable view of JIT-held Items, or (b) remove the relocation for JIT-referenced payloads.

## 3.3 Tuning proposal (layered)

### L0 — Re-measure first *(free)*

Re-run the benchmark suite on current master before further work: the honest-typing fix already removed root slots from packed-scalar frames, so part of the Result9 regression may already be gone. This re-scopes T0's bisect.

### L1 — Elide empty frames *(hours, zero risk)*

Do not emit enter/exit for functions that create no root slots. The transpiler knows the final `jit_root_next` at end-of-body; emit the prologue `enter` unconditionally, then in a post-pass delete it (and the paired `exit`s at each return) when `jit_root_next == 0` — MIR permits instruction removal before `MIR_finish_func`. Every scalar-only function (fib, tak, ack, arithmetic leaves) then pays **zero**. This also removes the enter/exit pair from frames that keep all rootable values dead before the first GC point once L3 lands.

### L2 — Inline shadow stack *(the core fix)*

Replace the C-call + heap-block machinery with plain emitted MIR, keeping the GC contract identical:

- **Prologue:** `MIR_ALLOCA` a frame record `[prev, slot_count, slots[N]]` — `N` is known at transpile time (final `jit_root_next`); zero the slots (the GC must never read garbage from not-yet-set slots — the same invariant the transient args stack maintains). Two inline stores link it: `frame.prev = g_jit_root_head; g_jit_root_head = &frame`. The transpiler already bakes runtime-global addresses into emitted MIR — the inline heap-bump allocation does exactly this (`static_assert` at `transpile-mir.cpp:80`) — so the pattern is precedented.
- **`set`** = one MIR store to `frame.slots[i]`. **`get`** = one load. The set+get round-trip in `root_gc_result_if_needed` becomes ~2 instructions.
- **Every return path:** one store, `g_jit_root_head = frame.prev`. Abrupt/error paths must pop too — the same discipline the current exit-emission sites already follow, as a store instead of a call.
- **GC side:** at collection, walk the `g_jit_root_head` chain and treat each frame's slot array exactly like a registered root range today — markable *and rewritable*, preserving the relocation contract. Delete `JitGcRootFrame`, `JitGcRootBlock`, the frame cache, and all per-frame `gc_register/unregister_root_range` churn.

**Effect:** per-call cost drops from 2–4+ C calls plus a `mem_calloc`/`mem_free` plus two linear range-array scans to **~4–6 inline instructions**, and the O(depth²) disappears structurally — collection walks the chain once, O(live frames), instead of the mutator maintaining a ranges array on every call. Recursion-heavy rooted code (deltablue-class) stops paying GC-registration rent per call.

**Risks / notes:** `MIR_ALLOCA` frame slots must not be reused across the `enter`-less tail after a pop (emit the pop only at genuine exits); MIR interp executes the same lowered MIR, so the interpreter path is covered; single-threaded runtime makes a plain global head acceptable (mirror the current `__thread` if that changes).

### L3 — Root fewer values *(orthogonal, on top of L1/L2)*

- **GC-point liveness:** a local needs a slot only if it is *live across a potential allocation* (a call that can allocate, or a direct allocation site). A rootable value consumed before the next GC point can stay in its register. Requires a per-callee "can-allocate" summary — conservative for unknown calls, precise for arithmetic builtins.
- **Can-allocate function summaries** also feed L1: a body that provably cannot reach an allocation needs no frame regardless of local types.
- With L2's one-instruction sets, the payoff of L3 shifts from mutator time to **GC time** (fewer slots to mark/update per collection) — do it after L2, guided by profile.

### L4 — Remove the requirement *(later; couples with Part 2 / T4)*

The frame slots exist only because promotion **moves** data-zone payloads. If nursery blocks referenced by JIT roots were **pinned** (sticky blocks: survivors stay in place; the block is retained until empty) instead of copied, the conservative stack scan would cover JIT locals and the entire mechanism — even L2's inline version — could be deleted. Trade-offs: nursery retention/fragmentation, and container-buffer relocation must still work through owner-slot fixup (it already does — the rewrites at `gc_heap.c:1258`+ go through the owning object's field, not JIT roots). This becomes most attractive after Part 2, which keeps frame-local payloads out of the nursery entirely, leaving mostly container-owned data there. Treat as part of a deliberate nursery redesign (with T4), not a first move.

### Sequencing & validation

**L0 → L1 → L2 → L3 (profile-guided) → L4 (with T4/Part 2).** Gates: `make test-lambda-baseline` 100 %, `test_lambda_errors_gtest` 61/61 (the StackOverflow test is the canary for over-rooting cost; deltablue/deltablue2 are the canaries for under-rooting correctness — both directions burned us before, see `Lambda_Issue_GC_Root (fixed).md` §4), plus an ASan pass of the benchmark suite for the L2 lifetime changes. Measure fib/ack (call overhead), deltablue (rooted recursion), gcbench (GC-side range walking) before/after each layer.

---

# Part 4 — Design Assessment: High-Byte Tagging vs. NaN-Boxing

Parts 1–3 propose work *within* the current value representation. This part answers the design question directly: how does Lambda's high-byte tag scheme compare to NaN-boxing, which would run faster, and which is the better design for this codebase. The conclusion feeds the strategy choice behind T5/Part 2 (typed storage) versus a representation migration.

## 4.1 The two schemes

**Lambda's high-byte tagging** (`lambda.h`, JS_03 §2): `Item` is a 64-bit word whose high byte `[63:56]` is the `TypeId` tag. Small ints pack inline as sign-extended **int56**; sub-word numerics use the `NUM_SIZED` layout (value in `[31:0]`, sub-type in `[55:48]` — inline f32/u32/i16/…); containers are bare pointers (tag byte 0, type read from the object header); **doubles cannot fit inline** — `LMD_TYPE_FLOAT` is a tagged *pointer* to a heap-allocated 8-byte payload (`d2it`, `lambda.h:890`).

**NaN-boxing** (JavaScriptCore, SpiderMonkey, LuaJIT): the 64-bit word *is* an IEEE-754 double whenever its bit pattern is not in the quiet-NaN space; pointers, int32s, booleans and friends are encoded inside the ~2^51 unused quiet-NaN payload patterns. A double is stored, passed, and read with **zero boxing** — the value is the Item.

## 4.2 The one decisive difference

Everything else is close to a wash; the schemes genuinely diverge only on **where doubles live**, and that single difference is LambdaJS's largest measured wound (Part 1, Wall 1: matmul 176×, navier_stokes 155×, nbody 163×):

- Under NaN-boxing, a float crossing any boundary — array element, property value, argument, return — costs nothing. No allocation, no dereference, no GC pressure, no rooting.
- Under high-byte tagging, every float that must exist as a boxed `Item` is a `push_d` nursery allocation, a later cache-missing dereference, GC-trigger pressure (whole-heap mark per ~3 MB, Part 1 Wall 4), and — because the nursery payload is *relocated* at collection — the entire precise-rooting apparatus of Part 3.

A NaN-boxed Lambda would erase the float-boxing wall *by construction*. On the narrow question "which runs faster": **for untyped, float-heavy dynamic code, NaN-boxing wins, and not marginally.** That is precisely why JSC, SpiderMonkey and LuaJIT chose it.

## 4.3 Dimension-by-dimension comparison

| Dimension | High-byte (Lambda) | NaN-boxing | Verdict |
|---|---|---|---|
| Inline doubles | ✗ heap-boxed — the big loss | ✓ free | **NaN-box, decisively** |
| Inline int range | **int56** — overflow-to-heap is rare; good fit for Lambda's int64 semantics | int32 typical (payload space) | high-byte |
| Type-tag space | **256 uniform tags** — fits Lambda's 20+ types; `NUM_SIZED` sub-typed inline scalars (f32/u32/…) live naturally in `[55:48]` | ~a dozen clean tags; a rich taxonomy gets cramped | high-byte |
| Pointer bits | 56 — survives 57-bit / 5-level paging | 48–51 — needs care on new address layouts | high-byte |
| Type dispatch | one shift (`>>56`) then compare/jump — uniform | mask + range compares; "is double" is the cheapest test, others comparable | ≈ parity (high-byte marginally simpler) |
| ARM synergy | top-byte-ignore (TBI) on Apple Silicon could allow dereferencing tagged pointers **without masking** (currently `it2*` masks with `0x00FF…`) — an unexploited micro-win | n/a | high-byte |
| Warts | container tag = 0 forces a header deref in `type_id()`; the `JS_SYMBOL_BASE` negative-int/Symbol collision (JS_03 §4, Known Issue 1) | NaN canonicalization on float results; float bit patterns must never alias tag space; conservative scanning must not mistake doubles for pointers | ≈ parity |
| GC interaction | boxed floats churn the nursery and demand precise, updatable roots (Part 3) | no float boxes → far less nursery traffic; pointer discrimination by tag range works | **NaN-box** (as a consequence of inline doubles) |

## 4.4 The V8 counterexample — representation vs. specialization

The most instructive external data point: **V8, the fastest engine in the benchmark table, does not NaN-box.** It uses tagged pointers + SMIs + heap numbers — superficially *closer* to Lambda's scheme than to JSC's. V8 gets away with boxed heap numbers because its feedback-driven JIT keeps hot doubles unboxed in registers and in typed object fields (mutable double fields, double element kinds), so the universal boxed representation almost never appears in hot code.

The lesson: **value representation matters in inverse proportion to the strength of type specialization.** LambdaJS's static inference is far weaker than V8's runtime feedback, so today the representation is load-bearing — which is exactly why the float wall shows up at benchmark-defining scale. There are two ways to close the gap:

1. Change the universal representation (NaN-boxing) so the slow case is cheap.
2. Ensure hot code never sees the universal representation (typed storage + unboxed native paths) — the V8 strategy.

Lambda has already started down road 2: shaped object slots store raw doubles inline (`js_set_slot_f`), `ArrayNum` exists, the native `MIR_T_D` register paths exist. The Part 1/Part 2 proposals (T5 dense-double arrays, T2.1 native-version widening, Part 2 stack-boxed temporaries) are road 2 *completed*.

## 4.5 What a NaN-boxing migration would actually cost Lambda

The tag layout is not a local decision — it is baked into:

- The GC's pointer discrimination (conservative stack scan, `is_gc_object` tag masking) and the data-zone relocation/fixup paths (`gc_fixup_embedded_pointers`).
- Every container API and every `it2*`/`*2it` packing macro, used by all input parsers, formatters, the validator, and Radiant.
- The JIT lowerings of **four** language frontends (Lambda, JS, plus the Jube runtimes), including inline tag arithmetic emitted as MIR (`jm_box_int_reg`'s shift/OR sequences, the raw-constant `s2it` bakes).
- The rich scalar taxonomy: int56 and the `NUM_SIZED` inline sub-typed scalars have no comfortable NaN-payload encoding; datetime/decimal/symbol/binary would keep working as pointers, but the *inline* variants would regress or need redesign.

A realistic estimate is a ~50 % runtime refactor with a long correctness tail (every subsystem that pattern-matches on the high byte), in exchange for speed in exactly one type category — a category that road 2 addresses for the hot paths at a fraction of the risk.

**A middle path exists in the literature** — low-bit tagging with biased/rotated doubles ("self-tagging floats"), where common double exponent ranges become inline-representable. As published it assumes low-bit tags, sacrificing the clean 256-tag byte *and* int56 — but Lambda's high-byte layout admits a **no-rotation adaptation** that keeps both. That adaptation is developed in full as **Part 5** of this document; it is the scheme to evaluate before ever committing to a full NaN-boxing migration.

## 4.6 Verdict & recommendation

- **Which runs faster?** For the workload JS benchmarks measure — untyped float-heavy dynamic code — **NaN-boxing**, decisively; it removes the float-boxing wall by construction. For int-heavy, pointer-heavy, and dispatch-heavy code the two are near parity, with high-byte slightly ahead on inline-int range and tag-space uniformity.
- **Which is the better design?** **For Lambda as a system, high-byte tagging.** Lambda is not a JS engine that happens to process data — it is a multi-format data-processing language (datetime, decimal, symbol, binary, element, range, sized numerics) with four frontends on one substrate. The 256-tag space is a genuine architectural asset: uniform, legible, room to grow, with `NUM_SIZED` inline scalars that NaN-payload space cannot express cleanly. For a from-scratch, JS-only VM, NaN-boxing would be the right call.
- **Recommended strategy:** keep the high-byte scheme and **finish road 2** — T5 (dense-double arrays), T2.1 (native-version widening), Part 2 (stack-boxed temporaries), plus the already-landed typed slots. That converges on the V8 configuration: doubles unboxed everywhere it matters, universal representation only on cold paths. Revisit the representation question only if, after those land, float boxing still dominates profiles — and at that point evaluate the **Part 5** high-byte self-tagging adaptation before full NaN-boxing.

---

# Part 5 — Inline Doubles via High-Byte Float Self-Tagging

A concrete candidate fix for the double-boxing problem (Part 1 Wall 1, Part 4 §4.2): store the overwhelming majority of doubles **directly inside the `Item` word**, with no heap box, while keeping Lambda's high-byte tag scheme, int56, containers-as-bare-pointers, and the 256-value tag space. It adapts the published "Float Self-Tagging" technique to Lambda's layout — and because Lambda's tag byte already sits where a double's sign and exponent live, the adaptation needs **no rotation at all**, and value discrimination reduces to **one bit-mask test**.

## 5.1 The source idea: "Float Self-Tagging"

**Paper:** *Float Self-Tagging* — Olivier Melançon, Marc Feeley et al. (Université de Montréal / Gambit Scheme group, ≈2024; prototyped in Gambit **and in Google V8**). *(Verify the exact citation when picking this up; the mechanism below is what matters.)*

**Context.** In classic **low-bit tagging** (OCaml, many Schemes, V8's SMIs), the low 2–3 bits of a word are the tag; 8-byte-aligned pointers are "self-tagged" by their alignment (low bits `000`) and dereference without masking. The scheme's weakness is doubles: an IEEE-754 double uses all 64 bits, leaving no room for tag bits, so floats must be heap-boxed — the same disease Lambda has.

**The insight.** A double's **high** bits (sign + top exponent bits) are highly predictable: real programs' doubles live almost entirely in a moderate magnitude band, where the top few bits take only a handful of values. So:

1. **Rotate** the double's bit pattern left by *k* (the tag width). The predictable top bits land in the low-bit tag position.
2. **Designate** the tag values corresponding to the common exponent band as "this word *is* an inline double". A double in the band stores as its rotated bits — it *tagged itself*. Encode = 1-cycle rotate (+ band check); decode = 1-cycle rotate back.
3. Doubles outside the band — huge/tiny magnitudes, subnormals, ±0.0, Inf, NaN — fall back to the existing heap box. The paper reports **>99 % of doubles self-tag** in practice; the box survives only as a rare-case escape hatch.
4. A **bias** variant adds a constant to the exponent field instead of (or in addition to) rotating, repositioning the band onto mask-friendly tag values.

**Why it beats NaN-boxing as a retrofit:** NaN-boxing consumes the entire 64-bit encoding — pointers shrink to ≤48–51 masked bits, ints to int32, the whole type system squeezes into NaN payload patterns. Self-tagging is a *local carve-out*: pointers, fixnums, and the existing tag space stay untouched; you donate a few tag values to the common float exponent band.

## 5.2 The no-rotation, high-byte adaptation for Lambda

Lambda's tag is the **high byte** — exactly where a double's sign and exponent already live. So no rotation is needed; only a **bias** to center the common band onto a single-bit-testable tag region.

### Encoding

Let `BIAS = 0x2000_0000_0000_0000` (adds `0x200` to the 11-bit exponent field) and `BIT62 = 0x4000_0000_0000_0000`.

```c
// encode: double → Item        (replaces push_d at boxing sites)
uint64_t u = bits(d);            // bit-identical reinterpret
uint64_t t = u + BIAS;           // one integer add
if (t & BIT62)  item = t;        // inline self-tagged double
else            item = d2it(push_d(d));   // rare: out-of-band → boxed as today

// decode: Item → double        (replaces the it2d pointer dereference)
if (item & BIT62)  d = bits⁻¹(item - BIAS);   // one subtract + reinterpret
else               d = *(double*)(item & MASK56);  // boxed fallback

// discrimination — the single bit-mask test:
is_inline_double(item)  =  (item & BIT62) != 0
```

### Why the arithmetic works out

The biased exponent `e' = e + 0x200` has its MSB (word bit 62) set **iff** `e ∈ [0x200, 0x5FF]`:

| Input double | `e` | `t` high byte | bit 62 | Result |
|---|---|---|---|---|
| `1.0` (`0x3FF0…`) | 0x3FF | 0x5F | set | inline |
| `-1.0` (`0xBFF0…`) | 0x3FF | 0xDF | set | inline |
| `2^-500` | 0x20B | 0x40 | set | inline |
| `1e-300` | ~0x01A | 0x21 | clear | boxed |
| `1e300` | ~0x7E3 | 0x9E (carry into sign) | clear | boxed |
| `-1e300` | ~0x7E3 | 0x1E (wraps mod 2⁶⁴) | clear | boxed |
| `±0.0`, subnormals | 0x000–0x1FF | 0x20–0x3F | clear | boxed (see §5.4) |
| `±Inf`, `NaN` | 0x7FF | 0x9F / 0x1F | clear | boxed |

Note the rejection is *automatic*: out-of-band inputs produce a transient `t` with bit 62 clear, which is discarded — no out-of-band bit pattern is ever **stored**, so there is no aliasing with real tagged Items. The occupied inline-double space is exactly the high bytes `0x40–0x7F` (positive) and `0xC0–0xFF` (negative) — i.e., **all values with byte-bit 6 set**.

### Coverage

`e ∈ [0x200, 0x5FF]` → unbiased exponent −511…+512 → magnitudes **≈1.5·10⁻¹⁵⁴ to ≈2.7·10¹⁵⁴** — the square root of the double range in both directions. Every value that any benchmark or realistic workload produces self-tags; the paper's >99 % measured rate would be effectively 100 % here. Only subnormals, zeros, infinities, NaNs, and astronomically large/small magnitudes take the boxed path.

## 5.3 TypeId re-alignment: the one-bit partition invariant

The scheme partitions the high byte by **one bit**:

> **Invariant:** word bit 62 (`0x40` in the high byte) set ⟺ the Item is an inline self-tagged double. Every TypeId, sentinel, and packed encoding must keep bit 62 **clear** — i.e., high bytes must stay within `0x00–0x3F` and `0x80–0xBF`.

Audit of current occupancy (`lambda.h:83`–`:121`):

- **TypeId enum** — sequential from 0, ~30 values, all `< 0x1E` (`LMD_TYPE_COUNT`). ✓ Compliant with huge headroom (`0x1E–0x3F` free for future types; `0x80–0xBF` also available if ever needed).
- **Containers** — bare pointers; canonical user-space addresses keep the high byte `0x00` and bit 62 clear. ✓
- **Packed layouts** — `ITEM_INT` int56 (high byte = `LMD_TYPE_INT`), `NUM_SIZED` (sub-type in `[55:48]`, high byte = `LMD_TYPE_NUM_SIZED`), bool/null/undefined/TDZ: all keep their TypeId in the high byte. ✓
- **✗ Two violations to fix:** `JS_DELETED_SENTINEL_VAL = 0x7E00…` and `JS_ITER_DONE_SENTINEL = 0x7F00…` (`js_runtime.h:26`, `:31`) sit in bit-62-set space and would read as inline doubles. Both are engine-internal constants chosen precisely *because* their tags were unused — renumber them to free bit-62-clear values (e.g. `0x3E`/`0x3F`). One-line changes plus their comparison sites.
- **Verify during implementation:** any GC-internal header type ids past `LMD_TYPE_COUNT` (`LMD_TYPE_MAP_` etc. in `gc_heap.c`) live in object headers, not Item high bytes — confirm none is ever materialized into an Item tag; likewise grep for any ad-hoc `raw_item >> 56` comparisons against literals.

The enum itself does **not** need renumbering — the user-visible re-alignment is just the two sentinels plus adopting the invariant as a documented rule for all future tag assignments (add a `static_assert(LMD_TYPE_COUNT < 0x40)` and a comment on the enum).

### What each primitive becomes

- `get_type_id(item)` — prepend one predictable branch: `if (item & BIT62) return LMD_TYPE_FLOAT;` then the existing logic. This is the hottest primitive in the runtime; the branch is one test against a constant.
- `it2d` — bit-62 test → subtract + bitcast on the hot arm; pointer deref on the cold arm. (MIR has no int↔float bitcast instruction; use a store/load through a scratch frame slot — still far cheaper than the current call + cache-missing deref — or add a `bitcast` insn to the vendored MIR, a small patch.)
- Float boxing sites (`jm_box_float`, `push_d` callers, `js_make_number`) — inline `ADD + BT` with a cold call to the boxed path. The `push_d` C call disappears from all in-band traffic.
- Type-switch sites — any `switch (get_type_id(v))` is already correct once `get_type_id` normalizes; only code that reads the raw high byte directly needs the §5.3 audit.

## 5.4 Special values

- **±0.0** — extremely common, but out-of-band (`e = 0`). Do **not** allocate: intern two static immutable payloads and return the same tagged-pointer Items forever (`ITEM_FLOAT_POS0`/`ITEM_FLOAT_NEG0` constants). Same for **NaN** and **±Inf** singletons. With these interned, the *allocating* fallback is reached only for subnormals and magnitudes beyond 10^±154 — negligible in real workloads.
- **Optional later refinement:** dedicate packed encodings for ±0.0 under a spare bit-62-clear tag so even the interned-pointer indirection disappears; not needed for v1.
- **`-0.0` semantics** are preserved automatically: `-0.0` has its own distinct encoding (interned box), and in-band negatives keep their sign bit through the bias add/subtract, so `js_make_number`'s existing minus-zero guard (JS_03 §2) carries over.

## 5.5 Integration points & risks

1. **GC pointer discrimination.** Marking code that switches on the tag byte to find pointer-typed Items naturally ignores inline doubles (their high bytes match no pointer tag) — but **audit every path that masks the low 56 bits unconditionally**. `gc_fixup_embedded_pointers` (`gc_heap.c:1203`) rewrites embedded float/int64/datetime pointers in relocated buffers keyed on the tag byte — inline doubles fall outside those tags and are correctly skipped, but this must be verified, not assumed. The conservative stack scan may false-retain when an inline double's low bits alias a heap address — the same benign over-approximation packed int56 already causes today (conservative scan never rewrites).
2. **Canonical-encoding invariant.** An in-band double must **always** be encoded inline, never boxed — otherwise the same value has two representations and raw-bit comparisons diverge. Enforce at every producer (`js_make_number`, `push_d` wrappers, input parsers, `it2d`-round-trips). Note today's baseline is *worse* (equal boxed floats are already pointer-distinct — cf. the known ArrayNum `==` representation-sensitivity), so canonicalization strictly improves bit-comparability; the risk is only in a *mixed* transition state, which the compile-time flag (below) avoids.
3. **Two float representations, permanently.** Every float consumer keeps a boxed arm. The hot arm is inline; the cold arm is the existing code — so this is additive, but it doubles the test matrix for float paths (in-band / out-of-band / specials).
4. **Both engines + interpreter.** `get_type_id`/`it2d` changes cover the C runtime and MIR-interp paths automatically; the JIT lowerings (`jm_box_float`, `jm_emit_unbox_float`, and Lambda-side `push_d` emission) need the inline sequences explicitly.
5. **Nursery/rooting interaction.** In-band floats never touch the nursery — no allocation, no relocation, **no rooting** (an inline double is a non-pointer; the Part 3 machinery and the `gc_data_zone_copy` fixups simply stop applying to them). Boxed-float traffic collapses to the special/extreme residue.

## 5.6 Expected impact, relation to other parts, and plan

**Reach.** This removes float boxing at **every** boundary — array elements, call arguments, returns, untyped locals, property values in non-shaped objects — in **both** engines, for effectively all real doubles. That is broader than Part 2 (frame-local values only) and broader than T5 (arrays only):

- **vs. Part 2 (stack boxing):** self-tagging subsumes Part 2's float case entirely (no escape analysis, no sink promotion needed — an inline double *is* a value). Part 2 remains valuable for **int64/datetime** payloads, which cannot fit inline.
- **vs. T5 (dense-double arrays):** T5 still wins inside numeric array kernels (raw `MIR_T_D` loads with no encode/decode per element, SIMD-friendly layout), but self-tagging fixes the general case T5 can't reach and removes T5's transition pressure (a boxed Item array holding inline doubles is already allocation-free).
- **vs. Part 4's verdict:** this is "road 1.5" made concrete — the representation fix that keeps the system design. It upgrades Part 4's recommendation from "typed storage only" to "typed storage first, self-tagging as the representation-level complement".

**Expected effect:** the float share of Wall 1 and Wall 4 (nursery-triggered whole-heap GCs from `push_d` churn) drops to near zero; nbody/mandelbrot/navier_stokes-class benchmarks lose their boxing component entirely (dispatch and call overhead remain — Parts 1 T1/T2 still apply). Encode/decode adds ~2 ALU ops per float boundary crossing, orders of magnitude cheaper than allocation + dereference.

**Phased plan.**

| Phase | Content | Gate |
|---|---|---|
| S0 | Occupancy audit (§5.3), renumber the two sentinels, add the `static_assert` + invariant comment; land independently (zero behavior change) | full baselines green |
| S1 | Runtime level behind `LAMBDA_SELF_TAG_FLOAT` compile flag: `get_type_id`, `it2d`, `js_make_number`, producer canonicalization, interned specials | test262 full + `make test-lambda-baseline` + Radiant baseline, flag on vs. off |
| S2 | JIT lowering inline fast paths in both transpilers (box/unbox sequences; MIR bitcast-via-slot or vendored `bitcast` insn) | same gates + benchmark A/B (nbody, mandelbrot, navier_stokes, matmul, splay for float keys) |
| S3 | GC audit pass (§5.5.1) + ASan run of the full benchmark suite; then default the flag on | clean ASan, no baseline regressions |

**Fallback:** if S2 measures worse than expected (branch-prediction cost in `get_type_id` on non-float-heavy code), the flag confines the experiment; the S0 sentinel cleanup is worth keeping regardless.
