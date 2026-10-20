# JS Runtime Tuning History

**Date**: 2026-10-20 (consolidated; first written 2026-08-07) · **Status**: retrospective record · **Scope**: every LambdaJS tuning round from 2026-05-26 to 2026-10-07, in three series

> **Source docs.** The 30 round docs (`Transpile_Js_Tune*.md`, `Transpile_JS_MIR_Tune.md`, `JS_Tune1…16*.md`) were folded into this file and deleted on 2026-10-20. Their full text (raw measurements, profiles, rejected designs, war stories) remains at commit `294779edbb`: `git show 294779edbb:"vibe/jube/<file>"`. Package and ledger IDs that other docs cite (E0–E8, X0–X7, JR3.3–JR3.9, N0–N8, C0–C8, T10-0…T16-6) are summarised below, so those citations resolve to this file.

This doc records *what was tried, what it bought, and why*, not how it was coded. The body keeps the tunings that were measured and kept. The appendices keep the failures: in this history, reverted work is nearly as instructive as the wins, because most of it failed for a reason that generalises.

| Round | Date | Theme | Headline outcome |
| --- | --- | --- | --- |
| **Series A** — `Transpile_Js_Tune*.md` | | | |
| A1 | 05-26 | Call/argument path | O(n²) call args → O(1); 44–480× on call-heavy loops |
| A2 | 05-27 | Parser / name-pool / eval | 0-for-3; every candidate reverted |
| A3 | 05-28 | RegExp property walk + constant folding | −39% property cluster, −86% shift cluster |
| A4 | 05-30 | TypedArray bulk + large-source interpreter | bulk ops up to 270×; identifier cluster −70% |
| A5 | 06-02 | Benchmark regression bisect | sieve 114×, deriv/gcbench ~19× recovered |
| A6 | 06-06 | Compile latency (AST / MIR / link) | link root-caused; interpreter policy 4–6× on vendor libs (retired 09-16) |
| A7 | 06-08 | Interpreter vs JIT verification | interpreter −17…−28% on short scripts; TCO gap found |
| A8 | 06-12 | Runtime ABI / registry surface | 547 → 452 entries, −7.2% aggregate |
| A9 | 06-16 | test262 slow-cluster sweep | Atomics 60×, identifier rows 3.4×, Script aliases −33% |
| A10 | 06-16 | Execution profiler + shape guards | profiler landed; structural slot guard 100% hit |
| A11 | 06-24 | Plain-map property ICs | megamorphism eliminated; intrinsic proto cache −23% |
| A12 | 06-25 | Array ICs, prototype shapes, GC | cube3d 73.0 s → 3.75 s |
| **MIR** — `Transpile_JS_MIR_Tune.md` | 07-20 | JS-specific MIR emission | MIR insns −29.1%; runtime flat |
| **Series B** — `JS_Tune*.md` | | | |
| B1 | 08-07 | Helper-cost profile; in-band error lane (E0–E8) | one error channel, poll sites → 0; no speed measured |
| B2 | 08-09 | Exception catalog conformance (X0–X7) | 37 + 85 catalog violations → 0; −1,180 LOC |
| B3 | 08-11 | NameId-first name identity (N0–N8) | one property identity; −2,067 LOC |
| B4 | 08-12 | Direct callable kernels (C0–C8) | intrinsic call −31.95%; −4,334 LOC |
| B5 | 08-11 | Eight property operations + elements kinds (JR6) | structural only; release A/B skipped |
| B6 | 08-13 | `JsClassMeta` + `JsPropertyOps` (JR4) | structural only; release A/B skipped |
| B7 | 08-13 | GC-owned Promise + growable async deque (JR7) | fixed caps and silent drops removed; partial |
| B8 | 08-13 | Unified module registry (JR9) | foundation slice only |
| B9 | 08-14 | Result28 → 29 regression recovery | sieve 2,533×; LJS/Node geomean 43.85× → 23.71× |
| B10 | 09-09 | Fast paths aligned with untyped Lambda (T10-0…5) | primes 27×, cd2 5.6×; test262 peak RSS 5,662 → 891 MB |
| B11 | 09-15 | Shared physical plans (T11-0…6) | spectralnorm 0.50×, matmul 0.55×; LJS/QJS 2.588× |
| B12 | 09-16 | Full-JS paths from the MVP designs | 63-row paired geomean 0.563× |
| B13 | 09-17 | Native-only MIR (D8.1.3v11) | MIR interpreter retired for JS; hyphen 130 → 90 ms |
| B14 | 09-21 | Native hot-loop regions, cheaper ordinary ops | C14 control 1.120× QuickJS; quicksort 0.18×, Navier 0.36× |
| B15 | 09-22 | Constructor-recipe regression diagnosis | R48 regression traced to B14 recipes; plan only |
| B16 | 09-22 | Helper / call-protocol overhead | one ASCII leaf kept (log_pipeline 0.929×) |

---

## 1. The measurement discipline (the most reusable output)

A1 landed a huge win from a hypothesis. A2 then landed nothing: three well-argued hypotheses (name-pool hashing, an ID-classification trie, an eval compile cache) all failed against the real binary. From A3 onwards the process itself became the deliverable. Series B kept tightening it after each self-inflicted regression.

