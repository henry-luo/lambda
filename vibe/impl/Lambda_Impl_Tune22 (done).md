# Tune 22: Result38 Proposal — Completed Implementation

- **Date:** 2026-09-09
- **Status:** IMPLEMENTATION COMPLETE. T22-0 through T22-8 are landed with
  semantic/MIR fixtures, native C2MIR ports, full baseline validation, and the
  final release measurement. The implementation closes every executable track;
  it does **not** meet the forecast text-performance gates in §5, which remain
  measured input for the next proposal rather than silently reclassified as a
  success.
- **Final evidence:** `test/benchmark/Overall_Result40.md` /
  `benchmark_results_v40.json` (2026-09-09), from archived release binary
  `test/benchmark/exe/lambda-v40-440c6485d1` (19,479,624 bytes; SHA-256
  `5719fa18fee9fec514463ae7112c6db8f9d970836bbfd1eb2ed83fd9a26091f6`). It
  records 15 raw samples per cell in JSON; valid cells are checksum-verified,
  while Node's known `nbody` wrong-output cell is explicitly excluded. The
  pre-measurement Test262 baseline is 40,261 / 40,261.
- **Input diagnosis:** `test/benchmark/Overall_Result38.md` /
  `benchmark_results_v38.json` (2026-09-08, commit `b23793e83`). Working
  files: `temp/prof/` (dumps, samples, loop copies, `callhist2.py`),
  `temp/prof/mb/` (micro-experiments).
