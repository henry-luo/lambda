# Lambda Impl Plan: Tune 9 — High-Impact GC Allocation Throughput

**Status: REVISED 2026-07-27 — post-bitmap profiling complete; implementation
limited to two measured allocation bottlenecks.**

**Scope rule:** Tune9 is not a general GC cleanup. A phase may land only when it
improves its declared slow benchmark target by at least **10%** and introduces
no correctness or confirmed performance regression elsewhere. Sub-10% tweaks
are not accumulated into a grab bag; they are deferred.

**Implements:** the allocation portion of `Lambda_Tuning_Proposal.md` R7, based
on the current tree and the post-bitmap follow-up to
`vibe/Lambda_Impl_Tune8_Result15_Bottlenecks.md`. Precise `RootFrame`/`Rooted`
ownership remains mandatory. Objects remain non-moving. Conservative native
stack scanning remains retired.

---

## 1. Profiling checkpoint (2026-07-27)

### 1.1 Build and method

- Source: current working tree at `6ca43e72f`; the proposal file was the only
  pre-existing untracked file.
- Binary: fresh `make release`; no debug or JS execution-profile build.
- Standard timings: unmodified authoritative benchmark inputs, three runs,
  median of `__TIMING__`.
- Attribution: macOS `sample` on release processes. Short MIR workloads were
  scaled only in `temp/prof9/` to obtain stable samples; the scaling preserved
  the hot operation and was not used for acceptance timings.
- GC counters: `LAMBDA_GC_STATS=1` on the unmodified benchmark.

Standard release baselines:

| Benchmark | Runs (ms) | Median | Collections / mark |
|---|---|---:|---:|
| larceny/gcbench MIR | 384.634 / 384.935 / 385.550 | **384.935 ms** | 0 / 0 ms |
| jetstream/splay MIR | 156.894 / 162.559 / 156.612 | **156.894 ms** | 2 / 32.573 ms |
| kostya/json_gen MIR | 76.568 / 75.808 / 76.672 | **76.568 ms** | 0 / 0 ms |
| kostya/base64 MIR | 312.075 / 309.606 / 310.713 | **310.713 ms** | 0 / 0 ms |
| jetstream/hashmap LJS | 22463.275 / 22457.729 / 22425.956 | **22457.729 ms** | 9 / 30–32 ms |

Performance guard baselines:

| Guard | Median |
|---|---:|
| kostya/matmul MIR | **42.095 ms** |
| larceny/ray MIR | **11.466 ms** |
| jetstream/richards MIR | **208.235 ms** |

The current release fails both `awfy/havlak2_bundle.js` and
`larceny/gcbench.js` immediately with:

```text
TypeError: Cannot destructure 'undefined' as it is undefined.
```

Those LambdaJS rows are excluded from Tune9 performance claims until their
pre-existing execution regression is fixed. Tune9 must not hide, work around,
or claim improvement on a row that does not currently complete.

### 1.2 Current attribution

Busy leaf shares from the fresh release samples:

| Target | Current dominant costs | Tune9 interpretation |
|---|---|---|
| gcbench MIR | `gc_object_zone_alloc` **85.2%**; 75% of its samples under `map_with_tl` | active: typed-map birth path has a large, direct ceiling |
| json_gen MIR | `gc_object_zone_alloc` **39.3%**, `__bzero` **33.6%**, memmove/memset **15.1%** | active: large transient strings are zeroed immediately before overwrite |
| base64 MIR | memset **28.5%**, `gc_object_zone_alloc` **20.3%**, malloc free-list paths **16.3%**, memmove **8.8%** | active: large transient strings pay redundant zero-fill |
| splay MIR | `gc_collect_with_root_region` leaf **18.2%** on the scaled churn sample; standard run has only 2 collections | insufficient margin for a generational redesign at this stage |
| hashmap LJS | shape/property lookup and double↔string conversion dominate; total mark is only 30–32 ms of ~22.46 s | not a Tune9 GC target |

Consequences:

1. **Hashmap no longer justifies sticky marking or slot-trace tuning.** Its
   post-bitmap mark ceiling is about 0.14% of wall time. Its remaining work
   belongs to JS property shapes and numeric-key conversion.
