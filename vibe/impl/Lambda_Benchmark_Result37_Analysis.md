# Result37 vs Result36 — analysis

**Date:** 2026-09-07. **Inputs:** `test/benchmark/Overall_Result37.md` (commit `4758dc716`, archived
`test/benchmark/exe/lambda-v37-4758dc7165`) against `test/benchmark/Overall_Result36.md` (commit `33a178ed0`,
archived `lambda-v36-33a178ed0`); per-row ratios computed from `benchmark_results_v36.json` / `_v37.json`.
Machine drift control: Node.js geomean R37/R36 = 1.004x, C2MIR 1.024x — the two runs are
comparable. Between the two commits Tune21 landed in full (T21-1..5), then D8.1.1v6 (satellite admission +
write-back), the T20-5 GC slices (nursery pacing, lazy zeroing), CW34 (read-modify-write handle borrows) and two
JIT correctness fixes; the merge also brought the 2026-09-05 upstream commits.

## 1. Headline

| Metric | Result36 | Result37 | Tune21 §5 target | met? |
|---|---:|---:|---:|---|
| MIR (untyped)/Node geo, JIT | 1.37x | **0.97x** | ≤ 1.0x | **yes** |
| MIR (typed)/Node geo, JIT | 0.85x | **0.78x** | ≤ 0.70x | no (close) |
| MIR (untyped)/C2MIR geo | 9.17x | **6.34x** | ≤ 6.0x | no (close) |
| MIR (typed)/C2MIR geo (static ceiling) | 5.67x | **5.12x** | ≤ 4.0x | no |
| Auto e2e (untyped)/Node geo | 4.13x | **1.52x** | ≤ 2.5x | **yes** |
| Auto e2e (typed)/Node geo | 2.79x | **2.06x** | — | |
| Untyped rows > 2x their typed twin | 18 | **10** | ≤ 4 | no |
| Typed rows > 20x C2MIR | 8 | **7** | ≤ 3 | no |

Per-row geomeans R37/R36 over the 59 rows: **untyped JIT 0.708x**, typed JIT 0.925x,
**auto e2e untyped 0.383x**, auto e2e typed 0.763x. The round's two
levers did what the proposal predicted: untyped-lane parity (T21-2) collapsed the untyped/typed gap (18 → 10 rows,
untyped/Node 1.37 → 0.97), and the D8.1.1v6 admission widening turned the auto tier from 4.1x Node to 1.5x.
The static ceiling moved less (5.67 → 5.12) because its widest rows are the container-protocol family
(deltablue 106x, havlak 42x, cd 33x, hashmap 30x, cube3d 28x) that Tune21 explicitly did not slice.

## 2. Where the wins came from (JIT, self-reported)

| Row | R36 untyped (ms) | R37 untyped (ms) | R37/R36 |
|---|---:|---:|---:|
| larceny/ray | 8.852 | 0.297 | 0.03x |
| beng/spectralnorm | 24.0 | 1.656 | 0.07x |
| r7rs/mbrot | 9.898 | 0.701 | 0.07x |
| larceny/quicksort | 10.6 | 0.992 | 0.09x |
| larceny/puzzle | 13.1 | 2.388 | 0.18x |
| kostya/levenshtein | 35.5 | 10.4 | 0.29x |
| larceny/divrec | 15.2 | 5.415 | 0.36x |
| awfy/permute | 0.763 | 0.273 | 0.36x |
| jetstream/crypto_sha1 | 137.4 | 49.2 | 0.36x |
| jetstream/navier_stokes | 392.8 | 153.5 | 0.39x |
| awfy/havlak | 152.6 | 72.1 | 0.47x |
| awfy/towers | 1.182 | 0.671 | 0.57x |
| r7rs/nqueens | 1.641 | 1.020 | 0.62x |
| kostya/matmul | 22.8 | 15.3 | 0.67x |
| awfy/storage | 0.745 | 0.512 | 0.69x |
| kostya/brainfuck | 311.5 | 227.2 | 0.73x |

- ray, spectralnorm, mbrot, quicksort, puzzle, levenshtein, permute, towers, nqueens, matmul: **T21-2** call-site
  inference (2a witness edges, 2c boxed-arithmetic round trips, 2d `var` array witness, 2e string lane); the
  untyped column now matches the typed one on 8 of those rows.