1. **Release build only.** A1 opened by finding that its own top-30 slow list was a debug/O0 artifact: the "slowest" Unicode tests ran in 0.01–0.06 s in release. Debug profiles invent bottlenecks unevenly.
2. **Measure before designing.** A6 rev 3 killed its own AST-first premise: AST build is 0.4–2.3% of compile time, while MIR link is 50–82%. A10 replaced "tune by benchmark ratio" with "tune by profiled helper self-time".
3. **Land behind an env gate** (`LAMBDA_JS_TA_RAW_FAST`, `LAMBDA_JS_CONST_FOLD`, …), so the A/B compares one binary with itself, not two builds.
4. **Two gates, both required**: zero test262 pass/fail regressions, *and* a win beyond noise in the targeted cluster. Run-to-run noise on this harness is about 15%, and machine load has repeatedly faked both regressions and wins.
5. **Neutral means revert.** A change that is correct but performance-neutral is only a maintenance surface. This rule retired all of A2 and several later slices. (The MIR round is the deliberate exception: it was kept as structural Stack-API work.)
6. **Keep the honest post-mortem.** Every round records what was rejected, which is why later rounds skip paths that already failed.
7. **Exact-archive paired replay** (B11 onwards). Interleave the candidate with the *exact predecessor archive binary*: at least 11 pairs, 31 when noisy. Report the median candidate/control ratio and the one-sided 95% paired-bootstrap upper bound (UB); a gain needs UB < 1. The tool is `run_paired_benchmarks.py`. Sequential ResultN snapshots are never attribution evidence: R47's 21.3% raw "gain" was host noise, because Node improved by 19% on the same run.
8. **Every phase carries its own release gate, "structural" ones included.** B5 and B6 closed with the release A/B unchecked. Result29 then showed a 2.74× geomean regression (sieve 6,142×), and B9 had to recover it.
9. **Counts are not speed.** Compile-time admission counters (`mir_number_admitted`, `JS_OPT_MIR_LITERAL_FIELD_ADMITTED`) are not runtime hits, and fewer helper calls is not a speedup. In A10 P2, `property_set` calls fell nearly 10×, yet wall time rose. In B14, `ToNumeric` calls fell from 982k to 180k with neutral timing. B14 also widened constructor admission without a paired result, which caused R48's regression.
10. **Verify the control binary.** B12's "Phase 2" control was byte-identical to the pre-B12 control, so its Phase 2 ratios were really cumulative (B13 §2.2). B10's NULL-only retag regressed 9.7×, but a guard-on/guard-off A/B could not see it; only the archived binary could.
11. **Report both the geomean and the sum** over the 63-row matrix, where G = exp(mean log(LJS/QuickJS)). Halving text_search, at 32–48% of summed time, would move G by only about 1.1%. G moves with breadth: a 20% gain on 25 rows moves it about 8.5%.

---

## 2. Series A — `Transpile_Js_Tune1–12` (2026-05-26 → 06-26)

### 2.1 Call and argument path (A1)

**Defect.** Every JS call with at least one argument allocated its argument buffer through `js_alloc_env`, which called `heap_register_gc_root_range`. That registered a *permanent* GC root with a linear dedup scan, never popped and never freed. Call *k* scanned the *k−1* earlier ranges, and every GC cycle walked them all again. Call-heavy loops were therefore **O(n²) in time and unbounded in memory**. A 0-argument call stayed flat, which made the diagnostic "cliff" sit at exactly one argument.

**Fixes.**
- **Argument stack.** A transient bump-allocated stack, registered with the GC once as a single growable root range. `js_args_push` / `js_args_save` / `js_args_restore(mark)` bracket each call, and try/catch resets the mark on entry to catch or finally.
- **Static dispatch for `const` functions.** A function expression or arrow bound by `const` becomes a direct MIR call. A textual "call site after initializer end" check preserves the temporal dead zone (TDZ).
- **Interned ASCII strings.** The 128 single-byte ASCII strings are interned, so `s[i]`, `charAt` and `String.fromCharCode(n<128)` return table entries (`s[i]` 2M: −14%).

| Workload | Before | After |
| --- | ---: | ---: |
| Dynamic call loop, 160k iterations | 6,008 ms | 12.5 ms |
| Scaling 20k → 160k | O(n²) | linear |
| `assert.sameValue` ×65,536 | 4,154 ms | 83 ms |
| `character-class-escape-non-whitespace.js` | 9.6 s | 0.22 s |
| test262 per-test sum, `const` dispatch A/B alone | 431.0 s | 284.1 s (−34%) |
| test262 per-test sum, all A1 work | 529 s | 284 s (−46%) |

**Why it generalises.** One builder function (`jm_build_args_array`) feeds more than 30 call-lowering sites, so the fix sped up every call. This set the template for the whole history: find the *lifetime* or *algorithmic* defect behind a chokepoint, not the slow test.

### 2.2 Constant folding and dead-branch elimination (A3)

The transpiler had **no constant folding at all**. `jm_try_fold_const` folds literal unary, binary, bitwise, shift and comparison subtrees with exact runtime semantics:
- it applies ToInt32/ToUint32 and checks that values round-trip within ±2⁵³;
- it bails on non-finite values, `-0`, bigint, `**`, `&&`, `||` and `??`.

It is wired into `if` conditions, where a whitelist keeps Annex-B function hoisting intact, and into value sites. A differential test ran the same binary with the folder on and off over 3,780 constant conditions and more than 4,800 expressions, with bit-identical results. Cluster E (`S11.7.*_A4`) went from **4.55 s to 0.63 s (−86%)**.

### 2.3 Locality in Unicode property matching (A3)

`^\p{X}+$` restarted a binary search over the range table for every code point. Real text is locally clustered, so the walk now keeps a **resumable cursor**: it checks the previous range and its neighbours first and falls back to binary search on a miss. The generated-property cluster went from **61.84 s to 37.67 s (−39.1%)**.

### 2.4 Compile latency: run cold code in the MIR interpreter (A4 / A6 / A7)

> **Superseded 2026-09-16** by **D8.1.3v11** (B13, §4.6): LambdaJS MIR selection always produces native code, and MIR-interpreter thresholds were removed. The LambdaJS interpreter is now its AST backend. The diagnosis below still holds; the policy does not.

**Root cause (A6).** `link_us` was 50–82% of compile time. It was not symbol resolution: `MIR_set_gen_interface` ran `MIR_gen` **eagerly for every function**, including functions that were never called. For a large module, opt0 JIT took about as long as opt2 (3,691 vs 3,703 ms). That made the existing ">100k insns → opt=0" downgrade a no-op; the real lever was skipping code generation entirely.

**The policy, before it was retired.** Above 100k MIR instructions, or above 20k for document JS, the module linked with `MIR_set_interp_interface`, keeping the generator initialized. Pure-interp mode was different: it left the generator uninitialized and broke 49 UI tests.

