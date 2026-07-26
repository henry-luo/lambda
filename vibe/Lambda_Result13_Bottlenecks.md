# Result13 Slow-Row Bottleneck Analysis

- **Date:** 2026-07-26
- **Input:** `test/benchmark/Overall_Result13.md` (commit `22eefe3f1`, MIR/Node dedup geo 2.94x, LambdaJS/Node 15.4x, QuickJS control 7.45x)
- **Binary:** the Result13 release binary itself (verified: splay.ls 155.5 ms vs report 157.2, fib2 6.03 vs 7.66; no code commits since `22eefe3f1`, docs only)
- **Method:** macOS `sample` (1 ms) leaf-merge across repeated runs; short rows re-run from `temp/prof/` copies with the driver loop scaled ×5–×20 so steady state dominates. LJS rows sampled mid-run on the worker thread (`__ulock_wait` main-thread parking excluded from busy shares). Attribution instrument, not a timing instrument: percentages below are leaf-sample shares of busy time, good to ~±5 points.
- **Reading aid:** `??? (in lambda.exe) + offset` frames are stripped statics/outlined code; the hot ones were resolved via `nm -n` bisection (e.g. the hashmap cluster is inside `gc_mark_item`). `??? (in <unknown binary>)` is JIT-emitted code, and it is a *minor* fraction on every slow row — the slow rows spend their time in C runtime helpers, which is itself the headline.

---

## 1. Per-row attribution

### 1.1 MIR (Lambda script) tail

| Row | Ratio | Dominant leaf costs (busy share) | Cluster |
|---|---:|---|---|
| jetstream/splay | 47x | `gc_object_zone_alloc` 38% + `gc_collect…` 7% + destroy/finalize ≈ **52% GC**; map get/set helpers ≈ 21% | GC + untyped maps |
| jetstream/richards | 44x | `fn_member_ic` 26%, `fn_map_set` 11%, `memcmp`+`hashmap_sip`+`strlen` 15%, name interning (`name_pool_create_strview`, `heap_create_name`) 5%, scalar-home ABI 8% | untyped-map helper tax |
| jetstream/crypto_sha1 | 30x | `fn_int`+`it2i`+`box_int64_value`+`coerce_num_sized` ≈ 25%, malloc family ≈ 20%, GC 8%, `mpd_qnew_size` (decimal!) 3%, shift-helper statics hot | int64/bit-op boxing |
| jetstream/navier_stokes | 25x | **`fn_add` 33%**, `it2i` 10%, `fn_index_assign`+`array_num_set_item` 10%, `fn_mul`/`fn_sub` 5% | boxed locals/arith |
| beng/spectralnorm | 17.5x | `fn_add` 24%, `fn_div`+`fn_mul` 13%, `push_d` 7% | boxed locals/arith |
| larceny/pnpoly | 17.5x | `item_at`+`fn_index`+`array_num_read_item`+`array_num_get` ≈ 35%, `push_d` 7%, `fn_sub`/`fn_gt` 10% | boxed element reads |
| kostya/base64 | 16.4x | `memset` 28%, finalize+destroy 32%, `memmove` 10%, malloc ≈ 15% (teardown partly outside timed region) | string-alloc churn / GC |
| larceny/gcbench | 16.2x | **`gc_object_zone_alloc` 52%**, map helpers (`fn_member_ic`, `js_map_get_fast`, `set_fields`) 16% | GC allocation |
| awfy+beng+js nbody | 15x/14.7x/10.8x | `fn_mul` 15%, `fn_index_assign`+`fn_array_set`+`array_num_set_item` 20%, `it2i` 11%, `push_d` 8% | boxed locals/arith |
| kostya/brainfuck | 13.4x | `fn_fill` 18%, `__bzero`+`madvise` 9% (tape refills), `item_at`+`fn_index` 15%, `it2i`+`fn_lt`+`fn_mod`+`fn_ord` 14% | fill churn + boxed ops |
| kostya+larceny primes | 13.3x | `fn_add` 20%, `fn_le` 11%, `fn_index_assign`+`fn_array_set`+`array_set` 24%, `is_truthy` 6% | boxed locals/arith |
| kostya/levenshtein | 11.2x | `item_at`+`fn_index`+`array_num_*` ≈ 40%, `fn_add` 12%, `is_truthy` 5% | boxed element reads |
| kostya/json_gen | 11.5x | **`__bzero` 31% + `heap_finalize_gc_objects` 23%** + `memmove`/`memset` 22% + zone alloc 9% (`fn_strcat` itself ~1%) | GC + zero-fill churn |
| awfy/cd (cd2.ls) | 10.2x | element reads (`item_at`, `fn_index`, `scalar_storage_read`, `it2i`) ≈ 35%, `fn_member_ic` 6%, scalar-home ABI 11% | boxed reads + maps |
| larceny/triangl | 9.6x | **`is_truthy` 32%**, `item_at` 15%, `scalar_storage_read` 12%, `it2i` 9% (JIT code 28% — the compute) | boxed reads/conditions |
| beng/mandelbrot | 8.6x | `fn_add` 19%, `push_d`+`it2i` 15% (JIT 34%) — awfy variant is 1.47x, gap = inference loss | boxed locals/arith |
| jetstream/raytrace3d | 8.2x | `fn_member_ic` 12%, scalar-home ABI 15%, `fn_add`/`fn_mul`/`fn_mod`/`fn_sub` 23%, `push_d` 6% | maps + boxed arith |
| larceny/diviter | 6.5x | `it2i` + JIT (prior attribution stands: M2 residual, boxed list ops) | boxed locals |

