# Lambda Impl Plan: Tune 5 — LambdaJS Result11 Tail (holey-array stores + canonical numeric index)

**Status: PLANNED — 2026-07-24.** Split from `vibe/Lambda_Impl_Tune4.md` (which
keeps the shared-GC and Lambda/MIR phases); this plan owns the **JS-engine-specific**
tunings. The two plans share one diagnosis (Tune4 §1 / §1 below), one measurement
protocol, and one exit run (Result12).
**Dependency:** Tune4 **G1** (large-object registry) and **G2** (O(1) slab
ownership in mark) change every LambdaJS baseline — awfy/cd's 2360x is ~70%
GC-2, not a JS-engine defect. Land G1/G2, or at minimum re-run the J0 probe
baselines after them, before attributing wins to J1/J2.
**Related:** `vibe/Lambda_Tuning_Proposal.md` R2/R3 and OI-6 own the property-IC
work that this plan deliberately does not touch; `vibe/Lambda_Impl_JS_Tune (done).md`
is the completed Tune-6 predecessor.
**Diagnosis provenance:** release-build `sample` profiles + disassembly + source
tracing, 2026-07-24, two commits past the Result11 commit (`52c0f3c02`, benchmark
commit `1704c2f43`). Absolutes ran ~1.3–1.5x above the Result11 report (host
state); conclusions rest on hot-frame proportions.
**Convention:** `file:line` refs verified 2026-07-24; they drift — confirm against
symbol names before editing.

**Governing invariant.** Every phase is a pure performance change: no observable
semantic change to JS programs. The only permitted observable deltas are timing,
memory footprint, and GC collection counts. J2's key-canonicalization edge cases
(`-0`, NaN, index ceiling) must prove equivalence with targeted conformance tests
before landing. `test-lambda-baseline` stays 100% at every phase boundary;
`make node-baseline` (1492/3517 floor) and the relevant test262 chunks are
non-regression gates; MIR emission budgets (`test/mir/mir_budgets.json`) are
updated only by deliberate manual lifts per MT7.

---

## 0. What this plan fixes

Result11 LambdaJS/Node geometric mean (dedup): **19.3x**, with a catastrophic
tail. The rows owned here, with measured root causes:

| Cluster | Rows (ratio vs Node) | Root cause | Phase |
|---|---|---|---|
| Holey-array store O(capacity) | jetstream/hashmap **2609x**, jetstream/navier_stokes **885x** | `js_array_store_owned` → `js_array_dense_required` backwards hole scan per store into a hole/sparse slot; every `new Array(n)` fill pattern is O(n²) | **J1** |
| Double-typed keys stringify | awfy/sieve **127x**, permute 87x, towers 61x, queens 50x, storage 21x (= the R0 "regression cluster") | float-typed index misses the `LMD_TYPE_INT` fast path → `js_to_property_key` → dtoa → sscanf parse-back → proto walks per access | **J2** |

Owned elsewhere, recorded so the residuals aren't re-diagnosed:

- **GC-2 mark owns() O(slabs)** — awfy/cd **2360x** (~70% of busy samples in
  `gc_object_zone_owns`), larceny/gcbench class: **Tune4 G2**.
- **Per-access C-call shape lookups** — larceny/puzzle 250x, crypto_sha1 122x,
  richards 29.6x, deltablue 47.5x: `js_map_get_fast` / `js_find_shape_entry` /
  `js_own_shape_slot_status` / `js_intrinsic_note_property_mutation` per op.
  This is the ~20–50x OO band that remains after J1/J2 — **R2/R3/OI-6**
  (`Lambda_Tuning_Proposal.md`), not this plan.
- Healthy paths confirmed: beng/pidigits LJS 0.16x (faster than Node),
  jetstream/splay LJS 64ms (6.4x). Untouched.

---

## 1. Evidence appendix (compact)

Profiles: macOS `sample`, 1ms interval, release build. Top-of-stack sample
counts. LJS runs scripts on a worker thread; the main thread parks in
`__ulock_wait` — ignore that symbol in all LJS profiles.

