# Tune 22: Result38 — the Text Rows and the Typed-vs-C2MIR Tail

- **Date:** 2026-09-09
- **Status:** PROPOSAL. Nothing in this round is implemented. §2 is measured
  evidence (archived release binary `test/benchmark/exe/lambda-v38-b23793e832`,
  `LAMBDA_TIER=jit`, workload-only `__TIMING__`, macOS `sample` on ×N loop
  copies, MIR dumps via `LAMBDA_MIR_DUMP_PATH`); §3 ranks the tracks; §4 lists
  what not to do; §5 sets targets.
- **Input:** `test/benchmark/Overall_Result38.md` / `benchmark_results_v38.json`
  (2026-09-08, commit `b23793e83`). Working files: `temp/prof/` (dumps,
  samples, loop copies, `callhist2.py`), `temp/prof/mb/` (micro-experiments).
- **Related:** `Lambda_Impl_Tune21.md` (T21-#; §4 inherited decisions stand),
  `Lambda_Impl_Tune20 (done).md` (T20-# status table §2.5 and the §2.3
  structural finding), `../Lambda_Tune_Typed_Vs_C2MIR.md` (Result18 mechanism
  catalog), `../Lambda_Design_Runtime_COW.md` (CW29–CW34),
  `../Lambda_Design_Compiling_Lane.md` (ValueRep / `MirValue`).
- **Formal authority cited:** **D8.4.1v2** (no inline caches; specialization
  over caching), **D8.1.1v9** (tiers; satellite images), **D8.3.1** (one unboxed
  version per function), **D3.4.1** (packed map storage), **D3.2.4v2** (a named
  map contract is a reification), **D2.5.1** (optional lanes), **D2.6.5**
  (append API), **D5.4.3** (no context value at a baked address), **D4.4.4**
  (RMW place copies, CW34), **S4.1.2** (int is closed and total), **S9.1.2** /
  **S9.3.1** (sharing unobservable; capture by value), **S12.2.1** (`let` and
  parameters are immutable), **S17.1.1** (`split` follows ECMAScript).

---

## 1. Where v38 stands

| Metric | v36 | v37 | **v38** | Tune21 round target |
|---|---:|---:|---:|---:|
| MIR (typed) / C2MIR geo (59 rows) | 5.67x | 5.12x | **5.05x** | ≤ 4.0x |
| MIR (typed) / Node geo (63 rows) | 0.85x | 0.78x | **0.91x** † | ≤ 0.70x |
| MIR (untyped) / Node geo (63 rows) | 1.37x | 0.97x | **1.08x** † | ≤ 1.0x |
| Rows > 20x C2MIR (typed) | 8 | — | **7** | ≤ 3 |
| Text rows with a typed source | 3/3 | 3/3 | **7/7** (T22-7) | — |

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
| index a `join`-built 70 KB string 11,000 times vs the same string built with `++` | **377 ms vs 0.09 ms** | `fn_join` sets `is_ascii = 0` ("safe default", `lambda-eval.cpp:6468`), so `s[i]` on a joined string walks UTF-8 from the start; this is three_way_merge's 10.5% `str_utf8_char_to_byte` |
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

## 3. Tracks (ranked; each separately land-able and gate-able)

### T22-1 — String runtime: `is_ascii`, `split`, substring, literal compares (text rows; days)

The four cheapest, best-measured items in this round. All are runtime or
emitter changes with existing tests as the gate.

- **1a `fn_join` propagates `is_ascii`** (`lambda-eval.cpp:6468`): the result
  is ASCII iff every part and the separator are; every input already carries
  the flag, so this is an AND across the parts that the length pass already
  walks. Measured: three_way_merge's 10.5% (`str_utf8_char_to_byte`) goes to
  zero; the micro is 377 ms → 0.09 ms. Same audit for every other producer
  that writes `is_ascii = 0` as a default (`grep -n "is_ascii = 0"`).
- **1b `fn_split` allocation**: pre-count separators to size the result list
  once; inherit `is_ascii` from the source instead of re-scanning each part;
  allocate the parts as **slices of the source** (`String` gains a
  `is_slice` form pointing into a rooted parent) — strings are values and
  `let`/parameters are immutable (S12.2.1), so a slice is observationally a
  copy. V8's sliced strings are the precedent; this is what makes Node's
  `split` 30 ns against Lambda's 750 ns. The same representation serves
  `fn_substring` / `slice` (log_pipeline's 10%).
- **1c literal-RHS string compare inline**: `x == "lit"` with an untyped `x`
  must not take `fn_eq`. Emit: tag test (STRING), length compare against the
  literal's constant length, then pointer identity, then `memcmp` (or the
  interned-literal identity when `x` came from the same literal). Covers
  `set_field`'s chain, `doc.kind == "text"`, `node.type == …`, `record.service
  == "api"`. Follow-up **1c′**: a chain of literal compares on one scrutinee
  (`if (k == "a") … else if (k == "b") …`, ≥4 arms) lowers to a switch on
  `(length, first byte)`; json2's `p_is_digit` (10 arms) and log_pipeline's
  `set_field` (8 arms) are the customers.
- **1d string constants without a call**: the two-load inline form
  (`state->consts[index]`, `transpile-mir.cpp:5127`) exists only for
  satellites; the main module emits `lambda_module_const_at` as a C call at
  every use (prettier `_print_node` ×71). Use the inline form everywhere the
  module state register is available (D5.4.3 is satisfied the same way the
  satellite form satisfies it: the state is re-read, nothing is baked).

**Exit:** three_way_merge ≤ 2.4 s, log_pipeline ≤ 4.0 s, prettier_ast ≤ 0.9 s
(from 3.65 / 6.34 / 1.12), `make test-lambda-baseline` green, the string
fixtures identical on both tiers.

### T22-2 — Untyped and typed loop parity on integer arrays (text_search, cd2, brainfuck2; the T21-2 family)

Three emitter gaps, all visible in `_naive_search`:

- **2a int-lane compare**: when both operands carry `VALUE_REP_INT_LANE` —
  which a guarded array element read already establishes — `==`/`!=`/`<` must
  lower to `MIR_EQ`/`MIR_LT`, not `i2d, i2d, deq` (`:9381`, `use_float`), and
  never to `fn_eq` on the boxed Items (`:9711`). Today the guarded read boxes
  its result and the compare unboxes it again through `fn_eq`.
- **2b `len()` on parameters and hoisting**: extend
  `mir_guarded_array_num_witness` (`:6853`) with arm (b), a parameter whose
  contract or `ensure_typed_array` admission fixed the carrier; and hoist
  `len(x)` for an immutable binding (`let`/parameter, S12.2.1) that is not
  written in the loop — the value cannot change, so one call per loop entry
  is exact. `fn_len` is 12% of text_search and 4% of cd2.
- **2c parameter error-tag checks once per entry**: the `ursh 56; eq 27`
  test on each parameter is emitted at every use inside the loop; a
  parameter is immutable, so the check belongs at function entry (or nowhere
  once T21-2e's `any \ error` elision applies).
- **2d plain-array element witnesses from call sites**: `text` in
  text_search is a plain `Array` of ints built by `push`; D8.1.1v9's
  module-wide call-site inference already proves the argument at every call
  is that array — publish the element lane into the callee's parameter
  witness so 2a/2b fire without annotations (the untyped row must reach its
  typed row, Result32 finding #1).

**Exit:** text_search untyped ≤ 3 s, typed variant ≤ 1.5 s (from 15.2 / 7.15);
cd2 `_find_intersection` boxed arithmetic gone from the dump (needs the T20-3
companion-lane work for values read out of untyped arrays — see 2d).

### T22-3 — Boundary checks that re-prove known facts (havlak2, json2, splay2, cd2, three_way_merge)

`lambda_type_check` + `lambda_type_matches` + `lambda_item_resolve_pending`
are 8.7% of three_way_merge and 149/143 sites in havlak2. Extend
`mir_boundary_is_redundant` (`:4274`):

- a declaration whose initializer is a **sysfunc row with a known return
  contract** (`split` → `string[]`, `slice` → `string`, `len` → `int`): the
  producer already fixed the type; admitting it again is the T20-3 duplicate
  crossing, one frame lower;
- a value that just passed a **guarded read** (T20-1a) or a guarded element
  load carries its lane — no check at the receiving `let`;
- `_b` adapter returns (`lambda_item_resolve_pending` ×130 in deltablue2):
  when the caller consumes the companion lane directly, the pending
  resolution is dead — this is the T20-3 "#2 needs companion lanes" item and
  is the one piece here that is a design slice, not an emitter fix.

**Exit:** module-wide `lambda_type_check` count halves on havlak2/json2/splay2
with identical goldens on both tiers.

### T22-4 — The nested-store protocol on untyped graphs (deltablue2, richards2, hashmap2, log_pipeline's `add_to_group`)

Two independent halves.

- **4a static path descriptors for CW34/CW33 transports.** `int_slot_set(hm.values,
  idx, v)` builds its borrow path as a **heap array at runtime**
  (`array_plain` + `array_push`, `:5582`) and walks it in `cow_path_borrow`
  with a `RootFrame` and `fn_index` per link; `cow_bind_rmw_handle(root, value,
  count, k1, k2)` already shows the right shape — keys as immediates. Give the
  borrow transport the same fixed-arity descriptor (member NameIds / int
  literals as operands, ≤3 links), so a `var`-param borrow through one field
  is one call and zero allocations. hashmap2's 15% + 10.5% and every
  `pn f(var x, …)` call on a field go through this.
- **4b one reinstall per statement, not per link.** The spine protocol emits
  `cow_prepare_write` + `cow_path_set_raw` at **every** link of
  `w.vars[o].walkStrength = cs` (8 calls). With T20-1d's guarded store landed,
  the emitter can test unshared-ness of the whole spine once
  (`cow_bind_rmw_handle`'s spine test already does this at runtime) and take
  a direct leaf store when every link is unique; the reinstall chain stays as
  the miss path. deltablue2 has 183 `cow_path_set_raw` sites; richards2 104.
- **4c the road to the C ceiling** is not in this round: closing the C4.1
  catalog (S9.1.2 sharing unobservable), then rewriting the graph ports with
  explicit identity so their nodes can be **typed records with direct slot
  stores** (Tune15 B3 lowering, D3.4.1). Tune20 §2.3's corollary stands: the
  untyped sources will change behaviour when C4.1 closes anyway. This is the
  only path below ~10x on deltablue2; it needs a ruling, not a slice.

**Exit:** hashmap2 ≤ 45 ms (from 90), richards2/deltablue2 −20% each, no COW
semantics change (`COW_EXEC_PROFILE` copies unchanged).

### T22-5 — Small typed vectors and typed-array admission (cube3d2, queens2, towers2, brainfuck2)

- `[x, y, z]` float vector literals that do not escape a function: build in
  the side number stack / a frame slab instead of the heap and skip
  `push_d` boxing between vector ops (`_run_cube` has 52 `array` + 108
  `array_push_capture` + 181 `push_d` sites). The stack-allocation-of-boxes
  design in `vibe/Lambda_Tuning_Proposal.md` Part 2 is the reference; the
  escape rule is "consumed by a native float op or a typed store in the same
  function".
- `ensure_typed_array` per **access** on an `int[]` parameter (queens2 ×38,
  towers2 ×34): admit once at entry and keep the raw carrier in a register;
  T20-6 recorded this as blocked on two rulings — restate them and get the
  rulings, because the same defect makes every "annotate the parameter"
  advice pay a per-access tax.

### T22-6 — Strings as values in the char-at-a-time rows (base642, hyphen2, json2, json_gen2)

- `fn_strcat` converts its operands through `fn_string`/`it2s` even when both
  are statically strings; the nested RHS chain `TABLE[a] ++ TABLE[b] ++ …`
  should fold into the owned builder (`s = s ++ …`, T20-2's landed
  mechanism) as a multi-append.
- A single-character comparison against a literal char (`cur == "0"`) should
  be an `ord` compare on the ASCII lane (the interned 1-char table already
  exists; json2 spends 10 helper calls per digit test).

### T22-7 — Benchmark hygiene (do first; small) — **LANDED 2026-09-09**

The four rows now have a typed variant and a native port, so the Text suite is
7/7 on both and the ceiling table covers it.

**Typed variants** (`text_search2.ls`, `three_way_merge2.ls`,
`log_pipeline2.ls`, `prettier_ast2.ls`, each with its `.txt` golden).
Parameters only, no typed locals, no named contracts on the document or record
maps. Checksums match the untyped goldens and JIT/interpreter output is
identical on all four.

| Row | untyped | typed | typed/untyped |
|---|---:|---:|---:|
| text_search | 15.28 s | **7.11 s** | 0.47 |
| prettier_ast | 1.15 s | 1.05 s | 0.91 |
| three_way_merge | 3.65 s | 3.62 s | 0.99 |
| log_pipeline | 6.55 s | 6.44 s | 0.98 |

Three annotation facts fell out of building them, all measured on the v38
archive, and all of them are evidence for the tracks above rather than for
annotating harder:

1. **A `pn` return contract costs about 1.5x when the function returns from
   inside a loop.** Adding `int` returns to text_search's three search
   functions took it from 7.8 s to 12.2 s; typed `int` locals in the same
   functions cost a further ~1.2x. Only the parameter contracts pay, because a
   parameter is admitted once per call while a return contract is re-admitted
   per return. This is the T20-6 annotation-tax ledger, restated on a new row.
2. **`string` parameters do fire the inline compare, and the admission
   cancels it.** three_way_merge's dump goes from `fn_eq` 11 / `fn_str_eq_ptr`
   0 to `fn_eq` 6 / `fn_str_eq_ptr` 7, and log_pipeline's from 13 / 0 to
   5 / 10 — so the annotation reaches the `transpile-mir.cpp:9647` path
   exactly as intended. But `lambda_type_check` sites rise with it (10 to 15,
   and 12 to 21) and both rows come out flat. **T22-1c is therefore the right
   place for this win, not the source**: the inline literal compare belongs in
   the untyped lane, where there is no admission to pay for it.
3. **text_search is the one row where annotation alone is worth 2.1x**, and
   §2.2 already says what the remaining ~9x over Node is (T22-2a/2b).

**C2MIR ports** (`text/c2mir/{text_search,three_way_merge,log_pipeline,
prettier_ast}.c`), registered in `run_c2mir_benchmarks.py`; `7/7` pass with the
same checksums as the `.ls`. `prettier_ast.c` embeds the JSON fixture as a
string literal (the `awfy/c2mir/json.c` pattern) and parses it into typed
`Node` structs once, then rebuilds the document IR on each of the 256
iterations; its formatted output is byte-identical to the Lambda port's, so the
shared `text/prettier_ast.txt` golden — now the full expected stdout rather
than a one-line marker — checks both. The one-off parse is inside the C timer
and outside Lambda's, and is well under 1% of that row.

Ceiling for the four new rows, first measurement (single run, machine not
quiet — treat as provisional):

| Row | MIR-T | C2MIR | MIR-T / C2MIR |
|---|---:|---:|---:|
| prettier_ast | 1.05 s | 25.7 | **40.9x** |
| text_search | 7.11 s | 538 | 13.2x |
| log_pipeline | 6.44 s | 645 | 10.0x |
| three_way_merge | 3.62 s | 840 | 4.3x |

prettier_ast at ~41x lands it in the widest-gap cohort with deltablue, havlak
and hashmap, and unlike those three it is not a mutable aliasing graph — it is
allocation and dispatch over immutable document nodes, which makes it the most
tractable member of that group.

⚠ log_pipeline is bimodal under load (2.5–3.2 s and 6–7.3 s at 60 rounds on
the same binary and source); nine interleaved pairs put typed at 0.92 of
untyped by median and faster by minimum, but a publishable A/B for that row
still needs a quiet machine.

**Still open in this track:**
- knucleotide2's `main` should not be a task frame: move the
  `io.read(...)^` outside the timed region into a helper, or (engine side)
  keep frame-word spills only for locals live across a suspension point —
  246 frame calls for a function that never suspends is a codegen defect.
- The Result38 report should mark the four rows' typed cells as "reuses
  untyped" in the ceiling section as well, not only with `*` in the table.

### T22-8 — GC on the allocation-heavy rows (splay2, log_pipeline, three_way_merge)

splay2: 5 collections, 77 ms of marking in a 300 ms run — pacing already grew
the thresholds (data 256 MB, object 210 MB); the cost is the live graph
(8,000 nodes with payload arrays) marked five times. A generational or
sticky-mark nursery is the lever, not more pacing. log_pipeline (114
collections) and three_way_merge (307) are volume from `split`; T22-1b
removes most of it. Gate as T20-5 was gated: ≥10% collector share on the row
after T22-1 lands.

---

## 4. What NOT to do (inherited and new)

- **No inline caches, no feedback vectors, no patched code** — **D8.4.1v2**,
  LC1v2 (both lanes). Every fast path above is a static lowering behind a
  compile-time guard.
- **No typed contracts on the graph benchmark sources** until the C4.1
  catalog closes (Tune20 §2.3, §4). T22-4c is the ruling to seek; do not
  "fix" deltablue2 by annotating it.
- **Do not add `int[]`/`float[]` to locals** in the new typed text variants
  (3–5x regression, `typed-benchmark-annotation-rules`); parameters only.
- **Member-result lanes are not a slice** (T20-3 #2 refused on soundness);
  T22-3's third bullet is the companion-lane design, and it is named as such.
- **Do not make `split` return views without rooting the parent**: the slice
  form must keep the source alive through the GC (precise rooting, D5).
- **No benchmark-source rewrites to hide a runtime cost** (e.g. hoisting
  `len()` by hand in text_search): the row exists to measure the compiler.
- crypto_sha1 / hyphen historical cells: the Tune21 §4 caveats still apply.

---

## 5. Round targets

| Metric | v38 | after T22-1/2/7 | round target | stretch |
|---|---:|---:|---:|---:|
| text_search JIT ms (untyped) | 15,222 | ~3,000 | **≤ 2,000** | ≤ 1,200 |
| three_way_merge JIT ms | 3,649 | ~2,400 | **≤ 1,800** | ≤ 1,200 |
| log_pipeline JIT ms | 6,340 | ~4,000 | **≤ 2,500** | ≤ 1,500 |
| prettier_ast JIT ms | 1,121 | ~900 | **≤ 500** | ≤ 300 |
| MIR (typed) / C2MIR geo (59 rows) | 5.05x | ~4.6x | **≤ 4.0x** | ≤ 3.5x |
| Rows > 20x C2MIR (typed) | 7 | 6 | **≤ 4** | ≤ 2 |
| MIR (typed) / Node geo (63 rows) | 0.91x | ~0.8x | **≤ 0.70x** | ≤ 0.60x |
| Text rows with a typed source and a C2MIR port | **7/7 (landed)** | 7/7 | 7/7 | — |

---

## 6. Gates, method, recipes

- Acceptance is unchanged (Tune21 §6.1): release build, quiet machine,
  interleaved ×3–×5 medians, zero regressions on `auto`/`jit`/`interp`
  sweeps, `make test-lambda-baseline` green, goldens identical on both tiers.
- Profiles: `LAMBDA_TIER=jit <exe> run temp/prof/<row>_x.ls & sample $! N 1
  -mayDie -file f.sample`, then `python3 temp/prof/entry2.py f.sample`.
- Dumps: `LAMBDA_MIR_DUMP_PATH=temp/prof/<row>_mir.txt <exe> run <row>.ls`,
  then `python3 temp/prof/callhist2.py temp/prof/<row>_mir.txt 8`.
- Micro-experiments used in §2.2 are checked in as
  `test/benchmark/ls_micro/{string_index,split_call,literal_compare,
  text_search_typed_params}.ls`; that directory's README carries their v38
  baselines and is the A/B harness for T22-1 and T22-2.