| Fixture | Total before | Total after | Link before | Link after |
| --- | ---: | ---: | ---: | ---: |
| `lib_lodash.js` (680k insns) | 6,731 ms | 1,291 ms | 4,809 ms | 128 ms |
| `ramda_src_min.js` (304k) | 2,451 ms | 408 ms | 1,781 ms | 48 ms |
| `dom_jquery_lib.js` (253k) | 1,449 ms | 332 ms | 949 ms | 32 ms |

- **Radiant.** The `web-tmpl` suite ran 3.05× faster in wall time.
- **Short scripts (A7).** The interpreter was faster in aggregate: test262 −17.4% and `test_js_gtest` −28%.
- **Hot loops.** The JIT still won on a hot loop (1.65×).
- **Divergence.** A7 also found the one semantic difference: the MIR interpreter does not eliminate tail calls.
- **What B13 later measured.** Under this policy, hyphen's whole module ran interpreted even when the runner reported `tier=jit`. Its workload took 590.7 ms interpreted vs 372.7 ms native, while link took 46.7 vs 934.9 ms.

### 2.5 Benchmark regressions: bisect, then profile (A5)

LambdaJS had silently regressed 2–350× against a saved April run. Both root causes were fixed by choosing the *semantically correct primitive*.

- **Array index writes.** A correctness commit added an `OrdinarySet` inherited-accessor walk to *every* indexed write. The fix short-circuits writes to an existing own dense data element of a plain array, which `OrdinarySet` itself requires to be written without consulting the prototype. Results: sieve **93.1 ms → 0.82 ms (114×)**, puzzle 46×, queens 2.9×, towers 2.5×, permute 4.3×.
- **Object literals.** Each field was going through the full `Object.defineProperty` machinery. The fix performs `[[DefineOwnProperty]]` through the raw `map_put`, guarded to plain extensible objects. Results: deriv **3,630 → 194 ms**, gcbench **41,530 → 2,219 ms** (~19×).

**The recurring lesson.** An earlier attempt used `js_property_set` and regressed **2,704 test262 tests**, because `[[Set]]` honours inherited accessors and `CreateDataProperty` does not. A fast path must be built on a primitive with the *same* observable contract.

### 2.6 Bulk data paths (A4 / A9)

- **TypedArray raw views (A4).** Same-type raw copy, cross-type numeric conversion, and raw `with`/`reverse`/`indexOf`/`includes`. These were neutral on test262, but bulk workloads moved: 200k-element `Int32Array` ×80 went **0.80 s → 0.16 s**, and a 400k-element `Float64Array` search went **30.01 s → 0.11 s**.
- **RegExp / Unicode-property plumbing (A9).** `.test()` reads the compiled property kind directly. Script/Script_Extensions aliases share one canonical kind, so they share a result cache: 350 rows went **13.281 s → 8.873 s**.
- **Atomics virtual time (A9).** Short `Atomics.waitAsync` waits now use the js262 virtual clock: about 1,000 ms per test → 17–32 ms.

### 2.7 Attacking compile *volume*, not compile speed (A9)

For a large generated declaration file, the declaration body emitted 40k–66k MIR instructions, and execution then created thousands of global `var` properties one at a time. A9 made two fixes:
- It removed an O(n²) declaration-site scope lookup and collapsed the body, bringing MIR from **40–66k instructions to 35**.
- It added a bulk append path for globals that are verified absent (`js_define_global_var_properties_bulk_absent`), bringing execution from **~80 ms to ~5 ms**.

The largest identifier rows went from **106–108 ms to ~32 ms**.

### 2.8 Runtime ABI surface reduction (A8)

The JIT import table held **547 `js_*` functions**. A8 deleted entries that were both unused in telemetry and never quoted in lowering, and folded inverse pairs into one helper plus an inline XOR (`js_ne_raw` → `js_eq_raw ^ 1`). It kept hot heads direct. Result: **452 entries**, aggregate per-test wall time **484.9 s → 449.7 s (−7.24%)**.

Later updates:
- **2026-08-16.** The `JS_TEST262_FAST_PATHS=0` transpiler-side gate landed; it is worth −14 entries in that build.
- **2026-09-10.** The dead `js_math_round/trunc/sign/floor/ceil*` bodies were retired once `js_math_intrinsic` became the live dispatcher.

### 2.9 Property access: shapes and inline caches (A10 / A11 / A12)

> **Outcome.** The inline caches did not survive. `JsLoadIC`/`JsStoreIC` were deleted on 2026-08-15 (`../Lambda_Design_JS_IC_Retire.md`), and per-site caches are now banned in both lanes (**D8.4.1v2**, LC1v2). On the same day, named and indexed access moved onto the shared runtime property kernels, and the compiler-side array/typed-array specializations were removed. Separately, B3 N8 (2026-08-11) deleted the compile-time constructor shape inference/composition phases. B10 and B11 later rebuilt predicted shapes as immutable recipes, guarded per access. **The intrinsic-prototype cache (P6a) is still runtime behaviour.**

The honest summary of this arc: **the ICs themselves were neutral; the shape work and the prototype cache were the wins.**

- **Measured the IC on its own:** js262 suite **+1.0%** and benchmark geomean **−0.12%**.
- **Structural slot guard (A10).** `js_shape_slot_guard` proves the same property name at the same byte offset. The hot `richards` site went from 2,322 hits / 113,778 misses to **116,100 / 0**.
- **Shape sharing (A11 P4/P5, A12 P2).** Canonical constructor shapes and combined derived pre-shaping drove load/store megamorphic counts to **0** on richards and deltablue.
- **Cached intrinsic prototypes (A11 P6a).** `js_get_intrinsic_prototype_for_class` stopped re-deriving built-in prototypes on every chain walk. richards `property_get` fell **22.86M → 9.34M (−59%)**, and wall time **6,874.9 → 5,276.3 ms (−23.3%)**.
- **Sparse → dense array promotion (A12).** jetstream/hashmap **−11.5%**.
- **Dense own-read fast path (A12).** navier_stokes **222 → 149 ms**.

### 2.10 GC sweep ownership classification (A12, the largest single win)

A profile blamed `array_push_direct_expand` for about 67.7 s on `cube3d`. The real cost was one collection spending **67.1 s in `gc_sweep()`**. For each of 4.45M dead headers, the sweep *searched* thousands of slabs to find which allocator owned the object.