Missing rows (excluded from geo, correctness not perf): **fft2** rejected at compile (`E201: cannot assign float value to var 'm' of type int`, fft2.ls:24); **list2** runs but wrong result (`FAIL result=6`); **pidigits** parser rejects its literals (`Unexpected syntax near '1'`, pidigits.ls:26:27).

### 1.2 LambdaJS tail

| Row | Ratio | Dominant leaf costs (busy share) | Cluster |
|---|---:|---|---|
| jetstream/hashmap | 1121x | **`gc_mark_item` hot loop ≈ 58%** (7 adjacent PCs, `gc_heap.c`), shape probes (`js_map_get_fast`+`js_find_shape_entry`+`js_own_shape_slot_status`) ≈ 8%, dtoa/sscanf key round-trip 4% | **GC mark**, then shapes |
| awfy/havlak | 920x | shape chain ≈ 50% (`js_map_get_fast` 21%, `js_find_shape_entry` 15%, `memcmp` 6%, `map_put` 3%), `gc_object_zone_alloc` 25%, number↔string keys 9% | R2b shapes + GC |
| awfy/cd | 256x | shape chain ≈ 55% (`js_find_shape_entry` ≈ `js_map_get_fast` — megamorphic walks), `gc_object_zone_alloc` 15% | R2b shapes |
| jetstream/navier_stokes | 144x | shape chain ≈ 40% (array property gets through map lookup!), **`getenv` via `js_array_stats_touch` ≈ 5%**, dtoa/sscanf ≈ 6%, `js_add`/`js_get_number` boxed arith | arrays-as-maps + stringify |
| beng/spectralnorm | 107x | **snprintf/dtoa/sscanf ≈ 60%** under `js_ta_key_canonical_numeric` / `js_property_set` / `js_to_string` | numeric-key stringify |
| jetstream/crypto_sha1 | 74x | **`js_string_method` 48%** (per-char charCodeAt dispatch), shape chain 33% | string-method dispatch |
| awfy/deltablue | 68x | property chain ≈ 85% (`js_map_get_fast` 29%, `js_find_shape_entry` 28%, `js_own_shape_slot_status` 13%, `js_property_get`, `memcmp`, `_map_read_field`) | per-hit helper tax |
| larceny/triangl | 59x | **typed-array chain ≈ 45%**: `array_num_resolve_data` 19%, `js_typed_array_get`, `byte_buffer_data_const`, OOB+canonical checks; `it2d`/`flt2it` 11% | typed-array access |
| kostya/matmul | 55x | same typed-array chain ≈ 45%, `js_property_get/access` 10%, `js_multiply`/`js_add`/`js_get_number` 9%, globals via `radiant_dom_window_get_property` | typed-array access |
| jetstream/richards | 47x | property chain ≈ 90% of busy (same helpers as deltablue) | per-hit helper tax |
| larceny/gcbench | 38.6x | birth path 25% (`js_new_object_with_shape` 14%, `pool_calloc` 7%), property chain ≈ 25%, `js_vm_swap_global_this` 4% | GC birth + shapes |

---

## 2. Root-cause clusters

### MIR