- **hashmap (LJS, 143.8s):** hot loop = backwards scan comparing against the
  hole sentinel `0x9e00dead00dead00`, tail-calling `array_set` — matches
  `js_array_dense_required` inlined into `js_array_store_owned`
  (`lambda/js/js_runtime.cpp:288-322`). ~5.04M map ops ⇒ ~28µs/op ≈ one
  131,072-slot scan per `put` (Apache-Harmony HashMap: `new Array(capacity)`
  buckets, capacity 131,072). Secondary: GC mark cluster + `js_map_get_fast`.
- **sieve (LJS, looped ×60):** `js_map_get_fast` 963, `js_find_shape_entry` 725,
  `__svfscanf_l` 276, `__dtoa` 210, `__vfprintf` 168 under
  `js_property_set` → `js_to_property_key` → `js_to_string` — double-typed
  index round-trips through digit strings; sscanf is the canonical-index
  parse-back; `__dtoa` (not itoa) confirms the keys arrive as doubles.
- **puzzle / crypto_sha1 (LJS):** `js_map_get_fast` 297/235,
  `js_find_shape_entry` 212/176, `js_own_shape_slot_status`,
  `js_intrinsic_note_property_mutation`; sha1 adds `js_string_method` 216 —
  the post-J1/J2 residual band, recorded for R2/R3.
- **cd2_bundle (LJS, 87.3s):** `gc_object_zone_owns` 11,575 busy-thread samples
  (~70%) — Tune4 G2's evidence, cited here only to explain why cd is not a
  J1/J2 target.

---

## 2. Phase J0 — instrumentation and probes (½ day, shares Tune4 G0 infra)

Release-safe counters in the style of `js_exec_profile` (no `log_debug`, which
is stripped under NDEBUG). Reuse the `temp/tune4_probes.sh` harness from Tune4
G0.4 (same 3-run-median protocol, raw JSON under `temp/` with timestamped
filenames, never overwritten); add the LJS probe set below.

- **J0.1 array counters:** `js_array_dense_required` invocations + cumulative
  scanned slots; `js_array_store_owned` fast-bypass vs scan-path counts;
  watermark hit/invalidate counts (added in J1); canonical-index fast-path
  hits/misses (added in J2). `js_property_set` branch counters already exist
  (`JS_PROPERTY_SET_BRANCH`) — extend with `top_array_float_index` once J2
  lands.
- **J0.2 probe baselines:** record before/after medians for every phase on the
  same host, same day. Gate benchmarks:

| Phase | Primary probes | Guard probes (must not regress >3%) |
|---|---|---|
| J1 | LJS jetstream/hashmap, LJS jetstream/navier_stokes | LJS jetstream/splay (already fast: 64ms), LJS beng/fasta |
| J2 | LJS awfy/sieve, permute, towers, queens, storage; larceny/puzzle | LJS awfy/json, beng/revcomp, kostya/base64 (LJS) |

LJS invocation notes (from the Result11 runner): AWFY rows run the
`test/benchmark/awfy/*2_bundle.js` standalone bundles; JetStream rows run
generated wrappers `temp/_ljs_jetstream_<name>.js` (x8 loop); both via
`./lambda.exe js <file>`. Short-running probes (sieve at ~50ms) need a looped
variant for profiling — loop the `innerBenchmarkLoop` call in a temp copy, as
the diagnosis did.

---

## 3. Phase J1 — holey-array stores: cached dense watermark (2–3 days)