The fix classifies ownership **at allocation time**: bump allocation sets `GC_FLAG_BUMP`, and the sweep branches on header flags instead of searching. **cube3d went from 73.0 s to 3.75 s**, and `gc_sweep` from 67,084 ms to 9.8 ms.

### 2.11 Series A headline results

| Change | Round | Measured effect |
| --- | --- | --- |
| Transient call-argument stack | A1 | 160k-call loop 6,008 → 12.5 ms; O(n²) → linear |
| `const`-bound static dispatch | A1 | test262 per-test sum −34% |
| Resumable property-range cursor | A3 | generated-property cluster −39.1% |
| Constant folding + dead branch | A3 | shift-operator cluster −86.2% |
| TypedArray raw bulk paths | A4 | 200k `Int32Array` bulk 5×; 400k numeric search 273× |
| Large/cold module → MIR interpreter *(retired 09-16)* | A4, A6 | lodash 6.73 → 1.29 s; Radiant web-tmpl 3.05× |
| Dense own-write fast path | A5 | sieve 114×, puzzle 46× |
| Object-literal `map_put` fast path | A5 | deriv ~19×, gcbench ~19× |
| Registry/ABI slimming | A8 | 547 → 452 entries, aggregate −7.24% |
| Atomics virtual time | A9 | waitAsync tests ~60× |
| Generated-declaration bulk init | A9 | identifier rows 106–108 → ~32 ms |
| Cached intrinsic prototypes | A11 | richards −23.3% wall |
| Sparse → dense array promotion | A12 | jetstream/hashmap −11.5% |
| Dense own-read fast path | A12 | navier_stokes −33% |
| GC sweep ownership flags | A12 | **cube3d 73.0 → 3.75 s** |

---

## 3. MIR emission tuning — `Transpile_JS_MIR_Tune` (2026-07-19 → 07-20)

This round worked on top of the common Stack API (`../Lambda_Design_Stack_API.md`). It removed redundant protocol work from JS-specific lowering. It was measured on a fixed corpus of 88 functions (`test/js/mir_tune_*.js`, `utils/analyze_js_mir.py`).

| Phase | Mechanism | Effect |
| --- | --- | --- |
| P2 | Full-CFG `CLEAN`/`UNKNOWN` fixed point over normalized exception effects removes proven-redundant polls | polls 236 → 190 (−19.5%) |
| P3 | Eval frame emitted only when the function contains a direct eval | eval ops 85 → 0 |
| P4 | Arg-stack marks only on paths that push; `this`/`new.target`/`with` save skipped when unobservable | arg ops 97 → 42 (−56.7%) |
| P5 | Eligibility checked before native boxing; canonical constant encodings; return facts feed `FnVariantAnalysis` | conversions 363 → 186 (−48.8%) |
| P6 | TDZ lattice with structured dominance | TDZ checks 17 → 14 |
| P7/P8 | Exact-capacity literals, prebuilt literal shapes, one-step `js_finalize_function` | (no separate number) |

- **Aggregate:** MIR instructions **7,646 → 5,419 (−29.1%)**, locals −23.9%, safepoints 1,164 → 647, root stores 842 → 487.
- **Release runtime:** flat (±1.4% on sieve, deltablue, richards, nqueens).
- **Compile time:** worse, jQuery +20.1% and lodash +4.8%. This is the accepted cost of the exception dataflow.

This was a code-volume round, not a speed round.

---

## 4. Series B — `JS_Tune1–16` (2026-08-07 → 10-07)

Series B began as the `JS_Runtime_Redesign.md` (JR-numbered) implementation line, B1–B8. The rounds cut representation and protocol debt, each with its own census tool and LOC ratchet. Speed was mostly not measured. The second half, B9–B16, is performance recovery against QuickJS on the 63-row matrix. It was driven first by the regression the redesign caused, then by B14's own constructor regression.

### 4.1 The redesign line (B1–B8): structure first, speed unmeasured

| Round | IDs | What landed | Measured |
| --- | --- | --- | --- |
| B1 Helpers | P1a | Profile of the 261-script batch: runtime helpers were **43.5%** of working CPU, GC root registration 7.1% (all `js_alloc_promise`), ABI trio 0.22%. Only P1a landed: promise slots register their roots lazily, at first allocation in the epoch. P2–P4 were superseded by the redesign. | Projection only |
| B1 Runtime | E0–E8 | In-band merged-lane error ABI (**D8.4.3**): `js_throw_value` is the single carrier, the pending flag and exception slots are gone, the lowering tests the result tag, and `LambdaError` gets a Map-compatible prologue (R2b). | error-channel census 1; poll sites 0 |
| B2 Exception | X0–X7; JR3.3–JR3.9 | Catalog conformance and lane-check elision. `jm_publish_call_result` admits a result to the error lane only when the callee's catalog `ret_class` is `JIT_VALUE_BOXED_ITEM`. Unknown calls keep the I64 fallback (fail-closed). Raw-scalar imports are declared `PRESERVES`, and void imports may not claim `MAY_SET`. The 372 remaining `MAY_SET` rows stay until each body is proven infallible. There is no generic "transparent" effect class. `make test-js-exception-catalog` gates test262. Consolidated helpers (`JS_ASSIGN_OR_RETURN_INTO`). | raw-scalar violations 37 → 0, void `MAY_SET` 85 → 0; **−1,180 net LOC** |
| B3 Name | N0–N8 | `NameId` (from `NamePool`) is the only property identity (**D3.4.4v2, D4.6.1v2–D4.6.2v2**). `ShapeEntry` keeps `name_id` only, and modules carry dense `NameId[]` tables. Pointer-key code and the old constructor shape optimizer were deleted. | **−2,067 LOC** |
| B4 Callable | C0–C8 | One `js_call_value` kernel and one `js_construct_value` kernel, with per-callee entries chosen at creation (**D6.2.2v2**). `js_dispatch_builtin` (337 cases) was deleted. Explicit `new.target`. Method calls lower to an observable `Get → Call`. | intrinsic call **−31.95%**, bound call −11.95%, GC slot 400 → 272 B; **−4,334 LOC**. Static method **+220%** and Date construction +31% are the cost of the restored semantics |
| B5 Property | JR6 | Exactly eight receiver-aware operations (`js_get`, `js_set`, `js_define_own`, …) on a `JsPropertyLane`. `JsElementsKind` lives in `array_flags` with monotone promotion. A realm prototype clean-guard replaced repeated numeric scans. | none (release A/B not run) |
| B6 Object | JOP1–18 | Immutable `TypeMap::js_meta` and one `JsPropertyOps` table (**D3.4.7**). Native payloads moved off `Map.data`. One prototype resolver/mutator pair. The exotic adapter and the sentinels were deleted. | none (release A/B not run) |
| B7 Promise | JR7 | A Promise is one GC-owned VMap (**D7.4.1v2**) with growable reactions. One `RuntimeAsyncDeque` serves nextTick, microtasks and unhandled rejections. This removed the caps: 8 reactions, 8,192 promises, 1,024 queue entries, and the silent drops past them. | none; R6b scheduler handoff open |
| B8 Module | JR9 | Runtime-owned `ModuleRegistry`, with paths canonicalised before lookup. Only this foundation slice landed. | none; loader migration open |