- **Related:** `Lambda_Impl_Tune21 (done).md` (T21-#; §4 inherited decisions stand),
  `Lambda_Impl_Tune20 (done).md` (T20-# status table §2.5 and the §2.3
  structural finding), `../Lambda_Tune_Typed_Vs_C2MIR.md` (Result18 mechanism
  catalog), `../Lambda_Design_Runtime_COW.md` (CW29–CW34),
  `../Lambda_Design_Compiling_Lane.md` (ValueRep / `MirValue`).
- **Formal authority cited:** **D8.4.1v2** (no inline caches; specialization
  over caching), **D8.1.1v9** (tiers; satellite images), **D8.3.1** (one unboxed
  version per function), **D3.4.1** (packed map storage), **D3.2.4v2** (a named
  map contract is a reification), **D2.1.6** (a representation includes its
  lifecycle), **D2.2.2** / **D2.5.1** (int poison and optional lanes),
  **D2.6.5** (append API), **D5.1.1v2** / **D5.2.2v3** (root and scalar-home
  stacks), **D5.3.4** (precise rootability), **D5.4.3** (no context value at a
  baked address), **D4.4.4** (RMW
  place copies, CW34), **S4.1.2** (int is closed and total), **S5.1.1** /
  **S5.1.2** (equality is total; `nan` is unequal to itself), **S7.1.1**
  (invalid reads yield `null`), **S9.1.1** /
  **S9.1.2** / **S9.2.2** / **S9.3.1** (`let` is final; sharing
  unobservable; exclusive mutable borrows; capture by value), **S11.4.2** /
  **S11.4.3** (success/error contracts and the
  `any \ error` boundary), **S12.2.1** (`let` and parameters are immutable),
  **S17.1.1** (`split` follows ECMAScript).

---

## 1. Where v38 stands

| Metric | v36 | v37 | **v38** | Tune21 round target |
|---|---:|---:|---:|---:|
| MIR (typed) / C2MIR geo (59 rows) | 5.67x | 5.12x | **5.05x** | ≤ 4.0x |
| MIR (typed) / Node geo (63 rows) | 0.85x | 0.78x | **0.91x** † | ≤ 0.70x |
| MIR (untyped) / Node geo (63 rows) | 1.37x | 0.97x | **1.08x** † | ≤ 1.0x |
| Rows > 20x C2MIR (typed) | 8 | — | **7** | ≤ 3 |
| Text rows with a typed source | 3/3 | 3/3 | **3/7** | — |

† The v38 geomeans include four new text rows that have no typed source
(`untyped_fallback`) and no C2MIR port; on the 59 shared rows typed/Node is
0.78x and untyped/Node 0.97x, both flat against v37. The round's two questions:

1. **The four new text rows** (`prettier_ast`, `text_search`, `three_way_merge`,
   `log_pipeline`) are 3.8x–19.5x behind Node and are the widest Node gaps in
   the whole suite. They exercise strings, `split`, untyped maps with string
   tags, and integer arrays — the "document processing" profile the language
   is for — and none of the machinery Tune19–21 built for numeric kernels
   reaches them.
2. **The typed tail against C2MIR** is unchanged in shape since Result34: the
   same 15 rows sit at 11x–106x, and the profiles say the same four things
   (map field protocol, boxed dispatch on values whose lane is known, per-element
   admission, allocation volume) plus the COW spine protocol Tune21 named.
   What is new is that this round has MIR histograms for every one of them
   and can name the emitter behind each cost.

### 1.1 Completion measurement (Result40)

The final run used the archived release binary, 15 samples per engine/variant,
and the same `__TIMING__` boundary as Result38. Both populations are retained
because only the 59-row population is historical-comparable. The complete
ordered samples, statuses, and checksums are in `benchmark_results_v40.json`;
the table intentionally reports the generated medians and ranges, not best
runs.

| Population | Rows | typed/Node geo | untyped/Node geo | typed/C2MIR geo |
|---|---:|---:|---:|---:|
| v38-comparable | 59 | 1.07x | 1.26x | 5.34x |
| complete current suite | 63 | 1.20x | 1.42x | 5.49x |

| Text row | MIR untyped median [range] | MIR typed median [range] | C2MIR | Node |
|---|---:|---:|---:|---:|
| prettier_ast | 911.5 ms [850.7–932.2] | 912.3 ms [844.2–959.4] | 41.2 ms | 90.2 ms |
| text_search | 14.97 s [14.51–15.40] | 5.30 s [4.96–5.63] | 556.2 ms | 747.2 ms |
| three_way_merge | 3.45 s [3.37–3.51] | 3.19 s [3.12–3.27] | 1.44 s | 1.02 s |
| log_pipeline | 5.97 s [5.89–6.18] | 5.63 s [5.55–5.78] | 570.1 ms | 1.04 s |

T22-0's coverage, provenance, and reproducibility goals pass. The T22-2
emission/correctness gates pass, but its 3.0 s / 1.5 s text-search forecasts
do not: `text_search` remains 14.97 s / 5.30 s. The residual gaps are
therefore explicitly carried into the design queue; they are not a reason to
weaken S5.1.2, S9.1.2, or the D5.1.1v2 rooting discipline.

---

## 2. Evidence

### 2.1 Method

- `sample <pid> N 1 -mayDie` on the release binary; the script runs on a
  worker thread (the main thread sits in `pthread_join`), so attribution picks
  the busiest non-waiting thread. `temp/prof/entry2.py` attributes each sample
  to the runtime helper the JIT code called (inclusive) and lists leaf symbols.
- `LAMBDA_MIR_DUMP_PATH=<f> <exe> run x.ls` on the release binary; the
  per-function helper-call histogram is `temp/prof/callhist2.py <dump>`.
- Micro-experiments in `temp/prof/mb/*.ls` isolate one mechanism each.
- `LAMBDA_GC_STATS=1` for collection counts and mark time.

### 2.2 The text rows

| Row | v38 JIT ms | Node ms | ratio | top runtime entries (share of samples) |
|---|---:|---:|---:|---|
| text_search | 15,222 | 782 | 19.5x | JIT code 54%, **`fn_index` 34%**, **`fn_len` 12%** |
| log_pipeline | 6,340 | 947 | 6.7x | JIT 27%, **`fn_split` 24%**, **`fn_substring` 10%**, GC 6%, `memcmp` 6% (string `==`), `fn_map_set` 5%, `lambda_module_const_at` 3%, type checks 2.5%, `cow_path_borrow` 1.5% |
| three_way_merge | 3,649 | 967 | 3.8x | **`fn_split` 32%**, JIT 22%, **`fn_index` 18%**, GC 5%, `lambda_type_check`+`lambda_type_matches` 8.7%, `fn_join2` 3.5%, `pn_push` 3%; leaf **`str_utf8_char_to_byte` 10.5%** |
| prettier_ast | 1,121 | 99 | 11.3x | JIT 53%, GC 5%, `lambda_type_matches` 4.4%, `map_shape_field_to_item` 4.4%, `lambda_type_check` 4.2%, `fn_member_by_id` 2.2%, `lambda_module_name_id_at` 2.2% |

What the MIR says the "JIT code" is doing (helper calls per function, from
the dumps):

- `_naive_search` (text_search): per inner iteration `fn_len` (C call, not
  hoisted — `len(pattern)` on an immutable parameter), the guarded inline
  array read (tag 17, bounds, load, **box to Item**), then **`fn_eq` on the two
  boxed ints**, plus the parameter error-tag checks (`ursh 56; eq 27`) re-run
  inside the loop. Five C calls per compare. `int2it_lane`×4, `fn_len`×5,
  `fn_index`×2, `fn_eq`×1 per function.
- `_merge_words` (three_way_merge): `fn_split2`×3, `fn_eq`×6 (string `==` on
  untyped parameters takes the generic `fn_eq`; the inline string compare at
  `transpile-mir.cpp:9647` requires both static types to be STRING),
  `lambda_type_check`×3 (one per `let x = split(...)` declaration — the
  sysfunc result is re-admitted against its own inferred contract),
  `lambda_module_const_at`×7 (each string literal is a C call in the main
  module; the inline two-load form exists only for satellites, `:5127`).
- `_set_field` (log_pipeline): `fn_eq`×8 + `lambda_module_const_at`×8 (the
  8-way `key == "level"` chain), `fn_map_set`×8, `cow_capture_value`×5;
  `_add_to_group` is reached through its `_b` adapter with
  `fn_member_by_id`×7 + `fn_add`×4 (boxed adds on map fields).
- `_render_doc` (prettier_ast): 1,190 instructions, 150 calls:
  `fn_member_by_id`×32, `lambda_type_check`×23, `lambda_module_const_at`×19,
  `fn_eq`×11 (the `doc.kind == "text"` chain), `lambda_item_resolve_pending`×9.
  `_print_node`: 3,871 instructions, 521 calls (`fn_member_by_id`×89,
  `lambda_module_const_at`×71, `array_push`×63, `lambda_type_check`×42,
  `fn_eq`×36). The source's functional accumulators (`acc ++ [x]`,
  `acc ++ rendered.value` through recursion) copy on every step.

Micro-experiments (release, `LAMBDA_TIER=jit`):

