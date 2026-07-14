# Lambda Code Deduplication — Implementation Plan

**Date:** 2026-07-14  
**Live scan commit:** `339a2840e`  
**Scope:** maintained C/C++ under `lambda/`; scan hygiene plus Phases 1–4 below  
**Companion docs:** `vibe/Lambda_Design_Code_Dedup.md`, `vibe/Lambda_Code_Clean_Up.md`, `vibe/Lambda_Impl_Unified_AST (done).md`, `vibe/Lambda_Jube_DOM3.md`

This plan turns the corrected Lizard scan into a sequence of narrow, verifiable cleanups. It does not treat every reported window as a defect: generated/vendor code, declarative tables, independent struct layouts, and grammar/state-machine shapes must be separated from executable duplication before the result becomes a ratchet.

The plan deliberately stops at Phase 4. Splitting the large JS source files merely to reduce file size is out of scope; structural extraction should happen only when a shared catalog, emitter, or semantic helper gives the moved code a clear owner.

---

## Implementation progress

| Phase | Step | Status |
|---|---|---|
| **0** | Force the scan to C/C++ (`-l cpp`) | ✅ implemented and verified; 0 non-C/C++ files in the report |
| 0 | Exclude all vendored tree-sitter trees | ⬜ pending |
| 0 | Review declarative/independent false positives | ⬜ pending |
| 0 | Cluster overlapping windows and establish the first-party ratchet | ⬜ pending |
| **1** | Adopt `MirEmitter` in Python, Ruby, and Bash transpilers | ⬜ pending |
| 1 | Extract the common typed-index load emitter | ⬜ pending |
| 1 | Remove trivial Radiant DOM array-constructor copies | ⬜ pending |
| **2** | Establish one JS builtin catalog | ⬜ pending |
| 2 | Drive registration/name lookup and MIR classification from the catalog | ⬜ pending |
| 2 | Reduce runtime dispatch to explicit descriptor groups plus custom handlers | ⬜ pending |
| **3** | Extract JS generator/async resume-state helpers | ⬜ pending |
| 3 | Extract JS exception-routing and class-method installation helpers | ⬜ pending |
| **4** | Make Radiant DOM reflected attributes source-generated from one spec | ⬜ pending |
| 4 | Consider generic Jube reflection metadata only when a second module needs it | ⬜ pending |

---

## 1. Measured baseline and interpretation

### 1.1 Corrected scan

`make check-lambda-dup` now forces Lizard's C/C++ frontend. The verified full report is `temp/lambda_dup_scan.txt`.

| Measure | Result |
|---|---:|
| Remaining duplicate blocks | **3,658** |
| Same-file blocks | **2,980 (81.5%)** |
| Cross-file blocks | **678 (18.5%)** |
| Median matched location span | **6 lines** |
| 90th-percentile span | **18 lines** |
| Blocks touching vendored tree-sitter | **171** |
| JS-only blocks | **1,865 (51.0%)** |

Running the documented first-party scan with all tree-sitter trees excluded produces **3,520 blocks**. The older design-doc baseline was 3,519, so maintained-code duplication is effectively unchanged; the 3,658 Make result is higher because its current exclusion file still admits vendored tree-sitter C/C++.

### 1.2 Concentration

Lizard uses overlapping token windows, so “appearances” and union-covered source lines identify leverage better than the raw block count.

| File | Appearances | Union-covered lines | Dominant cause |
|---|---:|---:|---|
| `lambda/js/js_mir_expression_lowering.cpp` | 1,523 | 4,727 | builtin-name ladders; generator/class lowering templates |
| `lambda/js/js_runtime.cpp` | 1,421 | 5,021 | builtin dispatch; repeated per-operation runtime shapes |
| `lambda/transpile-mir.cpp` | 979 | 3,128 | typed array/index fast paths expanded per representation |
| `lambda/js/js_globals.cpp` | 883 | 2,463 | constructor/type/name inventories repeated across policies |
| `lambda/input/css/css_properties.cpp` | 843 | 439 | declarative property rows, not duplicated algorithms |
| `lambda/module/radiant/radiant_dom_bridge.cpp` | 381 | 1,031 | reflected-property adapters expanded per IDL member |
| Python/Ruby/Bash MIR transpilers | ~793 combined | ~2,843 | parallel register/import/call emitter infrastructure |

