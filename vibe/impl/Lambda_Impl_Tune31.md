# Lambda Impl Proposal: Tune31 — The Runtime-Call Band

- **Status:** PROPOSAL (2026-09-18), not started
- **Evidence:** `test/benchmark/Overall_Result47.md`, `benchmark_results_v47.json`; comparison script `temp/r47/cmp.py`; Tune30 §6 cluster profiles (`temp/t30/prof/jit_self_table.txt`, R46-era binary)
- **Predecessors:** `Lambda_Impl_Tune29.md` (record rows, handles, boxed-scalar admission), `Lambda_Impl_Tune30.md` (codegen-bound class)
- **Rulings cited:** D4.4.4v4 (handle facts), D4.4.6 (place-copy marking), S9.1.2 (two-owner states), D3.2.4v4 (layout facts), D8.3.2–D8.3.3 (member-to-union returns), S4.1.2 (int band), TE-15 (defect containment)

## 1. What Result47 says about Tune29 + Tune30

Both rounds landed between Result46 (`9697f43375`) and Result47 (`fc0755a79d`).
R46 was load-inflated: the identical C2MIR ports ran 0.914x and Node 0.809x
of their R46 times in R47, so every Lambda ratio below is **normalised by
the row's own C2MIR drift** (`tNorm = typed47/typed46 ÷ c2mir47/c2mir46`).

| measure | R46 | R47 |
|---|---:|---:|
| typed / C2MIR geomean (63 rows) | 4.26x | **3.59x** |
| typed / Node geomean | 0.75x | 0.71x |
| untyped / Node geomean | 1.20x | 1.26x (Node got faster; untyped norm ≈ 0.93) |
| typed auto e2e / Node | 0.87x | 0.85x |
| typed geomean, normalised by C2MIR drift | — | **0.843** (≈16% real) |
| rows ≤2x C2MIR / 2–5x / 5–20x / >20x | 19 / 14 / 25 / 5 | 21 / 17 / 21 / 4 |

### 1.1 Improved (tNorm ≤ 0.8, all attributable)

| row | typed/C2MIR R46 → R47 | tNorm | what moved it |
|---|---:|---:|---|
| nbody | 7.7 → 2.3 | 0.30 | T30-3 NO_GC libm + loop versioning + float-tree versioning |
| cd | 37.2 → 12.0 | 0.32 | Tune29 P0: RbtTable + Motion typed (benchmark edit, 57 lines) + LR12-11 root-vs-part |
| spectralnorm | 5.3 → 2.2 | 0.42 | T30-5 leaf inlining + T30-4 argument intervals (§10.5) |
| queens | 6.0 → 2.9 | 0.48 | T30-5 inlining of get/set_row_column, LR12-21 sentinel leaves |
| fft | 6.7 → 3.7 | 0.55 | T30-4 affine/scaling counter intervals |
| mbrot | 2.1 → 1.2 | 0.58 | T29-5 `cow_path_set_packed_index` |
| nqueens | 14.7 → 3.4 | 0.23 raw | **port artefact**: C port rewritten (2.63x slower C); Lambda side 2.07 → 1.25 ms = T29-5 cert interning + §18 boxed-scalar admission |
| deltablue | 31.7 → 21.9 | 0.69 | §18 boxed-scalar admission −12%, §20.2 cert through field −12% |
| paraffins | 4.6 → 3.2 | 0.71 | T30-4 intervals, T30-5 div/% leaves |
| ray, navier_stokes, richards, cube3d | 1.6→1.2, 1.9→1.5, 14.4→11.1, 16.7→13.2 | 0.76–0.79 | NO_GC sqrt; P0 typing of richards tasks (−11%) + handles (−5%); T29-5 admission (cube3d −11%) |

Tune30's own gate (≤2x on the seven codegen rows) was **not met on any row**;
spectralnorm/nbody at 2.2/2.3x are the closest. §10.10 bounded the remaining
index-check cost at <10% and §12 says the rest needs a structural change.

### 1.2 Did not move (tNorm ≥ 0.95) — the whole runtime-call band