| Experiment | Result | What it proves |
|---|---|---|
| text_search with `int[]` on the search-function parameters (nothing else changed) | 15.2 s → **7.15 s** | typed params buy 2.1x; the remaining 9x over Node is the typed lane itself: the element compare is lowered `i2d`, `i2d`, `deq` (two int lanes compared through doubles, `:9381`) and `fn_len_a` is still a C call per iteration because the inline `len` witness accepts only guarded locals and module bindings, not typed parameters (`mir_guarded_array_num_witness`, `:6853`) |
| index a `join`-built 70 KB string 11,000 times vs the same string built with `++` | **377 ms vs 0.09 ms** | list join's `fn_join2` sets `is_ascii = 0` ("safe default", `lambda-eval.cpp:6468`), so `s[i]` on a joined string walks UTF-8 from the start; binary `++` already propagates the flag; this is three_way_merge's 10.5% `str_utf8_char_to_byte` |
| 8-way `key == "literal"` chain, 2M calls: untyped param vs `string` param | 184 ms vs 125 ms (~18 vs ~12 ns per compare; Node ~1 ns) | both paths call a helper per compare (`fn_eq` vs `fn_str_eq_ptr`); a literal-RHS compare should be inline |
| `split` of a 12-word line, 1M calls, `let`-bound vs passed directly | 746 ms either way; **Node 30 ms** | the declaration check is not the cost; `fn_split` is 750 ns per call: one `list()`, 12 `heap_alloc`+`memcpy`+`str_is_ascii` parts, `array_push` growth |

GC (`LAMBDA_GC_STATS`): log_pipeline 114 collections / 40 ms mark;
three_way_merge 307 collections / 7.7 ms mark (the `large_*` tracker saw
11,000 adds); prettier_ast 22 collections but `finds=319,145` in the large-object
tracker. Collection is 5–6% of these rows; allocation *volume* from `split`
and substring copies is what drives it.

### 2.3 The typed tail against C2MIR

Profiles are on ×5–×10 loop copies (`temp/prof/*_x.ls`); histograms are from
the single-run dumps.

| Row | T/C2M | JIT code | dominant helpers (profile) | what the dump emits per hot function |
|---|---:|---:|---|---|
| deltablue2 | 106x | 60% | `fn_index` 16%, `cow_path_set_raw` 3.5%, map statics ~8% | `_c_recalculate` 2,234 insns / 230 calls: `fn_member_by_id`×50, `fn_index`×41, `int2it_lane`×38, `cow_path_set_raw`×24, `cow_prepare_write`×16, `fn_eq`×12. A nested store `w.vars[o].walkStrength = cs` is **8 C calls** (`fn_index`, `cow_prepare_write`, `cow_path_set_raw` ×2 for the spine reinstall, `fn_index`, `cow_prepare_write`, `cow_path_set_raw` for the leaf) plus side-stack spills; C is one store. Module-wide: `int2it_lane`×350, `fn_member_by_id`×284, `fn_index`×265, `cow_path_set_raw`×183 |
| havlak2 | 41x | 36% | GC 24% (loop copy; single run 3 collections), `fn_index` 6% | `lambda_type_check`×149 and `lambda_item_resolve_pending`×143 module-wide; `_hlf_find_loops` alone `lambda_type_check`×71 |
| hashmap2 | 33x | 40% | **`cow_path_borrow` 15%**, `fn_index` 14%, `array_push` 10.5% | `_hashmap_put`: each `int_slot_set(hm.values, idx, v)` emits `cow_prepare_write` + **`array_plain` + `array_push` (a heap-allocated 1-element path array)** + `cow_path_borrow` (RootFrame, `fn_index` walk) + the `_b` adapter call — 6 helpers and 2 allocations per integer store (`transpile-mir.cpp:5581`); C: `values[i] = v` |
| cd2 | 32x | 55% | `fn_index` 17%, `array_push` 9.5%, `fn_len` 4% | `_find_intersection` 1,711 insns: **`fn_mul`×29, `fn_sub`×23, `fn_add`×22, `fn_le`×11, `is_truthy`×15** — the vector math is boxed because the operands come out of untyped arrays; `_handle_new_frame`: `lambda_type_check`×43 |
| cube3d2 | 26x | — | (Tune20 T20-4 row) | `_run_cube` **21,712 insns / 953 calls**: `push_d`×181, `lambda_float_null_lane_c`×161, `array_push_capture`×108, `fn_array_set`×87, `ensure_typed_array`×68, `array`×52 — every `[x, y, z]` vector literal is a heap array built element by element and every float crosses a box |
| hyphen2 | 24x | — | | `_hyphenate`: `it2s`×12, `is_truthy`×7, `fn_string_ascii_at`×5, `fn_strcat`×4 — char-at-a-time string work through Items |
| base642 | 21x | — | | `_b64_encode`: `it2s`×13, `fn_strcat`×11, `fn_string`×9 — the nested `TABLE[a] ++ TABLE[b] ++ …` RHS chain is not covered by the owned-builder path (T20-2 note) |
| knucleotide2 | 18x | — | | **`_main` is lowered as a task frame** because of `io.read(INPUT_PATH)^`: 246 `lambda_async_frame_get_word`/`set_word` calls; the k-mer counter uses `vmap_new` + `fn_index_set` per k-mer |
| json_gen2 | 17.5x | — | | `fn_join`×30, `it2s`×25 |
| queens2 / towers2 | 17x / 15x | — | | `ensure_typed_array`×38 / ×34 — re-admission of the `int[]` parameter at **every** access (`_place_queen` ×17, `_move_disks` ×10); the T20-6 "per-call `int[]` param re-admission" item, blocked on two rulings |
| splay2 | 16x | 29% | **GC 18% + `gc_mark_item` 6%** (5 collections, **77 ms marking in a 300 ms run**), `cow_mark_shape_children` 3%, `map_set_cow` 3% | `_splay_node`: `lambda_type_check`×30, `push_d`×12, `heap_create_symbol`×4 (symbol creation inside the splay), `map_set_cow`×4 |
| json2 | 15x | — | | `_p_is_digit` 612 insns: **`fn_str_eq_ptr`×10 + `lambda_module_const_at`×10** to test one character against `"0"`…`"9"`; module-wide `lambda_type_check`×92, `lambda_item_resolve_pending`×99, `fn_str_eq_ptr`×33 |
| richards2 | 14x | 58% | `fn_index` 13%, `cow_path_set_raw` 6%, `fn_map_set` 3%, map statics ~10% | same family as deltablue2: `int2it_lane`×273, `fn_index`×106, `cow_path_set_raw`×104, `fn_member_by_id`×91 |
| brainfuck2 | 11x | — | | `_run_bf` 676 insns: `int2it_lane`×6, `lambda_int_lane_add_slow`×4, `fn_array_set`×2, `ensure_typed_array`×1 |