The conclusion is not “Lambda has 3,658 functions to merge.” The actionable result is four structural problems:

1. one concept is declared in several JS builtin inventories;
2. MIR backend mechanics are still cloned in the guest-language transpilers;
3. typed or semantic variants are hand-expanded instead of parameterized at the third variant;
4. reflected DOM members have a descriptor table but still carry hand-expanded source adapters.

### 1.3 Non-actionable or review-first findings

- `css_properties.cpp` property rows are readable declarative data. They should be a documented block exclusion, not hidden behind runtime builders.
- Vendored tree-sitter C/C++ must not affect the first-party baseline.
- Similar AST/CSS/parser struct declarations often represent independent domains. Lizard discards the identifiers that distinguish them; sharing these layouts would create false coupling.
- HTML5 tokenizer and parser branches may repeat the grammar shape while enforcing different states. Extract only a verified common invariant, never a callback abstraction around two short loops.
- A falling raw block count is not sufficient evidence of improvement. LOC, number of synchronized inventories, and single-point-of-change properties matter more.

---

## 2. Phase 0 — Measurement hygiene and ratchet

**Goal:** make `make check-lambda-dup` a stable first-party C/C++ measurement and a usable cleanup queue.

### P0.1 — Force C/C++ mode — complete

The wrapper constructs `lizard -Eduplicate -l cpp ...`. A focused unit test pins the command, and the rerun contained only `.c`, `.cc`, `.cpp`, `.h`, and `.hpp` paths.

### P0.2 — Exclude vendored/generated trees

Add a documented `file_exclusions` rule covering all `lambda/tree-sitter*` paths, not only `parser.c`. Keep `lambda/lambda-embed.h` excluded. Do not add broad exclusions for maintained parser or transpiler code.

**Acceptance:** the Make target reproduces the documented **3,520** first-party result before block exclusions, modulo changes landed after this snapshot.

### P0.3 — Review declarative and independent false positives

Add marker-based block exclusions only after source review. The first candidates are:

- `static CssProperty property_definitions[]` in `css_properties.cpp` — one declarative schema;
- independent AST/parser/CSS struct declarations that share only field-layout shape;
- other tables where rows contain no executable control flow.

Each exclusion must name the invariant and its reason. Ordinary per-type implementations, switches, loops, or adapters are never excluded merely because they are numerous.

### P0.4 — Report clone families, not only windows

Extend `check_code_dup.py` to merge overlapping windows with the same file set into a review family. Report:

- raw Lizard block count for reproducibility;
- first-party family count;
- union duplicate lines per file;
- same-file versus cross-file families;
- top files and file pairs;
- excluded counts by reviewed rule.

Default Make output should be the ranked summary. A `--full` option should preserve every Lizard location for audits. Long reports remain under `./temp/`.

### P0.5 — Ratchet

Commit the first-party baseline only after P0.2–P0.4. The gate fails on growth in reviewed family count or union duplicate lines. Raw Lizard block count remains diagnostic because a small edit can split one family into many overlapping windows.

**Phase 0 gate:** focused Python unit tests, `make check-lambda-dup`, deterministic rerun, `git diff --check`.

---

## 3. Phase 1 — Mechanical backend and typed-variant consolidation

**Goal:** remove exact emitter and typed-load templates without changing language semantics.

### P1.1 — Adopt `MirEmitter` in guest transpilers

The completed Unified-AST Phase 0 established `MirEmitter` for core Lambda and LambdaJS. Python, Ruby, and Bash were explicitly deferred and still implement their own register allocation, import cache, call-prototype creation, and fixed-arity call wrappers.

Migrate one language at a time:

1. Python `pm_new_reg`, `pm_ensure_import`, `pm_call_0..5`, and void calls;
2. Ruby `rm_new_reg`, `rm_ensure_import`, `rm_call_0..4`, and void calls;
3. Bash `bm_ensure_import*`, `bm_emit_call_0..3`, and void calls.

Each transpiler embeds a `MirEmitter`. Existing `pm_*`/`rm_*`/`bm_*` call-site names may remain as thin wrappers during its migration, but the wrapper body must delegate to `em_*`; delete obsolete private cache structs and call construction before closing that language slice.

Preserve these boundaries:

- language AST lowering, boxing policy, truthiness, exceptions, and runtime function choice stay language-local;
- import-key signature policy is an explicit emitter option, not silently normalized;
- no Python/Ruby/Bash AST unification is part of this phase;
- no `std::` containers or strings are introduced.

**Per-language gate:** `make build`, `make build-jube`, that language's focused/full gtests, `make test-lambda-baseline`, `make check-lambda-dup`, `git diff --check`.

### P1.2 — Extract common typed-index load emission

`transpile_index()` in `transpile-mir.cpp` repeats the same MIR structure for known integer arrays, floating arrays, boxed boolean arrays, runtime-guarded arrays, and `ANY` indexes:

1. produce/unbox the index;
2. load and check length;
3. branch on negative/out-of-bounds;
4. load the data pointer;
5. scale and add the element offset;
6. load a native or boxed element;
7. optionally perform a runtime-type guard and slow fallback.

Add one narrow helper, conceptually `emit_checked_index_load()`, parameterized by a C+ descriptor or explicit arguments:

- expected container/storage kind;
- element MIR type and byte width;
- native/boxed result representation;
- required runtime type guard;
- out-of-bounds value;
- optional slow-path callback identifier/policy.

Do not combine semantically different cases until the helper reproduces their current differences explicitly. In particular, integer, float, bool, mutation-widened generic arrays, and `ItemNull` out-of-bounds behavior must remain visible in the call-site policy.

**Gate:** focused typed-array/vector/index smokes, `make test-lambda-baseline` at 100%, `make check-lambda-dup`, `git diff --check`.

### P1.3 — Remove trivial DOM array constructors

Replace `radiant_dom_empty_node_list()`, `radiant_dom_empty_array_item()`, and `radiant_dom_array_item()` with one module-local array constructor. This is independent of the broader DOM descriptor work in Phase 4.

**Phase 1 exit:** all three guest transpilers use the shared emitter mechanics; typed indexing has one bounds/address emitter; no duplicate DOM empty-array constructors remain; every slice passes its real runtime baseline.

---

## 4. Phase 2 — One source of truth for JavaScript builtins

**Goal:** adding or changing a JS builtin should not require synchronized name/arity/ID inventories in `js_runtime_internal.hpp`, `js_runtime_builtin_registry.cpp`, `js_globals.cpp`, `js_mir_expression_lowering.cpp`, and `js_runtime.cpp`.

This is the highest-leverage phase and must be implemented by builtin family, not as one giant rewrite.

### P2.1 — Define the catalog contract

Introduce one C/C++ source catalog such as `js_builtin_catalog.def`. Each row records only metadata genuinely shared by consumers:

```text
owner/type, property name, builtin id, formal arity,
runtime dispatch group/op, MIR lowering kind, flags
```

The catalog may be an X-macro `.def` file or one static descriptor table. It must remain readable, preserve current enum ordering where range arithmetic depends on it, and avoid a generated checked-in copy that can drift from its source.

The catalog does not contain large behavior bodies. Custom semantics remain named functions or explicit custom cases.

### P2.2 — Derive enum, registration, and lookup

Move in this order:

1. generate/derive `JsBuiltinId` and compile-time range assertions;
2. derive `JsBuiltinMethodSpec` registration tables;
3. replace duplicated constructor, typed-array, error-class, and prototype name inventories with catalog queries;
4. make name-to-ID lookup use one indexed/hash lookup owned by the registry.

The first slices should be cohesive families with good tests: Math/JSON, Array, String, typed arrays, then Object/Function and the remaining constructors.