- crypto_sha1 0.36x / typed 0.28x: T21-1 u32 declaration witness + T21-2 (the `int_integral_division` fast path
  from T21-4 also lands here).
- havlak 0.47x (typed 0.50x): T20-5 nursery pacing (27 → 5 collections) and CW34 (205k → 41k copies).
- navier_stokes 0.39x, divrec 0.36x: T21-1b index-expression lowering and T21-2c float round trips.

Typed column: | Row | R36 typed (ms) | R37 typed (ms) | R37/R36 |
|---|---:|---:|---:|
| jetstream/crypto_sha1 | 101.0 | 28.0 | 0.28x |
| awfy/havlak | 153.9 | 76.9 | 0.50x |
| awfy/storage | 0.551 | 0.326 | 0.59x |
| kostya/primes | 5.623 | 3.412 | 0.61x |
| larceny/puzzle | 3.977 | 2.419 | 0.61x |
| awfy/bounce | 0.108 | 0.066 | 0.61x |
| larceny/gcbench | 257.8 | 175.5 | 0.68x |

## 3. Regressions in the JIT columns — verified, not noise

Every candidate below was re-run as an interleaved A/B between the two archived binaries (5 pairs) and bisected
across the intermediate release binaries of the session (`temp/lambda_t21_final.exe` = after T21-1, `t22` = after
T21-2a, … `t35` = final).

| Row | R36 → R37 (ms) | A/B ratio | window | verdict |
|---|---:|---:|---|---|
| larceny/paraffins (untyped) | 0.323 → 0.828 | 2.35x | **t22 (T21-2a)** | real; typed twin unaffected (0.86x) |
| larceny/triangl2 (typed) | 210 → 355 | 1.65x | **t21 (T21-1)** | real; untyped triangl improved 0.82x |
| jetstream/cube3d (untyped) | 11.4 → 13.6 | 1.20x | t21 (T21-1) | real, small row |
| r7rs/nqueens2 (typed) | 1.16 → 1.47 | 1.30x | t21 (T21-1) | real, sub-2 ms row |
| r7rs/fft (untyped) | 0.202 → 0.270 | 2.30x | fft miscompile fix | **correction**: R36's fft was the intermittent stall build; it now computes the right answer (documented 0.095 → 0.25) |
| jetstream/splay 1.10x, awfy/json 1.32x auto, text/hyphen 1.08x/1.14x, awfy/queens | — | — | **sources rewritten** between the commits | not comparable (see §5) |

The four real ones were never in a Tune21 A/B row list (paraffins is a 0.3 ms row; triangl2 is the typed twin of
a row that improved), which is how they passed the per-slice gates. They are the open items of this round
(§6). T21-1's window contains four slices (GC pacing, index lowering, native-bool descriptor, u32 witness); the
next step is one binary per slice on triangl2/cube3d/nqueens2, and the T21-2a witness edges on paraffins.

## 4. The auto tier (part 2, wall clock)

| Row | R36 untyped auto e2e (ms) | R37 untyped auto e2e (ms) | R37/R36 |
|---|---:|---:|---:|
| kostya/collatz | 36374.6 | 616.1 | 0.02x |
| r7rs/ack | 1319.7 | 29.7 | 0.02x |
| text/fast_diff | 6333.8 | 277.3 | 0.04x |
| kostya/brainfuck | 6202.4 | 447.7 | 0.07x |
| r7rs/fib | 217.6 | 17.6 | 0.08x |
| larceny/divrec | 451.8 | 41.0 | 0.09x |
| r7rs/fibfp | 202.4 | 18.6 | 0.09x |
| awfy/cd | 7773.3 | 810.7 | 0.10x |
| jetstream/crypto_sha1 | 2050.8 | 222.6 | 0.11x |
| r7rs/sum | 155.0 | 18.8 | 0.12x |
| larceny/pnpoly | 1382.3 | 170.1 | 0.12x |
| larceny/gcbench | 2844.8 | 399.6 | 0.14x |
| awfy/richards | 4107.0 | 601.2 | 0.15x |
| kostya/base64 | 459.1 | 76.3 | 0.17x |
| r7rs/cpstak | 92.7 | 16.3 | 0.18x |
| larceny/diviter | 107253.9 | 19241.9 | 0.18x |