Cross-cutting counts (module-wide helper calls in the dump, a proxy for
"how much of the code is protocol"):

| Row | `int2it_lane` | `lambda_type_check` | `lambda_item_resolve_pending` | `lambda_module_const_at` | `cow_*` |
|---|---:|---:|---:|---:|---:|
| deltablue2 | 350 | 98 | 130 | — | 183 set_raw + 134 prepare |
| richards2 | 273 | 58 | — | — | 104 + 69 |
| havlak2 | 253 | 149 | 143 | — | — |
| cd2 | 219 | 101 | 29 | — | — |
| json2 | 77 | 92 | 99 | 76 | 20+ |
| splay2 | 14 | 96 | 15 | — | 23 capture + 12 prepare |
| prettier_ast (untyped) | — | ~150 | ~90 | ~250 | — |

The ANY census the transpiler prints (`any_census:`) agrees: deltablue2
total=1507 with `dynamic_name=409 member_shape=409 index_elem=204`;
prettier_ast total=588 with `dynamic_name=226 member_shape=220`; richards2
559; cd2 520 with `arith_operand=85 index_elem=66`.

**The structural fact from Tune20 §2.3 still governs the four graph rows**
(deltablue2, richards2, havlak2, cd2's tree): their nodes are untyped maps
because typing the holders detaches them at shaped-storage admission
(D3.2.4v2) while the sources lean on today's C4.1 aliasing. Nothing in the
declared-record machinery can reach them until C4.1 closes; the untyped map
path itself has to get cheaper, statically (D8.4.1v2).

---

## 3. Execution tracks (ranked; each separately land-able and gate-able)

The executable round contains only changes whose representation and semantic
license already exist. §3.9 holds the representation/ruling-dependent work;
none of it is pulled into an implementation track by proximity to a hot row.

### T22-0 — Benchmark basis and hygiene (do first; small)

**Implementation (2026-09-09): landed.** Canonical typed sources now exist
for all four text rows and preserve each established checksum. Checksum-verified
native C2MIR ports cover `prettier_ast`, `text_search`, `three_way_merge`, and
`log_pipeline`; Result40 prints both the 59-row comparable and 63-row complete
populations with per-cell provenance and raw-sample ranges.

- Add typed variants for the four text rows (`*2.ls`) under the measured
  annotation rules: `int[]` on search-function parameters, `string` on line
  parameters, no typed locals, and no typed holders on prettier's aliasing
  document maps.
- Add native C reference ports for the four rows under `text/c2mir/`, using
  `fast_diff.c` as the template, and pin their output/checksum equivalence.
  These ports use the benchmark-only C2MIR reference driver; they do **not**
  add a C-text Lambda backend, runtime ABI, or product CLI path (D8.1.1v9).
- Report both populations after the ports land: the unchanged 59-row series
  for comparison with v38, and the new 63-row series for complete coverage.
  Never compare a 59-row geomean directly with a 63-row geomean.
- Mark the four v38 typed cells as `untyped_fallback` in every table that uses
  them. Do not change knucleotide2's source or timing window in this track.

**Exit: passed.** 7/7 text rows have typed sources and native C reference
ports; typed/untyped provenance and both row populations are explicit in
Result40. The standalone C2MIR text runner also passes 7/7.

### T22-1 — Low-risk text leaf paths (`fn_join2`, literals, constants)

**Implementation (2026-09-09): landed.** `fn_join2` now preserves exact ASCII
provenance across String and Symbol inputs; short double-quoted literal RHS
comparisons emit an exact tag/length/pointer/byte sequence; module constants
load through the context-owned state for every module. `tune22_text_paths` and
the literal MIR fixture cover the semantic and emitted shapes.

- **1a list-join ASCII propagation.** The defect at
  `lambda-eval.cpp:6468` is `fn_join2`; binary `fn_join`/`fn_strcat` already
  propagates `left->is_ascii && right->is_ascii`. During `fn_join2`'s existing
  length pass, AND the flags of String parts and a String separator. Symbol
  has no `is_ascii` field, so scan each Symbol part and a Symbol separator once
  with the shared `str_is_ascii` helper; do not assume every text input carries
  the flag. Set the result flag only when every copied byte is proven ASCII;
  a false flag may remain conservative.
- **1b literal-RHS string comparison.** For `x == "lit"` / `!=` with open
  `x`, emit an error-safe tag test, constant length check, pointer identity,
  then an exact byte comparison. A ≥4-arm chain may dispatch on
  `(length, first byte)` only when the scrutinee is the same immutable binding,
  is evaluated once, empty literals have a no-byte-read bucket, and every
  collision bucket still performs full equality. The miss path must be
  observationally identical to `fn_eq` (D8.4.1v2).
- **1c context-owned constants without a helper call.** Use the existing
  two-load `state->consts[index]` form in the main module wherever
  `emit_module_state` is available. Re-read through the context-owned state;
  never bake the constant pointer itself (D5.4.3).

**Exit:** the join-built ASCII indexing micro loses
`str_utf8_char_to_byte`; literal-compare dumps contain no `fn_eq` /
`fn_str_eq_ptr` / `lambda_module_const_at` in the selected hot chains; context
rebinding and same-length/first-byte collision fixtures pass on both tiers.

### T22-2 — Typed and untyped integer-loop parity (text_search, cd2, brainfuck2)

**Implementation (2026-09-09): landed; performance forecast missed.** 2a
shares the in-band guard and poison-preserving slow arm across `==`/`!=` as
well as ordered comparison; the equality MIR fixture pins both arms. 2b
recognizes an already-admitted immutable ArrayNum parameter and reuses its
entry length cache, including in loops. 2c retains T21's wrapper-level
parameter-error guard; 2d retains T21's guarded homogeneous-call-site witness.
Result40 confirms the intended emission/correctness shape but measures
`text_search` at 14.97 s untyped and 5.30 s typed, not the 3.0 s / 1.5 s
forecast.

- **2a poison-correct int-lane comparison.** `VALUE_REP_INT_LANE` alone is
  not a finite-value proof: the lane includes `nan` and infinities (D2.2.2),
  and an optional occurrence may include the null sentinel (D2.5.1). Enter a
  raw `MIR_EQ`/ordered compare only on an edge where both operands are proven
  in-band, statically or by an emitted guard. The out-of-band edge must retain
  the current poison-correct widening: preserve `nan != nan` (S5.1.2), make
  every ordered comparison with `nan` false, order `±inf` correctly, and
  discharge null before comparison.
  Reuse `mir_int_lane_operand_proven_in_band` /
  `mir_emit_int_lane_pair_in_band` and the existing lane constants; extend the
  ordered fast/slow shape to equality rather than adding a second poison
  classifier or an equality-only representation test.
- **2b parameter array witness and exact `len`.** Extend
  `mir_guarded_array_num_witness` with a parameter whose entry admission or
  guarded call-site witness fixed its carrier. Compute `len(x)` once at entry
  or at a dominated loop preheader only when `x` is an admitted immutable
  parameter/`let` for which `len` is total. This is not general speculative
  LICM: a zero-trip loop must not acquire a new failing/effectful evaluation
  (S9.1.1, S12.2.1).
- **2c one parameter error boundary.** An error reaching an implicit
  `any \ error` parameter never enters the body (S11.4.3). Put the check at the
  boxed adapter/call boundary, or at the prologue before all user code for a
  boxed-only body; do not repeat it at each use. Elide it only from a producer
  proof that excludes error.
- **2d call-site element witnesses.** Publish an element-lane witness only
  when every admitted call site proves the same element domain and the wrapper
  has the exact runtime carrier/element guard with a boxed fallback. This is a
  static specialization under D8.1.1v9/D8.4.1v2 and keeps D8.3.1's one
  unboxed version per function; an inferred type without a runtime guard is
  not a representation proof.

**Exit:** the emission and semantic exit passes: the hot compare contains no
box→`fn_eq`→unbox round trip, and tier fixtures cover finite values, both
infinities, `nan == nan`, `nan != nan`, and nullable out-of-bounds reads. The
performance forecast is explicitly missed and remains a next-round profiling
item rather than a correctness failure.

### T22-3 — `split` allocation without a new String representation

**Implementation (2026-09-09): landed.** Fixed-string `split` and kept-
delimiter `split3` pre-count results and reserve backing for every append;
copied parts inherit a proven String/Symbol ASCII result without per-part
rescans. No String representation or ownership rule changed.

- Pre-count fixed-string separators and reserve/construct the result list at
  its final capacity. Reuse or extract the exact-capacity allocation shape
  already used by `array_fill`/fixed-result builders; do not duplicate backing
  ownership or relocation logic. Keep `array_push`'s verbatim element
  semantics (D2.6.5); remove only growth.
- Continue producing ordinary copied, NUL-terminated Strings. For a String
  source, every part may conservatively inherit the source's `is_ascii` bit;
  for a Symbol source, compute source ASCII once. A false flag may remain
  conservative, but a true flag must be exact.
- Apply the same no-rescan rule to the fixed-string `split3` path. Pattern
  splitting stays separate until its match-count/capture semantics can reserve
  exactly under S17.1.1.
- Re-measure before opening any view/slice representation. If pre-sizing and
  no-rescan improve `split_call` by less than 15%, close this slice as measured
  and move the remaining cost to T22-D1; do not smuggle views into this track.

**Exit:** fixed-string `split` performs no result-list growth and no per-part
ASCII scan; empty, leading/trailing, repeated, Unicode, Symbol, and `split3`
fixtures remain identical on both tiers and under forced GC.

### T22-4 — Elide only boundaries whose emitted producer proves success

**Implementation (2026-09-09): landed for text `split` success paths.** The
registry distinguishes the text overload's `string[]` success carrier from
open/ArrayNum calls and retains its error completion. MIR removes a receiving
boundary only for a direct text `split`/`split3` with total text/null operands
and an exact `string[]` destination; every other path retains the ordinary
check. `tune22_split_metadata` covers wrong-domain/error/null behavior and
`tune22_split_success_boundary` pins the removed JIT boundary (S11.4.2,
D8.4.1v2).

`lambda_type_check` + `lambda_type_matches` +
`lambda_item_resolve_pending` are 8.7% of three_way_merge and 149/143 sites in
havlak2, but AST result type alone is not the proof.

- Audit and complete sysfunc success/error metadata first. `split` is
  overloaded (text and ArrayNum forms) and can return `ItemError`; it must not
  be declared blanket `string[]`. Derive the success contract from the
  selected overload and preserve its error union under S11.4.2.
- Extend `mir_boundary_is_redundant` only when the concrete emitted producer
  establishes the exact success representation and its error completion has
  already been propagated or discharged. `len` on an admitted supported
  carrier is the simple case; `split`/`slice` require the call-site proof.
- A guarded field/element read may suppress a receiving `let` boundary only
  for the representation established on the guard's success edge. Its shared
  miss edge retains the normal check.
- `_b` adapter companion-result elimination remains T22-D2. A semantic return
  type never authorizes reading an unbuilt companion lane.

**Exit:** targeted module-wide check counts fall without changing any invalid
input or error result; wrong-domain, error-valued, nullable, and overload
fixtures match on JIT/interp before the performance gate is considered.

### T22-5 — Static COW path transport and unique-spine stores

**Implementation (2026-09-09): landed.** Short paths of member NameIds and
integer literals lower to `cow_path_borrow_fixed` with three rooted Item
operands, so they allocate neither a Lambda descriptor Array nor use
`array_push`. `cow_path_borrow` and the fixed ABI share one rooted walker,
preserving S9.2.2's link validation and detach/reinstall protocol; the generic
path reloads the rooted owner after descriptor allocation. T22-5b is a narrow,
safepoint-free direct store for a terminal static packed `int` field: after the
rooted borrow it rechecks the map kind, shape, and in-band Item tag, then writes
the inline integer payload. Pointer-backed integers and every unproven path
take the ordinary generic store, so no interior pointer crosses a safepoint and
no capture or barrier obligation is skipped (D4.4.4, D5.1.1v2, S9.3.1). The
borrow and direct-store semantic/MIR fixtures pin both the success and fallback
paths.

- **5a fixed-arity borrow descriptors.** Replace the heap `array_plain` +
  `array_push` path in `mir_emit_cow_path_borrow` with fixed operands for
  statically known member NameIds and literal integer indices (initially ≤3
  links). Dynamic or longer paths retain `cow_path_borrow`; no second general
  path walker is added.
- **5b one spine decision per statement.** Let one rooted runtime operation
  verify every link's ownership and shape. A success edge may enter a
  safepoint-free direct-store block; no interior slot pointer survives a call
  or allocation. The existing reinstall chain remains the miss path
  (D4.4.4, D8.4.1v2).
- The fast leaf store still performs the destination admission, write barrier,
  and S9.3.1 capture/share action. A unique spine does not make capture-by-value
  optional and does not prove a packed field's contract.

**Exit:** hashmap2's static borrow path allocates no path Array;
`COW_EXEC_PROFILE` copy counts are unchanged; forced-GC fixtures cover named
replacement values, packed fields, shared intermediate children, dynamic keys,
and the fallback reinstall chain. Then test the forecast hashmap2 ≤45 ms and
richards2/deltablue2 −20%; timing is not a correctness gate.

### T22-6 — String-expression construction and proven character lanes

**Implementation (2026-09-09): landed.** All-string left-associated append
chains reuse the exclusive assignment builder; chains with a non-string or
error-capable suffix retain generic join lowering. A typed String subscript
compared with a one-byte ASCII literal now calls an allocation-free comparator:
its ASCII arm reads one byte, while its UTF-8 arm mirrors `item_at`'s character
and absence semantics before comparing. `tune22_concat_chain` and
`tune22_ascii_char_compare` pin the builder and character paths.

- Fold a statically-string nested RHS `a ++ b ++ c` into the existing owned
  builder as a multi-append. Preserve left-to-right operand evaluation and
  freeze at the same publication boundaries as the landed assignment builder.
- Lower `cur == "0"` to a byte/ordinal comparison only when provenance proves
  that `cur` is exactly one ASCII character (for example, an in-bounds ASCII
  string index). An arbitrary string is not equivalent to its first ordinal:
  `"0x"` must not compare equal to `"0"`. Otherwise use T22-1b's exact literal
  comparison.

**Exit:** base642/json2 selected chains contain neither repeated `fn_strcat`
conversions nor unsound first-character comparisons; empty, multi-byte, and
multi-character fixtures pass on both tiers.

### T22-7 — Async-frame liveness, not a benchmark rewrite (knucleotide2)

**Implementation (2026-09-09): landed.** Named locals retain ordinary exact
GC roots between suspension edges. Before each resume label is lowered, the
transpiler assigns slots only to source bindings live after that particular
call; restore reloads only that same state-local set, so a slot belonging to a
different await cannot overwrite an operand spill. A real suspend saves those
locals only on the `ItemTaskSuspended` edge; bindings introduced by its
synchronous tail do not incur frame traffic. Operand spills remain conservative
for evaluation replay, but boxed `Item` arguments now use the traced
`lambda_async_frame_set/get` half, while raw-word slots hold only non-GC
scalars. This preserves parked task handles and the precise-root rule of
D5.1.1v2/D5.3.4. `tune22_async_liveness` pins the named-local and traced
operand-spill shape; suspension, resume, error, loop-exit, and return fixtures
cover the task-frame paths.

Keep the existing source and timed `io.read(INPUT_PATH)^`. Moving the read
outside `__TIMING__` would change the workload and hide the codegen cost.
Engine-side, allocate task-frame slots only for values live across an actual
suspension point and emit save/restore only around may-suspend edges. After the
single read resumes, the synchronous k-mer tail must not spill every temporary
merely because its enclosing `pn` is may-await (D5.1.1v2, D8.1.1v9).

**Exit:** the unchanged benchmark output and timing boundary are pinned; MIR
frame get/set counts are proportional to the live-across set at `io.read`, not
the number of later expressions; async suspension/resume fixtures stay green.

### T22-8 — Reprofile and close the executable round

**Implementation (2026-09-09): landed.** Result40 is a clean release build
with profile instrumentation rejected, Test262 gated at 40,261 / 40,261, and
15 raw samples for every recorded engine/variant cell. It archives the exact
binary, prints median `[minimum–maximum]` cells, keeps the full sample list in
JSON, and excludes Node's known `nbody` wrong output rather than folding it
into a geomean. The 59-row and 63-row ledgers in §1.1 are deliberately
separate. The performance forecasts missed in §5 are recorded as such; no
§3.9 design item was started to force an aggregate number.

After each landed track, record per affected row: before/after median, speedup
factor, contribution `ln(speedup) / row_count` to each applicable geomean,
helper/instruction delta, code-size delta, and correctness gates. Re-run the
§2 profiles after T22-1 through T22-3 before starting T22-4/T22-5; static call
counts rank candidates but do not substitute for dynamic attribution.

Do not start a §3.9 item merely because an aggregate target remains open. The
round closes on the committed per-track gates; the aggregate ledger determines
which design item earns the next proposal.

### 3.9 Ruling/design queue (not executable in this round)

- **T22-D1 — String slices/shared storage.** A parent-backed String is a new
  physical form under D2.1.6. Specify allocation, tracing/compaction of the
  parent edge, equality/hash bytes, NUL-terminated C consumers, builder
  interaction, pooled/static parents, tiny-slice/large-parent retention, and
  forced-GC gates before implementation. Reuse the existing shared-byte-storage
  shape where suitable; do not add a second unaudited ownership scheme.
- **T22-D2 — Companion result lanes.** Design `_b` adapter result provenance
  before removing `lambda_item_resolve_pending`; this is the refused T20-3
  member-result case, not an emitter cleanup.
- **T22-D3 — Small-vector scalar replacement.** Represent a proven
  non-escaping `[x,y,z]` as three MIR scalar values and materialize an ArrayNum
  at the first escaping/opaque use. Do not place a fake container in the
  unscanned number stack: D5.1.1v2/D5.2.2v3 reserve it for scalar homes, while
  runtime container helpers expect the ordinary GC/container contract.
- **T22-D4 — Typed `var` array admission.** Resolve structural occurrence
  contract equivalence and mutable borrowed-container representation/rebind
  (DO29/CW33) before caching a raw `int[]` carrier for queens2/towers2. Value
  parameters already admit at entry; the hot cases are `var` parameters whose
  representation can legitimately be invalidated.
- **T22-D5 — Graph identity and typed records.** Close the C4.1 catalog under
  S9.1.2, then decide the explicit-identity rewrite that lets graph nodes use
  D3.4.1 direct slots. Do not annotate the current graph sources into changed
  behavior.
- **T22-D6 — Nursery/generational GC.** After T22-3, reopen only for a row
  still spending ≥10% in collection. A sticky/generational design must include
  old-to-young barriers and precise-root gates; allocation volume alone is not
  the gate.

---

## 4. What NOT to do (inherited and new)

- **No inline caches, feedback vectors, or patched code** — D8.4.1v2. Every
  fast path is static specialization with an observationally identical miss.
- **No raw int comparison from `VALUE_REP_INT_LANE` alone.** Prove in-band or
  handle `nan`, infinity, and optional null according to S5.1.2/D2.5.1.
- **No String views in T22-3.** Rooting the parent is necessary but not
  sufficient; T22-D1 owns the full D2.1.6 representation/lifecycle decision.
- **No stack-backed fake Array/ArrayNum in the number stack.** T22-D3 uses
  scalar replacement and materializes an ordinary container on escape.
- **No sysfunc boundary elision from a declared/AST result type alone.** The
  emitted success carrier and error completion are the proof (S11.4.2).
- **No typed contracts on graph benchmark sources** until T22-D5 closes the
  C4.1 catalog (S9.1.2). Do not “fix” deltablue2 by annotating it.
- **Do not add `int[]`/`float[]` to locals** in new typed text variants; the
  measured annotation rules permit parameters only.
- **Member-result lanes are not an implementation slice.** T22-D2 owns the
  companion-lane design.
- **No benchmark-source rewrite to hide runtime cost**, including hand-hoisted
  `len()` or moving knucleotide2's read outside its existing timer.
- Native C reference ports stay benchmark-only; the legacy Lambda C-text
  backend remains removed (D8.1.1v9).
- crypto_sha1 / hyphen historical cells retain the Tune21 §4 caveats.

---

## 5. Targets and accounting

The executable round commits to the per-track correctness/emission gates above.
Performance figures below are checkpoints or forecasts until an interleaved A/B
assigns them to a landed mechanism. The former 0.70 typed/Node and 4.0
typed/C2MIR numbers remain stretch goals, not acceptance criteria without a
row-by-row contribution ledger.

| Metric | v38 basis | Result40 | status |
|---|---:|---:|---|
| text_search JIT ms (untyped) | 15,222 | 14,970 [14,510–15,400] | 3,000 forecast missed; emission/correctness gate passed |
| text_search typed JIT ms | 7,150 | 5,300 [4,960–5,630] | 1,500 forecast missed; substantially faster than untyped but incomplete |
| three_way_merge JIT ms | 3,649 | 3,450 [3,370–3,510] | 2,400 forecast missed |
| log_pipeline JIT ms | 6,340 | 5,970 [5,890–6,180] | 4,000 forecast missed |
| prettier_ast JIT ms | 1,121 | 912 [851–932] | 900 forecast narrowly missed; ≤500 remains deferred |
| MIR typed / Node geo (63 rows) | 0.91x | 1.20x | complete-suite ledger; not comparable to 59-row history |
| MIR typed / Node geo (shared 59) | 0.78x | 1.07x | comparable trend series; regression is recorded, not hidden |
| MIR typed / C2MIR geo (shared 59) | 5.05x | 5.34x | 4.6 forecast / 4.0 stretch missed |
| MIR typed / C2MIR geo (new 63) | no v38 basis | 5.49x | complete coverage established; do not splice into the 59-row trend |
| Rows >20x C2MIR (shared 59) | 7 | 7 | ≤6 checkpoint missed |
| Text rows with typed source + C reference | 3/7 | **7/7** | committed hygiene gate passed |

The ~0.85 checkpoint is the four text rows' target factors applied to the
63-row geomean with every other row held fixed; those four improvements alone
cannot justify 0.70. Likewise, prettier_ast's measured functional accumulator
copies (`acc ++ ...`) have no executable track here, so ≤500 ms cannot be a
round commitment. After every track, update the contribution ledger before
revising either aggregate target.

---

## 6. Gates, method, recipes

### 6.1 Universal acceptance

- Use a release build only (`make release`) on a quiet machine. Run old and
  new binaries in an interleaved order against the same input and checksum;
  use at least five pairs for long rows and at least 15 pairs (or enough loop
  copies for a ≥1 s timed region) for short rows. Report medians and the full
  sample range rather than the best run.
- Keep the benchmark source, input, tier, timing boundary, loop multiplier,
  and output verification identical within each pair. T22-0's new typed/C
  ports establish new baselines; they are not retroactive v38 measurements.
- Require `make test-lambda-baseline` plus the affected unit/integration tests
  to pass, with identical outputs on `LAMBDA_TIER=jit` and `interp`; run the
  `auto` sweep as the end-to-end tier check. A performance win never waives a
  semantic, error, ownership, or forced-GC failure.
- Archive, per landed track, the binary/commit identity, commands, raw timing
  samples, checksums, relevant MIR dump, helper histogram, and profile under
  `temp/prof/t22/<track>/`. Keep the shared-59 and complete-63 aggregate
  ledgers separate as required by T22-0.

### 6.2 Track-specific correctness and emission gates

| Track | Required fixtures before timing | Required emitted/runtime evidence |
|---|---|---|
| T22-0 | typed and C-port checksums match the original four text rows | every report cell records source provenance; both 59- and 63-row populations are printed |
| T22-1 | String/Symbol parts and separators; ASCII/Unicode join; empty literals; equal-length and first-byte literal collisions; error operands; context rebinding | selected join index has no `str_utf8_char_to_byte`; selected literal chains have no equality/constant helper calls |
| T22-2 | finite extrema, `nan`, `±inf`, nullable miss, error at parameter boundary, and a zero-trip loop | hot element compare has no Item boxing or `fn_eq`; each admitted parameter has one dominating witness/boundary |
| T22-3 | empty input/separator, leading/trailing/repeated separator, Unicode, Symbol, `split3`, and forced GC | fixed-string split has one exact-capacity list-backing allocation, no growth, and no per-part ASCII scan |
| T22-4 | every selected overload plus wrong-domain, error-valued, nullable, guarded success, and guard-miss cases | each removed boundary cites the emitted producer proof; miss/error edges retain the ordinary check |
| T22-5 | replacement capture, packed-field admission, shared intermediate child, dynamic key, long path, and forced GC | static path allocates no path Array; success block is safepoint-free; copy counts and barriers remain correct |
| T22-6 | left-to-right evaluation, error propagation, empty/multibyte/multichar comparisons including `"0x" != "0"` | chosen append chain is one builder; ordinal compare appears only with a one-ASCII-character proof |
| T22-7 | unchanged knucleotide2 checksum/timing boundary and real suspend/resume/error paths | frame slots and get/set operations correspond to the values live across the actual await |

### 6.3 Reproduction recipes

- Profiles: `LAMBDA_TIER=jit <exe> run temp/prof/<row>_x.ls & sample $! N 1
  -mayDie -file temp/prof/t22/<track>/<row>.sample`, then
  `python3 temp/prof/entry2.py <sample>`.
- Dumps: `LAMBDA_MIR_DUMP_PATH=temp/prof/t22/<track>/<row>_mir.txt <exe>
  run <row>.ls`, then `python3 temp/prof/callhist2.py <dump> 8`.
- The §2.2 micro-experiments are checked in as
  `test/benchmark/ls_micro/{string_index,split_call,literal_compare,
  text_search_typed_params}.ls`; that directory's README carries the v38
  baselines. Add focused semantic fixtures to the normal test suites rather
  than treating a microbenchmark checksum as complete correctness coverage.

### 6.4 Completion record

- `make test-lambda-baseline`: **5,109 / 5,109** passed after the final async
  frame changes, including MIR forced-GC stress, MIR emission/ratchets, and
  concurrency coverage.
- `./test/test_js_test262_gtest.exe`: **40,261 / 40,261** baseline passes,
  zero regressions. The standardized release workflow repeated that gate before
  Result40.
- `python3 test/benchmark/run_c2mir_benchmarks.py --suite text --timeout 120`:
  **7 / 7** native C2MIR text benchmarks passed.
- Final release command:
  `python3 test/benchmark/run_standard_benchmarks.py --typed --engines mir,c2mir,nodejs --runs 15 --timeout 240 --cooldown 10 --results-output test/benchmark/benchmark_results_v40.json --report-output test/benchmark/Overall_Result40.md --report-title 'Lambda Benchmark Results — Tune22' --log-dir temp/benchmark_v40`.