### P2.3 — Replace MIR `strncmp` classification

`js_mir_expression_lowering.cpp` currently contains hundreds of string comparisons for builtin recognition and optimized lowering. Resolve the callee to a catalog entry once, then dispatch on `mir_lowering_kind` or builtin ID.

Use a small explicit custom switch for syntax- or receiver-sensitive intrinsics. Do not force generic runtime calls where the current MIR specialization is required for semantics or performance.

### P2.4 — Reduce runtime dispatch by behavior group

The giant `js_dispatch_builtin()` switch should become a top-level dispatch over coherent groups with shared adapters, for example Array, String, Number, Math, RegExp, Map/Set, Promise, iterator, and Object reflection. Within a group:

- use a common `Item handler(Item this_value, Item* args, int argc)` adapter where behavior already has that shape;
- use a compact operation enum for closely related algorithms;
- retain named custom functions for exceptional semantics;
- remove local name arrays and repeated ID membership chains once the catalog supplies them.

The goal is single-point-of-change, not a switch-free codebase.

### Phase 2 invariants and gate

- Builtin IDs and any contiguous ranges used for arithmetic remain compile-time asserted.
- Function `name`, `length`, prototype placement, enumerability, accessor kind, and cache identity remain unchanged.
- Optimized MIR and generic runtime calls produce the same observable result and exception ordering.
- Release performance is measured for builtin-heavy AWFY/Node workloads; debug builds are never used for performance conclusions.

**Gate per family:** `make build`, full/focused `test_js_gtest.exe`, `make test262-baseline`, `make node-baseline` with zero new failures/retries, release benchmark where lowering changes, `make check-lambda-dup`, `git diff --check`.

---

## 5. Phase 3 — Shared JavaScript MIR semantic helpers

**Goal:** collapse exact JavaScript lowering templates across expression, statement, and module lowering while keeping JavaScript semantics out of the generic MIR emitter.

The strongest cross-file scan pairs are:

| Pair | Shared Lizard blocks |
|---|---:|
| expression ↔ statement lowering | 101 |
| expression ↔ module-batch lowering | 98 |
| statement ↔ module-batch lowering | 82 |

### P3.1 — Generator/async environment helpers

Extract exact shared operations into a coherent JS MIR control-flow module:

- save env-backed locals before suspend/yield;
- restore env-backed locals on resume;
- reset try-context registers after resume;
- reload shared closure captures after async callbacks;
- validate and emit resume-state labels.

Suggested interfaces are narrow operations such as `jm_emit_suspend_env_save()`, `jm_emit_resume_env_restore()`, and `jm_emit_try_state_reset()`. They operate on `JsMirTranspiler`; they do not belong in `MirEmitter` because they encode JavaScript generator/async rules.

### P3.2 — Exception and completion routing

Unify repeated scans for the nearest active catch/finally context and the corresponding branch/return emission. Preserve the differences among throw, return-through-finally, generator return signals, rejected await, and iterator close.

Use explicit completion-kind parameters rather than callbacks or Boolean combinations whose meaning is unclear at call sites.

### P3.3 — Class method installation

The class lowering repeats function construction, closure creation, name/source metadata, home-class assignment, computed-key spilling, accessor handling, and property installation for inherited statics, own statics, and instance methods.

Extract one method-construction/install primitive with explicit policy:

- destination object;
- static/instance/inherited mode;
- computed/private/accessor flags;
- home-class object;
- override/install policy.

Superclass traversal and prototype-chain policy stay in the higher-level class lowering; the helper owns only the repeated method materialization invariant.

### P3.4 — Boundaries

- Do not resume or depend on unfinished Unified-AST shared-lowering phases; this plan reuses only the completed emitter substrate.
- Do not merge expression, statement, and module entry points merely to reduce file count.
- Extract exact shared shapes first; similar-but-different control flow remains separate with a comment naming the semantic distinction.
- Every bug fix discovered during extraction gets a focused regression and a root-cause comment at the fix point.