**Defect.** `js_array_store_owned` (`js_runtime.cpp:301`) bypasses directly to
`array_set` only when `arr->extra == 0 && index < dense_capacity`; otherwise it
calls `js_array_dense_required` (`:288`) — a backwards scan over
`JS_DELETED_SENTINEL` holes across the whole dense capacity — **before even
deciding whether growth is needed**. JS arrays routinely carry `extra != 0`
(reserved tail: wide-scalar homes + props-companion slot), so `new Array(n)`
fill patterns pay O(capacity) per store: hashmap = 131,072-slot scan × ~720K
puts. The scan is genuinely load-bearing for growth sizing (it discovers
trailing holes so sparse-spec-length arrays don't balloon — the `:292` comment),
so it cannot simply be deleted; it must be cached.

**Design: monotone upper-bound watermark.** Maintain per-array
`dense_watermark` = an upper bound on (highest non-hole dense index + 1), with
`0 = unknown`:

- store at dense index `i`: `if (i + 1 > wm) wm = i + 1` — O(1), keeps bound;
- hole creation (delete) never lowers it — still a bound;
- consumers needing the exact value (growth sizing) use `min(wm, dense_capacity)`;
  over-approximation only costs at most the doubling the old code would reach a
  few stores later — never the sparse-balloon failure, because `wm ≤
  dense_capacity` by construction;
- `0`/unknown ⇒ one exact scan (`js_array_dense_required`) then cache.

**Storage decision.** Append `int64_t dense_watermark` to `struct List` after
`capacity` (C mirror `lambda.h:758-767` and the C++ definition). Verified
constraints: the ABI static asserts pin only the 8-byte `Container` header
(`lambda.h:721-743`); JIT-emitted member offsets (`items` +8, `length` +16,
`extra` +24, `capacity` +32) are unchanged by appending; allocation sites size
by `sizeof` so zero-init comes free from pool/calloc paths (0 = unknown is the
correct cold value). **Gate:** the MT7 emission diff for the whole Lambda+JS
fixture set must be empty for this step alone — proving no offset leaked.
Lambda-native code never reads the field; it is JS-semantics-only. Verify the
`LAMBDA_C2MIR_RUNTIME` mirror of the struct carries the same field (the frozen
C2MIR path compiles against the same header and must keep identical sizeof).

**Steps.**

- **J1.0** Counter first (J0.1): confirm on hashmap that scanned-slots ≈
  capacity × puts (validates the diagnosis in-tree before changing behavior).
- **J1.1** Add the field + `js_array_watermark_note_store(arr, i)` /
  `js_array_watermark_invalidate(arr)` inline helpers next to
  `js_array_dense_required`; re-implement `js_array_dense_required` as: cached
  value if `wm != 0`, else exact scan + cache (biased +1 so 0 stays "unknown").
- **J1.2** Invalidate-or-maintain audit — every writer of `items[]`/`length`
  reachable from JS arrays. Enumerated closure to audit (grep list, not
  exhaustive until done): `js_array_store_owned`, `js_array_sparse_delete`,
  `js_array_stamp_dense_tail_holes` (`:5670`), sparse→dense promotion, the
  `js_array_set_length_throw` shrink path, `push`/`pop`/`shift`/`unshift`/
  `splice`/`sort`/`reverse`/`fill`/`copyWithin` implementations, and the
  Lambda-side `expand_list`/`list_relocate_owned_tail` (relocation copies the
  dense prefix — watermark survives; shrink must invalidate). Rule: when exact
  maintenance is not obvious in one line, call
  `js_array_watermark_invalidate` — correctness over cleverness; the next scan
  re-caches.
- **J1.3** Fix the store fast path: in `js_array_store_owned`, allow the direct
  `array_set` bypass whenever `index < dense_capacity` **and** the write cannot
  consume tail capacity — concretely, when the value is not a wide scalar
  needing a new tail home, or when the watermark-based headroom check
  (`min(wm, dc) + extra + 2 ≤ capacity`) passes. Requires reading `array_set`'s
  tail-consumption contract first (the `:304` comment); if `array_set` can
  internally grow safely, prefer delegating and delete the pre-sizing entirely
  for the in-capacity case.
- **J1.4** Re-examine `js_array_fast_own_dense_set` (`:5965`): it funnels into
  `js_array_store_owned` — after J1.3 its hit path must be scan-free
  end-to-end.

**Gates.** LJS hashmap ≥100× (143.8s → ≤1.5s class — residual is shape lookups
+ GC, the latter addressed by Tune4 G2); navier_stokes LJS ≥20×;
`make node-baseline` non-regression; test262 array chunks green; MT7 empty diff
for J1.1's struct step; J0.1 counters show scanned-slots collapse to ~one scan
per array lifetime.

---

## 4. Phase J2 — canonical numeric index fast path (1–2 days)

**Defect.** The array fast paths key on `get_type_id(key) == LMD_TYPE_INT`
(`js_runtime.cpp:6847`), but JS numbers are doubles: a float-typed index falls
through to `js_to_property_key` → `js_to_string` (dtoa) → sscanf canonical-index
parse-back → prototype-chain walk, **per element access** (sieve profile).

**Steps.**

- **J2.1** Add one helper, used by every entry point:
  `static inline bool js_key_as_index(Item key, int64_t* out)` — INT keys pass
  through; FLOAT keys qualify iff `(double)(int64_t)d == d && d >= 0 &&
  d <= 0xFFFFFFFE` (this accepts `-0.0` as index 0, which matches
  `ToString(-0) = "0"` = array index 0; rejects NaN/±inf/fractional/negative).
- **J2.2** Apply at: the `js_property_set` top fast path (`:6847`), the
  read-side equivalent in `js_property_access`/`js_array_fast_own_dense_get`
  (`:5947`), and `js_array_set_int`'s callers where the key is still boxed.
  The generic string path remains for everything the predicate rejects — no
  semantic surface moves.
- **J2.3** Conformance shields: property-key edge tests — `a[-0]`, `a[1.5]`
  (string key "1.5"), `a[NaN]`, `a[4294967294]` vs `a[4294967295]` (index
  ceiling), `a["01"]` (non-canonical string stays string), plus the relevant
  test262 chunks. `js_intrinsic_note_property_mutation` and IC invalidation
  behavior must be identical on both routes — verify by branch counters, not
  inspection.

**Gates.** sieve/permute/towers/queens/storage each ≥5× (targets: sieve 50ms →
≤10ms class); puzzle ≥3× (its residual is R2/R3 shape lookups); node-baseline
and test262 non-regression; dtoa/sscanf disappear from the sieve profile.

---

## 5. Sequencing, exit, and Result12

Order: **J0 → J1 → J2**, after (or re-baselined against) Tune4 G1/G2. J1 and
J2 are independent of each other in code but share probe workloads — land and
measure separately.

Each phase lands independently green: baseline suites 100%, node-baseline and
test262 non-regression, MT7 budgets current, probe medians recorded to `temp/`
with timestamped filenames.

**Exit = Result12**, one run shared with Tune4 (Result9/10/11 protocol: clean
release build, four-engine matrix, 3-run medians, correctness sweep, raw JSON
preserved). LJS-side success thresholds:

| Metric | Result11 | Target |
|---|---|---|
| LambdaJS/Node geo (dedup) | 19.3x | ≤ 10x |
| Worst LJS row | 2609x (hashmap) | ≤ 60x |
| LJS rows > 100x | 8 | 0 |

The ≤60x worst-row and ≤10x geo targets assume Tune4 G2 has landed (cd). After
Result12, the expected LJS residual is the R2/R3/OI-6 shape-lookup band
(~20–50x OO rows) — re-rank in `Lambda_Tuning_Proposal.md` with the J0 counter
data attached.

---

## 6. Risks

- **J1 struct append.** Any hidden `sizeof(List)` coupling (serialized layouts,
  arena block math, the frozen C2MIR header mirror) would shift. The MT7
  empty-diff gate, full baseline, and the `LAMBDA_C2MIR_RUNTIME` mirror check
  cover this. Watermark staleness is designed to be one-sided: invalidate-to-0
  is always available and always safe; a watermark that is ever *lower* than
  the true bound is the only dangerous state, and no code path lowers it except
  invalidate-to-0.
- **J1 audit completeness.** A missed `items[]` writer that shrinks the dense
  prefix without invalidating leaves an inflated bound — safe by design
  (over-preservation, never data loss), but verify with a randomized
  splice/delete/store fuzz probe comparing against a scan-always reference
  build.
- **J2 spec surface.** Canonical-index handling is where JS engines
  historically leak semantics (`-0`, `"01"`, 2³²−1 ceiling, frozen arrays,
  accessor shadowing). The J2.3 shield list is the contract; any failure
  demotes the key back to the string path rather than special-casing further.
- **Attribution.** cd and the OO band improve from Tune4 G2 and from R2/R3
  respectively — do not credit or debit J1/J2 for them; the J0 counters are the
  attribution record.
- **Machine-state variance.** Result11 absolutes were not reproducible on the
  profiling host (~1.3–1.5x). All phase gates are *ratios against same-day
  before/after runs* on one host, never against the Result11 report numbers.