**What this line taught.**
- The LOC and census ratchets worked: every planned deletion happened.
- But "structural" rounds that close without a release A/B can hide large regressions. B4's prototype re-publication silently disabled B5's dense array paths, and nobody measured until B9.
- **B5 also added a process rule:** capture a clean P0 snapshot before applying a numeric LOC ratchet.

### 4.2 B9 — Result28 → Result29 regression recovery (2026-08-14)

- **The regression.** Between Result28 (after B1–B2) and Result29 (after B3–B8), the LJS/Node geomean went from **16.7× to 45.8×**, and test262 wall time rose 40%. `awfy/sieve` was **6,142×** slower and `larceny/puzzle` 252× slower.
- **Five root causes:**
  - B4 re-published the canonical `Array.prototype`, creating companion metadata that turned off B5's dense paths.
  - Integral numbers carried as native doubles missed the index lane.
  - Computed access entered the semantic kernel before any guarded path.
  - B5 and B6 skipped their release gates.
  - Named-IC emission was gated on the profile build.
- **The fixes:** an identical-prototype skip, `js_number_key_to_index_fast`, a specialised index-assignment kernel, and a guarded existing-dense-element store.

| Benchmark | Result29 | Tuned | Speed-up |
| --- | ---: | ---: | ---: |
| `awfy/sieve` | 2,930.852 ms | 1.157 ms | **2,533×** |
| `larceny/puzzle` | 6,457.469 ms | 69.052 ms | 93.5× |
| `awfy/queens` | 242.043 ms | 6.865 ms | 35.3× |
| `kostya/primes` | 4,607.991 ms | 140.323 ms | 32.8× |
| `awfy/permute` | 625.496 ms | 27.237 ms | 23.0× |

- **Aggregate.** The LJS/Node geomean went **43.85× → 23.71×**, still 1.51× worse than Result28.
- **Release named ICs.** Making them non-profile-only sped nbody up about 2.3×; they were deleted the next day with the IC retirement.
- **test262 batch isolation.** P2.2 snapshots and restores intrinsic function maps between tests. P2.3 added a 3 s preflight (`make test-js262-prelim`).

**The ruling B9 established:** *one semantic authority does not require one physical execution path.* A guard proves the case, a direct operation handles it, and a miss falls back to the single kernel. Every later round follows that shape.

### 4.3 B10 — fast paths aligned with untyped Lambda (2026-09-09 → 09-10)

The baseline was Result38: LJS/QJS **4.35×**, against 29.2× for MIR-untyped. Following **D8.4.1v2**, there are no inline caches. B10 uses a compile-time prediction, an inline guard, and the shared kernel on a miss.

| Package | Mechanism | Effect |
| --- | --- | --- |
| T10-0 | `arguments` detection ignores key positions (`node.arguments`, `{arguments: v}`, `obj.eval`). Shortest-double printing takes an integer path below 2⁵³. | `o.arguments` read ×500k: 38,101 → **65 ms** |
| T10-1 | Numeric index lane, admitted by physical carrier (F64, or I64 holding an int) | `a[i]` ×4M 2,733 → 450 ms; **retired 09-10**: computed members box once into `js_get_reference`/`js_set` |
| T10-2 | Item 3: one byte check (`MAP_KIND_DESC`) replaces hashing the 24-byte dataset marker. Items 1–2: object literals get a predicted shape named by a module key-table range, read through the `js_shaped_slot_get` guard, with each slot retagged NULL→T exactly once. | literal + read ×500k: 541 → **68 ms (7.96×)**; `p.x + p.y` 2.18× |
| T10-2a | Shared extensibility-marker table; `js_set_prototype_fresh` skips the cycle walk on new objects | gcbench **2.19×**, deriv **2.42×** |
| T10-3 | A new own data property is decided from shape storage, then written with one slot write (no descriptor object) | `new P(i,1)` ×500k: 7,231 → **820 ms** |
| T10-4 | `arguments` `length`/`callee`/iterator are storage defines; the strict marker moves to `Container::reserved_state` | `arguments.length` call: 76.5 → **2.42 µs (31.6×)** |
| T10-5 | Residual: `js_object_meta` answers Maps first, and the precomputed `__internal_proto__` hash removes a cost that was 6.4% of cd2 | cd2 5.93× → 6.57×, richards2 1.25× → 1.37× (vs v38) |

- **Shared shape-transition root (§9.3–§9.9; distilled in `../Lambda_Design_Shape_Transitions.md`).**
  - **How it works:** an add-transition tree with a 256-edge root, 16 edges per interior shape, and `MAX_SHAPE_GRAPH = 1024` shapes per `Input`.
  - **Memory is the reason to keep it:** 20,000 same-shape objects went from **932 MB to 62 MB**, and test262 peak RSS from **5,662 MB to 891 MB**.
  - **Speed:** gcbench 1.72×; otherwise neutral.
- **Round-1 rows vs v38.** These were measured while T10-1 was still in place:
  - primes **27×**, sieve2 7.1×
  - cd2 **5.6×**, prettier_ast 4.3×, havlak2 4.0×
  - levenshtein 3.8×, deltablue2 2.7×, quicksort 2.7×

