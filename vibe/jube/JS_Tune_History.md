# JS Runtime Tuning History (Tune1 – Tune12)

**Date**: 2026-08-07 · **Status**: retrospective summary · **Scope**: `vibe/jube/Transpile_Js_Tune*.md`, rounds 1–12 (2026-05-26 → 2026-06-26)

This doc recaps *what was tried and why*, not how it was coded. The main body keeps the tunings that were measured, kept, and are still worth understanding. The appendices keep the failures — in this series the reverted work is nearly as instructive as the wins, because most of it failed for a reason that generalises.

| Round | Doc | Theme | Headline outcome |
| --- | --- | --- | --- |
| 1 | `Transpile_Js_Tune.md` | Call/argument path | O(n²) call args → O(1); 44–480× on call-heavy loops |
| 2 | `Transpile_Js_Tune2.md` | Parser / name-pool / eval | 0-for-3; every candidate reverted |
| 3 | `Transpile_Js_Tune3.md` | RegExp property walk + constant folding | −39% property cluster, −86% shift cluster |
| 4 | `Transpile_Js_Tune4.md` | TypedArray bulk + large-source interpreter | bulk ops up to 270×; identifier cluster −70% |
| 5 | `Transpile_Js_Tune5.md` | Benchmark regression bisect | sieve 114×, deriv/gcbench ~19× recovered |
| 6 | `Transpile_Js_Tune6_AST.md` | Compile latency (AST / MIR / link) | link root-caused; interpreter policy 4–6× on vendor libs |
| 7 | `Transpile_Js_Tune7_Interp.md` | Interpreter vs JIT verification | interpreter −17…−28% on short scripts; TCO gap found |
| 8 | `Transpile_Js_Tune8_Sys_Func.md` | Runtime ABI / registry surface | 547 → 452 entries, −7.2% aggregate |
| 9 | `Transpile_Js_Tune9.md` | test262 slow-cluster sweep | Atomics 60×, identifier rows 3.4×, Script aliases −33% |
| 10 | `Transpile_Js_Tune10.md` | Execution profiler + shape guards | profiler landed; structural slot guard 100% hit |
| 11 | `Transpile_Js_Tune11_Callsite_Cache.md` | Plain-map property ICs | megamorphism eliminated; intrinsic proto cache −23% |
| 12 | `Transpile_Js_Tune12_Array_Prototype_IC.md` | Array ICs, prototype shapes, GC | cube3d 73.0s → 3.75s |

---

## 1. The measurement discipline (the most reusable output of the series)

Tune1 landed a huge win from a hypothesis. Tune2 then landed nothing at all: three separate well-argued hypotheses (name-pool hashing, an ID-classification trie, an eval compile cache) all failed against the real binary. From Tune3 onwards the process itself became the deliverable, and every later round follows it:

1. **Release build only.** Tune1 opened by discovering its own top-30 slow list was a debug/O0 artifact — the "slowest" Unicode tests ran 0.01–0.06 s in release. Debug profiles fabricate bottlenecks non-uniformly.
2. **Measure before designing.** Tune6 rev 3 killed its own AST-first premise: AST build is 0.4–2.3% of compile time; MIR link is 50–82%. Tune10 replaced "tune by benchmark ratio" with "tune by profiled helper self-time".
3. **Land behind an env gate** (`LAMBDA_JS_TA_RAW_FAST`, `LAMBDA_JS_LARGE_INTERP`, `LAMBDA_JS_CONST_FOLD`, `LAMBDA_JS_LOAD_IC`, `LAMBDA_JS_SHARED_CTOR_SHAPE`, …) so A/B is the *same binary*, not two builds.
4. **Two gates, both required**: zero test262 pass/fail regressions, *and* a beyond-noise win in the targeted cluster. Run-to-run noise on this harness is ~15%, and machine load has repeatedly faked both regressions and wins — Tune3 and Tune4 each had a run contaminated by a concurrent build.
5. **Neutral means revert.** A correctness-clean but performance-neutral change is not kept; it is only a maintenance surface. This rule retired all of Tune2 and several Tune9–Tune12 slices.
6. **Keep the honest post-mortem in the doc.** Every round records what was tried and rejected, which is why later rounds explicitly skip the paths Tune2 burned.

---

## 2. Effective tunings

### 2.1 Call and argument path — Tune1

**Defect.** Every JS call with ≥1 argument allocated its argument buffer through `js_alloc_env`, which called `heap_register_gc_root_range` — a *permanent* GC root registration with a linear dedup scan, never popped and never freed. Call *k* scanned *k−1* prior ranges, and every GC cycle re-walked them all. Call-heavy loops were therefore **O(n²) in time and unbounded in memory**; a 0-arg call was flat, giving a diagnostic "cliff" at exactly 1 argument.

