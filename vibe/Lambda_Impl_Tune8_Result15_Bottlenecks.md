# Result15 LambdaJS Slow-Row Bottleneck Analysis

- **Date:** 2026-07-27
- **Input:** `test/benchmark/Overall_Result15.md` (commit `7703c784c`, LambdaJS/Node dedup geo 14.4x). Top-5 LJS rows analyzed: hashmap 1131x, havlak 929x, cd 261x, navier_stokes 133x, spectralnorm 111x.
- **Binary:** release `lambda.exe` built 02:36 (commit `7703c784c` + the in-flight working-tree double-bits fix). All five profiles predate the 03:00 debug rebuild done by a parallel session; none of the five shows debug-only frames (`js_debug_assert_exception_clear`, `gc_no_gc_scope_*`), confirming release. A dense-store micro test accidentally ran on the debug binary and was discarded.
- **Method:** macOS `sample` (1 ms) leaf-merge on the worker thread (Result13 protocol), plus inclusive-subtree shares for runtime entry points called from JIT frames, plus `nm -n` bisection for stripped statics, plus `LAMBDA_GC_STATS=1` for hashmap. Attribution instrument, ±5 points. hashmap/havlak sampled 15 s mid-run; cd 6 s; navier 3 s; spectralnorm scaled ×20 (driver loop 10→200) and sampled 4 s.
- **Caveat:** the `LAMBDA_GC_STATS` hashmap run's wall time (124.8 s vs 64.1 s reported) is ~2x contaminated — the parallel session's debug build saturated the machine mid-run. Structural counters (collection count, relative mark share) remain valid.

The headline: the same five rows as Result13, but the profiles now name **mechanisms**, not just clusters. Three of the five bottlenecks turn out to be four specific code paths, each individually fixable: an O(n²) GC mark-time ownership test, eager numeric-key stringification (with a brute-force shortest-double converter), spec-descriptor materialization on the store slow path, and per-evaluation function-object births.

---

## 1. Per-row attribution

Shares are % of busy (worker-thread) samples.

### 1.1 jetstream/hashmap — 1131x (worst cell in the matrix)

| Cost | Share | Detail |
|---|---:|---|
| GC mark hot loop | ~53% | 7 adjacent PCs `gc_mark_item+0x318..0x3ac` — see §2 L-C′ |
| Shape probes | ~14% | `js_map_get_fast` 7.7 + `js_find_shape_entry` 4.5 + `js_own_shape_slot_status` 1.8 |
| double↔string | ~9% | `__svfscanf_l` 3.0 + `__dtoa` 2.2 + `__vfprintf` 1.9 + … under `js_to_string` ← `js_has_own_property` |

`LAMBDA_GC_STATS=1` (release): **`mark_collections=9`, `mark_ms=92474`** — ~5–10 s per mark phase (≈4 s/collection normalizing to the uncontaminated 64 s run, where mark ≈ 53–58% of wall). Marking a ~90K-entry live HashMap should take milliseconds; it takes seconds. Root cause in §2 L-C′.

Inclusive from JIT: `js_property_set` 74.7%, `js_property_access` 18.4% — the GC pressure itself is generated inside the store path (see L-I: every bucket store on the holey `new Array(capacity)` backbone takes the slow path and litters descriptor maps + interned digit strings).

### 1.2 awfy/havlak — 929x

| Cost | Share | Detail |
|---|---:|---|
| Shape/property chain | ~36% | `js_map_get_fast` 12.8 + `js_find_shape_entry` 9.8 + `memcmp` 4.8 + `slot_status` 3.2 + `map_put` 1.8 + `note_property_mutation` 1.7 + … |
| Function-object births | ~24% | `gc_object_zone_alloc` subtree: **48.5% under `js_new_closure`, 46.8% under `js_new_method_function`** — all called from JIT code |
| number↔string | ~8% | same `js_to_string`/`sscanf` machinery |

Inclusive from JIT: `js_property_set(+_named_ic)` 36.8%, births 25.2%, `js_property_access` 11.8%. The births are the SOM-style hot-loop arrows (`nodeW.getInEdges().forEach((nodeV) => {…})`, `nodePool.forEach(…)`, union-find `nodeList.forEach((iter) => iter.union(this.parent))` at havlak2_bundle.js:932–1086) — one JsFunction allocation per evaluation. §2 L-H.

### 1.3 awfy/cd — 261x