**Gate:** generator/async/class/module focused fixtures, full `test_js_gtest.exe`, `make test262-baseline`, `make node-baseline` with zero new failures/retries, `make check-lambda-dup`, `git diff --check`.

---

## 6. Phase 4 — Descriptor-driven Radiant DOM reflection source

**Goal:** one reflected IDL member declaration should drive its source adapters and Jube binding entry without hand-synchronizing getter/setter bodies, declarations, defaults, guards, and attribute names.

The live bridge already has Jube member descriptors, but the reflected-property implementation remains hand-expanded across `radiant_dom_bridge.cpp` and `radiant_dom_iface.cpp`. The DOM3 progress note describes one specification; the source should make that single source mechanically real.

### P4.1 — Establish one reflection spec

Create a readable module-owned definition list, for example `radiant_dom_reflect.def`, with rows for:

- Lambda and optional JS-visible property names;
- reflected attribute name;
- reflection kind: boolean, integer, string, keyword/custom;
- default value/policy;
- tag/receiver guard;
- optional side-effect hook;
- getter/setter availability and descriptor flags.

Keep live-control semantics explicit: `disabled`, select `multiple`, `defaultChecked`, `defaultSelected`, `contentEditable`, and similar members retain named hooks and normalization functions.

### P4.2 — Share reflection primitives

Provide one implementation per reflection kind:

- boolean attribute presence and ToBoolean set/remove;
- integer conversion/default/serialization;
- string get/coerce/set;
- named keyword canonicalization/custom error path.

The spec expands thin C-ABI adapters where the current Jube handler signature requires distinct function symbols. Generate the declarations and `JubeMemberDef` rows from the same definition list so neither can drift.

### P4.3 — Delete the hand-expanded copies

Migrate by kind, preserving tests after each slice:

1. side-effect-free booleans;
2. integers;
3. strings;
4. booleans with live-state hooks;
5. keyword/custom members.

Delete each old getter/setter only when its generated adapter and descriptor row are active. Do not maintain permanent old/new paths.

### P4.4 — Generic Jube reflection is conditional

Do not change the generic Jube ABI merely to eliminate module-local macro adapters. Add descriptor `user_data` or generic reflection metadata only when a second Jube module needs the same facility and the shared shape is proven. Until then, the module-owned `.def` list is the correct abstraction boundary.

### Phase 4 gate

- DOM property get/set, `in`, own keys, descriptors, deletion, aliases, and prototype behavior stay unchanged.
- Focus/select/dirty-state/mutation notifications run exactly once.
- `make build`, `make build-jube`, DOM-focused JS fixtures, full `test_js_gtest.exe`, Lambda Radiant module tests, relevant UI automation, `make check-lambda-dup`, and `git diff --check` pass.

---

## 7. Sequencing and completion criteria

```text
Phase 0 measurement hygiene
    ↓
Phase 1 mechanical emitter/index cleanup
    ↓
Phase 2 builtin catalog ─────┐
    ↓                        │ independent after Phase 0
Phase 3 JS MIR helpers       ├── Phase 4 DOM reflection
```

Phase 4 may proceed independently after Phase 0 because it does not share implementation files with the builtin or MIR-lowering work. Within every phase, land one language, builtin family, typed representation, semantic helper, or reflection kind at a time.

A phase is complete only when:

1. the old duplicate implementation is deleted, not retained as fallback;
2. the new shared abstraction names the invariant and keeps policy differences explicit;
3. the relevant real regression gate passes;
4. the first-party dedup family/union-line metric does not regress;
5. LOC and synchronized-inventory counts are recorded in this document;
6. no unrelated working-tree edits are overwritten.

## 8. Explicit non-goals

- No broad file-splitting campaign for `js_runtime.cpp`, `js_globals.cpp`, `js_mir_expression_lowering.cpp`, or `transpile-mir.cpp`.
- No ban on file-local `static` functions.
- No Python/Ruby/Bash AST or language-semantics unification.
- No automatic unification of independent parser states, struct layouts, or declarative tables.
- No new `std::` types, handwritten build Lua changes, or generated-parser edits.
- No performance claims from debug builds.