### 4.4 B11 — shared physical plans (2026-09-15)

- **Trigger.** Commit `85dcc897e` ("JS code reduction") had deleted 1,404 lines of native numeric, index and predicted-shape lowering. Result44 then stood at LJS/QJS **2.91×**: primes 12.96× and spectralnorm 6.33× slower than Result41.
- **Approach.** Rebuild these paths as **immutable MIR plans shared with Lambda**: language admission → plan → shared emitter → direct hit → one JS kernel on a miss. Restoring the old JS-only dispatcher was rejected: an A/B of it lost quicksort (0.947×).

| Package | Mechanism | 11-pair result |
| --- | --- | --- |
| T11-1 | `MirNumericOpPlan` shared by Lambda F64 ops and JS Number-proven arithmetic and comparison (`+` never admits a string) | spectralnorm **0.5048×** |
| T11-2 | Native key in `JsMirReference` plus a direct packed-array read (receiver, bounds, address, one tagged load) | matmul **0.5546×**, array1 **0.5785×** |
| T11-3 / T11-4 | Predicted exact-field plan from immutable TypeMap recipes; ASCII string leaves plus a 1,024-entry substring cache | log_pipeline 0.9865× |
| T11-5 | One shared post-evaluation get/put selector | ~130 safepoints/root stores removed |

- **Result45:** LJS/QJS **2.588×**. All three aggregate targets were missed (≤2.0× QuickJS, ≤10× MIR-untyped, ≤160 s total).
- **Rule kept:** read proofs and write policy are separate products. A store guard must never poison a read loop.

### 4.5 B12 — full-JS paths from the MVP designs (2026-09-16)

B12 made common operations do the small physical work that made the MVP runtime fast, while keeping full JS semantics and Lambda runtime types. The MVP backend was removed from the tree on 2026-10-07.

| Package | Mechanism | Result |
| --- | --- | --- |
| T12-1 | Map/Set hash entries point at stable `JsCollectionOrderNode`s with tombstones (removes a second linear search) | **0.3333×**; growing a map from 1k to 8k entries had cost 12 → 687 ms |
| T12-2/3 | Numeric function admission: binding-identity dataflow, a bounded fixed point for recursion, a `_n` native variant beside the boxed body, no TDZ checks or root homes in the native body | fib **0.1262×**, sum **0.0249×** |
| T12-8 | Bulk RegExp match/replace through existing entries | regexredux **0.3018×** |
| P2-2 | O(1) relevance classifier for intrinsic mutation; the class-wide scan had been ~17% of a Havlak sample | havlak **0.7933×** |
| P2-4 | Guarded typed load → compute → store (Uint8/Float64) with parameter receiver facts | FFT **0.2337×**, primes **0.0565×** |

- **Aggregate, Phases 1+2 combined (63 rows × 11 pairs, all output-equal):** geomean **0.562698×**; total medians 211.9 s → 140.0 s (0.660772×); text_search **0.5118×**.
- **Not attributable per phase.** The Phase 2 control was the pre-B12 binary (see §1.10).
- **Milestone missed.** The 0.80× QuickJS milestone was not met; the snapshot stood at 2.936× QuickJS.

### 4.6 B13 — native-only MIR (2026-09-17 → 09-21)

**D8.1.3v11:** MIR selection always produces native code. There is no size threshold, no interpreter fallback, and `--mir-interp` is rejected for JS. The large-module interpreter of §2.4 had been routing hyphen's whole module (289,375 MIR instructions) to the interpreter even under `tier=jit`.

B13 then attacked hyphen's allocation, name and property residual. These are unpaired three-sample medians from different binaries, so they are not additive:

| Change | Result |
| --- | --- |
| Lazy Function `name`/`length` metadata (41,043 pending functions) | 130.201 → 105.083 ms (**−19.3%**) |
| T13-8: intrusive buffer list replaces a reverse-ownership HashMap for external dense buffers | **−7.6%** |
| `name_classify_ordinary` single byte walk; `name_pool_create_strview` catalog lookup once | −2.3%, −1.4% |
| **Overall** | **130.201 → 90.268 ms** (QuickJS 26.0 ms) |

The implementation record is `../impl/JS_Tune13_Impl.md`.

### 4.7 B14 — native hot-loop regions (2026-09-19 → 09-21; edited to 10-07)

- **Unit of optimization.** A *complete native region*, not a faster helper.
- **The C14 control.** A frozen 63-workload manifest plus a Navier frame-15 oracle; the oracle exposed a real MIR write-reference bug, fixed as T14-1. The frozen C14 release had **LJS/QuickJS G = 1.120086**, down from R46's 1.464372.
- **Measurement.** Results below are 31-pair, exact-predecessor runs unless noted.

| Package | Mechanism | Result |
| --- | --- | --- |
| T14-3 | Int32Array lowering plus parameter-kind evidence across direct calls makes quicksort's `partition` native | quicksort **0.183468×** |
| T14-3 | Reuse the live typed-view witness on write; reuse the ArrayNum read pointer within one buffer generation | quicksort 0.902×, then 0.831× (17.9 ms vs QuickJS 19.4) |
| T14-3 | Typed-view arithmetic for both operand orders | FFT **0.846079×** |
| T14-3 | Dense reads from arrays that also carry named properties (`js_array_get_existing_own_dense_with_props_or_missing`) | Navier **0.357866×** |
| T14-2 | Native F64 `++`/`--` on direct locals (`dadd`/`dsub`) | diviter **0.744×** (3 pairs; 0.049× cumulative with aliases) |
| T14-2 | Number-only bitwise pair head `js_numeric_bitwise_number_pair` | crypto_sha1 **0.823464×** |
| T14-5 | Recursive literal-return recipes (named probes 270,351 → 5) | binarytrees **0.548164×** |
| T14-5 | Three property-head trims: lazy Function metadata only for Functions, Window hooks only for `globalThis`, special-name preamble skipped for non-global Maps | richards 0.967×, 0.968×, 0.974× |
| T14-7 | `js_finalize_function` reuses the code record's `source_text` | havlak **0.960570×** |