2. **gcbench/json_gen/base64 perform no collections.** Collection pacing,
   remembered sets, sticky marks, and lazy sweep cannot improve these rows.
3. **Only two Tune9 mechanisms clear the entry bar:** typed-map birth on
   gcbench, and redundant large-string zero-fill on json_gen/base64.

Raw profiling artifacts are under `temp/prof9/`:
`{hashmap,base64_scaled,json_gen_scaled,gcbench_scaled,splay_scaled}.sample`
plus the scaled input copies and captured output.

---

## 2. Active phase A — uninitialized large-string payloads

### Evidence and target

`gc_heap_alloc` uses `malloc` plus `memset(header, 0, total)` for allocations
above `GC_LARGE_OBJECT_THRESHOLD`. `fn_strcat` then writes the `String` fields
and copies the complete character payload. Fresh profiles attribute 33.6% of
json_gen and 28.5% of base64 to this redundant zeroing.

Declared targets and minimum accepted medians:

| Target | Baseline | Required candidate (≥10% faster) |
|---|---:|---:|
| kostya/json_gen MIR | 76.568 ms | **≤68.911 ms** |
| kostya/base64 MIR | 310.713 ms | **≤279.642 ms** |

Both declared targets must clear the threshold. Otherwise the phase is not a
Tune9 win and is reverted/deferred.

### Implementation boundary

1. Add a narrow large-object allocation primitive that initializes every
   `gc_header_t` field but leaves the user payload uninitialized. It must keep
   large-set insertion, `all_objects` linking, exact counters, allocation
   guards, forced-collection behavior, and failure rollback identical to
   `gc_heap_alloc`.
2. Expose it through a string/symbol-specific heap helper, not a generic
   arbitrary-`type_tag` API. `Binary` is a fixed descriptor over
   `ByteStorage`, not an assumed inline byte buffer, and is outside this phase.
3. Convert only constructors that immediately initialize every semantic field
   and every visible byte through the terminating NUL. Start with `fn_strcat`
   and the directly measured copy-producing string paths. Do not convert
   incremental builders or partially initialized objects.
4. Under poison/stress mode, fill the otherwise-uninitialized payload with a
   non-zero diagnostic byte and validate termination using a bounded search.
   Debug-mode poison cost is not part of release timing.
5. Keep the ≤384-byte object-zone path unchanged. Narrowing free-list reuse
   memset is a separate sub-10% idea and is not part of phase A.

### Gates

- Interleaved baseline/candidate release runs, at least 3 pairs; both target
  medians improve by ≥10%.
- Exact benchmark output and exit code unchanged.
- gcbench, splay, matmul, ray, and richards show no confirmed regression.
- Forced-GC/poison rooting gates and `make test-lambda-baseline` remain green.
- Because the heap allocator is shared with LambdaJS, `make test262-baseline`
  and `make node-baseline` must also be green before landing.
- If a guard moves adversely by >3%, rerun 5 interleaved pairs. A confirmed
  adverse movement rejects the phase even if the targets improve.

---

## 3. Active phase B — typed-map birth fast path

### Evidence and target

The scaled gcbench release profile spends 85.2% of busy samples in
`gc_object_zone_alloc`; 75% of that allocator time is reached through
`map_with_tl`. The standard row performs no GC, so the target is object birth
and initialization—not collection.

The old direct typed-map construction block in
`lambda/runtime/transpile-mir.cpp` is currently wrapped in `if (false)`. It was
disabled during precise-rooting work. Therefore this phase is not described as
repairing two stale offsets: it must first revalidate the entire direct
construction lane.

Declared target:

| Target | Baseline | Required candidate (≥10% faster) |
|---|---:|---:|
| larceny/gcbench MIR | 384.935 ms | **≤346.442 ms** |

### Staging

#### B1 — re-enable direct construction through the existing C allocator

1. Audit why the direct map lane was disabled and prove that every value which
   may allocate is precisely rooted across field evaluation.
2. Re-enable direct typed-map construction while calling the existing
   `heap_calloc_class`/`gc_heap_bump_alloc` path. This isolates direct
   construction and field stores from JIT-inlined allocator mechanics.
3. Preserve `all_objects`, `alloc_bits`, `GC_FLAG_BUMP`, `object_count`,
   `total_allocated`, forced-collection behavior, and any future allocation
   debt exactly by continuing through the C allocator.