| Cost | Share | Detail |
|---|---:|---|
| Shape/property chain | ~49% | `js_map_get_fast` 16.5 + `js_find_shape_entry` 15.5 + `slot_status` 5.2 + `note_property_mutation` 3.2 + `memcmp` 2.9 + `property_get` 2.7 + `_map_read_field` 1.1 + `hashmap_sip` 0.8 |
| Function-object births | ~16% | zone-alloc subtree 88% under `js_new_method_function` (per-frame arrows in `simulate`/`reduceCollisionSet`, cd2_bundle.js:1183–1339) |

Inclusive from JIT: `js_property_set` 51.1%, `js_property_access` 14.4%, `js_new_method_function` 14.1%. R2b (shared-root shape megamorphism) stands as recorded; the proto-hop tax (§2 L-B′) multiplies it.

### 1.4 jetstream/navier_stokes — 133x

| Cost | Share | Detail |
|---|---:|---|
| Store slow path | dominant | inclusive `js_property_set` **67%**, of which `js_object_get_own_property_descriptor` subtree **56%** |
| Shape leaves | ~47% | `js_map_get_fast` 17.6 + `js_find_shape_entry` 13.2 + `slot_status` 3.1 + `note_mutation` 2.6 + `exotic_before_get` 2.6 + proto/get/set/access/`hashmap_sip`/`map_put` … |
| number↔string | ~11% | `__dtoa` 2.7 + `__svfscanf_l` 2.7 + `__vfprintf` 2.6 + `fastParse64` 1.6 + `__sfvwrite` 1.1 + `name_pool_create_strview` 1.5 |
| Boxed arith | ~7% | `js_get_number` 2.0 + `it2d` 1.9 + `js_add` 1.7 + `flt2it` 1.0 |

Mechanism (§2 L-D′ + L-I): the hot compound stores — `addFields`: `x[i] += dt*s[i]`; `project`: `u[++currentPos] -= …` — go through `jm_emit_canonicalize_computed_key_for_get_put`, which calls `js_to_property_key` on the numeric key → **string key**. The dense fast gates (`js_key_as_array_index`) deliberately exclude strings, so every such store falls into `js_property_set_array`'s J39-7 ordinary-set block. Plain stores (lin_solve's `x[currentRow] = …`, the bulk by count) hit the dense gate; the ~4%-by-count compound stores cost ~50–100× each and dominate time. Reads on Result13's "arrays-as-maps" theory are revised: reads mostly take the dense path; the store slow path is the disaster.

The Result13 L-G.1 `getenv` fix is confirmed effective — no `getenv` frames remain.

### 1.5 beng/spectralnorm — 111x

| Cost | Share | Detail |
|---|---:|---|
| snprintf/sscanf machinery | ~46% | `__vfprintf` 10.5 + `__svfscanf_l` 7.3 + `__dtoa` 5.8 + `__sfvwrite` 5.2 + `fastParse64` 2.9 + `_vsnprintf`/`__v2printf`/`__lo0bits`/`__Balloc`/`__Bfree`/`localeconv_l`/`pthread_getspecific` + `lambda_finite_double_to_shortest` 1.2 |
| Typed-array chain | ~10% | `array_num_resolve_data` 4.2 + `js_ta_key_canonical_numeric` 2.9 + `js_typed_array_get` 1.6 + `byte_buffer_data_const` 1.4 |
| IC invalidation bookkeeping | 4.5% | `js_intrinsic_note_property_mutation` per Float64Array store |
| Boxing | ~2% | `it2d` 1.1 + `flt2it` 1.0 |

Inclusive from JIT: `js_to_string` 42.3%, `js_property_access` 24.8%, `js_property_set` 23.1%. The O(n²) kernel `atv[i] += a(j,i)*v[j]` is a compound assignment → key stringified per iteration (`js_to_property_key` → `js_to_string`), then parsed back per access (`js_ta_key_canonical_numeric` string branch: `strtod` + `%.15f` re-format + `strncmp`). Both directions of the round-trip are visible in the callers.

---

## 2. Root-cause mechanisms (named code paths)

