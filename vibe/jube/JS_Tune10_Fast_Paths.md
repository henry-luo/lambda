# JS Tune10 — LambdaJS Fast Paths, Aligned With Untyped Lambda

**Date:** 2026-09-09
**Status:** T10-0, T10-1 and T10-3 **LANDED** 2026-09-09 (plus the D-C kernel-head slice of T10-2); T10-2 shape prediction, T10-4 and T10-5 OPEN. See [§8](#8-implementation-status-2026-09-09).
**Measured snapshot:** [Result38](../../test/benchmark/Overall_Result38.md), commit `b23793e83`, archived binary `test/benchmark/exe/lambda-v38-b23793e832`
**Predecessors:** [JS_Tune9_Performance.md](JS_Tune9_Performance.md) (Result28→Result29 diagnosis), [Lambda_Design_JS_IC_Retire.md](../Lambda_Design_JS_IC_Retire.md) (IR1–IR8, 2026-08-15), [JS_Tune_History.md](JS_Tune_History.md)
**Formal authority:** **D8.4.1v2** (no inline caches in either lane; specialization over caching), **D1.3** (one core runtime; guests reuse contracts), **D3.4.4v2** (ShapeEntry identity is `name_id`), **D4.6.1v2** (property identity is a `NameId`, never a `String*`), **D6.2.2v2** (observable `Get` before `Call`), **D8.4.3v2** (explicit completion ABI for hosted helpers), **D8.1.1v9** (tiered execution). Compilation strategy: **LC1v2** in [`../Lambda_Design_Compiling.md`](../Lambda_Design_Compiling.md) — the LambdaJS carve-out was closed by the owner on 2026-09-09, so every measure below is compile-predicted specialization with an inline guard and the shared kernel on a miss, never a cache.

---

## 1. Conclusion

LambdaJS in Result38 is **4.35× slower than QuickJS** (geomean, 63 rows) and **29× slower than untyped Lambda** running the same workloads through the same MIR backend. The gap is not the backend, the GC, or the value width. Profiles of the six worst rows show that **every JS property or element access crosses into a C helper that re-derives facts the compiler already knew**, and that four of those helpers are pathological:

| Defect | Where | Cost per operation (v38) | Rows it dominates |
|---|---|---|---|
| **D-A** Numeric index → `ToPropertyKey` → string → name-pool intern | `jm_emit_reference_key` → `js_to_property_key` → `js_to_string` → `lambda_finite_double_to_shortest` (up to 21 `snprintf`+`sscanf` round trips) → `name_pool_create_strview` | ~330 ns per `a[i]` (Node 4 ns, QuickJS 26 ns) | primes 40× QJS, quicksort 21×, sieve 16×, fft 19×, fannkuch 8× |
| **D-B** Property **add** runs the full ES `OrdinarySet` → `CreateDataProperty` → `Reflect.defineProperty` with a **materialized descriptor object** that is then re-parsed | `js_set_name_id` miss → `js_set_key_strict_policy` → `js_set_completion_with_key` → `js_reflect_set_define_receiver` → `js_make_reflect_set_value_desc` (alloc + 4 puts) → `js_reflect_define_property` → `js_descriptor_from_object` (6× `js_in` + 4× get) | ~7 µs per `new P(x,y)` (Node 5 ns, QuickJS 200 ns) | havlak 25× QJS, cd 27×, deltablue 14×, prettier_ast 20× (each spends 25–40% here) |
| **D-C** Named get/set "fast head" is a C call that hashes strings on every hit | `js_get_name_id` → `name_pool_resolve_id` → `js_get_host_dynamic_property` → `js_named_fast_receiver_map` → **`js_map_shape_lookup_ext(map, "__lambda_dataset_element", 24)`** (FNV/Sip hash of a 24-byte string per access) → `typemap_hash_lookup_by_name_id` → guards → read | ~50 ns per `p.x` (Node 0.4 ns, QuickJS 19 ns); v28 was 16 ns | every object-heavy row; `well_known_name_id`, `hashmap_sip`, `name_pool_lookup_strview` are the top three leaf symbols in havlak/cd/prettier |
| **D-D** `arguments` object materialized on every call of any function whose body *mentions the identifier* `arguments`, including as a member name (`node.arguments`) | `js_ast_children.cpp:208` (`js_ast_identifier_named(node,"arguments")` fires on member property identifiers) + `js_build_arguments_object` (array + companion map + descriptor object + two symbol keys + accessor install) | **51 µs per call** (500k calls: 25.6 s vs 43 ms without the word); real `arguments` use costs 50 µs per call on every release since v29 | prettier_ast (13% of samples in `js_build_arguments_object`; the source never uses the `arguments` object) |
| **D-E** No compile-predicted shape for object literals or constructor `this.x = …` sequences; every instance builds its own `TypeMap` through the D-B path | constructor pre-shaping (`js_set_class_ctor_shape_metadata`, `js_constructor_create_object_shaped_cached`) no longer exists in the tree; JS_06 §10 still documents it | `{x,y}` literal ~1 µs (Node 4 ns, QuickJS 170 ns) | gcbench, binarytrees, json, deltablue, havlak allocation phases |

Untyped Lambda has none of these costs because its lowering (a) keeps a static member name as a **`NameId` constant** and calls `fn_member_by_id` → `map_get_by_name_id` with no string work (D4.6.1v2), (b) guards a **compile-predicted `TypeMap` pointer inline** and reads the slot directly on a hit (T20-1c guarded member read, `transpile-mir.cpp` ≈16020), (c) carries an integer index in an **unboxed lane** straight into `item_at(Item, int64_t)`, and (d) gives every literal site its own shape, so adds never happen on the hot path.

The proposal is therefore not "add optimizations to JS"; it is **route JS's ordinary cases through the same physical machinery untyped Lambda already uses**, and reserve the semantic kernel for misses. That is IC_Retire's **Tier A** (designed there as IR10–IR15) and the "guarded direct operation with one fallback" of Tune9 §9.1 — both designed, neither implemented. Under **D8.4.1v2** / **LC1v2** (owner ruling, 2026-09-09) it is also the *only* sanctioned way to close the gap: inline caches are banned in both lanes, so the speed must come from compile-time prediction plus an inline guard, with the shared semantic kernel on every miss.

---

## 2. Measurements

### 2.1 Suite level (Result38, Part 1, JIT-pinned)

| Ratio | Geomean | Rows |
|---|---:|---:|
| LambdaJS / QuickJS | **4.35×** | 63 |
| LambdaJS / MIR untyped (same workload, same backend) | **29.2×** | 63 |
| LambdaJS / Node | 31.5× | 63 |
| Sum of timed work: LambdaJS 340 s, QuickJS 102 s, MIR untyped 30 s | | |

LambdaJS beats QuickJS only where the whole loop lives in native scalar registers: collatz 0.94×, mbrot 0.90×, sum 0.84×, sumfp 0.71×, pnpoly 0.53×, mandelbrot 0.51×, diviter 0.43×. Every row that touches an array element, a property, or allocates an object is 3–40× behind. The ranking (LJS/QJS): primes 39.6, cd 27.3, havlak 24.9, knucleotide 21.2, quicksort 20.8, prettier_ast 19.6, fft 19.0, sieve 16.5, hashmap 15.4, deltablue 14.4.

### 2.2 When it broke: Result28 → Result29

The archived reports show one cliff, 2026-08-10 → 2026-08-13 (commits `e91432d4aa` → `211fea19fb`, the JS Tune4–Tune8 redesign window), and **no recovery since**:

| LambdaJS ms | R28 | R29 | R38 |
|---|---:|---:|---:|
| kostya/primes | 101.8 | 4,610 | 3,720 |
| awfy/sieve | 0.477 | 2,930 | 10.1 |
| larceny/quicksort | 64.8 | 489.5 | 399.6 |
| awfy/havlak | 54,210 | 107,290 | 82,500 |
| awfy/richards | 1,850 | 7,670 | 1,330 |

Tune9 (2026-08-14) diagnosed this correctly (§5.1–5.5) and planned P1 (indexed recovery) and P2 (named recovery). IC_Retire (2026-08-15) then removed the per-callsite ICs **and the compiler-side indexed lanes**, leaving `js_number_key_to_index_fast`, `js_get_number_reference`, `js_set_number_assignment`, `js_elements_set_existing_dense_int_fast`, `js_array_fast_own_dense_get/set` defined in the runtime but **never emitted by the lowering** (`grep` over `js_mir_*.cpp`: 0 emit sites). The only emitted keyed path is `js_to_property_key` → `js_property_lane_for_canonical_key` → `js_get`/`js_set` (`js_mir_expression_lowering.cpp:1195–1231`, `:1287`).

### 2.3 Micro-benchmarks (same machine, same day; archived release binaries)

Scripts in the session scratchpad (`mb/*.js`, `mb/*.ls`); each is one hot loop, `performance.now()` / `clock()` around the loop only.

| Operation | v28 | v29 | **v38** | QuickJS | Node | **Untyped Lambda (v38, JIT)** |
|---|---:|---:|---:|---:|---:|---:|
| `a[i]` read+write, plain array, 4M ops | 81 ms | 1,330 | **1,336** | 104 | 15.7 | **4.7** |
| `a[i]` read+write, `Uint8Array`, 4M ops | 157 | 1,925 | **1,624** | 89 | 2.3 | — |
| `p.x + p.y` named reads, 4M | 65 | 226 | **196** | 77 | 1.7 | **33.5** |
| `p.x = i; p.y = i` existing-slot writes, 4M | 41 | 6,229 | **169** | 63 | 1.5 | **5.0** |
| `new P(i,1)` constructor (2 adds), 500k | 1,140 | 6,538 | **6,926** | 101 | 2.5 | — |
| `{x:i, y:1}` literal, 500k | 111 | 499 | **529** | 85 | 2.0 | **17.6** |
| `p.get()` method call, 2M | 64 | 453 | **407** | 107 | 1.7 | 193 (typed object) |
| call `f(o)` where body reads `o.args`, 500k | 14.6 | 50.9 | **43** | 30 | 1.1 | — |
| call `f(o)` where body reads `o.arguments`, 500k | 15.0 | 49.9 | **25,615** | 29 | 0.6 | — |
| call `f(a,b)` that uses `arguments.length`, 200k | 2,032 | 9,962 | **10,413** | — | — | — |

The `o.arguments` false positive appears between v35 (2026-08-26) and v36 (2026-09-02); the 50 µs real-`arguments` cost has existed since v29 and was 10 µs at v28.

### 2.4 Profiles (macOS `sample`, release v38, JS runs on a worker thread; main thread sits in `pthread_join`)

Inclusive share by the runtime helper the JIT code called:

| Row | Top entries (share of worker-thread samples) |
|---|---|
| kostya/primes | `js_to_property_key` **80.8%**, `js_set` 11.5%, JIT code 3.6% |
| larceny/quicksort | `js_to_property_key` 75%, `js_set` 12.5% |
| awfy/havlak | JIT code 23%, `js_set_name_id` 13.3%, `js_reflect_define_property` 12.7%, `js_descriptor_from_object` 9.5%, `js_map_shape_lookup` 4.1%, `js_has_own_property` 3.8%, `js_get_reference` 3.7%, `js_to_property_key` 3.4%, `name_pool_create_strview` 3.1%, GC ≈2.5% |
| awfy/cd | JIT 21%, `js_reflect_define_property` 16.1%, `js_set_name_id` 15.9%, `js_descriptor_from_object` 11.2%, `js_map_shape_lookup` 5.0%, `js_has_own_property` 4.3%, `name_pool_create_strview` 4.1% |
| awfy/deltablue | JIT 26%, `js_set_name_id` 11.6%, `js_reflect_define_property` 10.9%, `js_descriptor_from_object` 6.6%, `js_get_reference` 4.8% |
| text/prettier_ast | JIT 20%, **`js_build_arguments_object` 13.1%**, `js_reflect_define_property` 12.4%, `js_descriptor_from_object` 10.5%, `js_has_own_property` 6.8%, GC 3.3% |

Leaf symbols common to all object-heavy rows: `well_known_name_id`, `hashmap_sip`, `js_object_meta`, `name_pool_lookup_strview`, `js_map_shape_lookup(Map*, const char*, int, bool*)`, `js_intrinsic_note_property_mutation`, `_platform_memcmp`. All are string-keyed work performed on a path whose key was a compile-time constant.

Neither of the benchmark bundles uses `Object.defineProperty`, `arguments`, or class fields (`grep` count 0 in `havlak2_bundle.js`, `cd2_bundle.js`, `deltablue2_bundle.js`); `prettier_ast.js` mentions `arguments` only as `node.arguments`. The descriptor traffic is entirely runtime-internal.

---

## 3. Why LambdaJS is slower than QuickJS

QuickJS has no JIT, no ICs, reference counting, and a wider `JSValue`. It still wins 4.35× because its **ordinary case is decided in a handful of tag tests inside the opcode**, and its generic machinery runs only on a miss (Tune9 §7.3 already lists the six mechanisms). LambdaJS inverted that order after the redesign:

1. **The compiler forgets what it knows.** `flags[j]` has a `let j` that MIR carries as a native double or int; the lowering boxes it, calls `js_to_property_key`, which formats it as text with `snprintf`, re-parses it with `sscanf` to check round-tripping (up to 21 times), interns the text in the NamePool, then `js_set` parses the text back into an index. QuickJS keeps integer keys as integer atoms and never leaves the integer domain (Tune9 §7.3 item 2).
2. **The hit path is a C call that re-classifies the receiver on every access.** `js_get_name_id` re-resolves the `NameId` to a `NameRef`, probes the window/DOM host hook, does a **string-hash lookup for `__lambda_dataset_element`** to rule out a DOM dataset view, classifies the receiver kind, hashes again to probe the shape table, then runs five guard checks before reading. QuickJS's `OP_get_field` compares the atom against the shape's property array and loads. IC_Retire's IR3 said the fast head is "today's IC hit verbatim"; the DOM checks were added around it afterwards and now cost more than the lookup.
3. **Property addition is spec-literal.** A miss in `js_set_name_id` (which every constructor store is, because there is no pre-shaping) falls into `OrdinarySet`, which in this runtime builds a real descriptor **object**, hands it to `Reflect.defineProperty`, which parses it back with six `in` tests and four `Get`s. QuickJS's `add_property` is one shape transition and one slot write. This is the single largest cost in havlak, cd, deltablue and prettier.
4. **Shapes are not shared at allocation.** Without constructor/literal pre-shaping every instance walks the transition path through D-B, and `js_intrinsic_note_property_mutation` (an O(class-count) scan comparing prototype roots) runs on each of those adds.
5. **`arguments` is rebuilt per call and detected by name.** Materializing it costs 50 µs; detecting it on `x.arguments` member names makes the cost land on code that never asked for it.

The Tune9 verdict stands: "the performance mistake was to treat semantic consolidation, old-fast-path deletion, and replacement-fast-path delivery as separate milestones." Three weeks later the replacement fast paths still do not exist in the lowering.

---

## 4. What untyped Lambda does differently (the alignment target)

| Operation | Untyped Lambda lowering (`lambda/runtime/transpile-mir.cpp`) | LambdaJS lowering (`lambda/js/js_mir_expression_lowering.cpp`) |
|---|---|---|
| `p.x` read | Static name → `module_property_key_index` → **`NameId` register**, no string. If the receiver's shape is predicted at compile time (T20-1c): inline tag test, container-kind byte test, **`TypeMap*` pointer compare**, direct slot load and box; miss → `fn_member_by_id(obj, name_id)` → `map_get_by_name_id` (D4.6.1v2). | `js_get_name_id(obj, name_id)` C call; inside: `name_pool_resolve_id`, host hook, **string hash for dataset marker**, receiver classification, hash probe, guards. No inline guard, no predicted shape. |
| `p.x = v` write | Guarded direct slot store on the predicted shape; miss → `fn_member` store kernel. Typed-lane widening rules (INT→FLOAT) in the guard. | `js_set_name_id` C call; same head as read; miss → full `OrdinarySet` → descriptor object → `Reflect.defineProperty`. |
| `a[i]` | Index stays in the **int64 lane**; `item_at(Item, int64_t)` → `array_get` (10 emit sites). Element type known from `mir_known_index_element_type` when the array is a typed/inferred `T[]`. | Index boxed → `js_to_property_key` → `js_property_lane_for_canonical_key` → `js_get(obj, lane, key, obj)`: three C calls, one string alloc. |
| `{x: 1, y: 2}` literal | Per-site `TypeMap` (map-literal shape + trust flag, see memory note "Map literal shape + trust flag"); fields written by offset. | `js_new_object()` then per-property generic put through D-B; each instance grows its own `TypeMap`. |
| Method call `p.get()` | Direct MIR call when the callee is a known typed-object method. | `Get` then `fn->invoke` (D6.2.2v2, correct) but the `Get` pays D-C. |

The two runtimes share `Item`, `Map`, `TypeMap`, `ShapeEntry`, `NameId`, the GC and the MIR driver (D1.3, JS_Unified §2.1). Nothing in the JS semantics forbids the Lambda physical routes; the JS lowering simply does not use them.

---

## 5. Proposal

Ordering is by measured payoff divided by risk. Every phase lands behind a same-binary env gate for A/B (`JS_Tune_History` §1 rule 3), must show zero Test262 regressions, and must show a beyond-noise win on its named rows or be reverted (rule 5).

### T10-0 Two bug fixes (days, no design) — **LANDED 2026-09-09**

- **`arguments` detection (D-D).** In `js_ast_children.cpp` the observation walk must not treat the property identifier of a non-computed member expression (or a property key in an object literal / class member) as a reference to `arguments`, `this`, or `new.target`. Expected: `prettier_ast` −13%, and any code that reads `node.arguments` (every AST walker, i.e. real-world JS) stops paying 51 µs per call.
- **`ToPropertyKey` for numbers (D-A, runtime half).** `lambda_finite_double_to_shortest` must not loop `snprintf`/`sscanf`; integral doubles in index range format through an integer path, and the general case uses one shortest-round-trip conversion (the project already has `lambda-decimal`; a Ryu/Grisu-style shortest formatter is the standard answer). This helps every remaining generic keyed access and `String(n)`.

### T10-1 Numeric index lane (D-A, compiler half) — the primes/quicksort/sieve/fft recovery — **LANDED 2026-09-09**

Emit, for a computed reference whose key is carried as INT or FLOAT (`jm_is_native_type`) or is a boxed Number:

```text
if key is exact non-negative integer < 2^32-1            (js_number_key_to_index_fast semantics, inline)
   and receiver tag == container and kind ∈ {ARRAY, ARRAY_NUM/typed-array}
   and receiver has no companion facts that matter (Tune9 §9.2 split facts)
   and index < length (read) / index < length or exact append (write):
        direct element load/store  (array_get / typed lane store, boxing only at the Item boundary)
else:
        js_get_number_reference / js_set_number_assignment   (already exist; pass the double, not a string)
else (non-number key): today's js_to_property_key path
```

This reuses the Lambda index lane: the same `item_at`-style entry and the same unboxed-int register the untyped tier uses, with JS-specific guards. Target: `a[i]` ≤ 15 ns (QuickJS 26 ns), primes ≤ 100 ms (v28 level), sieve/quicksort/fft/fannkuch/permute back to R28.

### T10-2 Named access: predicted shape + inline guard, kernel on miss (D-C, D-E) — **item 3 LANDED, items 1–2 OPEN**

This is IC_Retire's **Tier A**, already designed there as IR10–IR15 (type identity and guard, `slot_entries[]` as the sole ordinal index, where `type_index` comes from, subtype admission by id ranges). Under LC1v2 it is no longer an optional follow-up but the only sanctioned route. It introduces **no per-site mutable state** and no MIR-cache-key churn:

1. **Per-site shape prediction.** Object literals get a per-site `TypeMap` exactly as Lambda literals do. Constructors get a predicted `TypeMap` from their `this.x = …` prefix (the retired pre-shaping, re-done as immutable compile-time metadata rather than a runtime cache). `new C()` allocates the instance with the predicted shape and reserved slots (`ctor_reserved_mask` already exists for Lambda), so constructor stores are **existing-slot writes**, not adds.
2. **Inline guard, direct slot.** Named get/set on a receiver whose static type carries a predicted shape emits the T20-1c guard sequence (tag, kind byte, `TypeMap*` compare, `ctor_reserved` bit, flags == 0) and a direct slot load/store with the lane-widening rules from `js_named_fast_store_can_write_same_slot`. Miss → `js_get_name_id` / `js_set_name_id` unchanged.
3. **Cheapen the kernel head.** Even for unpredicted receivers, `js_get_name_id` must not hash strings: make the dataset-view marker a `map_kind`/`js_meta` bit (it is a property of the object's class, not of its shape contents), take the host hook only when the receiver is the realm's `globalThis` (one pointer compare), and pass the `NameRef` and its cached `hash`/`name_id` from the compile-time constant instead of re-resolving.

Target: `p.x` ≤ 5 ns on a hit, `{x,y}` literal ≤ 100 ns, `new P` ≤ 150 ns; havlak/cd/deltablue/richards halve or better.

### T10-3 Property add without descriptors (D-B) — **LANDED 2026-09-09**

`OrdinarySet` on an ordinary extensible receiver with no accessor/Proxy/exotic in its prototype chain must reach an `add_property` kernel: shape transition (or the predicted shape's next slot) plus one write. The descriptor-object path stays for `Object.defineProperty`, accessors, non-writable/non-configurable, Proxy and exotic receivers. `js_intrinsic_note_property_mutation` runs only when the receiver is itself an intrinsic prototype root (a flag on `js_meta`), not on every store. This is the part of D-B that T10-2 does not remove (dynamic objects, dictionary-mode maps, JSON parsing).

### T10-4 `arguments` object (D-D, cost half) — **OPEN**

Build it lazily on first observation or as a cheap fixed-layout iterator-style carrier (JS_08 pattern) instead of an array + companion map + descriptor object + two symbol installs per call. Sloppy-mode mapped `arguments` keeps its aliasing semantics through the existing `js_arguments_mapped_get`.

### T10-5 Residual (measure first) — **OPEN**

`js_object_meta` on every access (make it a TypeMap field read, already true for plain maps), GC share (2–3% in havlak, not a priority), method-call `Get→Call` feedback (JS_15 §5.2–5.3; only after T10-2, because most of that cost is the `Get`).

---

## 6. Gates and expected outcome

| Phase | Named rows | Exit |
|---|---|---|
| T10-0 | prettier_ast, any `arguments` user | `o.arguments` ≡ `o.args` timing; real `arguments` ≤ 5 µs/call |
| T10-1 | primes, sieve, quicksort, fft, fannkuch, permute, levenshtein, navier_stokes | each ≤ Result28 LambdaJS time; Test262 0 regressions |
| T10-2 | havlak, cd, deltablue, richards, bounce, nbody, json, towers | LJS/QJS ≤ 3× on each; no per-site mutable state introduced (D8.4.1v2, LC1v2, IR1) |
| T10-3 | gcbench, binarytrees, json_gen, hashmap | `new P` ≤ 150 ns |
| overall | 63 rows | LambdaJS/QuickJS geomean ≤ 1.5× (R28 was 2.24×); LambdaJS/MIR-untyped ≤ 8× |

Micro-benchmark scripts and the `sample`-parsing scripts (`tree.py`, `entry.py`) used here should be checked in under `test/benchmark/js_micro/` so the A/B is repeatable; the archived `test/benchmark/exe/lambda-v28-e91432d4aa` binary is the pre-regression control.

---

## 7. Open points for the owner

1. **LC1 vs IC_Retire — RESOLVED 2026-09-09.** The owner ruled: no inline caches in LambdaJS or Lambda. LC1 was revised in place to **LC1v2** (the "LJS keeps its ICs" clause is gone), **D8.4.1v2** was extended to name the sanctioned replacement, IC_Retire's Tier A was promoted from optional follow-up to the mandated route, and the JR8 feedback-vector plan in JS_15 §5.2–5.3 and JS_Tune9 §6 was retired. This proposal is the execution plan for that ruling.
2. **`S#7.3.32`** is cited in `js_globals.cpp:6538` as the ruling that removed the typed-array MIR specialization, but no document under `doc/` or `vibe/` defines it. The ruling needs a home before T10-1 re-adds a guarded typed-array lane.
3. **JS_06 §10** documents constructor shape pre-allocation that no longer exists; JS_15 §2.3 likewise. Both need a "retired" note or the T10-2 replacement.
4. **Exit noise.** Every `lambda.exe js`/`run` exit prints `[ERR!] stack cleanup: sigaltstack restore failed`. Not a perf issue; noted here because it appears in every benchmark log.

---

## 8. Implementation status (2026-09-09)

Landed in one change against `b23793e83`; the control is the archived binary of
that exact commit (`test/benchmark/exe/lambda-v38-b23793e832`), which is a
stronger A/B than the same-binary env gate of `JS_Tune_History` §1 rule 3 —
the two sides are the pinned measured snapshot and the new build, not two
rebuilds. No gate was added: every effect below is 1.7×–402×, far outside the
~15 % harness noise, and rule 5's "neutral means revert" never came into play.

### 8.1 What landed

| Phase | Change | Files |
|---|---|---|
| T10-0 (D-D) | The function-facts walk no longer reads a **key position** as a reference. `node.arguments`, `{arguments: v}`, `class { arguments() {} }` and `obj.eval` stopped forcing an `arguments` object (and stopped clearing `tail_reuse_safe`). A shorthand `{arguments}` key still is a reference and is not skipped. | `lambda/js/js_ast_children.cpp` |
| T10-0 (D-A, runtime half) | `lambda_finite_double_to_shortest` takes an integer path for integral magnitudes below 2^53 instead of up to 21 `snprintf`/`sscanf` round trips. Adjacent doubles there are ≥1 apart, so the exact integer digits *are* the shortest round-tripping form, and ES `Number::toString` prints them (k ≤ e ≤ 21). | `lambda/core/lambda-decimal.cpp` |
| T10-1 (D-A, compiler half) | Computed member get/put and the compound-update canonicalizer emit `js_get_number_reference` / `js_set_number_assignment` with the key in a **native double**, instead of `js_to_property_key` → name-pool intern → `js_get`/`js_set` re-parsing the text back into an index. The two runtime entries already existed (IC_Retire left them unemitted); nothing new was added below the semantic boundary, so D1.3 holds. `ToPropertyKey` on a Number runs no user code, so `a[i] += v` keeps the lane across both halves. | `lambda/js/js_mir_expression_lowering.cpp`, `js_mir_internal.hpp` |
| T10-2 item 3 (D-C) | `js_get_name_id` / `js_set_name_id` no longer hash the 24-byte `__lambda_dataset_element` marker on every named access. The marker is installed non-enumerable, which promotes the view's map to `MAP_KIND_DESC`, so that byte rules a dataset out first. `js_dataset_owner` takes the same precondition. | `lambda/js/js_runtime.cpp` |
| T10-3 (D-B) | `js_set_completion_with_key` decides the ordinary "create a new own data property" case from shape storage — own-entry absence, extensibility, and a prototype chain of ordinary shape-backed maps with no entry for the key — and then performs the single slot write. The descriptor-object path (`js_reflect_set_define_receiver` → `js_make_reflect_set_value_desc` → `js_reflect_define_property` → `js_descriptor_from_object`) still owns everything the kernel cannot prove cheaply. | `lambda/js/js_globals.cpp` |

### 8.2 The T10-1 admission rule: carrier, not inferred type

§5 T10-1 says "carried as INT or FLOAT (`jm_is_native_type`) or is a boxed
Number". The **carrier** half is load-bearing and the first implementation got
it wrong: admitting on `jm_get_effective_type` alone silently coerced

```js
let m = 0; for (let i = 0; i < 2; i++) m = m + 1; m = "x";
o[m]        // read 'NaN', not 'x' — ToNumber("x") reached the index kernel
```

`jm_get_effective_type` is an inference used to *choose* an arithmetic lane; it
is not a proof about the runtime value of a rebindable binding. The shipped
rule is a physical one — `jm_emit_computed_key_number_lane` lowers the key at
its natural representation and admits only `VALUE_REP_F64`, or `VALUE_REP_I64`
carrying `LMD_TYPE_INT`. A native numeric register cannot hold a string, so no
`ToPrimitive`/`ToPropertyKey` step is observable. Everything else stays generic.
This is the same "carrier oracle, not lane witness" discipline as T19-3.

Two consequences worth keeping:
* the `JsOpt.Result29NumberIndexedLaneUsesSharedReferenceSemantics` ratchet
  still passes unmodified — its `var numberKey = 1.0` is a boxed module var, so
  it is not admitted, and no design ratchet had to be rewritten;
* the boxed-Number half of §5 T10-1 (an inline tag test on a boxed key) is
  therefore **not implemented** and remains available as a follow-up.

The boxed key is still published into `JsMirReference::key_reg`, because
`delete`, `in`, super references and the compound-update path address the
reference through it.

### 8.3 Measured (release build, idle machine, one run each)

Micro-benchmarks (`temp/jstune10/mb_*.js`, same shape as §2.3):

| Operation | v38 | after | QuickJS (§2.3) |
|---|---:|---:|---:|
| `a[i]` read+write, plain array, 4M ops | 2733 ms | **450** | 104 |
| `new P(i,1)` constructor (2 adds), 500k | 7231 | **820** | 101 |
| `p.x + p.y` named reads, 4M | 395 | **229** | 77 |
| `p.x = i; p.y = i` existing-slot writes, 4M | 336 | **170** | 63 |
| `{x:i, y:1}` literal, 500k | 562 | 541 | 85 |
| call `f(o)` where body reads `o.arguments`, 500k | 38 101 | **65** | 29 |

Benchmark rows (`__TIMING__`, ms; both sides are the same `make release`
artifact shape, run back to back on an idle machine; every one of the 78
standalone-JS rows produced byte-identical output, and the five `exit 1/1` rows
fail identically on both binaries):

| Row | v38 | after | speed-up |
|---|---:|---:|---:|
| kostya/primes, larceny/primes | 3727.1, 3714.3 | **137.7**, **137.8** | 27.1×, 27.0× |
| awfy/sieve2 | 9.98 | **1.40** | 7.1× |
| awfy/cd2 | 25 496 | **4551** | 5.6× |
| text/prettier_ast | 27 062 | **6321** | 4.3× |
| awfy/havlak2 | 80 920 | **20 467** | 4.0× |
| larceny/levenshtein | 437.6 | **115.6** | 3.8× |
| awfy/deltablue2 | 1418.4 | **530.4** | 2.7× |
| larceny/quicksort | 398.0 | **148.8** | 2.7× |
| r7rs/fft2 | 53.4 | **23.2** | 2.3× |
| larceny/puzzle | 108.9 | **47.8** | 2.3× |
| awfy/bounce2, larceny/array1, awfy/json2 | 6.97, 78.3, 90.6 | 3.22, 38.1, 44.9 | 2.0–2.2× |
| larceny/brainfuck, matmul, paraffins | 3656.9, 1282.9, 4.08 | 1920.2, 681.8, 2.36 | 1.7–1.9× |
| awfy/list2, nbody2, queens2, triangl, json_gen, text/fast_diff, storage2, permute2, towers2, nqueens2, richards2, mbrot2, ray | | | 1.1–1.6× |
| collatz, diviter, divrec, gcbench, pnpoly, deriv, base64, log_pipeline, microdiff, text_search, three_way_merge, r7rs scalar rows | | | 0.99–1.07× (noise) |

No row regressed beyond noise; the worst is 0.99× on divrec, a scalar row the
change does not touch.

Gate status against §6: T10-0 met (`o.arguments` ≡ `o.args`, 65 ms vs 65 ms;
the *real*-`arguments` 50 µs/call cost is untouched and is T10-4). T10-1 met on
primes (138 ms vs R28's 101.8), sieve, quicksort, fft, permute and levenshtein;
`a[i]` is 41 ns, not the ≤15 ns target, because the remaining cost is the C
call itself. T10-3's `new P` is 1.6 µs, not ≤150 ns: each instance still grows
its own `TypeMap`, which is T10-2 item 1.

### 8.4 Verification

* **test262** 40261/40261, **0 regressions** (`make test262-baseline`).
* **Lambda baseline** 5084/5084 (`make test-lambda-baseline`) — the shortest-double
  formatter is shared with Lambda, so this gate is required, not optional.
* `test_js_opt_gtest` 19/19 and `test_js_gtest` 378/378.
* Two Node-differential regressions are checked in and auto-discovered by
  `test_js_gtest`, so both gates cover them from now on:
  * `test/js/js_tune10_number_key_lane.js` — fractional, negative, `NaN`,
    `Infinity`, ≥2^32 and 2^53 keys; a key rebound from number to string;
    `valueOf`-bearing key objects; BigInt and Symbol keys; index accessors;
    typed-array, string and `arguments` receivers; compound updates; sparse
    growth; and the T10-0 `Number::toString` cases.
  * `test/js/js_tune10_ordinary_property_add.js` — attribute defaults,
    inherited setters, inherited non-writable data properties in both modes,
    `preventExtensions`/`seal`/`freeze` including the empty-object case,
    `__proto__`, `Object.prototype` accessors, symbols, Proxy, private fields,
    array and function receivers, a prototype accessor defined *after* the
    instance, and a six-deep ordinary prototype chain.
  Their `.txt` expectations are Node's output verbatim.
* Global-object adds (`globalThis.x = …`, bare assignment, `delete`, descriptor
  attributes, module-binding sync) are byte-identical to v38. They are not in a
  `.txt` fixture because LambdaJS runs a file as a script and Node as a CommonJS
  module, so top-level `var` legitimately differs.
* `test/js/dom_dataset_observer_feedback_bound` unchanged — the dataset write
  still routes through the DOM catalog, not the raw slot.
* Micro-benchmarks are checked in under `test/benchmark/js_micro/` (§6), with a
  README naming what each isolates. `args_fp.js` and `args_ctl.js` must time the
  same; a gap means the observation walk has regressed to reading key positions
  as references.

### 8.5 What is still open

1. **T10-2 items 1–2** — per-site `TypeMap` prediction for object literals and
   constructor `this.x = …` prefixes, plus the inline guard + direct slot for
   named get/set. This is the largest remaining item: it is what stands between
   `new P` at 1.6 µs and QuickJS's 200 ns, and between `p.x` at ~30 ns and
   QuickJS's 19 ns.
2. **T10-4** — `arguments` still costs ~50 µs per call when a function really
   uses it: `js_build_arguments_object` builds an array, a companion map, two
   throwaway descriptor objects (each with four interned attribute writes that
   `js_object_define_property` re-parses), and re-reads
   `Array.prototype.values` from the intrinsic on every call.
3. **The boxed-Number half of T10-1** (§8.2).
4. **§7 points 2–4** are untouched: `S#7.3.32` still has no home, JS_06 §10 and
   JS_15 §2.3 still document retired constructor pre-shaping, and every exit
   still prints `[ERR!] stack cleanup: sigaltstack restore failed`.