These are the D8.1.1v6 rows: bodies that were pinned to T0 for an `any` parameter, a local `var`, an indexed
store or an untyped `var` parameter now promote, and the tier lands within 0.35–0.7x of Node on the R7RS/BENG
micro rows because Lambda's startup is a third of Node's.

**Rows still dominated by T0** (auto e2e / JIT self-reported): mandelbrot 3.98 s / 40 ms, navier_stokes 9.4 s /
153 ms, matmul 2.2 s / 15 ms, diviter 19.2 s / 267 ms, brainfuck typed 6.7 s / 308 ms, nbody typed 1.0 s /
15 ms, pnpoly typed 1.6 s / 14 ms, gcbench typed 1.6 s / 175 ms, deriv typed 427 ms / 13 ms. Two known pins:
the once-called `main` hot loop (no loop-entry OSR — mandelbrot, matmul, navier_stokes, diviter) and typed
aggregate parameters (`float[]`, record contracts — nbody2, pnpoly2, gcbench2, deriv2). Both are named in
D8.1.1v6 as deliberate; they are now the whole auto-tier residual.

**Three cells need a reading note.**

- *jetstream/hashmap2 (typed) auto = timeout.* Verified: the v37 binary never finishes hashmap2 on the auto **or
  the interp** tier (300 s cap), while the v36 binary runs the same source in 87 ms on interp. Sampling the stalled
  process shows T0 converting the record's `values: array` field element by element (`ensure_typed_array` →
  `array_num_read_item`/`array_push`) on **every** `int_slot_set(var slots: array, …)` call, and a
  `interp: scratch overflow depth=7 cap=7 fn=put` error on the same call shape. **Attribution (settled by
  `git blame`, no build needed):** upstream `66284c54c` in the v36→base window made the T0 plan *admit* CW25
  place-borrow arguments (`int_slot_set(hm.values, …)`) that previously pinned the whole file to the JIT — so at
  v36 "interp 87 ms" was the JIT running hashmap2 as a fallback, and R37 is the first time T0 executes this shape.
  Two T0 defects surface at once: the plan does not budget the scratch slots the path-borrow argument path uses
  (the overflow), and the `var slots: array` declaration boundary converts a borrowed numeric-array field to a
  boxed array on every call (`interp_coerce_declared_array` → `ensure_typed_array(…, ANY)`), which is O(n) per
  call and never lands back in the field. Bisect: v36 finishes, every binary from `t21` on stalls.
- *larceny/diviter2 (typed) auto: 15 ms → 281 ms (18.7x).* R36's 15 ms was a no-work run (its JIT time is 267 ms);
  R37 is the first correct reading.
- *awfy/json2 (typed) auto: 53 → 154 ms* and *json (untyped) 54 → 72 ms.* The source was rewritten (65 lines); on
  the new source the typed twin runs 40x slower on auto than on jit, i.e. it is pinned — a planner item on the new
  shape, not a regression of the tier.

## 5. Rows whose sources changed between the commits