4. Land B1 only if gcbench improves by ≥10%. If it does not, stop phase B; do
   not add inline allocation to rescue a weak result.

#### B2 — inline allocation only with a second independent ≥10% win

Inline bump allocation is optional. Attempt it only if a post-B1 profile still
attributes at least 15% of gcbench wall time to the C allocation call path.

The inline lane must:

- use `offsetof`, never literal `gc_heap_t` offsets;
- set the owning bump block's `alloc_bits` bit;
- set `GC_FLAG_BUMP`;
- link `all_objects`;
- update exact byte/object counters;
- participate in ordinary and forced GC safepoints;
- preserve the active mark-sense initialization if generation work ever lands.

B2 lands only if it improves the B1 gcbench median by another ≥10% with no
regression. Otherwise the C allocator remains the fast path. Instruction-count
or helper-call reductions alone do not qualify.

### Gates

- Interleaved baseline/candidate release runs, at least 3 pairs.
- gcbench output and exit code byte-identical; median ≤346.442 ms for B1.
- MT7 MIR-emission budgets updated deliberately with a reviewed dump diff.
- `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`,
  `test-gc-rooting-core`, and `test_mir_gc_stress_gtest` pass.
- json_gen, base64, splay, matmul, ray, and richards show no confirmed
  regression.
- `make test-lambda-baseline` remains green.

---

## 4. Deferred or removed from active Tune9

### Allocation debt and trigger pacing

Deferred to a separate memory-policy proposal. It has no speed ceiling on
gcbench/json_gen/base64 because they perform zero collections, and charging
context-lifetime `pool_calloc` storage as reclaimable GC debt can create futile
collections. Telemetry or footprint work is not presented as a Tune9 speedup.

### Slot-array shape tracing

Removed from active Tune9. Post-bitmap hashmap marking is ~0.14% of wall time,
and the current map tracer also performs a full `gc_trace_data_words` fallback
after its shape walk. There is no current ≥10% target-row ceiling.

### Sticky-mark minor collections

Deferred. The current evidence does not justify its write-barrier and
data-zone correctness surface:

- hashmap marking is negligible;
- gcbench/json_gen/base64 do not collect;
- standard splay performs only two collections, so eliminating most work from
  only the second cycle has too little margin over the 10% acceptance bar;
- current LambdaJS havlak/gcbench rows do not complete and cannot be used as
  performance evidence.

Reopen generational work only after a valid, completing slow target shows at
least 20% repeatable full-collection cost across three or more cycles, and a
prototype demonstrates ≥10% end-to-end improvement. Its design must separately
cover old objects that acquire nursery data buffers; “nursery data belongs only
to young objects” is not a valid invariant in the current runtime.

### Narrow free-list memset and lazy sweep

Removed from active Tune9. Neither has an independently measured ≥10% target
gain. They are micro-tuning unless a future profile establishes a new ceiling.

---

## 5. Acceptance protocol for every Tune9 phase

1. Build baseline and candidate release binaries before timing.
2. Run the declared target and all guards interleaved A/B/A/B for at least
   three pairs; use medians.
3. Require ≥10% improvement on every declared target of the phase.
4. Require byte-identical output, identical exit status, and no new failures in
   the authoritative test gates.
5. Treat an adverse guard movement >3% as suspicious; rerun five interleaved
   pairs. A confirmed adverse movement rejects the change.
6. Do not combine independent sub-10% patches and call their aggregate a phase.
7. After each accepted phase, rebuild the next baseline and re-profile its
   target. Old sample shares are not carried forward as current evidence.
8. LambdaJS performance is not a Tune9 acceptance claim until the current
   havlak/gcbench execution regression is repaired independently.

## 6. Execution order

1. **Phase A:** uninitialized large-string payloads — smaller correctness
   surface, two independently measured >25% ceilings.
2. **Phase B1:** direct typed-map construction through the existing allocator —
   high gcbench ceiling, but requires precise-rooting revalidation.
3. **Phase B2:** optional and only after a fresh B1 profile plus an independent
   ≥10% measured win.
4. Stop. Generational collection, trace-table tuning, allocation pacing,
   free-list memset changes, and lazy sweep are not part of the active plan.