**Fix (§3.1).** A transient bump-allocated **argument stack**, registered with the GC exactly once as a single growable root range. `js_args_push(n)` / `js_args_save` / `js_args_restore(mark)` bracket each call; try/catch lowering resets the mark at catch/finally entry so a throw during argument evaluation reclaims the frame. Slots above the top stay zeroed so GC marks only live frames, and the base never moves so partially-built args stay rooted.

**Fix (§3.3).** Widen static dispatch: a `const`-bound function expression or arrow is as immutable as a `function` declaration, so devirtualise it to a direct MIR call — guarded by a textual "call site after initializer end" check that preserves TDZ.

**Fix (§7.2.B).** Intern the 128 single-byte ASCII strings; `s[i]`, `charAt`, and `String.fromCharCode(n<128)` return table entries instead of allocating. Strings are immutable, so this is observationally identical.

| Workload | Before | After |
| --- | ---: | ---: |
| Dynamic call loop, 160k iters | 6008 ms | 12.5 ms |
| Scaling 20k→160k | O(n²) | linear |
| `assert.sameValue` ×65 536 | 4154 ms | 83 ms |
| `character-class-escape-non-whitespace.js` | 9.6 s | 0.22 s |
| test262 sum of per-test elapsed (§3.3 A/B) | 431.0 s | 284.1 s (−34%) |

**Why it generalises.** One builder function (`jm_build_args_array`) feeds 30+ call-lowering sites, so the fix accelerates every call in every program. This is the template the whole series follows: find the *lifetime* or *algorithmic* defect behind a chokepoint, not the slow test.

### 2.2 Constant folding and dead-branch elimination — Tune3 §3.2