**M-A — `pn`-body locals are boxed; every assignment round-trips box→unbox (systemic, the largest MIR factor).**
MIR dumps (`LAMBDA_MIR_DUMP_PATH`, release binary) prove it: for `var i: int`, `i = i + 1` emits native add → full flex-int box (range check, tag, out-of-band double spill via `push_d` + `lambda_item_adopt_scalar_home`) → **`it2i` C call to unbox back into the local**. Float locals are worse (number-stack spill), and any binary op with an unproven operand emits generic `fn_add`/`fn_mul`/`fn_div`. Explicit `: float` annotations on locals do **not** produce native storage (verified: annotating nbody2's `distance`/`mag` changed nothing; `advance` still emits `fn_mul`×7 + `fn_array_set`×6 on boxed `letv` locals). Only straight-line chains (element read → op → store, e.g. a micro-kernel `x[i] = x[i] + 1.5`) stay native — that kernel runs 3.5 ns/iter while equivalent benchmark code with named locals runs 5–10× slower.
Contributing inference gaps that feed M-A: (1) module-level literal `let` constants are ANY (a global `G` in a loop costs 1.4x even in the best case, and poisons every downstream untyped `var` — navier's `ROW_SIZE`/`GRID_SIZE`, spectralnorm's `N`); (2) `math.*` calls carry static type ANY even after Tune6 L3's native lowering, so `var distance = math.sqrt(d2)` poisons the rest of the loop; (3) untyped fn/pn params without call-site inference coverage (primes' `sieve(limit)` — `limit` escapes into `fill(limit+1,…)` and apparently loses M2 specialization).
**Affected:** navier 25x, nbody 15x×3, spectralnorm 17.5x, pnpoly 17.5x, primes 13.3x, levenshtein 11.2x, triangl 9.6x, mandelbrot(beng) 8.6x, cd 10.2x, raytrace3d partially, diviter residual — essentially the whole numeric tail.

**M-B — Untyped map field ops pay a per-access C-helper tax; stores have no IC at all.**
richards.ls (the JetStream row times the *untyped* variant by design; AWFY times typed `2.ls` — that is the whole 3.92x vs 44x gap): `fn_member_ic` 26% (the Tune6 L2 read IC — a hit is still a C call + guards), `fn_map_set` 11% plus `hashmap_sip`/`memcmp`/`strlen` 15% plus per-store name interning (`name_pool_create_strview`, `heap_create_name`) — `fn_map_set` (`lambda-eval.cpp:6809`) re-extracts key text and re-searches the shape on **every** store.
**Affected:** richards 44x, splay 21% share, raytrace3d, gcbench 16% share.

**M-C — GC allocation/collection throughput.**
`gc_object_zone_alloc` alone is 38–52% on splay/gcbench; json_gen is ~85% memory ops (`__bzero` 31% + `heap_finalize_gc_objects` 23% + memmove/memset) and base64 similar — allocation count and zero-fill policy dominate, not string-copy CPU (`fn_strcat` ~1%). Node's generational GC runs these rows 16–47× faster.
**Affected:** splay 47x, gcbench 16.2x, json_gen 11.5x, base64 16.4x, binarytrees, deriv, brainfuck (tape refills: `fn_fill` 18% + bzero + madvise).

**M-D — crypto_sha1: 32-bit-unsigned arithmetic boxes into int64 heap cells (and occasionally decimal).**
`fn_int`, `it2i`, `box_int64_value`, `coerce_num_sized`, xzone malloc traffic, and `mpd_qnew_size` all hot. SHA-1's values fit in 2^32 < the 56-bit packed-int domain, so the boxing threshold/paths are leaving large wins on the table.
**Affected:** crypto_sha1 30x (and any bit-twiddling workload).

### LambdaJS

**L-A — The property *hit* path is a C-helper chain, and it is the time on call-dense OOP rows.**
richards ~90%, deltablue ~85% of busy in `js_map_get_fast` → `js_find_shape_entry` → `js_own_shape_slot_status` → `js_intrinsic_note_property_mutation` → `js_property_get`. The Tune6 census showed miss rates of 0.01–0.8%, so these are *hits* — the per-hit C-call tax (the dropped J3's territory) plus accesses that bypass the IC entirely (`js_find_shape_entry` ≈ `js_map_get_fast` share on deltablue suggests a non-IC'd access family — method loads or keyed paths; Tune7 T0 should attribute per site).