awfy/havlak2, awfy/json, awfy/json2, awfy/nbody, awfy/nbody2, awfy/queens, awfy/queens2, awfy/richards, awfy/sieve, awfy/sieve2, jetstream/hashmap, jetstream/hashmap2, jetstream/navier_stokes, jetstream/navier_stokes2, jetstream/splay, jetstream/splay2, larceny/quicksort, r7rs/mbrot, text/fast_diff, text/fast_diff2, text/hyphen, text/hyphen2, text/microdiff, text/microdiff2. Their cells are not like-for-like: the T21-5 hygiene edits (checksums printed as FAIL,
hyphen/hyphen2's spec-invalid bool return corrected, queens rewritten so it does work again), the `var`
migrations (richards, splay, havlak2, hashmap), and fast_diff/microdiff's verified outputs. Where such a row
"regressed" (splay 1.10x, hyphen 1.08x/1.14x, json auto) the delta is the work the corrected source now does.

## 6. Open items surfaced by this comparison

1. **typed hashmap stalls on T0/auto** — **FIXED 2026-09-07** (`lambda/runtime/interp.cpp`,
   `interp_plan.cpp`): (a) `interp_coerce_parameter_binding` no longer widens a plain packed numeric array at a
   `var` parameter's `any[]` boundary — the parameter borrows the caller's container (S9.1.3), MIR admits the
   packed carrier in place under the open contract (D3.2.1), and the copy both lost the writes and cost O(n) per
   call; N-D/view carriers still take the boundary. (b) `plan_need`'s call arm budgets the three scratch homes
   a CW25 place-borrow argument holds while its path keys evaluate (and the declarator arm budgets the four the
   CW34 bind holds). hashmap2 now finishes on every tier (interp 7.3 s, auto 7.0 s, jit 110 ms, all PASS); the
   remaining auto gap is the documented pin on typed `var` parameters (`hashmap_put(var hm: HashMap)`), not a
   defect. Fixture `test/lambda/proc/var_array_param_borrow.ls` (golden identical on three tiers; the previous
   binary logs `scratch overflow` on it).
2. **Four real JIT regressions — bisected per slice 2026-09-07.** Dump diffs between the archived v36 and the
   post-T21-1 binary, normalized for the upstream window's struct-offset and TypeId renumbering, plus profiles:
   - *paraffins 2.35x = T21-2a's call-site typer.* `count_ccp`/`count_radicals`/`count_bcp` lost their
     `_array_witness` because three subscript keys typed ANY and `container_key_dynamic` withheld the witness:
     `half = shr(n, 1)` (the `shr` row's success type is `any` although its C return is a raw int64) and
     `nc4 = remain - nc3` / `nc3 = target - nc1 - nc2` (initializer chains five hops deep against a depth-4
     guard). **Fixed:** the typer treats a `C_RET_INT64` sys-func result as the int lane and the chain guard is 8.
     Release A/B t38 → t39: **1.095 → 0.232 ms (0.21x)** — below the v36 reading (0.30).
   - *cube3d 1.20x = T21-1b's branch-free index check.* `i * 4 + j` inside `while` loops has no counted-loop
     nonnegativity proof, so every product fell to the double-lane check (two `i2d`, `dmul`, two double compares).
     **Fixed:** a literal factor below 2^26 needs only `|other| < 2^27` — one add and one unsigned compare
     (`mir_int_literal_value`). cube3d's `i2d` count 130 → 6; release A/B 0.93x (cube3d2 0.95x); the remaining
     gap to v36 is the same lowering on non-literal products.
   - *triangl2 1.65x = T21-1c's nullable-bool lane at the `and` chain.* `board[mfrom[mi]] and board[mover[mi]]
     and (not board[mto[mi]])` over a declared `bool[]`: each read now carries the `bool?` contract (a total
     indexed read is nullable), so the inner `and` node's AST type is that contract rather than `bool`, the outer
     `and` refused it as a native operand, boxed both sides and called `is_truthy` twice (v36's reads were plain
     `bool`, one `fn_not`). **Fixed (2026-09-07):** `mir_expr_native_bool_operand` admits a nested native
     `and`/`or` (both operands native) and `not`; the native arms already publish `(VALUE_REP_I64, BOOL)` and
     test `== 1`, so the nullable lane's null (2) stays falsy at every gate (S3, D2.5.1). triangl2 `is_truthy`
     2 → 0, release A/B **0.59x** (triangl 0.62x); probe `test/lambda/proc/bool_lane_and_chain.ls` (out-of-range
     operands in every position, three tiers and the v37 archive agree). Found on the way, same blindness in two
     more places: (a) the index emitter's "type-valued subscript" guard (`arr[int]`) tested `type_id ==
     LMD_TYPE_TYPE` alone, which a `T?` contract also carries, so every `a[b[i]]` keyed by a declared `int[]`
     element read went through boxed `fn_index` — `mir_type_is_type_value` (simple kind only) at both guards,
     plus the typed-`int[]` element read as a native index leaf; (b) `lambda_condition_lint` reported a
     `bool?` condition as "container type type" — it now unwraps occurrence/union contracts. Pin
     `test/mir/lambda/r37_nested_int_index` forbids `fn_index`/`is_truthy`/`fn_not`/`it2b` in the scan loop.
   - *nqueens2 1.30x = runtime, not emission.* Its MIR is byte-identical (modulo the renumbering) between v36 and
     t21; a ×3000 profile shows `lambda_type_matches` doing 2.5x the work (the declared `int[]` parameter
     admission in `solve`), i.e. the upstream window's validator refactor, not a Tune21 slice. Left as a runtime
     cost item.
3. **Pre-existing JIT lost write through a typed record `var` parameter — FIXED 2026-09-07.** `pn f(var m: Rec,
   i, v) { m.values[i] = v }` (also `m.inner.k = v` and `m.inner[key] = v`) left the caller's record untouched on
   the JIT: both typed nested-store sites in `transpile-mir.cpp` called the detaching `lambda_map_path_set_checked`
   and republished the swapped-in candidate into the callee's own register — but a typed `var` parameter takes
   the raw-lane ABI and has no home to publish through (T0 works because its CW33 stage writes the final
   parameter value back). Both sites now select the runtime's existing NM-O8 typed arm
   `lambda_map_path_set_checked_inplace` when the root is a `var` parameter, exactly as the flat typed store
   already did. That helper now stages the write on a detached candidate first (validation and numeric structural
   admission happen there, so a rejected `person.child.score = 3.5` leaves the record untouched instead of
   leaving float bits in an int lane) and then lands the coerced leaf in the caller's record through the same
   path. Covered by `test/lambda/proc/typed_var_record_store.ls` (index, member, computed-key, int→float and
   rejected shapes; golden identical on three tiers; the previous binary prints the stale values). hashmap/hashmap2
   read 1.05–1.09x on the release A/B with byte-identical MIR dumps — link-layout noise, not this change.
4. **Auto-tier residue — D8.1.1v7 (2026-09-07).** Two policy changes: a loop-bodied procedure promotes at its
   first entry (the once-called `main` rows), and aggregate/structured value-parameter contracts are admitted to
   satellites (the typed rows). Release auto e2e (wall ms, `temp/lambda_t38_final.exe` → `t41_final`, median of 3;
   jit reference alongside): mandelbrot 4040 → 75 (jit 77), matmul 2255 → 40 (39), brainfuck2 6598 → 346 (347),
   pnpoly2 1605 → 75 (40), gcbench2 1648 → 238 (241), deriv2 453 → 40 (40), array1 187 → 20 (20), base642 512 → 40
   (40), ray2 77 → 40 (20), levenshtein 238 → 77 (39); fib/sum/tak/hyphen/binarytrees unchanged at parity. Still
   slow: navier_stokes 9.7 s (40x jit), diviter 19.2 s (66x), nbody2 1.05 s (13.7x), json2 183 ms (2.4x), richards
   1.23x, deltablue 1.44x. Admitting typed bodies exposed DO28 (a satellite decoded T0's `string[]` module carrier as boxed items —
   base642 read its table as null; fixed by keeping the generic index path under a satellite). Still pinned by the
   scan: navier_stokes, nbody2, json2, gcbench2 (reject node kinds now logged); diviter's satellites run boxed
   arithmetic for lack of cross-function call-site inference — the whole-script route (`LAMBDA_AUTO_WHOLE_SCRIPT=1`)
   takes it from 20.7 s to 0.29 s and is neutral elsewhere; satellite call-site specialization is the design item.
   The static-ceiling family (deltablue/havlak/cd/hashmap/cube3d) is untouched by design: T20-1 map field
   protocol, (B) "moved argument" (crypto_aes) and the tenured-zone reclamation (brainfuck's 195 MB RSS).
   **Follow-up, D8.1.1v8 (2026-09-07): the scan pins are gone.** Every pinned body in navier_stokes, nbody2 and
   json2 had a typed `var` parameter. They are admitted now: every `var` position travels through the CW33 home
   cells on the boxed edges, the `_b` wrapper prepares (un-share-at-borrow), admits and stores the container back
   through the home, and a satellite's dynamic edge transports typed positions and reloads them keeping the raw
   descriptor; a body that REBINDS a typed `var` parameter, and its direct satellite callers, stay in T0 (the
   raw entry has no home to publish a rebind through; the eager tier loses it -- DO29). Release auto e2e
   (median of 3, wall ms, `t45` → `t46`, jit reference): navier_stokes 11.1 s → 281 ms (jit 264; 40x → 1.06x), awfy/nbody2 1230 → 83 ms (jit 84; parity), beng/nbody2 1207 → 87 ms (jit 83), json2 194 → 91 ms (jit 80; 2.4x → 1.13x); jetstream/nbody2 flat at 104 ms (it was never pinned), richards 702 → 684 ms and deltablue 371 → 376 ms unchanged (their residue is the map field protocol, not a scan pin). Eager JIT A/B `t45 → t46` (×3) on the `var`-heavy rows richards, deltablue, havlak, json2, nbody2, navier_stokes, quicksort, permute, towers, hashmap2, cd2, splay: 0.98–1.03, flat. Gates: auto / jit / interp sweeps (763 scripts) 0 regressions -- the jit sweep counts the new `interp_typed_var_rebind` fixture as its 5th known failure (DO29, T0 golden); `make test-lambda-baseline` 4137 / 4137; `make interp-sweep` regenerated; auto-tier golden check over all 113 benchmark rows 109 PASS with the four pre-existing T0 divergences (cd2_orig, fasta, nbody, spectralnorm).
   **Follow-up, D8.1.1v9 (2026-09-07): the satellite cluster.** richards/deltablue paid 1.2-1.4x for boxed dynamic
   dispatch between one-definition images, diviter 66x for a raw entry inferred without its callers. A satellite
   image is now the target plus its direct-callee closure in module order, with call-site collection over the
   whole module and every member's boxed entry published. Release auto e2e (median of 3, wall ms, `t46` → `t47`,
   jit reference): diviter 19.96 s → 349 ms (jit 345; 66x → 1.01x), richards 786 → 620 ms (jit 614; 1.01x), deltablue 464 → 348 (jit 347; 1.00x), deltablue2 1039 → 365 (jit 370), havlak 274 → 237 (jit 238; 0.99x), json2 193 → 77 (jit 75; 1.02x), navier_stokes 292 (jit 288), jetstream/richards 72 → 77 (jit 74), base64 38 → 40 (jit 40), hashmap2 1093 → 162 (jit 160), cd 1122 → 973 (jit 974), cd2 1296 → 787 (jit 807), spectralnorm 99 → 41 (jit 41; its T0 divergence is gone because the module promotes as one image), nbody2 1348 → 97 (jit 112); the self-timed deltablue workload is 127 ms on both tiers, so every remaining auto/jit difference is startup plus the image compile. ⚠ splay 256 → 446 (jit 444): the uninferred per-function satellites had been 1.7x FASTER than the eager tier here; with the eager tier's call-site inference the auto tier now matches it (DO30 -- an eager-tier item: `splay_find(tree, key)` takes a float `key` lane and `next_random`/`insert_new_node` native float returns, and the inferred code is slower than the boxed bodies). Gates: auto / jit / interp sweeps (764 scripts) 0 regressions (jit: the DO29 fixture is its 5th known failure); `make test-lambda-baseline` 4138 / 4138; `make interp-sweep` regenerated; benchmark golden check on the EAGER and the AUTO tier 111 / 113 each (cd2_orig and nbody pre-existing; fasta and spectralnorm now pass on auto); eager A/B `t45 → t51` on 16 rows 0.90–1.07 (fib 1.11 on a 1.7 ms row).

### 6.1 Gates on the final tree (2026-09-07, debug `temp/lambda_t43_debug.exe`, release `temp/lambda_t43_final.exe`)

| Gate | result |
|---|---|
| `jit_sweep.py` auto / jit / interp vs the previous build (760 scripts each) | 0 regressions on all three tiers (jit: 4 pre-existing failures, unchanged) |
| `make test-lambda-baseline` | 4132 / 4132 (the nested-index pin is parked under `temp/t3b/`, so it is not counted) |
| `make interp-sweep` | partition regenerated; three new fixtures admitted, no script dropped |
| release A/B `t41 → t43` (×3, ms) | triangl2 412 → 383, cube3d 15.0 → 15.0, paraffins 0.240 → 0.236, nqueens2 1.75 → 1.77, mandelbrot 49.4 → 50.4, fib 1.76 → 1.76 — flat, as expected for a trace-only removal |
| release A/B `t44 → t45` (×3, ms; and-chain + nested-index fix) | triangl2 449 → 263 (**0.59x**), triangl 591 → 365 (0.62x), cube3d 0.95, primes2 0.92, json2 0.91, puzzle/sieve2/nqueens2/queens/bounce2 0.97–1.02; richards 1.03 and deltablue2 1.02 at ×5 with byte-identical emission (link-layout noise) |
| auto / jit / interp sweeps (761 scripts) after the fix | 0 regressions; `make test-lambda-baseline` 4135 / 4135; `make interp-sweep` regenerated (+1 fixture) |