| row | typed/C2MIR R47 | tNorm | dominant runtime call (Tune30 §6 profile) |
|---|---:|---:|---|
| havlak | 32.7 (was 26.7) | 1.22 | copies are program-level two-owner states (S9.1.2, Tune29 §20.3) + GC 29% |
| splay | 17.1 (was 15.8) | 1.08 | `cow_prepare_write` 27%, `map_set_cow` 19%, ArrayNum alloc 16% |
| knucleotide | 17.3 | 1.00 | `vmap_set` 39%, `fn_substring` 20% |
| crypto_sha1 | 8.2 | 1.01 | `fn_is` 20% (`match {case error … case int}`), `lambda_type_check` 12% |
| brainfuck | 7.1 | 1.01 | `fn_fill` 51%, `fn_ord` 14% |
| towers | 11.5 | 0.99 | `lambda_type_check` 33% |
| hashmap | 13.2 | 0.96 | `cow_path_borrow_impl` 39%, `fn_eq_depth` 11% |
| levenshtein / base64 / pnpoly | 7.2 / 15.1 / 6.2 | 0.97 / 0.91 / 0.88 | `fn_eq_depth`+`ascii_at` / `fn_strcat_many` / **`fn_ne` 45% on two `bool` locals** |
| text rows (fast_diff, text_search, three_way_merge, microdiff, log_pipeline) | 10.4 / 3.2 / 1.1 / 22.6 / 6.5 | 0.91–1.00 | `fn_split`, `pn_push`, `fn_join`, generic compares |
| fannkuch | 2.35 (was 2.0) | **1.17** | 98% JIT self — a Tune30 codegen regression candidate (LR12-21 leaf validity test?) — needs A/B on the archived v46/v47 binaries |

### 1.3 Typed still slower than untyped (gate "typed ≤ untyped" fails on 4 rows)