**L-B — R2b shape identity (constructed objects), exactly as recorded.**
cd ~55% and havlak ~50% in the lookup chain with heavy `js_find_shape_entry` (megamorphic walks) + `map_put` transitions. Confirms `Lambda_Tuning_Proposal.md` R2b as the right fix and its mechanism (pointer-distinct TypeMaps per `run()`).

**L-C — GC mark and birth dominate the worst row.**
jetstream/hashmap (1121x — the single worst cell in the matrix) is **~58% inside `gc_mark_item`'s loop**; shape probes are only ~8%. Frequent collections (garbage from key stringification + entry churn) × a large stable live map = full re-mark every cycle. havlak adds 25% `gc_object_zone_alloc`; gcbench 25% birth (`js_new_object_with_shape` + `pool_calloc`). **R2b will not move hashmap's 58%; a sticky-mark/generational GC will.**

**L-D — Numeric keys still round-trip double→snprintf→string→sscanf on uncovered paths.**
spectralnorm is ~60% in snprintf/dtoa/sscanf under `js_ta_key_canonical_numeric` / `js_property_set` / `js_to_string`; havlak 9%, hashmap 4%, navier ~6%. Tune5 J2 covered the common get/set fast paths; the typed-array canonical-key check and property_set slow path still stringify integral doubles per access.

**L-E — Typed-array element access re-validates and re-resolves per element.**
triangl/matmul ~45% in `array_num_resolve_data` + `js_typed_array_get` + `byte_buffer_data_const` + OOB/canonical checks + `js_typed_array_raw_get_item` — 5+ C calls per element, no caching of (data pointer, length) across a loop.

**L-F — String method dispatch.**
crypto_sha1: 48% in `js_string_method` (per-character `charCodeAt` through generic method dispatch).

