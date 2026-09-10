# JS Tune10 — LambdaJS Fast Paths, Aligned With Untyped Lambda

**Date:** 2026-09-09
**Status:** T10-0, T10-1, T10-3 and T10-4 **LANDED** 2026-09-09, with the D-C kernel-head slice of T10-2 item 3; T10-2 item 1 was attempted and reverted (§9.6); T10-2 item 2 and T10-5 OPEN. See [§8](#8-implementation-status-2026-09-09), [§9](#9-round-2--allocation-and-shape-sharing) and [§10](#10-round-3--t10-4-the-arguments-object).
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
| kostya/primes (†) | 101.8 | 4,610 | 3,720 |
| awfy/sieve | 0.477 | 2,930 | 10.1 |
| larceny/quicksort (†) | 64.8 | 489.5 | 399.6 |
| awfy/havlak | 54,210 | 107,290 | 82,500 |
| awfy/richards | 1,850 | 7,670 | 1,330 |

Tune9 (2026-08-14) diagnosed this correctly (§5.1–5.5) and planned P1 (indexed recovery) and P2 (named recovery). IC_Retire (2026-08-15) initially removed the per-callsite ICs **and the compiler-side indexed lanes**. T10-1 restored the semantic `js_number_key_to_index_fast`, `js_get_number_reference`, and `js_set_number_assignment` fallback seam. The old direct-store imports, `js_elements_set_existing_dense_int_fast` and the append-or-dense pair, still had no lowering or C client and were retired rather than preserved as dormant alternatives; `js_array_fast_own_dense_get/set` remain the shared runtime kernel internals. The follow-up runtime consolidation (2026-09-10) retired the numeric ABI seam as well: computed members now enter the single boxed `js_get_reference` / `js_set` path. This follows **D1.3**, **D8.4.1v2**, and **D8.4.3v2**: one current lowering uses one semantic fallback, while a replaced fast-path ABI is deleted.

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
| text/prettier_ast (†) | JIT 20%, **`js_build_arguments_object` 13.1%**, `js_reflect_define_property` 12.4%, `js_descriptor_from_object` 10.5%, `js_has_own_property` 6.8%, GC 3.3% |

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

### T10-1 Numeric index lane (D-A, compiler half) — the primes/quicksort/sieve/fft recovery — **RETIRED 2026-09-10**

The original proposal emitted a native-number reference lane:

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

This was retired because its compiler and runtime specialization duplicated the generic property semantics. Computed members now box their key once and call `js_get_reference` / `js_set`; array and typed-array specialization remains encapsulated inside those runtime kernels.

### T10-2 Named access: predicted shape + inline guard, kernel on miss (D-C, D-E) — **item 3 LANDED 2026-09-09; items 1-2 LANDED for object literals 2026-09-09 (constructor prefixes and the `set` half still open — §12)**

This is IC_Retire's **Tier A**, already designed there as IR10–IR15 (type identity and guard, `slot_entries[]` as the sole ordinal index, where `type_index` comes from, subtype admission by id ranges). Under LC1v2 it is no longer an optional follow-up but the only sanctioned route. It introduces **no per-site mutable state** and no MIR-cache-key churn:

1. **Per-site shape prediction.** Object literals get a per-site `TypeMap` exactly as Lambda literals do. Constructors get a predicted `TypeMap` from their `this.x = …` prefix (the retired pre-shaping, re-done as immutable compile-time metadata rather than a runtime cache). `new C()` allocates the instance with the predicted shape and reserved slots (`ctor_reserved_mask` already exists for Lambda), so constructor stores are **existing-slot writes**, not adds.
2. **Inline guard, direct slot.** Named get/set on a receiver whose static type carries a predicted shape emits the T20-1c guard sequence (tag, kind byte, `TypeMap*` compare, `ctor_reserved` bit, flags == 0) and a direct slot load/store with the lane-widening rules from `js_named_fast_store_can_write_same_slot`. Miss → `js_get_name_id` / `js_set_name_id` unchanged.
3. **Cheapen the kernel head.** Even for unpredicted receivers, `js_get_name_id` must not hash strings: make the dataset-view marker a `map_kind`/`js_meta` bit (it is a property of the object's class, not of its shape contents), take the host hook only when the receiver is the realm's `globalThis` (one pointer compare), and pass the `NameRef` and its cached `hash`/`name_id` from the compile-time constant instead of re-resolving.

Target: `p.x` ≤ 5 ns on a hit, `{x,y}` literal ≤ 100 ns, `new P` ≤ 150 ns; havlak/cd/deltablue/richards halve or better.

### T10-3 Property add without descriptors (D-B) — **LANDED 2026-09-09**

`OrdinarySet` on an ordinary extensible receiver with no accessor/Proxy/exotic in its prototype chain must reach an `add_property` kernel: shape transition (or the predicted shape's next slot) plus one write. The descriptor-object path stays for `Object.defineProperty`, accessors, non-writable/non-configurable, Proxy and exotic receivers. `js_intrinsic_note_property_mutation` runs only when the receiver is itself an intrinsic prototype root (a flag on `js_meta`), not on every store. This is the part of D-B that T10-2 does not remove (dynamic objects, dictionary-mode maps, JSON parsing).

### T10-4 `arguments` object (D-D, cost half) — **LANDED 2026-09-09**

Build it lazily on first observation or as a cheap fixed-layout iterator-style carrier (JS_08 pattern) instead of an array + companion map + descriptor object + two symbol installs per call. Sloppy-mode mapped `arguments` keeps its aliasing semantics through the existing `js_arguments_mapped_get`.

### T10-5 Residual (measure first) — **`js_object_meta` slice LANDED 2026-09-09**

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
| T10-1 (D-A, compiler half) | Retired 2026-09-10. Computed members now box the key once and use the canonical `js_get_reference` / `js_set` property kernels. The numeric ABI wrappers and their import metadata were removed, while array and typed-array handling remains internal to the semantic kernels. | `lambda/js/js_mir_expression_lowering.cpp`, `js_props.cpp` |
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
| awfy/sieve2 (†) | 9.98 | **1.40** | 7.1× |
| awfy/cd2 | 25 496 | **4551** | 5.6× |
| text/prettier_ast | 27 062 | **6321** | 4.3× |
| awfy/havlak2 (†) | 80 920 | **20 467** | 4.0× |
| larceny/levenshtein (†) | 437.6 | **115.6** | 3.8× |
| awfy/deltablue2 | 1418.4 | **530.4** | 2.7× |
| larceny/quicksort | 398.0 | **148.8** | 2.7× |
| r7rs/fft2 (†) | 53.4 | **23.2** | 2.3× |
| larceny/puzzle (†) | 108.9 | **47.8** | 2.3× |
| awfy/bounce2, larceny/array1, awfy/json2 | 6.97, 78.3, 90.6 | 3.22, 38.1, 44.9 | 2.0–2.2× |
| larceny/brainfuck, matmul, paraffins (†) | 3656.9, 1282.9, 4.08 | 1920.2, 681.8, 2.36 | 1.7–1.9× |
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

1. **T10-2 items 1–2** — **superseded by §12.** The object-literal half of both
   landed on 2026-09-09; what remains is the constructor `this.x = …` prefix,
   the `set` half of the guard, and inlining the guard sequence itself.
2. **T10-4** — `arguments` still costs ~50 µs per call when a function really
   uses it: `js_build_arguments_object` builds an array, a companion map, two
   throwaway descriptor objects (each with four interned attribute writes that
   `js_object_define_property` re-parses), and re-reads
   `Array.prototype.values` from the intrinsic on every call.
3. **The boxed-Number half of T10-1** (§8.2).
4. **§7 points 2–4** are untouched: `S#7.3.32` still has no home, JS_06 §10 and
   JS_15 §2.3 still document retired constructor pre-shaping, and every exit
   still prints `[ERR!] stack cleanup: sigaltstack restore failed`.

---

## 9. Round 2 — allocation and shape sharing

Same day, same control (`lambda-v38-b23793e832`). Every measurement in this
section is an **interleaved** A/B — the two binaries run alternately on the same
workload — because the machine was carrying an unrelated test suite at load
average 20–50 throughout. Absolute times here are therefore not comparable with
§8.3; the ratios are, and they were stable to ±2% across three interleaved
repeats.

### 9.1 What the profile said, before designing (rule 2)

After §8's landing, `new P(x,y)` and `{x, y}` no longer looked anything like
§2.4. Neither of the two costs that dominated them is in §5's list:

| Row | Finding |
|---|---|
| `{x, y}` literal | `js_object_is_extensible` **9.4%** — `js_create_data_property`'s own fast path asked "is this object extensible" by interning three C strings (`js_name_item`, a further 4.9%) and doing three `map_get`s, per literal |
| `new P(x,y)` | `js_constructor_create_object` 33.8%, of which `js_set_prototype` **19.8%**: a cycle walk over the prototype chain plus `js_intrinsic_note_property_mutation`, which scans every `JS_CLASS` prototype root and the constructor cache |
| both | `map_put_with_data_growth` + `alloc_shape_entry` + `name_pool_create_strview` ≈ 10–15%: **every instance grew its own ShapeEntry chain** |

### 9.2 T10-2a — allocation plumbing

* **One extensibility probe.** `preventExtensions`/`seal`/`freeze` are ordinary
  own marker properties, so IsExtensible is the same three probes on every
  receiver kind — and there were four near-identical copies of them
  (array, function, error, map), the map one uniquely paying to intern the
  names. They now share one marker table and one shape-storage probe.
* **A fresh-object prototype link.** `OrdinaryCreateFromConstructor` links the
  `[[Prototype]]` of an instance allocated moments earlier and not yet reachable
  from anywhere. For such a receiver the cycle walk is vacuous (a fresh object
  is in no chain) and so is the intrinsic-mutation notice (it looks for an Item
  that was just minted). `js_set_prototype_fresh` skips both; the public
  `Object.setPrototypeOf` path is untouched and still rejects cycles.
* **The add kernel moved up.** §8's ordinary-add kernel sat in
  `js_set_completion_with_key`, three frames below `js_set_name_id` — frames
  that re-derive facts the caller already had. It is now offered directly on
  `js_set_name_id`'s miss path. The kernel proves every premise itself, so it is
  offered on *every* miss rather than only `NAMED_FAST_NO_ENTRY`; that matters
  because an object with no properties yet has `data == NULL` and misses as
  `NO_RECEIVER`.

### 9.3 T10-2 item 1 — shape sharing, and why it was not working

§5 item 1 asks for per-site predicted shapes. The owner chose the
**add-transition** form on 2026-09-09: `shape S + add(name, valueType) -> S'`,
memoized on `S`. This is shape metadata keyed on immutable inputs and attached
to a shape rather than a call site, so D8.4.1v2/LC1v2 hold; it also needs no
compiler plumbing and covers literals, constructors and every incremental add
alike.

**That table already existed** — `map_transition_target_for_add` in
`lambda/input/input.cpp` — and was dead for every runtime-created property, for
two independent reasons:

1. **The lookup could not match a pooled name.** It compared `tr->name` only,
   but the record side sets `tr->name` **only when `added->name_id ==
   NAME_ID_NONE`** — i.e. exclusively for the id-less Input seam. Every
   JS- or Lambda-created property carries a `NameId`, so `tr->name` was NULL and
   no comparison could ever succeed. Fixed by matching on `name_id` + `key_kind`
   first (D4.6.1v2: identity is the `NameId`), keeping the byte arm for id-less
   Input names and refusing to let a pooled key match through it.
2. **The graph had no shared root.** The transition arm is guarded by
   `typemap_is_shared_shape(map_type)`, but the *first* add on a fresh object
   takes the `map_type == &EmptyMap` branch, which mints a TypeMap that is
   neither private nor shared — so the object never entered the graph, and
   nothing downstream could put it back.

Only fix 1 shipped. **The shared root was attempted and reverted**; §9.6 records
what it cost and why.

The scan is bounded at 16 outgoing edges per shape: beyond that a site is
dictionary-shaped (parsed records, per-row keys) and cannot pay for a linear
walk on every add, so it keeps its private shape exactly as before.

**A cost inside the table that turned out not to matter — recorded because the
first analysis of it was wrong.** Each transition hit re-validates the child by
comparing its whole cloned prefix against the parent: O(depth) per add, so
building an n-field object is O(n^2) in that check. A loaded single-pair sweep
showed the object-shaped rows at a *smaller* speed-up than the previous one
(storage2 1.33x -> 1.15x, towers2 1.28x -> 1.15x, richards2 1.24x -> 1.12x), and
this walk was the obvious suspect. It was not the cause.

An O(1) replacement for shared parents — one the clone-before-mutate protocol
forbids anyone to change in place, so `length` + `byte_size` confirm it — was
implemented and A/B'd directly against the full walk, min-of-5 per width on a
synthetic object-builder (`temp/jstune10/scale.js`, constant total field count,
widths 8..256):

| fields | full walk | O(1) check | saved |
|---:|---:|---:|---:|
| 8 | 518.8 ns | 511.2 ns | 1.5% |
| 32 | 637.1 | 621.8 | 2.4% |
| 128 | 1133.0 | 1098.6 | 3.0% |
| 256 | 1739.5 | 1686.1 | 3.1% |

and on the benchmark rows it was worth nothing at all: storage2/towers2/
richards2 measured 1.32x/1.28x/1.25x **without** it and 1.29x/1.30x/1.24x with
it. The apparent dip was measurement noise (§9.5). Note also that per-field cost
grows with width identically with and without the check (511 -> 1686 vs
519 -> 1740), so the O(depth) walk is not what makes wide objects cost more per
field — that is elsewhere (data-block growth and cache).

The O(1) check was therefore **reverted** under `JS_Tune_History` rule 5: it is
performance-neutral on every real row and would have traded a fully-verifying
structural comparison for a weaker one. A wide object built from many distinct
adds also exceeds the 16-edge bound and leaves the graph, so the walk never runs
deep in practice.

A branded map (`js_meta`), an array-index face, an identity key (Symbol,
private name) or a non-`MAP_KIND_PLAIN` receiver stays on the private path:
those are all part of shape identity and the root does not carry them.

### 9.4 Measured (ratio to v38)

Every number below is **min-of-N alternating runs** on a build whose release
banner was verified immediately beforehand. Min is the least load-perturbed
sample, which is what made these usable while an unrelated suite came and went
on the same machine. The scalar rows are the control: they contain no property
or element access, so anything other than ~1.00× on them means the measurement
is wrong, not the code.

| Row | after §8 | after §9 |
|---|---:|---:|
| **fib2, sum2, tak2** (scalar controls) | — | **0.98×, 1.00×, 0.99×** |
| kostya/primes | 27.1× | 26.4× |
| awfy/sieve2 | 7.1× | 7.5× |
| awfy/cd2 | 5.60× | **5.93×** |
| text/prettier_ast | 4.28× | **4.75×** |
| awfy/havlak2 | 3.95× | **4.02×** |
| larceny/levenshtein | 3.84× | 3.84× |
| awfy/deltablue2 | 2.67× | **2.73×** |
| larceny/quicksort | 2.67× | 2.64× |
| larceny/deriv (†) | 1.07× | **2.42×** |
| r7rs/fft2 | 2.30× | 2.25× |
| larceny/puzzle | 2.28× | 2.21× |
| **larceny/gcbench** | 1.02× | **2.19×** |
| awfy/bounce2 (†), awfy/json2, larceny/array1 (†) | 2.16×, 2.01×, 2.06× | 2.21×, 2.11×, 2.07× |
| larceny/brainfuck, matmul, paraffins | 1.90×, 1.88×, 1.73× | 1.92×, 1.87×, 1.73× |
| awfy/list2, nbody2, queens2, triangl, json_gen, permute2 (†) | 1.57–1.44× | 1.57×, 1.46×, 1.48×, 1.44×, 1.34×, 1.32× |
| awfy/storage2, towers2, richards2 (†) | 1.33×, 1.28×, 1.24× | 1.28×, 1.29×, 1.25× |

Micro-benchmarks, interleaved (†): `new P(i,1)` **9.6×**, `{x:i, y:1}` literal
**2.6×**, `p.x + p.y` 1.7×.

Correctness across the 78-row standalone-JS sweep: every row's output is
identical to v38. The five `exit 1/1` rows fail identically on both binaries,
and the one nominal `DIFF` (`run_jetstream_node.js`) is a column-alignment
difference caused by the embedded timing number's width — all nine of its
sub-benchmarks report OK on both.

**gcbench (1.02× → 2.2×) and deriv (1.07× → 2.42×)** are allocation-shaped rows
that no earlier phase touched. Their gain is the **extensibility probe** —
`js_create_data_property` was interning three marker names per property — not
shape sharing, which never ran (§9.6). The shipped cd2/deltablue2/json2 figures
are 5.93×/2.73×/2.11×.

### 9.6 The shared transition root: attempted, reverted

Fix 2 above — a per-`Input` root so an object joins the transition graph at its
first property — was built, measured and then **removed**. It is recorded here
because both of its defects are instructive and because it cost the owner two
machine hangs.

**It was dead on arrival.** The entry gate read `if (!js_meta && ...)`, but the
blueprint there is `&EmptyMap`, whose `js_meta` is set to `JS_CLASS_OBJECT` by
`js_object_metadata_initialize` before any JS object exists. The gate was false
for every JS object, so the path never ran — and the round-2 gcbench/deriv gains
attributed to shape sharing were actually the extensibility probe's.

**Defect A — an uninitialized field.** `Input` is allocated with `pool_alloc`,
not `pool_calloc`, so *every* field must be assigned in
`Input::create_with_name_parent`. The new `shape_transition_root` was not, so it
held pool garbage. Dereferencing it segfaulted (`EXC_BAD_ACCESS` at
`input.cpp:537`), and a garbage `TypeMap::byte_size` read through it is a
plausible source of the ~150 GB allocation that hung the owner's machine twice.
Small tests missed it because a fresh pool is often already zeroed; it bit only
in long-lived processes with reused pools.

**Defect B — unsound even once initialized.** With the field fixed,
`test_js_gtest` still failed `lib_marked` and `lib_handlebars` and peaked at
**4401 MB** (one `lambda.exe` at 3988 MB) against **834 MB** with the root
disabled. Both scripts pass standalone, so the fault is accumulated cross-object
state: sharing one TypeMap across every ordinary map in a long-lived realm
breaks an assumption something downstream still makes. That was not chased
further — the tree was left green instead.

**Kept from the same work:** the `name_id` lookup arm (fix 1), which makes the
pre-existing shared-shape branch function, and the 16-edge scan bound. Those two
were gated clean at 40261/40261 and 5086/5086.

### 9.7 Defect B, diagnosed

Bisected 2026-09-09. It is **not** a batch-accumulation effect and none of the
three suspects above was involved.

**Repro — six lines, and it needs the AST interpreter:**

```js
class C { f; constructor() { this.f = []; this.f.x = 1; } }
new C();
```

`JS_EXECUTION_BACKEND=ast` throws `Object.set called on non-object`;
`JS_EXECUTION_BACKEND=mir` and Node are both correct. The `test_js_gtest`
failures were `lib_marked` and `lib_handlebars`, which run under both backends;
both scripts pass when run directly, which is why the harness looked like the
variable. Of every script in `test/js`, only those two are root-caused — the
rest of the AST-backend failures fail on v38 as well.

**The trigger is narrower than "shared shapes".** Variants:

| | result |
|---|---|
| `f;` declared, then `this.f = []` | **throws** |
| `f;` declared, then `this.f = {}` | **throws** |
| `f;` declared, then `this.f = 5` | **silently `undefined`** |
| no declaration, `this.f = []` | ok |
| `f = []` declared *with* initializer | ok |
| plain object, `o.f = undefined; o.f = []` | ok |

A class field declared **without** an initializer installs `undefined`, whose
storage width is **1 byte**. Assigning a wider value then needs a width-changing
retag. The instrumented path says exactly that:

```
retag entry=f storage_t=2 (UNDEFINED, width 1) value_t=18 (ARRAY, width 8)
      shared=1  fixed=0
```

**Mechanism.** `fn_map_set` -> `map_detach_shared_ctor_shape_for_type` ->
`js_typemap_transition_for_type`. That function returns `source` untouched when
the shape is **not** shared:

```c
if (!source || !typemap_is_shared_shape(source) || ...) return source;
```

so before the root existed, an instance shape was private, this returned early,
and `fn_map_set`'s own in-place rebuild widened the field. With the root, the
shape is shared, so it proceeds — and the width guard

```c
if (!typemap_entry_uses_fixed_slot(source, entry) &&
        type_info[storage_type].byte_size != type_info[value_type].byte_size)
    return NULL;   // "make the caller rebuild"
```

returns NULL. The caller's fallback is `js_typemap_clone_for_mutation_pub`,
which **detaches the shape but does not widen the field**. The clone still
describes `f` as a 1-byte `undefined`, the 8-byte write is dropped, and the next
read yields `undefined`. The rebuild the guard asks for is only reachable when
the source shape is private.

### 9.8 Defect B, fixed

The diagnosis above stopped one step short. Instrumenting the detach gave the
real answer:

```
detach  src=0x…9350 len=2  transition=0x0  entry=0x…95b0
clone   len=2  refreshed=0x0  inchain=0  id_path=1
rebuild fields=2  changed_entry_in_chain=0
```

`map_detach_shared_ctor_shape_for_type` clones the shape correctly — the clone
has both fields — and then **fails to find the field in its own clone**
(`refreshed=0x0`). It looked up by **NameId only** when the *operation's* key is
pooled, but the `ShapeEntry` had been created from an unpooled key and so
carries `NAME_ID_NONE`. Having missed, it returned the **pre-detach** entry.
`map_rebuild_for_type_change` matches the changed field by pointer, cannot find
that entry in the chain it is rebuilding, and silently skips the retype — so the
8-byte write lands in a 1-byte `undefined` slot and the field reads back
`undefined`.

The fix is the two-probe rule used everywhere else (`js_named_fast_lookup`,
`js_ordinary_shape_entry_for`): **identity first, byte-confirmed seam second**,
extracted into `map_resolve_entry_in_shape` and used by both lookups in that
function. And the fallback that returned the stale entry now returns NULL and
logs, so an unresolvable field becomes an error instead of a wrong answer.

This is a latent bug in its own right, independent of the root: any shared shape
holding an id-less entry loses a width-changing store the same way.

With it fixed, every variant in the table above is correct (`this.f = 5` yields
`5`), `lib_marked` and `lib_handlebars` pass under both backends,
`test_js_gtest` is **379/379 at 754 MB** (was 2 failures at 4401 MB), and
test262 40261/40261 + Lambda baseline 5087/5087 pass with the root live.

### 9.9 The root, re-enabled — and what actually makes it worth keeping

With Defect B fixed the root is **kept**. Its case is **memory**, not speed:
sharing one shape between objects built the same way replaces one ShapeEntry
chain per object with one chain per *shape*.

#### Sizing the graph — two bounds, not one

Getting the win required fixing a bound that had quietly neutered the feature.
The original 16-edge cap was **per shape**, and the root is every plain map's
first-property node: its out-degree is the number of distinct first properties
in the *whole program*. Capped at 16 it saturated after ~835 puts, and every map
after that fell back to a private shape. Instrumented on a 6000-object run:
835 hits, then 6000+ consecutive misses.

Raising the root's budget to 256 (interior shapes keep 16) fixed that — and
immediately exposed the opposite failure: an edge cap bounds one shape's
fan-out, not the graph. A process running thousands of unrelated scripts through
one `Input` — the test262 batch runner is the extreme — kept minting shapes for
its whole life and reached **5.4 GB**. So there is now also a **graph budget**:
`MAX_SHAPE_GRAPH = 1024` shapes per `Input`, after which every map keeps its
private shape, exactly as before the graph existed. test262's peak went
5662 MB -> **891 MB**, below even the starved-16 run's 922 MB.

#### Memory (max RSS, min-of-3, release build asserted)

Objects of *n* fields built in a loop; `root_off` is the same source with only
the root disabled, so the column isolates the root from everything else in
rounds 1-4.

| workload | v38 | root_off | root_on | root saves |
|---|---:|---:|---:|---:|
| 1 500 objects x 30 fields, one shape | 99 MB | 46 | **37** | **20%** |
| 6 000 objects x 30 fields, one shape | 293 | 86 | **50** | **42%** |
| 20 000 objects x 30 fields, one shape | 932 | 180 | **62** | **66%** |
| 6 000 objects, diverging last field | 294 | 88 | 82 | 7% |
| 6 000 objects, all keys distinct | 337 | 126 | 129 | −2% |

The saving **grows with object count** — 20% -> 42% -> 66% — because the shape
cost becomes per-shape instead of per-object. That is the whole argument for
keeping it: an application holding many similar objects (a DOM, a parse tree, a
record set) pays for its shapes once.

It is also honest about the other direction: objects whose key sets genuinely
diverge share nothing, drop out at the bounds, and land within noise of the
private path. The mechanism costs nothing where it cannot help.

End to end against the measured v38 snapshot, 20 000 objects go
**932 MB -> 62 MB (15x)**; 180 -> 62 of that is the root, the rest is rounds 1-4.

#### Speed, secondary but no longer mixed

| Row | ratio | | Row | ratio |
|---|---:|---|---|---:|
| larceny/gcbench | **1.72x** | | awfy/richards2 | 1.00x |
| awfy/cd2 | **1.13x** | | r7rs/sum2 | 0.99x |
| awfy/deltablue2 | **1.08x** | | r7rs/mbrot2, fib2 | 1.00x |
| awfy/json2 | 1.05x | | | |

An earlier measurement of the root showed a broad 2-8% tax on scalar rows
(mbrot2 0.92x, sum2 0.94x) and a neutral 1.0015 geomean. That was the starved
16-edge bound: every object was paying the failed-transition probe and then the
private path anyway. With the bounds right the tax is gone — every scalar
control is 0.99-1.00x — and gcbench went from 0.97x to **1.72x**.

Gates with the root live: test262 40261/40261 with 0 regressions (peak 891 MB),
Lambda baseline 5087/5087 (peak 1708 MB), `test_js_gtest` 379/379 at 754 MB
(it was 2 failures at 4401 MB before the fix).

---

## 10. Round 3 — T10-4, the `arguments` object

`js_build_arguments_object` runs on every call of a function that genuinely uses
`arguments`. It cost **76.5 us per call** on v38; §6's gate asked for <=5 us.

### 10.1 What it was doing

Four of its own-property installs went the long way round. `length` and
`callee` each built a throwaway descriptor **object**
(`{value, writable: true, enumerable: false, configurable: true}`), set its
prototype to null, wrote four interned attribute names into it, and handed it to
`Object.defineProperty`, which parsed it straight back out.
`Symbol.toStringTag` and `Symbol.iterator` used `js_set_key_default` — an
OrdinarySet — which for a brand-new own property lands in
`Reflect.defineProperty` and materializes a descriptor there instead
(`js_reflect_define_property` was **24%** of the build).

All four are fresh own properties on a companion object nobody has seen yet, so
a storage define plus a non-enumerable mark **is** the whole operation — the
spelling already used two lines away for those same symbol keys'
`js_mark_non_enumerable` follow-ups. The T10-3 add kernel cannot shortcut the
symbol pair on its own, because it declines identity keys by construction.

### 10.2 Measured

| | v38 | after |
|---|---:|---:|
| `f(a,b)` using `arguments.length` + indices, 200k calls | 76.5 us/call | **2.42 us/call** |

**31.6x**, stable across three interleaved repeats, inside the <=5 us/call gate.
Removing the two descriptor objects alone reached 12.2 us/call; the symbol pair
was the other two thirds.

### 10.3 A pre-existing bug this surfaced, and the real fix

The internal `__strict_arguments__` marker was an **enumerable** own property,
so it appeared in `Object.keys(arguments)` for any strict callee. Eight sites in
`js_assert.cpp` and `js_util.cpp` filtered it out by name — workarounds for
exactly that.

Making it non-enumerable was not enough: own-key introspection includes
non-enumerable properties, so `Object.getOwnPropertyNames` still listed it (v38
did too). Strictness is engine bookkeeping and now rides a **container header
bit** — `Container::reserved_state` bit 0, via
`container_is_strict_arguments()` / `container_set_strict_arguments()` — on the
arguments array itself, the same container whose `is_content` bit already means
"this is an Arguments object". Both `callee`/`caller` guards test the bit
instead of a 20-byte shape lookup, and **all eight filters are gone**.
`Object.getOwnPropertyNames` on a strict arguments object is now
`0,1,length,callee`, matching Node.

Rejected on the way: a new `map_kind` would have to coexist with
`MAP_KIND_ARRAY_PROPS` *and* `MAP_KIND_ARRAY_SPARSE` on the companion; a
`JS_CLASS_ARGUMENTS` brand on the companion TypeMap would change `js_class_id`
for every arguments object and still could not separate strict from sloppy
without two classes.

**A landmine found next door.** `Container::array_flags` declared
`uint8_t array_flag_reserved:3`, but `JS_ELEMENTS_STATE_MASK` is `0xe0` —
exactly those three bits, holding `JsElementsKind`. Taking a "reserved" bit
there would have silently corrupted the elements state. Renamed
`array_flags_js_elements_kind`, with a comment pointing at `reserved_state` as
the byte that actually has room.

`test/js/js_tune10_arguments_object.js` is checked in and byte-identical to
Node, and asserts that no internal-looking own key survives on either strict or
sloppy arguments. One deviation is deliberately **not** covered because it
predates this work and remains: after `arguments.length = 5`,
`Array.prototype.slice.call(arguments)` returns 3 elements rather than 5.

### 10.4 Gating a change to `assert` / `util.inspect`

`make node-baseline` is a progress tracker, not a pass/fail gate — it exits
non-zero in its normal state — and its tests are **flaky**: three consecutive
runs of the *same* binary over `--modules=assert,util` failed 9, 12 and 10
tests. A single before/after proves nothing; an early comparison here showed
"4 new failures" that were pure noise.

What works: run the affected modules three times on each binary and compare the
**intersections**. Even that is not proof — the one newcomer it reported,
`test-util-internal.js`, needs `--expose-internals` and `internalBinding('util')`
which this runtime does not implement, and fails identically on v38. Confirm any
newcomer against the archived binary directly.

---

## 11. Round 4 — `js_object_meta` and the prototype key hash

### 11.1 `js_object_meta`

§5 T10-5 lists "`js_object_meta` on every access" as residual, to be measured
first. After rounds 1-3 it was the largest single contained cost left in cd2, at
**10.9%** of the row.

Every named access reaches it through
`js_named_fast_receiver_map_is_fast` -> `js_object_uses_ordinary_shape`, and the
overwhelmingly common receiver — an ordinary Map — was answered **last**. Before
the Map arm the function walked an exotic-type ladder whose error-carrier rung,
`value.map->type == js_error_carrier_type_map()`, is a **function call per
property access**; a Map with no `js_meta` then fell through four more
`ARRAY_NUM`/`ARRAY`/`FUNC` rungs it can never match.

The Map case now answers first and returns. The error-carrier test is a direct
address comparison against the file-static carrier — exact, and needing no lazy
initialization, because a Map can only carry that type after
`js_error_carrier_type_map()` produced it.

### 11.2 The prototype key hash

`js_get_prototype` then became the largest single item at **6.4%** of cd2, all
of it self time. `JS_INTERNAL_PROTO_KEY` is the compile-time constant
`"__internal_proto__"`, and its 18 bytes were re-hashed on **every prototype
hop**. `js_proto_shape_entry` now takes the hash as a parameter, computed once.
Its entry cache only engages for shared shapes, so the uncached lookup was the
live path for essentially every hop. No behavioural change:
`typemap_hash_lookup_by_hash` is what `typemap_hash_lookup` already called, and
it treats a zero hash as "compute it yourself", so a racing initializer is safe
(the value is deterministic).

### 11.3 Measured (min-of-3, release banner asserted)

| Row | after §10 | after 11.1 | after 11.2 |
|---|---:|---:|---:|
| **fib2** (scalar control) | — | 0.97x | **1.00x** |
| awfy/cd2 | 5.93x | 6.25x | **6.57x** |
| awfy/deltablue2 | 2.73x | 2.86x | **2.97x** |
| awfy/json2 | 2.11x | 2.19x | **2.25x** |
| awfy/richards2 | 1.25x | 1.29x | **1.37x** |

Gates for each: test262 40261/40261, 0 regressions; Lambda baseline 5087/5087.

### 11.4 What is left, and what it would cost

cd2's remaining named-path costs sit in a 3-5% band and none is as cheap as
these were:

* `js_intrinsic_note_property_mutation` **4.6%**, the O(`JS_CLASS__COUNT`) scan
  of prototype roots on every property mutation. T10-3's text proposes gating it
  on "a flag on `js_meta`", but `js_meta` is shared per class, so an intrinsic
  prototype and an ordinary instance of that class are indistinguishable through
  it. A container bit would work but needs marking at three independent points —
  the prototype-root registration, the `__is_proto__` application, and the
  constructor cache, whose entries are `LMD_TYPE_FUNC` and have no container
  header — and a missed mark leaves a stale intrinsic epoch, i.e. silently wrong
  results. Not attempted on a 4% budget;
* the T10-3 kernel's own three extensibility probes, inside
  `js_map_shape_lookup` **4.5%**. The obvious fix — a "has an integrity marker"
  bit set at the single `js_defprop_set_internal_state` choke point — would also
  change behaviour for an object that merely *has* a user property called
  `__frozen__`, which today makes it report non-extensible. Arguably a bug fix,
  but not a free one;
* `js_object_meta` is still 6.2% after 11.1, but now spread across many callers
  rather than one avoidable ladder.


---

## 12. Round 5 — T10-2 items 1 and 2, for object literals

Same control as every round since §8: the archived `lambda-v38-b23793e832`.

### 12.1 Item 2 could not be built on item 2's own terms

§5 item 2 says the guard fires "on a receiver whose static type carries a
predicted shape". No such static type existed: JS object literals lowered to
`js_new_object()` plus a sequence of adds, so there was no compile-time constant
for a `TypeMap*` compare to name. That is D-E, and it had to be built first.

Lambda's own T20-1a/1c is the precedent and it is instructive: its constant
comes from `transpile_map` writing a per-site `TypeMap*` into the header, and
its candidate comes from `mir_expr_candidate_shape` tracing an inference edge —
a *candidate*, never a proof, with the guard supplying the proof at runtime.
This round is the same shape of thing, one indirection further out because the
JS shape does not exist until the realm does.

### 12.2 How a JS literal names a shape it cannot see at compile time

A site's shape is named by a **contiguous range of the active module's
property-key table**: `(first_key_index, key_count)`, two integers baked as
immediates. The realm is resolved through active module state exactly as
`lambda_active_module_name_id` resolves a single name, so no realm pointer
enters shared MIR (D5.4.3, D5.4.4) and IR10's rule is satisfied without a new
module image section — the key table already transports everything a shape needs.

`jm_module_name_append` (not `_index`) reserves the range, because it preserves
duplicate spellings and therefore keeps the entries contiguous.

**Every slot is NULL-typed at pointer width.** That is what makes the shape
knowable without knowing value types, and it is the recipe the retired
pre-shaping used verbatim: *"correct 8-byte spacing so INT, FLOAT, STRING, MAP,
FUNC etc. all fit in-place via the NULL→same-byte-size fast path"*.

Admission is narrow: plain, statically named, pairwise-distinct data properties,
at most 16. Computed keys, spreads, accessors, methods, `__proto__` and
duplicate keys all keep the generic path — each either makes the own-slot set
dynamic or creates no data slot.

### 12.3 The guard

`js_shaped_slot_get(obj, first, count, slot, name_id)` guards tag →
`MAP_KIND_PLAIN` → `TypeMap*` compare → bounds → deleted sentinel, then reads
the slot; any miss falls through to `js_get_name_id`. Constants only, no code is
ever rewritten: a guard chain, not an inline cache (D8.4.1v2, LC1v2).

Shape identity subsumes most of what the kernel re-checks per access — every
descriptor, accessor, delete, freeze and extension path clones the `TypeMap`
before mutating it, so an instance that took any of them fails the compare. The
sentinel test stays because a sentinel can be written without cloning.

The candidate comes from the binding's own declarator through
`NameEntry::node` (D8.2.4: the resolved binding, never a re-resolved spelling).
An earlier version carried it as a flow fact on `VarEntry` and it was silently
dropped: scope maps rebuild those entries field by field, copying only the
fields they name. Reading the declarator removed both the failure mode and about
forty lines.

Per IR14 this is the **helper call**, not yet the inlined sequence. The operands
are already the ones an inline expansion would use.

### 12.4 The in-place retag, without which the guard is dead

Instances were detaching from their predicted shape *during their own
construction*: `js_create_data_property`'s fast path is gated on `!key_exists`,
and a predicted slot exists from allocation, so every literal write fell to the
descriptor path — the exact D-B cost T10-3 removed.

An existing but still-unwritten slot is an **initialization**, not a
redefinition. Two admissible cases and no third:

* a **NULL slot** retags to the value's lane — the direction
  `shape_entry_retag_is_safe` sanctions, since only T→NULL is refused on a
  shared shape (that one would make the collector skip a sibling's live pointer);
* an **already-typed slot** is admitted only when the value needs *no* retag.
  The shape is shared by every instance of the site, so retagging INT→STRING
  would have a sibling's integer read back as a pointer.

Everything else falls through and detaches. The invariant this buys: **a
predicted slot goes NULL→T exactly once and never changes again.**

The store reuses the existing same-size helper;
`js_array_companion_write_same_size_slot` was renamed
`js_shape_write_same_size_slot` because nothing in its body was array-specific.

### 12.5 Measured (min-of-3, release banner asserted, vs v38)

| micro | v38 | after | |
|---|---:|---:|---:|
| `{x:i, y:1}` literal + read, 500k | 541 ms | **68** | **7.96×** |
| `p.x + p.y`, 4M | 408 | **187** | **2.18×** |
| **scalar control** | 134 | 133 | **1.00×** |

The guard's own contribution, isolated: 238 → 191 ms on named reads (1.25×).
The rest is the shaped allocation.

### 12.6 Three defects this round produced, and what each cost to find

1. **A silent wrong answer, caught by the new fixture on its first run.**
   `delete d.k1; d.k1 = 9` returned the *stale* value with the right key
   present. Cause: reserving every slot via `ctor_reserved_mask` pushed the
   literal's own stores off the named fast path. A literal writes every slot
   before the object is reachable, so there is no window to protect and the mask
   is simply wrong here — a constructor prefix, where the window is real, will
   still need it.
2. **A 9.7× regression, invisible to its own A/B.** Admitting only the NULL case
   in §12.4 meant just the first instance took the fast path; every later one hit
   `key_exists` and landed in the descriptor path — 5237 ms against v38's 542.
   The guard-on/guard-off A/B showed nothing because *both sides had it*. Only
   the archived-binary control exposed it. Third time in this work that the
   choice of control decided whether a regression was visible.
3. **A dead guard.** Poisoning the hit path changed nothing, exactly as the
   starved shape root did in §9.6. Liveness is now checked by poisoning, never
   inferred from a passing test.

### 12.7 What is left in T10-2

* **Constructor `this.x = …` prefixes** — item 1's other half, and IR12's anchor
  case. Needs the reserved mask that literals do not, because the window between
  allocation and the field writes is real there.
* **The `set` half of the guard.** Only `get` is guarded today.
* **Inlining the guard sequence** (IR14's endgame). The helper stays as the
  out-of-line miss body.
* **Widening the candidate.** Only a receiver traced to its own declarator
  qualifies; parameters and returns publish nothing, which is where Lambda's
  T20-1b module-unique-shape fallback would map across.

### 12.8 Gates

test262 **40261/40261, 0 regressions**, peak 889 MB. `test_js_gtest` **381/381**.
The `js_tune10_predicted_shape` fixture is Node-exact and pins property order,
`delete`/re-add, per-site type polymorphism, descriptors, freeze, and the
shared-shape cross-retag invariant of §12.4.