bounce 1.48x (R46 root cause #1: `var axv: int = abs(bxv[j])` — Item-returning
builtin into a declared int, `is_truthy` 21%; T29-4's tag-tested store fixed the
variant, not the row), fannkuch 1.15x (new), fasta 1.14x (R46 #4, nullable
index read poisoning the int lane; T29-3's null split was timing-neutral),
microdiff 1.13x.

### 1.4 MIR evidence (v47 archived binary vs `c2m -S` of the C ports)

Dumps in `temp/r47/mir/` (`LAMBDA_TIER=jit LAMBDA_MIR_DUMP_PATH=…`), C side in
`temp/r47/cmir/` (`temp/t30/c2m -Dmain=c2mir_bench_body -S`). Hot function,
lines / calls, Lambda typed vs C, and what the listing shows:

| row | Lambda hot fn | C hot fn | mechanism in the listing |
|---|---:|---:|---|
| pnpoly | `_pnpoly` 559 / 11 calls | `pnpoly` 54 / 0 | `dgt` yields native 0/1; each is **boxed to a bool Item** (`or 0x0300…`, null arm `0x1B00…`), four live registers spilled, `call fn_ne`, result `and 255`, both array data pointers reloaded. `bool != bool` has no native lowering. |
| crypto_sha1 | `_core_sha1` 1744 / 98 | `hex_sha1` 269 / 6 | `shr(input_len + 64, 9)`, `shl`, `bor`, `bxor` are **`fn_shr_item`/`fn_shl_item` calls on boxed Items** even with a proven int and a literal operand (the literal 9 goes through `int2it_lane`). Their Item results poison the tree: `fn_add` ×5 generic adds, `base_type` ×2. `_safe_add` ×9 / `_rol` ×3 are not inlined (T30-5 rejects `u32` locals and the bitwise builtins); their direct entries take lanes, so the 32 `int2it_lane` / 17 `lambda_item_to_int_lane_c` are boxing *to and from the Item-typed intermediates*, not the call ABI. `fn_is` ×2 = the `match { case error … case int }`. |
| bounce | `_benchmark` 1738 / 88 | body 165 / 9 | `abs(bxv[j])` is `call fn_abs` on a boxed Item, 5 spills before, 9 reloads after, `ursh 56` tag test to get the `int` back. `if (bx[j] > 500)` on a nullable `int[]` read becomes a **nullable bool Item → `call is_truthy`** ×4. Both R46 root causes still live. |
| towers | `_move_disks` 302 / 14, `_push_disk` 249 / 10 | `move_disks` 29 / 6 | Every recursive call passes `_array_witness = 3`, so the per-entry `lambda_type_check` sites are **cold arms**; the R46-era "33% type_check" no longer describes this row. What each call pays: all live Items stored to the side-stack frame before the call and reloaded after (`i64:80..160(%r1e5)`), the CW33 `var`-home transport for two array params, and the `_b` wrapper's `cow_prepare_write` ×3 + type checks ×5 on the outer entry. 302 lines against 29 is **call-boundary cost**, not checks. |
| levenshtein | `_levenshtein` 1514 / 76 | `levenshtein` 85 / 3 | `s1[i-1] == s2[j-1]` = two `fn_string_ascii_at` calls, each **re-boxing the string parameter** (`or 0x0D00…, %p2`) and spilling/reloading 6 slots, then `fn_eq` on two char Items. `cow_bind_var` ×6 inside the loop. |
| storage | `_build_tree_depth` 313 / 19 | 45 / 4 | `fill(4, …)` boxes its count through `int2it_lane`, calls `fn_fill`, then `fn_array_set` the result into the parent. |
| fannkuch | `_main` 1890 / 78 | body 184 / 2 | §1.5 |

**What the listings change about the picture.** The R46-era profiles
attributed these rows to the runtime helper that was called
(`lambda_type_check`, `fn_ne`, `fn_abs`). The MIR shows the cost sits in the
**boundary around the helper**: boxing the operands, spilling every live Item
to the side stack, the call, reloading, and a tag test on the result — 15–30
instructions per site against the helper's own few. On four of the seven
rows the helper is a pure scalar builtin (`!=`, `abs`, `shr`, `ascii_at`)
whose native form is one or two instructions. Removing the call removes the
boundary; making the helper cheaper would not.

### 1.5 fannkuch regression: bisected to Tune29 §20 (`lambda-items`)

Interleaved `__TIMING__` (5–7 runs, min/median ms): v46 0.305/0.311 → every
Tune29 binary through `lambda-bsa` (§18 boxed-scalar admission) 0.304–0.312
→ **`lambda-items` 0.340/0.367** → all Tune30 binaries 0.336–0.355 → v47
0.336/0.341. The step is the §20 work (LR12-17 `MirCowJoin` / generalised
loop premark / `MirCowLoopJoin`, CW36 handle admission).

MIR diff of `_main` between `lambda-bsa` and `lambda-items`: same call
census, **+8 COW shared-bit tests (`u8:4(…) & 1`) and +22 branches** in the
store sites of `perm`, `perm1`, `count`. Nothing in the loop body hands the
three `int[]` locals to a second observer (no call takes them, nothing
aliases them; the only consumers are element reads and stores), so the
premark or the loop join marks them may-be-shared without a share source.
A precision defect in §20's join, not a real alias; every typed-array loop
in the suite pays the same test per store.

## 2. What the round targets

After Tune30 the ≤5x half of the suite (38 rows) is near what guard elision
reaches, and Tune30 §12 says the rest of that class needs a structural change.
The 25 rows at ≥5x are runtime-call bound. The MIR review of seven of them
(§1.4) found four mechanisms, each spanning several rows, and demoted two of
the R46-era clusters:

| mechanism | rows seen in MIR | rows expected by the same shape (unverified) |
|---|---|---|
| **M1** scalar builtin with no native lowering on a proven lane: bool `==`/`!=`, `abs`, `shl`/`shr`/`bor`/`bxor`/`band`, `ascii_at`, char `==`, `int()` of an Item | pnpoly, bounce, crypto_sha1, levenshtein | brainfuck (`fn_ord` 14%, `ascii_at` 8%), base64, fasta (`fn_lt` 17%), text rows |
| **M2** an Item-typed intermediate poisons the rest of the expression tree (crypto's `fn_add` ×5 after one `fn_shr_item`) | crypto_sha1, bounce | any row with an M1 site inside arithmetic |
| **M3** T30-5 inliner admission gaps: `u32` locals, bitwise builtins | crypto_sha1 | — |
| **M4** COW facts over-marked in loops (LR12-17 join) | fannkuch | every typed-array loop |
| **M5** call-boundary cost on recursive `var`-param leaves: spill-all, CW33 home transport, `_b` wrapper | towers | queens (Tune30 §10.9), quicksort, permute |
| **M6** `fill` temporaries (Cluster C, unchanged) | storage | brainfuck, nqueens, cube3d, raytrace3d |

Not MIR-reviewed this pass, so **not sized**: Cluster B COW rows (puzzle,
hashmap, richards, splay), the string-heavy rows (json_gen, base64, revcomp,
three_way_merge, knucleotide), and the type-check rows (raytrace3d, json,
list, deltablue, prettier_ast). towers shows why they must not be scheduled
from the R46-era table: its headline helper was a cold arm on v47.

## 3. Tracks

### T31-0 — Hygiene (first)

1. **Fix the LR12-17 loop-join over-marking** (§1.5). Reproduce on
   `test/benchmark/beng/fannkuch2.ls`: the `bsa`→`items` diff is the
   fixture; add an emission pin counting `u8:4(` tests in a loop that stores
   to a local `int[]` nobody else observes (expect 0). Ledger it as a new
   LR12 item. Gate: fannkuch back to ≤0.315 ms on the v46 machine baseline.
2. Bisect splay (+8%) and fib (+6%) the same way — `temp/t29/lambda-*.exe`,
   `temp/t30/lambda-t30*.exe`, interleaved min-of-N, C2MIR column as control.
3. Run the Tune30 §7 gates that were skipped: corpus JIT output diff vs v46,
   forced GC (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 LAMBDA_ROOT_WITNESS=1`).
4. Port hygiene found in the listings: `text_search`'s `fn_numeric_binary`
   is an untyped arithmetic site in the typed file; richards' `fn_member_by_id`
   19% says a member is still read untyped. Fix the ports before measuring.

### T31-1 — Native scalar builtins on proven lanes (M1, M2, M6)

The widest class in the review. Each item is an emitter fast arm keyed on
the operand carriers (`mir_value_carrier_type`), with the existing runtime
call as the cold arm for any other carrier, so no semantic change and every
item gets a sidecar that forbids the call in the pinned shape.

1. **`bool` `==`/`!=`** → `eq`/`ne` on the 0/1 lanes (pnpoly: two `dgt`
   feed `fn_ne`; bounce: `is_truthy` on a compare). A *nullable* compare (one
   operand from an unproven typed read) keeps the null arm but tests it
   natively (`eq x, ItemNull`) instead of boxing to a bool Item.
2. **`abs`, `min`, `max`, `int()`** on `int`/`float` lanes → `cmp`/`neg`,
   `d2i`, no call (bounce ×4, levenshtein's `min3` already inlines).
3. **`shl`, `shr`, `bor`, `bxor`, `band`** on int lanes → MIR `lsh`/`ursh`/
   `or`/`xor`/`and` with the S4.1.2 band re-check on the result only where the
   shift can leave the band (`shl`); `u32`-declared locals need no band test.
   crypto's five `fn_add` sites go native by consequence (M2).
4. **`fn_string_ascii_at` on a string carrier with a proven index** → inline
   byte load when the `is_ascii` fact (Tune22) holds; char `==` becomes `ne`
   on two u8 lanes. levenshtein, brainfuck, base64.
5. **Keep the boxed carrier of a string parameter** so a remaining call does
   not re-box it per site (levenshtein's `or 0x0D00…, %p2` ×6).
6. **NO_GC allowlist for pure scalar builtins** that stay as calls
   (`fn_ord`, `fn_lt` on mixed lanes, …), the T30-3 mechanism, so the
   spill-all around them disappears.

Gate: pnpoly ≤ 2x, bounce typed ≤ untyped, crypto_sha1 ≤ 4x, levenshtein ≤ 4x;
suite geomean ≤ 1.0 on every row.

### T31-2 — Leaf inlining admission (M3)

T30-5 admits named leaves whose bodies are int/float arithmetic, `int()`,
`div`/`%` by a nonzero literal, and element reads/stores. Add: `u32`
declared locals and their range tests, the five bitwise builtins (after
T31-1 item 3 makes them native), and `if`-expression bodies. crypto's
`safe_add`/`rol` then inline; the sidecar forbids both calls in `_core_sha1`.
Also admit `int(x)` where `x` is a `u32` lane (the `_rol` return).

Gate: crypto_sha1 ≤ 3x (from 8.2x).

### T31-3 — `fill` temporaries and `float[]` results (M6, Cluster C)

Rows from the R46-era profile, storage confirmed by MIR: storage 71%,
brainfuck 51%, nqueens 49%, cube3d 46%, raytrace3d 15%, splay 16%.

1. **Contract-directed `fill`.** `var r: float[] = fill(n, 0.0)` boxes both
   arguments, calls `fn_fill`, then admits the result. With the destination
   contract known at compile time emit `array_num_new(lane, n)` + a native
   fill loop (memset for zero) with the certificate stamped at construction.
   No boxing, no admission. Same for `int[]`/`bool[]`.
2. **Non-escaping fixed-size typed arrays.** cube3d's `vmulti` allocates 102
   `float[4]` per call. A `var r: float[] = fill(<literal>, …)` whose binding
   never escapes (`cow_param_root_retained` already answers this for callees)
   can live in a per-function region reclaimed at exit. **A D4 memory-
   ownership question (region allocation for non-escaping containers) —
   propose the ruling before building.** Tune29 §19.2 closed caller-provided
   storage as a 3-ABI change; the region form needs no ABI change.

Gate: `fn_fill` absent from storage/brainfuck/nqueens; cube3d ≤ 8x.

### T31-4 — Call-boundary cost on recursive `var`-param leaves (M5)

towers' checks are cold; what remains per call is the spill of every live
Item to the side stack, the reload after, and the CW33 `var`-home transport
(Tune30 §10.9: ~7 instructions per `var` array param, ABI not proof).
Before any code: profile towers/queens/quicksort on the **v47** binary with
`temp/t29/prof/jit_attr.py` and read the spill census
(`temp/t29/census/spill_census.py`). Two candidate levers, both design items:

- a fact "callee cannot publish a replacement when entered with a unique
  array", which lets the home transport be skipped (touches S9.2.2, Tune30
  §10.9 says PROPOSE BEFORE BUILDING);
- T29-7c stack maps (Tune29 §16.3), closed there because the reload was
  not the cost; towers' listing says it may be on this row.

Not scheduled for implementation in Tune31 unless the v47 profile puts the
boundary above 40% of the row.

### T31-5 — The un-reviewed band: profile and MIR-review on v47 first

For puzzle, hashmap, richards, splay, json, list, deltablue, prettier_ast,
raytrace3d, json_gen, base64, revcomp, knucleotide, three_way_merge: one
profile on the v47 binary and one hot-function listing each, in the §1.4
table format, **before** any track is sized. Hypotheses carried from Tune30
§6, to be confirmed or retired by the listing:

- Cluster A: `lambda_type_check` on certified-field array arguments and
  returns; the float extension of Tune29 §18's inline tag test.
- Cluster B: puzzle's `int[]` cloned per store (D4.4.6 place copy — trace
  the bind first; havlak's was a snapshot bind), hashmap's borrow spine
  re-navigated per call (D4.4.4v4), richards/splay path stores as one checked
  call per write. havlak stays out: its copies are program-level two-owner
  states (S9.1.2, Tune29 §20.3).
- Cluster D: string `==` as length + memcmp, `fn_string(int)` and
  `fn_strcat_many` fast paths, `fn_split`/`fn_substring`.

### T31-6 — Codegen class: the structural proposal (design only)

Tune30 §12: the seven rows are diffuse at 2.2–3.8x with no item above ~13%.
Two candidate designs, to be written up under D8 and ratified before code:
(a) whole-loop proof with a deoptimisation exit — unchecked element access
inside a loop whose length/ownership guard passed, with the generic body as
the exit target; (b) a narrower loop-local representation (raw `double`/
`int64` registers for proven-in-band locals, boxed only at the loop exit).

## 4. Gates for the round

- typed / C2MIR geomean ≤ 2.8x (from 3.59x); rows in the 5–20x band ≤ 12 (from 21).
- typed ≤ untyped on **every** row (currently bounce, fannkuch, fasta, microdiff fail).
- fannkuch, splay, fib back at or below their v46 normalised times.
- No output diff on the corpus, both tiers; all emission sidecars and MIR
  budgets green; `make test-lambda-baseline` unchanged (the two pre-existing
  JS failures excepted).
- Every landed mechanism gets an emission pin verified to bite (Tune30
  §10.12's lesson), and every track opened after T31-3 starts from a v47
  listing, not an R46 profile.

## 5. Method notes

- `temp/r47/cmp.py` — R46→R47 per row, normalised by the row's C2MIR drift.
- MIR census one-liners used for §1.4 live in the session log; the reusable
  ones are `temp/r46/mircensus.py`, `temp/r46/classify.py`,
  `temp/t30/mir/hotcensus.py`.
- Intermediate binaries for bisecting: `temp/t29/lambda-{t29,t291,t2934,t295a-c,t297a-b,bsa,sym,items,items2,items3}.exe`,
  `temp/t30/lambda-t30a..z.exe`, `temp/t30/lambda-t31a.exe`.
- ⚠ `sed -n "$((n-8)),…"` fails when the arithmetic yields a negative; use
  `awk -v s= -v e=` for context windows.