**L-G — Incidental hot-path bugs found (cheap, real):**
1. ~~`js_array_stats_touch` (`js_runtime.cpp:314`) calls `getenv` on **every array store** when `LAMBDA_JS_ARRAY_STATS` is unset (latch never sets) — ~5% of navier busy under a libc lock.~~ **FIXED 2026-07-26:** `LAMBDA_JS_ARRAY_STATS` is now a compile-time flag (default `0`, forced `0` under `NDEBUG`), so the whole census — counters, `getenv`, report — compiles out of release and the env var no longer exists. Two latent defects fixed alongside: the exit dump was armed only from `js_array_dense_required`, so runs whose stores all took the fast bypass reported nothing; and the report now goes through `log_info` from a `js_array_stats_dump()` call in `lambda_main_finish()`, because an `atexit` dump lands after `log_finish()` closes log.txt.
2. matmul resolves plain global reads through `radiant_dom_window_get_property`.
3. `js_vm_swap_global_this` runs per call on gcbench (Tune7 C1's same-realm skip covers it).
4. havlak2.js (non-bundle) fails immediately on LJS: `Map.size` accessor not found (`js_map_method fallback: method 'size' not found`) — compat gap masked by the runner using `havlak2_bundle.js`.

---

## 3. What to do — ranked

The two engines share one meta-finding: **slow rows spend their time in per-operation C helper calls and in the GC, not in JIT-emitted code.** Tuning that changes emission (keep values native, cache the access) or changes GC generational behavior moves whole clusters; per-helper micro-optimization does not.

### Tier 1 — new work that should enter the proposal

1. **R11 (new, top MIR item): native storage for typed/inferred scalar locals in `pn` bodies.**
   Keep `int`/`float` locals in native i64/d registers; assignment stays native with an overflow branch to the boxed slow path; box only at escape points (Item-taking calls, container stores, returns, ANY merges). Includes three inference feeders: static FLOAT return type for the 23 always-float `math.*` (the L3 allow-list already enumerates them), const-folding module-level literal `let` scalars to typed constants, and flowing initializer static types into untyped `var` locals. Evidence ceiling: micro loop 75 ms → ~15–20 ms; expect 2–5× on navier/nbody/spectralnorm/pnpoly/primes/levenshtein/triangl/mandelbrot/cd and the MIR geo moving from 2.94x toward ~2.0–2.2x. This is emission-layer work in `transpile-mir.cpp` (much smaller than R10 destination-passing, which stays parked).

2. **R7 promotion: GC generational/sticky-mark + cheaper birth + lazy zeroing — now evidence-complete, spans both engines.**
   The proposal gates R7 on "post-Result11 evidence"; that evidence now exists: hashmap 58% mark, splay/gcbench ~52% alloc, json_gen ~85% memory ops, havlak 25%, base64 ~60%. Components in impact order: (a) sticky-mark/nursery so a large stable live set is not re-marked every cycle (hashmap, havlak); (b) allocation fast path — `gc_object_zone_alloc` is 38–52% on churn rows; (c) zero-fill policy — `__bzero`/`memset` 28–31% on json_gen/base64 (zero lazily or track an init watermark); (d) finalize-sweep bookkeeping (23% on json_gen). Constraint per CLAUDE rule 15 stands: precise rooting only, non-moving.

3. **R2b as planned** — cd/havlak profiles confirm the shared-root-shape diagnosis (50%+ in megamorphic lookup). One correction to its record: hashmap is listed as its flagship row, but hashmap's time is 58% GC mark — after R2b it likely remains >500x until R7 lands. cd/havlak are where R2b shows up first.

### Tier 2 — mechanical, high-certainty LJS fixes (independent of the big tracks)

4. **Numeric canonical-key check without strings** (`js_ta_key_canonical_numeric`, `js_property_set`/`js_to_property_key` slow paths): integral-double test via arithmetic (`(double)(int64_t)d == d`, −0 and 2^53 edges) instead of snprintf/sscanf. Directly ~60% of spectralnorm (107x), plus havlak/hashmap/navier shares.
5. **Typed-array access IC**: cache (data ptr, length, detach epoch) per site or per loop; collapse the 5-call validate/resolve chain. matmul 55x and triangl 59x each have ~45% ceiling.
6. **String-method fast path** for hot `String.prototype` members on string receivers (`charCodeAt` first): crypto_sha1 48% share.
7. **The L-G quick fixes**: getenv latch (chip spawned), global-read slot cache (bypass the DOM-window property path), same-realm swap skip (already Tune7 C1).

### Tier 3 — Lambda-side map path (extends Tune6 L-track)

8. **Store-side member IC** mirroring `fn_member_ic` for `fn_map_set`, plus **emission-time interned name IDs** so per-store `strlen`/`memcmp`/strview interning disappears (`heap_create_name` on the hot path today). richards 44x has ~31% in exactly this; splay/raytrace3d/gcbench share it. The JS store-IC reserved-slot invariant (deltablue fix) is the pattern to follow.
9. **crypto_sha1 int-domain audit**: keep uint32-range results in the 56-bit packed domain instead of boxing to heap int64 (and find why `mpd_qnew_size` appears at all). Single-row 30x → likely single digits.
10. **`fn_fill` fast path** (int/bool fill via memset without page-cache churn) for brainfuck's tape refills.

### Correctness (blocks honest aggregates)

11. Fix the three missing MIR rows: fft2 transpiler rejection (`E201` on `var m` int/float — likely `/`-inference), **list2 wrong result (real miscompile — highest priority of the three)**, pidigits parser rejection of its literal syntax. Also the havlak2.js `Map.size` accessor compat gap (L-G.4).

### Sequencing interaction with Tune7

Tune7 (dynamic-call slimming) remains justified — richards/deltablue are ~85–90% in the per-access/call helper chain and the T0 census will split lookup-vs-call precisely. But on this evidence the **R11 unboxed-locals work is the bigger single lever for the MIR geo**, and **R7 is the bigger lever for the worst LJS rows** (hashmap/havlak). A reasonable order: R11 (MIR emission, no JS risk) in parallel with the Tier-2 LJS mechanical fixes → R2b → Tune7 T0 to re-census → decide C1 vs R7 order from that census. If only one big LJS track can run, the hashmap/havlak/gcbench evidence says R7's sticky-mark + birth path beats further call-path work for geo movement.

---

## 4. Raw data

Sample files: session scratchpad `prof/` (`mir_*`, `ljs_*`); scaled benchmark copies and MIR dumps: `temp/prof/` (`*_mir.txt` dumps for glob/store-kernel/nbody2 shape analysis). Symbol resolution for stripped statics: `nm -n` bisection (hashmap's hot cluster = `gc_mark_item`+0x318..0x3ac, decl `lambda/runtime/gc/gc_heap.c:355`). Protocol reminder from `Lambda_Tuning_Proposal.md` §1.3 applies to any follow-up measurement: interleaved A/B with both binaries prebuilt; the matrix is a floor recorder, not a phase instrument.