- **Gains preserved.** The B14 gains survived into R48: diviter 12,689 → 614 ms, quicksort 133 → 18 ms, Navier 565 → 184 ms, levenshtein 218 → 82 ms.
- **Reuse rules (Tune14 §3.3–§3.4).**
  - A shared primitive needs two live clients, and the old copies retire in the same change.
  - No parallel fact owners, mutable caches or feedback vectors (**D8.4.1v2**).
  - Do not resurrect the withdrawn P7 U-A structural driver.
- **The miss: constructor and alias recipes.** These T14-5 extensions shipped with semantic/GC tests only: `LMD_TYPE_NULL` placeholder slots for dynamic constructor fields, plain-function constructor prefixes, and `Ctor.prototype.method` aliases. No paired result was taken (§4.8).

### 4.8 B15 / B16 — regression diagnosis and protocol overhead (2026-09-22)

**B15 (plan only).**
- **The regression.** R48's G rose to **1.200379**, and its summed time was **+38.7%** over C14. CD, Havlak, HashMap and DeltaBlue grew from 19.4 s to 71.0 s.
- **Replay and ablation.**
  - An exact R47→R48 replay gave CD **4.735059×** and HashMap **2.217908×** slower.
  - A same-binary ablation disabled constructor-shape admission with a source no-op. The ablated runs were CD **0.20×**, Havlak 0.27×, HashMap 0.42×, DeltaBlue 0.43× of the original; 44/44 small-row pairs also favoured disabling.
- **Mechanism.**
  1. The reserved `LMD_TYPE_NULL` slot makes `js_ordinary_add_own_data_property` treat a raw shape hit as an existing property.
  2. The first store therefore falls to `js_reflect_define_property`.
  3. That store then transitions the shape, so the predicted-field guard misses from then on.
- **Fix plan.** T15-1: teach the ordinary-add proof to tell reserved storage from a live property.
- **What happened next is unrecorded.** No doc records the fix landing. Result52 (G 1.113, hashmap 4.87× QuickJS vs 10.98× in R48) suggests a later repair. B16 also cites a B15 build that moved `.length` into MIR (`lambda-tune15-mir-array-length-f4c4288f`), which no doc describes.

**B16** targeted the cost paid *after* a fast path qualifies.
- **Kept:** one leaf. T16-5 calls `js_try_ascii_string_builtin_no_gc` after the visible Get, only for the catalog `indexOf`/`charCodeAt` on ASCII primitives. log_pipeline **0.9291×** (UB 0.9484, 27/31).
- **Rejected:**
  - T16-1, a Number hit/miss split in MIR: mixed results.
  - T16-2, property-preamble leaves: below threshold.
  - T16-4, a receiver operand ABI: Splay +4.29%.
- **The ruling it left:** a fast branch *inside* a `MAY_GC` helper does not remove the caller's safepoint/root/scalar-home protocol, and relabelling the helper's effects would be unsound. Separate the proved non-allocating operation from the semantic fallback *at the MIR call boundary*.

### 4.9 Series B headline results

| Change | Round | Measured effect |
| --- | --- | --- |
| Direct intrinsic call targets (dispatcher deleted) | B4 | intrinsic call −31.95%; −4,334 LOC |
| Index/prototype recovery group | B9 | sieve 2,533×, puzzle 93.5×; LJS/Node 43.85× → 23.71× |
| `arguments` detection fix | B10 | `o.arguments` read 38,101 → 65 ms |
| Descriptor-free property add | B10 | `new P` ×500k 7,231 → 820 ms |
| `arguments` object storage defines | B10 | 31.6× per call |
| Predicted literal shapes | B10 | literal + read 7.96× |
| Shared shape-transition root | B10 | test262 peak RSS 5,662 → 891 MB |
| Shared numeric plan | B11 | spectralnorm 0.505× |
| Native-key packed array read | B11 | matmul 0.555×, array1 0.579× |
| Map/Set order nodes | B12 | 0.333× |
| Numeric function variants | B12 | sum 0.025×, fib 0.126× |
| Typed load–compute–store | B12 | FFT 0.234×, primes 0.057× |
| Whole matrix, Phases 1+2 | B12 | 63-row geomean 0.563× |
| Lazy Function metadata | B13 | hyphen −19.3% |
| Int32Array native partition | B14 | quicksort 0.183× |
| Companion dense reads | B14 | Navier 0.358× |
| Recursive literal-return recipes | B14 | binarytrees 0.548× |
| Number-only bitwise head | B14 | crypto_sha1 0.823× |
| ASCII string capability leaf | B16 | log_pipeline 0.929× |

**LambdaJS / QuickJS geomean over time** (snapshots from different sessions, so this shows the trend, not attribution):

| Point | G |
| --- | ---: |
| Result38 | 4.35× |
| Result44 | 2.91× |
| Result45 | 2.588× |
| B12 final snapshot | 2.936× |
| R46 | 1.464× |
| C14 | **1.120×** |
| R48 | 1.200× |
| Result52 | 1.113× |

The B12 → R46 drop spans the B13 period (see `../impl/JS_Tune13_Impl.md`). The goal, G < 1.00 in two sessions, has not been reached.

---

## Appendix A — Failed, reverted and rejected (Series A)

Grouped by *why*, because the failure modes repeat.

**A.1 The hypothesis was wrong about the real binary.**
- **Name-pool length bypass (A2):** +18% slower. SipHash already handles long keys, and the bypass re-copied bytes on every reference.
- **Length-prefixed hash (A2):** a no-op.
- **Skipping the dual strict/sloppy compile (A2):** false premise; each test compiles exactly once.
- **`eval()` compile cache (A2):** both target shapes route around the cached path.
- **Field-use inference from field names (A10):** `property_get` *increased*, 14.17M → 16.76M.
- **Generated property-name cache and descriptor/`ownKeys` caching (A9):** flat, and the caching added shape-mutation work.

**A.2 The primitive had the wrong observable contract.**
- **`js_property_set` for literal fields (A5):** 2,704 regressions, because `[[Set]]` consults the prototype chain.
- **Process-global intrinsic-prototype cache (A5):** 169 regressions, because test262 runs multiple realms in one process. It was later redone realm-aware as A11 P6a.
- **Pure-interp mode (A6):** 49 UI regressions.