**L-C′ — GC mark is O(n²): the mark-time ownership test linearly scans bump blocks.** `gc_mark_item` → `is_gc_object` (`gc_heap.c:1172`) falls through to **`gc_bump_block_owns_exact` (`gc_heap.c:1138`), which walks the containing bump block header-by-header from the block base** until it reaches the queried pointer. Constructor-shaped objects are bump-allocated (`js_constructor_create_object_shaped_cached` → `js_new_object_with_typemap` → `gc_heap_bump_alloc`), so hashmap's ~90K live Entry objects each cost an O(objects-before-it-in-block) scan **per mark visit** → O(n²) per collection → the measured seconds-per-mark and the 7-PC hot cluster (the scan loop, inlined into `gc_mark_item`). This **supersedes Result13's framing** ("sticky-mark/generational GC is the fix"): before any generational work, an O(1) ownership test — per-block allocation-start bitmap, or routing these births into the size-class object zone whose `gc_object_zone_owns` is already a binary search — collapses each mark from seconds to milliseconds. Generational/sticky-mark (R7) remains worthwhile afterwards, but it is no longer the first lever.

**L-D′ — Numeric keys are stringified eagerly, and both string directions are brute force.** Three named culprits:
1. `jm_emit_canonicalize_computed_key_for_get_put` (`js_mir_expression_lowering.cpp:1056`) calls `js_to_property_key` on **every computed compound-assign/update reference** — spec-shaped (ToPropertyKey once), but for number keys the conversion is unobservable, so keeping the numeric Item and letting the get/set numeric gates handle it is semantics-preserving.
2. `js_to_property_key` (`js_runtime_state.cpp:481`) sends FLOAT keys to `js_to_string` → **`lambda_finite_double_to_shortest` (`lambda-decimal.cpp:64`), which finds the shortest representation by looping precision 1..21, doing `snprintf("%.*e")` + `sscanf` per candidate** — up to 17 printf/scanf round-trips per conversion, plus `heap_create_name` interning. This function underlies every JS double→string anywhere, not just keys.
3. `js_ta_key_canonical_numeric` (`js_globals.cpp:921`) string branch parses back with `strtod` then re-formats with `%.15f`/`%.15g` + `strlen`/`strncmp` to verify canonicality — per typed-array access with a string key.

**L-I — The array store slow path materializes spec descriptors per store.** `js_property_set_array`'s J39-7 block (`js_runtime.cpp:6400–6442`): every numeric store that misses the dense gate does `snprintf` + `heap_create_name` (digit-string interning), then **`js_object_get_own_property_descriptor` — which allocates a fresh descriptor Map and populates it through 4 recursive generic `js_property_set` calls** (each paying `note_property_mutation` + shape searches + `map_put` transitions) — merely as an "own property exists?" probe, then discards it; absent an own descriptor it walks the prototype chain via `js_find_accessor_pair_inheritable`. Two feeder conditions make this path hot: (a) compound assigns arrive with string keys (L-D′.1) and are excluded from the dense gate by design; (b) **holes**: `new Array(n)` slots hold the deleted sentinel, and `js_array_fast_own_dense_set` (`js_runtime.cpp:6070`) rejects holes — so *first writes* to `new Array(n)` backbones (hashmap's `_elementData`, every Harmony-style bucket store) always take the descriptor dance.

**L-H — Per-evaluation function-object births.** Every arrow/function-expression evaluation calls `js_new_closure`/`js_new_method_function` → `heap_calloc(sizeof(JsFunction))` — a **~264-byte, 34-field struct** zeroed per birth — plus `js_get_global_this`, `js_function_capture_with_env`, `js_function_call_lane_recompute`. In SOM-style code (`Vector.forEach(fn)` in loops) this is havlak's 24% and cd's 16%. JS identity semantics require a fresh object per evaluation, so the fixes are: slim the birth (arrow-sized variant or deferred fields; precompute the call lane at transpile time), make births near-free (nursery/bump + cheap reclaim — R7 territory), or per-site reuse where escape analysis proves identity unobservable (larger project).

**L-B′ — The prototype hop itself costs 2–3 shape searches.** `js_get_prototype` (`js_runtime.cpp`) resolves [[Prototype]] by searching the object's own shape for `INTERNAL_PROTO_KEY` (`js_own_shape_slot_status`), then probing `__json_own_proto__` (`js_map_get_fast_ext`), then `js_find_shape_entry(PROTO_KEY)` for the accessor-redefinition edge case. Every method lookup on a class instance hops to the prototype, so each hop multiplies the R2b megamorphic walks — visible as the 18–24% of `js_find_shape_entry`/`js_map_get_fast` samples rooted in `js_get_prototype_of` on cd/havlak/navier. A dedicated proto field (or TypeMap-cached proto pointer) turns hops into pointer chases; independent of, and complementary to, R2b.