The JS transpiler had **no constant folding at all**. `jm_try_fold_const` folds literal unary/binary/bitwise/shift/comparison subtrees with exact runtime semantics (ToInt32/ToUint32, ±2⁵³ round-trip check, bail on non-finite / `-0` / bigint / `** && || ??`), wired into two sites: `if` conditions (dead branch dropped, guarded by a whitelist so Annex-B function hoisting can't be lost) and value sites (`jm_emit_folded_at_value_site` mirrors the native/boxed register convention and falls through to normal codegen on any disagreement).

Validated by a **differential test with the folder on vs off in the same binary** — 3,780 constant conditions and 4,800+ literal expressions, bit-identical, with the folder correctly *bailing* on the one pre-existing engine discrepancy. Cluster E (`S11.7.{1,2,3}_A4`, compile-bound files of ~640 constant `if` statements) fell **4.55 s → 0.63 s (−86%)**.

### 2.3 Exploiting locality in Unicode property matching — Tune3 §2.3.A

`^\p{X}+$` already bypassed RE2 with a code-point walk, but each code point restarted a binary search over the property's range table — O(input × log ranges). Both the test corpus and real text are **locally clustered**, so the walk carries a **resumable cursor**: check the previous range and its neighbours first (O(1)), fall back to binary search on a miss. Generated-property cluster (439 tests) **61.84 s → 37.67 s (−39.1%)**, zero flipped exit codes across all 583 property-escape tests.

### 2.4 Compile latency: run cold code in the MIR interpreter — Tune4 / Tune6 / Tune7

This is the single most valuable *structural* finding in the series, and it took three rounds to get right.

**Root cause (Tune6 §0.2a).** `link_us` — 50–82% of compile time — is not symbol resolution (that is O(n) through a hash table; see the round's Appendix A). It is `MIR_set_gen_interface` calling `MIR_gen` **eagerly for every function**, including ones never called. Link cost ≈ Σ MIR_gen(function), which is why lodash (78 KB, ~thousands of functions) cost 6.7 s while a 125 KB file cost 1.0 s.

**The correction that mattered (Tune6 §0.2d).** For a large module, opt0 JIT ≈ opt2 JIT (3,691 vs 3,703 ms) — link is dominated by base machine-code emission, not the optimizer. So the pre-existing ">100k insns → opt=0" downgrade was a **no-op**: it fired, but only twiddled the JIT opt level and never engaged the interpreter. The real lever is **skipping codegen entirely**.

**Landed policy (Tune6 §0.2e).** Count MIR insns after lowering, then link with `MIR_set_interp_interface` when `insns > 100k` (any context) or `document_context && insns > 20k`; `layout`/`render`/`view` force it for all document JS. Tune4's earlier source-size version (≥15 KB at opt 0) was the first cut of the same idea.

| Fixture | total before | total after | link before | link after |
| --- | ---: | ---: | ---: | ---: |
| `lib_lodash.js` (680k insns) | 6,731 ms | **1,291 ms** | 4,809 ms | **128 ms** |
| `ramda_src_min.js` (304k) | 2,451 ms | **408 ms** | 1,781 ms | **48 ms** |
| `dom_jquery_lib.js` (253k) | 1,449 ms | **332 ms** | 949 ms | **32 ms** |

Radiant `web-tmpl` suite (170 templates loading vendor JS): **3.05× wall / 3.24× CPU**. test262 zero regressions; Radiant baseline 5,715/5,715.

**Critical mechanism detail.** Use the **link-interface** interpreter (`MIR_set_interp_interface` with the generator still initialized, `g_mir_interp_mode = 0`) — *not* pure-interp mode. Setting `g_mir_interp_mode = 1` leaves the generator uninitialized and regressed 49 interactive UI-automation tests, because eval/batch lowering and on-demand generation key off that flag. Same compile savings, no divergence.

**Tune7 quantified the crossover** on real suites: on short-lived scripts the interpreter is *faster* in aggregate (test262 per-test sum −17.4%; `test_js_gtest` −28%, with link 4.29 s → 0.45 s and execute only +0.19 s). On a 50M-iteration hot loop the JIT wins 1.65×. It also found the one **semantic divergence**: MIR's interpreter does not eliminate tail calls, so TCO-dependent deep recursion (`test/js/tco.js`) stack-overflows under interp while passing under JIT. Any promotion of interp to a primary path must close that gap.

### 2.5 Benchmark regressions: bisect, then profile — Tune5

LambdaJS had silently regressed 2–350× against a saved April benchmark run. The round's value is the method: rule out CPU contention and workload drift first, pick the *most sensitive, fastest* benchmark as the bisect signal (`sieve`, 350×, sub-ms when healthy), and fall back to **profiling** when history is non-monotonic (a `deriv` bisect hit a 28 s spike at a merge whose parents were both fine).

Two independent root causes, both fixed by choosing the *semantically correct primitive*:

- **Array index writes.** A correctness commit added an ES `OrdinarySet` inherited-accessor walk (plus an index `snprintf` and a prototype fetch) to *every* indexed write. The fix short-circuits the common case — an existing own dense data element of a plain array (`arr->extra == 0`, `is_content != 1`, in-bounds, not the deleted sentinel). This is not merely an optimisation: `OrdinarySet` *requires* an own writable data property to be written without consulting the prototype, so the fix also removed a latent shadowing bug. sieve **93.1 ms → 0.82 ms (114×)**, puzzle 46×, permute 4.3×.
- **Object literals.** Fields had moved from a direct store to `js_create_data_property`, which ran the full `Object.defineProperty` machinery per field: a throwaway descriptor object plus four attribute-name re-interns. The fix performs `[[DefineOwnProperty]]` via the raw own-field store `map_put`, guarded to plain `MAP_KIND_PLAIN` objects with no `__`-prefixed key, key absent, target extensible. deriv **3,630 → 194 ms**, gcbench **41,530 → 2,219 ms** (~19×).

**The lesson that recurs later:** an earlier attempt used `js_property_set` for the same case and regressed **2,704 test262 tests**, because `[[Set]]` honours inherited non-writable/accessor properties while `CreateDataProperty` does not. A fast path must be built on a primitive with the *same* observable contract, not merely a faster function.

### 2.6 Bulk data paths — Tune4 / Tune9

**TypedArray raw views (Tune4 T4-P2).** Same-type raw copy and cross-type numeric conversion in the constructor and `%TypedArray%.prototype.set`; early BigInt/Number category rejection; raw paths for `with`, `reverse`, `toReversed`, and raw numeric scan for `indexOf`/`lastIndexOf`/`includes` (BigInt left boxed for exact comparison, `join` left alone since string conversion dominates).

test262 was **neutral** — compliance tests use tiny arrays and runner overhead dominates — but bulk workloads showed the intent: 80 iterations over 200k-element `Int32Array` **0.80 s → 0.16 s**; a 400k-element `Float64Array` search loop **30.01 s → 0.11 s**. Kept on the explicit reasoning that the optimisation targets large data movement, which the conformance suite does not represent.

**RegExp / Unicode-property plumbing (Tune9).** `.test()` reads the compiled `special_property_kind` directly instead of re-reading and re-parsing `source` (P1-A). Generated `Script`/`sc`/`Script_Extensions`/`scx` lookups scan only the matching table band, and aliases pointing at identical `(ranges, count)` tables are canonicalised to one kind id so they share the all-string result cache (P1-D): 350 rows **13.281 s → 8.873 s**, per-test 37.9 → 25.4 ms.

**Atomics virtual time (Tune9).** Agent-slot `Atomics.waitAsync` used a real libuv timeout for short finite waits while the synchronous path already used the js262 virtual clock. Aligning them: ~1000 ms/test → 17–32 ms, async batch 18.1 s → 1.0 s, suite wall 120.5 s → 108.5 s.

### 2.7 Attacking compile *volume*, not compile speed — Tune9 Phases 6–7

The generated Unicode-identifier tests resisted three rounds of parser/name-pool theories. Phase timing finally showed the cost was not parse or scan at all: for a large generated declaration file, AST build was ~72 ms and the declaration body emitted 40k–66k MIR instructions, then execution spent ~80 ms creating thousands of global `var` properties one at a time.

Two structural fixes, both general to large generated top-level declaration bodies:

- Remove the O(n²) declaration-site scope lookup and collapse the generated declaration body — **MIR body insns 40–66k → 35**, AST build → 1.4–2.8 ms.
- `map_put_undefined_unique_absent_bulk()` + `js_define_global_var_properties_bulk_absent()`: verify in the JS layer that every key is a valid, currently-absent string, then append all slots in one pass with a single buffer growth, falling back per-key on any anomaly — **execute ~80 ms → ~5 ms**.

Largest identifier rows: **106–108 ms → ~32 ms**. Full release gate 40,261/40,261, 0 regressions.

### 2.8 Runtime ABI surface reduction — Tune8

The JS engine registered **547 `js_*` functions** in the JIT import table. The method: build with `-DJS_MIR_EMIT_TELEMETRY`, run the full sweep, and delete only entries that are **both** telemetry-unused **and** never quoted in any MIR lowering file (the C functions stay linked and reachable from C). That intersection was 91 entries, of which 78 were kept deleted after 13 DOM/web-API rows were restored on a scope call; a smaller registry means shorter `import_cache` probe chains at JIT time. Aggregate per-test wall **484.9 s → 449.7 s (−7.24%)**.

Fold rules that emerged, all budgeted at **≤1 ns/call on the hot path**:

- **Inverse pairs fold for free**: `js_ne_raw` → `js_eq_raw` + an inline `MIR_XOR result, 1` (the XOR flips the value bit while preserving the type tag). Same for the loose pair and the four raw relops → `js_cmp_raw(op, l, r)`. Cost +0.4 ns against a 20 ns operation.
- **Fold the cold tail, keep the hot head direct.** `js_define_global_{var,eval_var,function}_property` → one `_v` dispatcher (−2); `js_property_set` (200k emissions) stays direct while only the 1.6k strict-mode sites route through a dispatcher.
- **Don't fold what the JIT already inlines.** Bitwise/shift helpers measure ~3.3 ns/op because `|0` coercion lets the JIT inline past the runtime call; folding them changes nothing.

Final: **452 entries** with test262 fast paths on, test262 39,255/39,255 stable through every intermediate fold.

### 2.9 Property access: shapes and inline caches — Tune10 / Tune11 / Tune12

This is the longest arc, and its honest summary is that **the ICs themselves were neutral; the shape work and the prototype cache were the wins.**

**Tune10 — build the instrument first.** `js_exec_profile` (`JS_EXEC_PROFILE=time`) with per-event call/self-time counters, MIR call-site counts per helper, shape-guard hit/miss counters, and per-site tables for load ICs and property-set sites. It immediately reframed the problem: `richards` spent ~3.5 s self time in `property_get` while `call_function` self time was small (its huge inclusive time is just the callee body).

**Tune10's kept change — the structural slot guard.** The existing class-field guard cached one constructor's `TypeMap*` and compared by pointer, so it hit 2,322 times and missed 113,778 times on the single hot `richards` site. Root cause: `js_new_object_with_shape` creates a fresh `TypeMap` per instance and class metadata writes extend each independently, producing many structurally-equivalent shapes with different pointers. Replacing the pointer-identity fallback with `js_shape_slot_guard(object, name, len, byte_offset)` — which proves the same property name at the same byte offset on a plain map, rejecting accessors and deleted slots — turned the site into **116,100 hits / 0 misses** at a total cost of 2.1 ms.

**Tune11 — make shapes shareable.**

- **P0**: split ordinary maps into descriptor-free `MAP_KIND_PLAIN` and descriptor-bearing `MAP_KIND_DESC`, so the IC hit path only needs receiver map + kind + data pointer + shape pointer.
- **P2/P3**: callsite load and store ICs for fixed-name members, mono/poly/megamorphic, with the full semantic path as the miss path. Store installs only *after* the ordinary setter has done the semantic checks, and only for existing own data slots whose type can accept the value in place.
- **A real bug found by per-site profiling**: `js_load_ic_offset_ok()` required pointer-sized alignment and bounds, which rejected hot compact boolean/object fields. Removing it took `miss_offset` from 2,958,200 to **0** — but the sites then went megamorphic, exposing the real problem.
- **P4/P5**: canonicalise constructor shapes (`is_shared_constructor_shape`, reuse the cached `TypeMap*` after the first instance, with detach-on-mutation rules), add name-keyed `TypeMapTransition` chains for post-`super()` extension, and — the slice that actually worked — **combined derived pre-shaping**: merge inherited constructor field metadata base-first at compile time so `new Derived(...)` allocates the final base+derived shape up front. `richards` load-IC megamorphic count went to **0** with `hit_mono` dominating.
- **P6a — the first clear wall-time win.** Profiling after P5 showed the remaining time was `js_get_implicit_proto()` / `js_get_prototype_of()` re-deriving intrinsic prototypes on every chain walk: resolve the built-in constructor, then read its public `.prototype` through `js_property_get()`. `js_get_intrinsic_prototype_for_class(class_id)` caches the built-in prototype objects for internal chain walking only (public `Ctor.prototype` access keeps normal semantics). `richards`: `property_get` **22.86M → 9.34M (−59%)**, wall **6,874.9 ms → 5,276.3 ms (−23.3%)**.

**Tune12 — the receivers the ICs could not reach.**

- **P1/P1b — arrays used as records.** Extend IC entries with a `receiver_kind` so array companion maps (`arr->extra`, `MAP_KIND_ARRAY_PROPS`) participate for non-index, non-`length` names; add an in-place same-size companion write; add `TypeMap::has_array_index_shape` so dense writes only probe the companion map once a numeric descriptor has actually been installed. `cube3d` load-IC misses **161,618 → 250**; release 77.86 s → 72.68 s.
- **P2/P2b — prototype-style constructors.** Cache a constructor shape per collected function declaration, and compose inherited fields for the recognisable hand-written inheritance idioms (`Ctor.superConstructor.call(this, …)`, `Ctor.super_.call(this, …)`, direct parent `.call(this, …)`, cross-checked against `Ctor.inheritsFrom(Base)`), rejecting anything dynamic. `jetstream/deltablue` load-IC megamorphic **2,803,686 → 0**, store megamorphic **344,466 → 0**.
- **P3 — `ShapeEntry::name_id`** as a *comparison accelerator only*: a compact id for early reject, with pointer/length/string comparison still the authority, so hash collisions stay safe. Hot fixed-name hits skip the FNV hash entirely when the site reuses the same name pointer.
- **Sparse→dense array promotion**: migrate a `SparseArrayMap` back to holey dense storage when it becomes dense enough (`length ≤ 262144`, ≥4096 sparse entries, ≥25% density, all in-bounds). `jetstream/hashmap` 10.70 s → **9.47 s (−11.5%)**.
- **Dense own-read fast path**: many computed index expressions are arithmetic-derived and lower to boxed property keys, so they reached `js_property_access` → `js_property_get`. `js_array_fast_own_dense_get()` returns `arr->items[idx]` directly for plain arrays with an in-bounds non-hole slot and no numeric companion descriptor. `navier_stokes` 222 ms → **149 ms**; `property_get` calls **2,105,925 → 1,821**.

### 2.10 GC sweep ownership classification — Tune12 (largest single win)

Profiling `cube3d` attributed ~67.7 s to `array_push_direct_expand`. Phase-level breakdown showed that was an artifact: the append triggers `heap_data_alloc`, which triggers **one** collection, and that collection spends **67.1 s in `gc_sweep()`** — nothing in copying items or stamping holes (2.05 ms).

Root cause: for each of 4,450,856 dead headers the sweep had to decide whether the object came from a bump block, an object-zone slab, or a large allocation, by *searching* thousands of slabs. The fix moves ownership classification to **allocation time**: set `GC_FLAG_BUMP` in the header at bump-pointer allocation, and have `gc_sweep()` branch on header flags (`GC_FLAG_LARGE` → free, `GC_FLAG_BUMP` → unlink only, else → object-zone free list).

**`cube3d` 73.0 s → 3.75 s**, with `gc_sweep` falling from 67,084 ms to 9.8 ms on the same 4.45M objects. The remaining GC cost is now object tracing, a different problem.

---

## 3. Headline results

| Change | Round | Measured effect |
| --- | --- | --- |
| Transient call-argument stack | 1 | 160k-call loop 6008 → 12.5 ms; call scaling O(n²) → linear |
| `const`-bound static dispatch | 1 | test262 per-test sum −34%, wall −8.7% |
| Resumable property-range cursor | 3 | generated-property cluster −39.1% |
| Constant folding + dead branch | 3 | shift-operator cluster −86.2% |
| TypedArray raw bulk paths | 4 | 200k `Int32Array` bulk 5×; 400k numeric search 273× |
| Large/cold module → MIR interpreter | 4, 6 | lodash 6.73 → 1.29 s; Radiant web-tmpl 3.05× wall |
| Dense own-write fast path | 5 | sieve 114×, puzzle 46× |
| Object-literal `map_put` fast path | 5 | deriv ~19×, gcbench ~19× |
| Registry/ABI slimming | 8 | 547 → 452 entries, aggregate −7.24% |
| Atomics virtual time | 9 | waitAsync tests ~60× |
| Generated-declaration bulk init/append | 9 | identifier rows 106–108 → ~32 ms |
| Structural slot guard | 10 | hot guard site 2% → 100% hit rate |
| Constructor shape sharing + derived pre-shaping | 11, 12 | load/store IC megamorphic → 0 on richards & deltablue |
| Cached intrinsic prototypes | 11 | richards −23.3% wall, `property_get` −59% |
| Sparse → dense array promotion | 12 | jetstream/hashmap −11.5% |
| Dense own-read fast path | 12 | navier_stokes −33% |
| GC sweep ownership flags | 12 | **cube3d 73.0 → 3.75 s** |

---

## Appendix A — Failed, reverted, and rejected

Grouped by *why*, because the failure modes repeat.

### A.1 The hypothesis was wrong about the real binary

| Attempt | Round | Outcome |
| --- | --- | --- |
| Name-pool length bypass for long identifiers | 2 §2.4.A | **+18% slower**. The hashmap is O(1)-amortised and SipHash-2-4 distributes long UTF-8 fine; the bypass re-copies the byte range on every reference instead of returning the existing `String*`. |
| Length-prefixed name-pool hash | 2 §2.4.B | Bundled with the above. SipHash already folds length in during finalisation — a no-op. |
| Skip dual-mode strict/sloppy compile | 2 §2.4.C | False premise: the harness compiles each test exactly once. |
| `eval()` compile cache | 2 §3.2 | No signal. Both target eval shapes route *around* the cached Phase A path — regex literals hit an earlier fast path, `var` decls route to Phase C. |
| Static field-use inference from body field names | 10 | `richards` `property_get` **increased** 14.17M → 16.76M and got slower. Field-name matching alone creates too many guard misses and slow fallbacks. |
| Fixed-size generated property-name cache | 9 P1-D | The repeated work was alias-equivalent all-string testing, not the one-time name scan. |
| Descriptor / `ownKeys` caching + TypeMap version stamps | 9 Pass 3 | Flat at ~4.1 s across four variants, while adding shape-mutation work to ordinary object creation. |

### A.2 The primitive had the wrong observable contract

| Attempt | Round | Outcome |
| --- | --- | --- |
| `js_property_set` for object-literal fields | 5 | **2,704 test262 regressions.** `[[Set]]` consults the prototype chain; `CreateDataProperty` must not. |
| Process-global intrinsic prototype cache | 5 §6a | **169 regressions.** test262 creates multiple realms and the batch runner shares a process, so one realm's `Array.prototype` leaked into another. The heap epoch doesn't change at realm boundaries. Redone correctly, realm-aware, as Tune11 P6a. |
| Pure-interp mode (`g_mir_interp_mode = 1`) | 6 §0.2e | **49 interactive UI tests regressed** — the generator stays uninitialized, and eval/batch lowering keys off that flag. The link-interface interp with the generator initialized gives identical savings and passes 234/234. |

### A.3 The fast path cost more than it saved

| Attempt | Round | Outcome |
| --- | --- | --- |
| Multi-operand string-concat fusion | 1 §7.2.A | Never fired on the hot path (`hex[i]` types as `ANY`), and where it did fire the args-stack save/push/restore ate the alloc savings. |
| Widening `jm_should_inline` past `has_native_version` | 1 §7.2.C | SIGSEGV / wrong values: the inliner assumes typed params and return types that only hold for native-versioned functions. |
| MIR native lazy gen interface | 6 §0.2b | Link collapses (4,804 → ~0 ms) but execution explodes 1.2–44×; lodash timed out >180 s. Per-function on-demand generation costs **~80× batch** and goes ≈O(n²) at opt≥2 — traced to optimizer state accumulating across interleaved generations, not to the thunk or to GC. Lazy generation is only viable as *coarse batched deferral*, never per-function at opt≥2. |
| Broad dynamic own-slot store guard | 10 P2 | `property_set` calls fell 1.74M → 181k, yet wall time **rose** (richards 4785 → 5102 ms). Call-count reductions are not evidence. |
| Whole class-propagation branch (constructor field classes, inherited slot lookup, nested guarded writes) | 10 | Dropped: js262 suite wall time regressed. The extra lowering analysis cost more across many short scripts than the guards returned. |
| Runtime-helper emission for array append/dense stores | 12 | 73.0 → 76.0/84.7 s. The inline-MIR POC was flat too — because the real cost was GC sweep, not the store path. |
| Retiring `js_uri_decode_equals_from_char_code`, `js_string_fromCharCode2`, `js_string_replace_nonws_global_fast_no_dollar` | 8 §2.5 | 2 hard timeouts and 8 batch-unstable tests; the generic path is ~100× slower per iteration. All restored. |
| Folding bitwise/shift helpers into dispatchers | 8 §2.1 | Skipped after measurement: ~3.3 ns/op because the JIT inlines past the call via `\|0` coercion. |

### A.4 Right direction, wrong knob

| Attempt | Round | Outcome |
| --- | --- | --- |
| External-scanner ID_Start/ID_Continue trie | 2 §2.4.E | Built and self-verified, then reverted. Both-paths form: +4% on the target cluster and **no conformance gain** (tree-sitter falls back to the internal lexer's permissive ranges). Authoritative form: breaks the `word` directive, so even `var __filename` fails to parse. The trie itself is salvageable for a runtime classifier. |
| Adaptive interpreter policy v2 (MIR insn/class complexity gates) | 4 | Class-heavy slice 16.7 → 11.7 s, but whole suite +1.8% and two URI tests went batch-unstable. Broad complexity thresholds select interp for too much unrelated code; use a tighter guard. |
| Raising `JS_LOAD_IC_POLY_MAX` (4 → 8) | 11, 12 | Removed megamorphic counters in the profile build but changed nothing in release timing. Per-object `TypeMap*` churn only delays megamorphic fallback while raising scan cost — the fix is shape sharing, not cache width. |
| Helper-call load IC on its own | 11 | js262 +1.0% wall, full benchmark sweep geomean −0.12%. The IC only paid off once P4/P5 made shapes shareable and P6a removed the prototype tax; the helper call itself must be inlined in MIR to win. |
| URI codec fast-path work | 1 §7, 4 T4-P1, 9 | Three rounds, no movement on the `decodeURI*/A2_5_T1` pair. The residual is the builtin's per-call result allocation and the million-iteration JS loop, not anything the JS-engine layer owns. Explicitly deferred from Tune9 onward. |

---

## Successor line

Numbering restarted in Aug 2026 with a new series driven by helper-level CPU profiling: `JS_Profiling_Helpers.md` (evidence), `JS_Tune1_Helpers.md`, `JS_Tune1_Runtime.md`, and `JS_Runtime_Redesign.md` (JR-numbered rulings). Those supersede the Tune1–Tune12 backlog where they overlap; this document is the historical record behind them.

---

## 4. Follow-ups

Ordered by how much they block the next round. F1 is a decision, not a task — it should be settled before any further IC work is scheduled.

### F1 — Decide the fate of the callsite ICs: keep, retire, or inline

**The honest verdict.** The Tune11/Tune12 inline-cache work delivered real wins, but **not from the caches**. The helper-call IC measured on its own was neutral-to-negative:

| Measurement | Result |
| --- | --- |
| js262 suite wall, IC on vs `LAMBDA_JS_LOAD_IC=0` | 110.5 s vs 109.4 s (**+1.0% with IC on**) |
| Full benchmark sweep (58 timed), geomean | **−0.12%** — noise |
| Focused property-heavy medians (8 benchmarks) | +0.74% |

Everything that actually moved the needle was adjacent: **P4/P5 constructor shape sharing and combined derived pre-shaping** (which fixed the megamorphism the IC exposed), and **P6a cached intrinsic prototypes** (richards `property_get` −59%, wall **−23.3%** — the first clear win of the whole arc, and it changed no IC hit behaviour at all). Tune12's array-receiver and function-constructor-shape work is the same story: the counters improved dramatically, release timing moved modestly (cube3d 77.9 → 72.7 s) or not at all.

This matters because the shape work and the prototype cache are **independently valuable** — they would still pay off with the ICs removed. So "the ICs are neutral" is not an argument that the arc failed; it is an argument that the IC layer specifically has not yet earned its keep.

**What has never been measured**, and must be before deciding: a clean A/B of `LAMBDA_JS_LOAD_IC=0` / `LAMBDA_JS_STORE_IC=0` **after** P4/P5/P6a/Tune12 landed. Every IC A/B number above predates the shape work being complete, so it measured an IC that was going megamorphic on almost every hot site. The current IC hits ~100% mono on those same sites. That single experiment decides between the three options:

| Option | When it is right | Cost |
| --- | --- | --- |
| **Retire** | If the post-shape-work A/B is still ≤ noise. The plain-map/`MAP_KIND_DESC` split, shape sharing, `name_id`, and the intrinsic-proto cache all stay — only the per-callsite cache and its helper call go. | Deletes a real correctness surface (install-time descriptor/accessor validation, detach rules, per-receiver-kind handling). |
| **Inline** (recommended if the A/B is positive) | Tune11 §P6 already specifies it: load `ic->state`, cached shape, and byte offset directly in MIR; check receiver map, `MAP_KIND_PLAIN`, data pointer, shape pointer inline; call the helper only on miss/poly/megamorphic. The helper call is currently the floor on any IC win. | A contained MIR-lowering change, but it is the first time IC state is read from generated code — needs its own correctness gate. |
| **Keep as-is** | Only if some workload class outside the benchmark set depends on it. | Carries the maintenance surface for no measured gain — this is exactly the state rule §1.5 says to revert. |

**Two cheap improvements that apply under either the keep or inline option**, both already scoped in Tune11 §P6: (1) **stop probing megamorphic sites** — once a site is marked megamorphic, call the semantic path directly instead of paying a helper call to be told to fall back; (2) **mark semantically uncacheable sites** — a site that repeatedly sees absent/inherited/descriptor/exotic receivers currently keeps probing forever with `miss_count` rising and zero installs. Both remove pure overhead and are independent of the inline decision.

**Do not** revisit widening the polymorphic limit; it was tried twice (Tune11, Tune12) and changed profile counters without changing release timing.

### F2 — Add a benchmark regression gate

Tune5 exists because a **350× regression on `sieve` and ~19× on `gcbench`/`deriv` sat undetected for roughly six weeks**, across two independent causes, both introduced by correctness fixes that passed test262 cleanly. test262 is a correctness gate and cannot catch this. A small periodic release-build benchmark run compared against a saved JSON baseline — the `run_benchmarks.py` machinery already exists and `benchmark_results_v3.json` is the proven comparison format — would have caught both within a day. This is the highest-value non-optimisation follow-up in the list.

### F3 — GC object tracing

After the Tune12 sweep-ownership fix, `cube3d`'s single collection is still visible, but the time has moved to `gc_trace_objects` (~592 ms of a ~619 ms collection). The sweep fix worked by moving classification to allocation time; the tracing cost needs its own root-cause pass, not a repeat of the same trick.

### F4 — Numeric/float boxing in hot loops

`mandelbrot`, `matmul`, `nbody`, `spectralnorm` still box floats (Tune5 §6b). Tune5's bounded ADD-inference recovery fixed the recursion cases (`fib`, `ack`) but the float-compute gap is untouched and needs a codegen pass, not a runtime fast path.

### F5 — Destination-passing MIR lowering

Tune6 §3.3 measured the generated MIR as **~88% MOV** — data movement, not calls — which is why helper extraction is the wrong lever for volume. Destination-passing lowering is estimated at ≈40% fewer instructions. It is a deep, high-risk codegen refactor and should be scoped as its own project rather than a tuning round.

### F6 — Compiled-artifact caching

Blocked on ~59 baked realm pointers in the lowering (Tune6 §3.4). The realm-safe slice (AST cache alone) measured only ~5–15% after the interpreter policy landed, so this is only worth reopening alongside a de-pointered/relocatable MIR lowering.

### F7 — MIR interpreter TCO gap

A real semantic divergence, not a performance item: the interpreter does not eliminate tail calls, so TCO-dependent deep recursion stack-overflows under interp and passes under JIT (Tune7 §3). Today the interpreter is only selected for large/cold and document JS, which limits exposure — but the policy has widened once already (render commands force it for all document JS), so this should be closed or explicitly documented as a policy constraint before it widens again.

### F8 — Re-baseline on one machine

Numbers across these twelve rounds come from different commits, different machines, and at least three runs later found to be load-contaminated. Before the next round claims a delta, capture one clean release baseline — test262 timing TSV plus the full benchmark sweep — on a quiet machine, and treat it as the single reference. Related housekeeping: the ~15% run-to-run noise floor should be stated in the harness output, not rediscovered each round.

### F9 — Low-priority carry-overs

- **`decodeURI`/`decodeURIComponent` `A2_5_T1`** — three rounds produced no movement; the residual is the builtin's per-call result allocation, which needs a C-side rewrite. Leave deferred unless it starts failing rather than merely being slow.
- **Tune8 §4 transpiler-side gate** — mechanical work to make `JS_TEST262_FAST_PATHS=0` actually link, worth −15 registry entries in production-only builds. Currently the macro is registry-side only, so turning it off fails at link.
- **Tune8 §3 DOM/web-API registry entries** (24 remaining) — parked on the "pure JS only" scope call; needs a clean Radiant baseline before any deletion.
- **The ID-classification trie** (Tune2 §2.4.E) — the generator and tables were correct and self-verified before revert. If a runtime consumer ever needs a fast spec-faithful classifier, reuse it there; do not retry the tree-sitter integration.