**A.3 The fast path cost more than it saved.**
- **String-concat fusion (A1):** never fired on the hot path.
- **Widening `jm_should_inline` (A1):** SIGSEGV.
- **Per-function lazy MIR generation (A6):** link time collapsed, but execution was 1.2–44× slower, because optimizer state accumulates across interleaved generations.
- **Broad dynamic own-slot store guard (A10):** calls fell 10× while wall time rose.
- **Class-propagation branch (A10):** js262 regressed.
- **Runtime-helper array append (A12):** flat, because the real cost was the GC sweep.
- **Retiring three hot string helpers (A8):** 2 timeouts, so they were restored.

**A.4 Right direction, wrong knob.**
- **ID_Start/ID_Continue trie in the external scanner (A2):** reverted. The trie itself is correct and reusable in a runtime classifier.
- **Adaptive interpreter policy v2 (A4):** whole suite +1.8%.
- **Widening `JS_LOAD_IC_POLY_MAX` (A11, A12):** changed profile counters but not release timing.
- **Helper-call load IC alone (A11):** +1.0%.
- **URI codec fast paths (A1, A4, A9):** no movement on `decodeURI*/A2_5_T1` in three rounds. The gate was later retired when the fast path merged into `lib/url.c` (2026-08-16).

## Appendix B — Failed, reverted and rejected (MIR round and Series B)

| Attempt | Round | Outcome |
| --- | --- | --- |
| Value/status fast-return ABI | MIR | Unnecessary once the CFG fixed point removed redundant polls |
| Generic "transparent" exception effect (X2) | B2 | Measured null. It preserves nothing useful or risks detaching a live carrier |
| Invalidating the remembered error carrier at every label | B2 | 715 test262 regressions; narrowed to async resume joins |
| First shared transition root | B10 | Dead on arrival (gate false for `EmptyMap`), an uninitialized field (segfault, ~150 GB allocation), and 4.4 GB test RSS. Fixed and re-enabled later |
| 16-edge root bound | B10 | 2–8% scalar tax (mbrot2 0.92×); replaced by the 256-edge / 1,024-shape bounds |
| NULL-only slot retag | B10 | 9.7× regression, invisible to the guard-on/off A/B and caught only against the archived binary |
| O(1) shared-parent child check | B10 | Neutral → reverted (rule 1.5) |
| Restoring the pre-`85dcc897e` dispatcher verbatim | B11 | quicksort 0.947× (4/11 wins) and more code; replaced by shared plans |
| P2-6 Havlak fields/calls | B12 | Refused: mutable fields and dynamic methods would need a mutable inline cache (banned) |
| Seven B14 micro-leaves: root-frame trim, `js_increment` head, copy-store leaf, typed-view compare, discarded-identifier leaf, raw-`Array*` companion read, public `++/--` head | B14 | Each within noise or worse (UB ≥ 1) → removed |
| Broader FFT F64 classification | B14 | FFT screen positive, but the MIR ratchet rose 3,237 → 3,255 → removed |
| Constructor/alias recipe extensions shipped without paired evidence | B14 | Became the R48 regression (§4.8) |
| T16-1 Number hit/miss MIR split; T16-2 property-preamble leaves | B16 | Mixed results, below threshold; rejected |
| T16-4 strict-body receiver operand ABI | B16 | Splay +4.29%, Log Pipeline +1.32% → reverted |
| Cached `slice` extension of the ASCII leaf | B16 | Log Pipeline +0.96% → removed |

---

## 5. Open items (as of 2026-10-20)

**Resolved since the first edition of this doc:**
- **The IC keep-retire-inline decision:** retired 2026-08-15, **D8.4.1v2**.
- **The MIR-interpreter TCO gap:** moot, because the interpreter was retired for JS (**D8.1.3v11**).
- **The A8 transpiler-side fast-path gate:** landed.
- **The URI gate:** retired.
- **Float boxing in hot loops:** largely addressed by B11 T11-1, B12 T12-2 and B14 T14-2.
- **Re-baselining:** replaced by exact-archive paired replay.

**Still open:**

1. **QuickJS milestone.** G < 1.00 in two independent sessions; the latest snapshot is Result52 at 1.113×. Each B15 counterfactual repair is worth only about 5–9% of G, so breadth work is required as well (§1.11).
2. **Confirm the constructor-recipe repair (T15-1).** Record which change recovered CD/Havlak/HashMap between R48 and Result52, with a paired result.
3. **Search Number/array paths** (T14-3 / T15-3): `naiveSearch`, `kmpSearch`, `prefixTable` and `boyerMooreSearch` in text_search, which is 32–48% of summed time. B16 still counted 687M Number-arithmetic and 882M comparison hits there.
4. **Unfinished redesign phases:**
   - B7 R6b: native completion subscriptions and the Lambda scheduler handoff.
   - B8 JR9: transaction API, JS/CJS cache migration, dynamic-import/TLA growth, and deletion of `JsModule[]`, the CJS arrays and the fixed caps.
5. **Green aggregate baseline.** The last recorded `make test-lambda-baseline` was 5,730/5,744 at the end of B14 and not clean at B16. test262 stays 40,261/40,261.
6. **Memory.** Unexplained RSS on hyphen (1.45 GiB), prettier_ast (1.36 GiB) and microdiff (151 MiB). GC object tracing (`gc_trace_objects`, about 592 of 619 ms in cube3d's collection) still needs its own root-cause pass.
7. **Code volume.** Destination-passing MIR lowering (generated MIR is about 88% MOV; estimated ≈40% fewer instructions) is a project of its own. Compiled-artifact reuse is blocked on de-pointered lowering (JM10, **D8.5.2–D8.5.3**).
8. **Benchmark regression gate.** A5 (a 350× sieve regression, undetected for about six weeks) and B9 (6,142×) both found regressions that test262 could not see. `run_paired_benchmarks.py` exists, but no periodic release-benchmark gate does.
9. **Carry-overs.** `decodeURI*/A2_5_T1` still needs a C-side rewrite of the per-call allocation. 24 DOM/web-API registry rows are parked pending a clean Radiant baseline.