Cross-cutting residue confirmed again: `js_intrinsic_note_property_mutation` (1.7–4.5% everywhere, runs per store even on typed arrays), `name_pool_create_strview` per slow store, `js_array_exotic_before_property_get` per dense read (2.6%), `_tlv_get_addr` (thread-local overhead ~1.4%).

---

## 3. What to do — ranked by measured ceiling

1. **O(1) bump-block ownership test** (L-C′): per-block allocation bitmap or move constructor births into the size-class zone. Ceiling: hashmap's ~53% mark share ≈ gone (seconds → ms per collection); also trims every allocation-heavy row's collections. Small, mechanical, no semantics. — **POC built and measured 2026-07-27; ceiling confirmed, see §5.**
2. **Stop stringifying numeric keys** (L-D′.1 + fast paths): keep number keys as Items through compound/update refs; add an integral fast path (`%lld`-free itoa) to `js_to_property_key`/`js_to_string`; replace `lambda_finite_double_to_shortest`'s search loop with a real shortest-double algorithm (Ryu/Grisu-class); arithmetic canonical check in `js_ta_key_canonical_numeric` (Result13 Tier-2 #4, still open). Ceiling: spectralnorm ~50%, navier ~15–25%, hashmap ~9%, havlak ~8%.
3. **De-descriptor the array store slow path** (L-I): lightweight own-property probe (no Map materialization); hole-aware dense define fast path so first writes to `new Array(n)` slots stay native when the receiver is a plain dense array and canonical prototypes are accessor-clean (epoch flag); this also deletes most of hashmap's garbage generation. Ceiling: navier's 56% descriptor subtree; hashmap's store-side litter; shares of cd/havlak stores.
4. **Dedicated [[Prototype]] slot** (L-B′): proto hops become pointer chases. Compounds with R2b on cd/havlak (their ~36–49% lookup chains have 18–24% rooted in proto resolution).
5. **Slim function births** (L-H): arrow-sized JsFunction or deferred fields + transpile-time call-lane. Ceiling: havlak ~24%, cd ~16%.
6. **R2b and R7 as already planned** — unchanged by this pass; R2b remains the cd/havlak shape fix; R7 (generational/sticky mark, cheap birth, lazy zeroing) remains right *after* items 1–3 pick up the newly named mechanisms.

Sequencing note vs the Tune7 line: items 1–3 are runtime/GC-side and do not touch the dynamic-call machinery this branch is developing; they can proceed in parallel without emission-layer conflicts. Item 2's ref-lowering half (`jm_emit_canonicalize_computed_key_for_get_put`) is the only transpiler touch.

---

## 4. Raw data

- Profiles: `temp/prof15/ljs_{hashmap,havlak,cd,navier,spectral}.txt` (+ discarded debug-binary `ljs_micro.txt`), leaf-merge script `temp/prof15/leafmerge.py`, symbol table `temp/prof15/nm_sorted.txt`.
- GC stats: `temp/prof15/hashmap_gcstats.log` (`__TIMING__:124837`, `mark_collections=9 mark_ms=92474.148`; wall 2x-contaminated, see caveat).
- Key resolutions: hashmap hot cluster `0x102be7710..0x102be77a4` → `_gc_mark_item+0x318..0x3ac` (inlined `is_gc_object`/`gc_bump_block_owns_exact` scan); havlak/cd zone-alloc callers → JIT-emitted `js_new_closure`/`js_new_method_function` sites; navier store statics → `js_property_set_array` region (`_js_array_sparse_collect_indices+…` by nm bisection).
- Benchmarks profiled exactly as the runner invokes them: `lambda.exe js temp/_ljs_jetstream_{hashmap,navier_stokes}.js`, `awfy/{havlak2,cd2}_bundle.js`, `beng/js/spectralnorm.js` (scaled copy `temp/prof15/spectralnorm_x20.js`).

## 5. POC — O(1) bump-ownership bitmap (item 1), built and measured

**Date:** 2026-07-27. **Diff:** `lambda/runtime/gc/gc_heap.{c,h}` (+ a warning comment in `transpile-mir.cpp`), ~100 lines. Both binaries built `make release` from the same source (HEAD `eca5fe446`, no commits between the two builds); baseline kept at `temp/prof15/lambda_base.exe`, patched at `temp/prof15/lambda_bitmap.exe`.

**Change.** Each `gc_bump_block_t` carries an allocation-start bitmap — one bit per 16-byte granule, set in `gc_heap_bump_alloc` when a slot header is placed. Every bump slot is a 16-byte `gc_header_t` plus an object-zone size class (all classes are multiples of 16), so slot headers always land on a 16-byte granule from the block base and the bitmap is exact. `gc_bump_block_owns_exact` becomes: reject against new `gc->bump_{min,max}_addr` bounds → find the block → require 16-byte alignment → test one bit. Bits are never cleared, which is correct because sweep never recycles bump slots (dead ones only get `GC_FLAG_FREED` and stay addressable), so a set bit keeps meaning exactly what the old scan reported. Cost: `block_size/128` bytes (32 KB per 4 MB block) and one OR per allocation. The old replay scan is retained as `gc_bump_block_scan_exact` and used if a bitmap allocation ever fails, so the change is strictly an optimization.

**Mechanism confirmed** (`LAMBDA_GC_STATS=1`, same workload, release):

| | collections | mark time | wall |
|---|---:|---:|---:|
| base | 9 | **39,316 ms** | 64.4 s |
| bitmap | 9 | **28.2 ms** | 24.9 s |

Identical collection count — this is not fewer GCs, it is **1393× cheaper marking**, which is the L-C′ prediction exactly. Marking ~90 K live objects now costs ~3 ms per collection instead of ~4.4 s.

**Interleaved A/B, jetstream/hashmap** (3 pairs): base 65.31 / 67.69 / 65.00 s → bitmap 25.58 / 25.42 / 24.96 s. Median **65.31 s → 25.42 s = 2.57× faster**; the row's LambdaJS/Node ratio drops from 1131x to roughly **440x**, and hashmap stops being the worst cell in the matrix.

**Regression controls** (interleaved, medians): larceny/gcbench LJS 862 → 827 ms (−4%); larceny/gcbench MIR 291 → 294 ms; jetstream/splay MIR 51.2 → 51.4 ms; jetstream/splay LJS 51.2 → 51.1 ms; beng/binarytrees LJS 34.8 → 35.2 ms; awfy/cd LJS ~9.9 s both. All within ±2% except gcbench LJS, which improves. No regression on the allocation-heavy rows where the added per-allocation store would show.

**Correctness.**
- All 8 `test-gc-rooting-core` gates pass under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (collection at every allocation, dead payloads poisoned) — the strongest available check that no live bump slot is misreported as unmanaged.
- `test_mir_gc_stress_gtest` (forced-GC corpus sweep): **25/25**. MIR emission + ratchet suites: **33/33** green, so no emission-budget movement.
- `make test-lambda-baseline`: 1587 gtests, **17 failures — all pre-existing**, not caused by this change. Proof: every failing script was re-run directly against both saved release binaries and produced **byte-identical output and identical exit codes** on base and patched (`zlib_*` ×6 rc=1 both, `jube_{fs_permission,zlib_dynamic,zlib_parity}_registry` rc=1 both, `jube_console_minimal_formatter` and `number_model_bigint_fs_position_range` rc=0 both). The registry/zlib rows need Jube modules that `test-lambda-baseline` does not build; the formatter row is a `"%d 42"` vs `"42"` golden mismatch. None is GC-related.
- hashmap's own in-benchmark result verification passes.

**Latent bug found while validating (separate from this POC).** `transpile-mir.cpp`'s inline bump-allocation fast path (the "#10 + #11" optimization) uses stale `gc_heap_t` offsets: it reads `bump_cursor`/`bump_end` at 16/24, but `gc_large_set_t` (24 bytes) sits between `all_objects` and `bump_cursor`, so the real offsets are 40/48 and it actually loads `large_objects.{slots,capacity}`. A live `slots` pointer always exceeds a small `capacity` count, and `capacity` is 0 whenever `slots` is NULL, so the guard branch always falls through to the slow path — **the inline allocator has never executed**, which is the only reason the wrong offsets are harmless. This matters for the bitmap: whoever repairs the offsets must also set the owning block's `alloc_bits` bit inline, or the unrecorded slot will be swept while live. A warning comment now sits at that site.

---
