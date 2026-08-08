# Tune 15: Result24 Analysis, Measured Attribution, and the Next Typed Round

- **Date:** 2026-08-07
- **Input:** `test/benchmark/Overall_Result24.md` (Lambda commit `83a7099e4e`, archived binary
  `test/benchmark/exe/lambda-v24-83a7099e4e`), `vibe/Lambda_Impl_Tune14 (done).md`,
  `test/benchmark/benchmark_results_v23_c2mir.json` (C2MIR ceiling; the C2MIR path is frozen
  per house rule 14, so v23 C2MIR timings remain valid), fresh sampling profiles in
  `temp/prof15/` (§5)
- **Status:** IMPLEMENTED — implementation slices and verification complete; residual performance gaps are recorded in §8
- **Related:** `vibe/Lambda_Impl_Tune14 (done).md`, `vibe/Lambda_Tune_Typed_Vs_C2MIR.md`,
  `vibe/Lambda_Design_Compiling_Dual_Func.md`, `vibe/Lambda_Design_Name_Identity.md`,
  `vibe/Lambda_Design_Type_Enforcement.md`
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1, S4.5.3;
  `doc/Lambda_Formal_Design.md` D2.2.2, D2.4, D3.2.1–D3.2.2, D3.3.1–D3.3.3, D4.4,
  D5.2–D5.3, D8.3.2–D8.3.3, D8.4.1, D8.6.1–D8.6.3

## 1. What Result24 says

| Metric | Result23 | Result24 |
|---|---:|---:|
| MIR (untyped)/Node geo | 2.70x | **2.65x** |
| MIR (typed)/Node geo | 1.91x | **1.68x** |
| MIR (typed)/C2MIR geo (44 matched rows) | 9.56x | **8.19x** |
| LambdaJS/Node geo | 15.4x | 15.5x |
| QuickJS/Node geo | 7.20x | 7.22x |

Tune14 delivered its headline: A3 (native recursive returns) collapsed the recursion family
— fib −70% (6.34→1.91 ms, now 1.06x Node), tak −78%, cpstak −81%, fibfp −53% — and A1+A2
fixed the two named defects: matmul −73% (69.7→18.6 ms, typed no longer worse than untyped),
levenshtein −51%, base64 −34% on the allocator fix, sha1 −24%, spectralnorm −14%, splay −11%.
Typed/Node geo moved 1.91→1.68 and typed/C2MIR 9.56→8.19.

The two headline problems for this round:

1. **Typed is still 8.19x off the C2MIR ceiling**, and the gap is concentrated: base64 121x,
   towers 74x, permute 58x, list 48x, spectralnorm 44x, paraffins 43x, bounce 33x, pnpoly 33x,
   queens 30x, quicksort 29x, levenshtein 25x, nbody 20x, brainfuck 19x. (Micro-row C2MIR
   denominators are sub-0.1 ms, so individual ratios are noisy — but the *family* is
   structural: per-call and per-element overhead that C2MIR does not pay at all.)
2. **Twelve rows still violate the categorical ≤5% annotation bar** (Tune14 §2.7): bounce
   +170%, storage +77%, towers +52%, permute +51%, brainfuck +41%, splay +40%, json_gen +11%,
   nqueens +10%, fasta +10%, revcomp +8%, knucleotide +8%, list +5.2%. Every one maps to a
   measured mechanism in §2.

Worst typed/Node rows (the geomean tail): crypto_sha1 26.0x, navier_stokes 20.5x, cd 18.6x,
brainfuck 15.4x, pnpoly 10.8x, gcbench 10.7x, splay 10.2x, raytrace3d 8.7x, deltablue 8.2x.

## 2. Measured attribution (2026-08-07, archived v24 release binary)

Same protocol as Tune14 §5: looped variants of the shipped typed benchmarks (workload code
unchanged, outer loop raised until ≥3 s), `/usr/bin/sample <pid> 3 1` against
`lambda-v24-83a7099e4e`, anonymous addresses resolved by `nm` bracketing
(`temp/prof15/nm_v24_sorted.txt`, slide base 0x100000000). Raw samples:
`temp/prof15/*.sample.txt` (base64, towers, brainfuck, splay, spectralnorm, nbody, pnpoly,
deltablue, cd, sha1, levenshtein). Caveat: rows whose looped run finished inside the sample
window (base64, sha1, splay, levenshtein) include some process-teardown samples
(`heap_finalize_gc_objects`/`gc_heap_destroy`) — discounted below.

The v24 profiles resolve into **six mechanism families**. Percentages are shares of
top-of-stack leaf samples.

### 2.1 F1 — Residual per-element boundary admission (the top family, again)

`lambda_numeric_boundary_admit` (a ~6 KB routine; hot interior offsets +0xad4…+0xf20) plus
`lambda_type_check`/`lambda_type_matches`:

| Row | admit+check share | Companion leaves |
|---|---:|---|
| pnpoly | **~44%** (671+147+96+…) | `it2d` 60, `fn_ne` 49, `validate_occurrence_type` 35 |
| crypto_sha1 | **~50%** (201 named + ~374 interior + 58 check) | `fn_int` 60, `it2i` 45 |
| cd | **~30%** (513+59) | `fn_index` 123, `item_at` 76, `array_get` 66 |
| brainfuck | **~28%** (673) | `item_at` 138, `fn_index` 99, `fn_array_set` 41 |
| towers | **~22%** (391+81) | validators, see F3 |
| levenshtein | ~35% of workload samples | `item_at` 56, `fn_index` 33 |
| base64 | ~14% (150) | string family, see F5 |

Tune14's A1 elided admission at *identity* boundaries, and it worked where it reached
(matmul's `boundary_admit` share collapsed). What remains are the boundaries A1's proof does
not reach [S4.1, D2.4, D3.2.1]:

- element reads feeding **comparisons and branch conditions** rather than declared
  assignments (`pnpoly`: `if (pnpoly(...))`, the cross-product sign tests; `fn_ne` boxed);
- **conversion builtins on proved operands** — `int()`/`float()`/truncation still route
  through `fn_int`/`it2i`/`it2d` even when the operand's lane is statically known
  (`crypto_sha1`, `spectralnorm`) [S4.5.3, D2.2.2];
- **untyped hot locals** that a one-line inference step could prove: `brainfuck`'s
  `var tape = fill(30000, 0)` has no declared type, so every `tape[dp]` read/write is generic
  `item_at`/`fn_array_set` — `fill(n, int-literal)` should produce (and be typed as) a packed
  int array witness (see F5 for its allocation cost);
- **polymorphic/mixed call sites** in `cd`'s vector math where the same helper serves
  int and float inputs.

### 2.2 F2 — Boxed scalar math across call boundaries

`spectralnorm` (44x C2MIR) is the clean specimen: the inner loop is
`s = s + eval_A(i, j) * v[j]` where `pn eval_A(i, j) float` returns
`1.0 / float((i+j)*(i+j+1)/2 + i + 1)`. Top leaves: `fn_div` 222, `fn_mul` 200, `fn_add` 173,
`lambda_restore_number_frame_top` 113, `lambda_module_const_at` 65, `fn_float` 43, plus ~20%
in unnamed statics adjacent to the boxed-arithmetic entry points in
`lambda/runtime/lambda-eval-num.cpp` — **~48% boxed float arithmetic in total**. The boxed
`eval_A` return poisons the whole chain: one boxed Item forces `fn_mul` and `fn_add` on
values that are all statically `float`.

Tune14's A3 made **self-recursive** success returns native. The general case — a
statically-bound `pn`/`fn` call with declared scalar params and return — still crosses the
boxed ABI, paying `lambda_item_adopt_scalar_home`/`lambda_restore_number_frame_top` per call.
That adopt/restore pair is visible everywhere: deltablue ~8% (164), cd ~10% (189), sha1 ~6%,
splay, towers. This is the Result18 T-B lever and the dual-func DF9 direction
[D3.3.1–D3.3.2, D8.3.3; DF9 entry-equivalence].

The micro family (towers 74x, permute 58x, list 48x, bounce 33x, queens 30x — all
typed-worse-than-untyped) is the same mechanism at small scale: every helper call round-trips
declared scalars through the boxed ABI plus F1 admission at each boundary.

### 2.3 F3 — Map-as-record machinery: string-compared fields, per-write validation, COW copies

- `deltablue` (**~28%**): `map_get` 134, `fn_member` 95, `map_field_to_item` 90,
  `_platform_memcmp` 95, `map_shape_field_to_item` 55, `strlen` 53, `fn_map_set` 75 — field
  access on declared record types is still a ShapeEntry chain walk with string compares.
  Plus **~14%** in `ensure_sized_array` interiors (see F4).
- `towers` (**~20% validators + ~19% allocation**): `TState = {moves: int}` passed as a
  `var` param; every `state.moves = state.moves + 1` goes `fn_map_set` → `map_set_cow` →
  `validate_against_map_type`/`validate_against_type`/`validate_against_base_type` (191) +
  `strcmp`/`strncmp` (74) + `pool_calloc`/`memset`/rpmalloc (~336). A statically-conforming
  int write into a declared int field is re-validated **and** COW-checked per write.
  (Tune14 A4 correctly reported no `validate_against_*` in the towers MIR — the calls come
  from inside the runtime `fn_map_set` path, which is why A4's elision missed them.)
- `splay` (**~19% map + ~14% GC**): node records as maps (`set_fields`, `fn_map_set`,
  `map_field_to_item`), `gc_collect_with_root_region` 123, `pool_calloc`+`memset` ~11%.

This family is Name-Identity W1/W2 plus fixed-shape lowering [D3.2.2, D8.3.2–D8.3.3;
`vibe/Lambda_Design_Name_Identity.md`]. It owns the macro tail: deltablue 8.2x, splay 10.2x,
gcbench 10.7x (gcbench's Tune14 profile showed the same `map_shape_field_to_item`/`fn_member`
signature), and a share of cd 18.6x.

### 2.4 F4 — Per-call typed-array re-validation

`ensure_typed_array`/`ensure_sized_array` re-derive the array witness at every call boundary:
nbody's top *named* leaf (42), deltablue **~14%** (293 in `ensure_sized_array` interiors),
towers ~5% (89). A callee with a declared `float[]` param re-proves per call what the caller
already proved — the witness should travel with the call [D3.3.1, D3.3.3].

### 2.5 F5 — Allocation and string residue

- `brainfuck` (**~21%**): `fn_fill` 396 + `__bzero` 110 — `fill(30000, 0)` allocates and
  fills a *generic Item* tape per iteration (80,000 boxed stores per fill in the generic
  path). A packed-int `fill` result is both the F1 fix (typed witness) and ~5x less memory
  traffic.
- `base64` (**~24% string family**, now the top family post-A2): `fn_string` 89 +
  `fn_join` 78 + `fn_strcat` 35 + `memmove` 46, plus gc alloc ~12%. This is the Tune13/14 A5
  string-builder track, whose entry gate (≥15% post-A2) is **now met**: the loop-carried
  `result = result ++ (TABLE[...] ++ TABLE[...] ++ …)` builds three temporary strings per
  3-byte group and re-copies the accumulator [S1.4–S1.6].
- `splay`: allocation + the only GC-bound MIR row in this set (gc_collect ~14%).

### 2.6 F6 — nbody: the time is *inside* the JIT code

nbody (typed 30.3 ms vs C2MIR 1.50 ms = 20x) profiles unlike every other row: the leaf list
is almost entirely anonymous JIT-code addresses, with `ensure_typed_array` the only hot named
runtime symbol. There is no runtime-helper family to remove — **the emitted code itself is
~20x worse than C2MIR's**, on a shape (7 `float[]` params, helper calls per step, 5-body
pair loops) that mixes F2 (call ABI) and F4 (per-call ensure) with whatever the generated
loop bodies do per element. Contrast mandelbrot: a single tight all-native loop, typed
49.9 ms vs C2MIR 30.4 ms = **1.64x** — proof that when the typed lane actually stays native,
we sit near the static ceiling. nbody needs a MIR-dump instruction-level diff against the
C2MIR-compiled C for `advance()` before any codegen work is scheduled (§4 B5 gate).

## 3. The C2MIR lesson, sharpened

C2MIR wins because the transpiled C carries every proof in the type system: locals are raw
`double`/`int64_t`, arrays are raw memory, fields are struct offsets, calls pass scalars in
registers. It consults **zero runtime metadata per operation**. Each §2 family is one place
the typed MIR lane still consults metadata per operation:

| A declared type should buy | Status in v24 |
|---|---|
| Unboxed local representation | ✅ landed (Tune13/14) |
| Native arithmetic on proved lanes | ◐ loops yes; across calls no (F2) |
| Direct element access | ◐ guarded lanes yes; comparison/branch/conversion consumers no (F1) |
| Zero per-element admission | ✗ F1 — admit/check still 20–50% on six rows |
| Native scalar call ABI | ✗ F2 — self-recursion only |
| Zero frame overhead when unused | ✗ F2 — every function loads/bumps/restores the number frame (restore is an outlined C call even in release); every scalar-classed helper call wraps in an adopt+restore call pair (B2.1) |
| Witness travels with call | ✗ F4 — `ensure_*_array` per call |
| Fixed-offset field access | ✗ F3 — ShapeEntry walk + strcmp |
| Write validation elided when statically conforming | ✗ F3 — validator + COW per write |

The target state is unchanged from the Result18 dissection: **the annotation pays for the
proof at compile time; the runtime never re-checks what the proof already established.**

## 4. Proposed MIR track (ranked by measured impact)

### B1 — Zero-per-element admission in proved regions (extends Tune14 A1)

Extend the static identity proof to the consumers A1 missed [S4.1, D2.4, D3.2.1, D3.3.3]:

1. proved element reads feeding **comparisons, branch conditions, and boolean contexts**
   emit native compares — no `boundary_admit`, no boxed `fn_ne`/`fn_lt` (`pnpoly`, `cd`);
2. **conversion builtins on proved operands** (`int()`, `float()`, `ord()`, truncating `/`)
   lower to carrier conversions — `fn_int`/`it2i`/`it2d`/`fn_float` leave the samples
   (`crypto_sha1`, `spectralnorm`) [S4.5.3, D2.2.2];
3. **infer packed witnesses for unannotated locals** initialized from
   witness-producing builtins: `fill(n, int-literal)` → packed int array typed as `int[]`
   (`brainfuck` tape; pairs with B4's packed fill);
4. where an admission call genuinely remains, **inline its fast path** (tag test + carrier
   move) into the JIT code so the slow general routine is the cold path only.

Acceptance: `boundary_admit` leaves the pnpoly/sha1/cd/levenshtein hot leaves; pnpoly −40%,
sha1 −30%, cd −20%, brainfuck typed ≤ untyped; typed/nullable rejection controls and
forced-GC sweeps green [D8.6.3].

### B2 — Call-boundary scalar machinery: elide it where proven, then go native

The emission audit (2026-08-07, `transpile-mir.cpp` + `mir_emitter_shared.hpp`) shows the
current per-call/per-function cost structure that F2 measures:

- **Every generated function** unconditionally runs `emit_number_frame_enter`
  (`transpile-mir.cpp:1223`): an inline load of `context->side_number_top` in the prologue,
  and — because any function making even one scalar-classed call reserves the discard
  scratch slot (`em_finalize_scalar_homes`, `mir_emitter_shared.hpp:2460`) — the full inline
  bump + commit-limit check + publish, with the outlined `lambda_side_stack_ensure_for`
  grow path.
- **The exits are outlined C calls, even in release**: `em_store_frame_top` special-cases
  `side_number_top` into a call to `lambda_restore_number_frame_top`
  (`mir_emitter_shared.hpp:845`; release body = range-validate + store, poisoning is
  debug-only), and scalar-classed returns call `lambda_item_adopt_scalar_home`.
- **Every scalar-classed runtime-helper call site** (`em_call_with_args`,
  `mir_emitter_shared.hpp:2959`) emits: scalar-home allocation, a `side_number_top` load,
  the helper call, then an **adopt call and a restore call** — two extra C calls wrapping
  every `fn_add`-class call. This is the adopt/restore pair at ~8–10% of deltablue/cd.
- Already elided today: declared plain-`int` returns get `MIR_SCALAR_RETURN_NONE`
  (int53 packs inline; `mir_emitter_shared.hpp:719`) so the return-adopt is skipped, and
  native bodies (`generate_native`) get `RETURN_LANE_NONE` — no `_scalar_home` param at
  all. Precedent for frame elision exists in the same prologue: the root-top store is
  erased when `root_slot_count == 0` (`mir_emitter_shared.hpp:1250`).

**B2.1 — cheap emission slices (no ABI change):**

1. **Inline the release-mode number-top restore**: emit a plain MIR store
   `context->side_number_top = base` instead of the `lambda_restore_number_frame_top`
   call; keep the checking/poisoning helper in debug builds. Removes one C call from every
   function exit and every scalar-classed helper call site [D5.2–D5.3].
2. **Elide the number frame for functions that provably never touch it** (the pure-int
   case): the transpiler knows every site where it materializes a home or emits a
   scalar-classed call — if a body has none (all arithmetic native, no wide-scalar locals,
   no error lane), `side_number_top` cannot move during its extent (callees restore their
   own balanced extents; caller-extent transients only arise via scalar-classed calls), so
   skip the base load, discard slot, bump/check, and restore entirely. The error lane keeps
   its scratch slot; `pn` bodies with awaits keep the dynamic path.
3. **Inline the adopt fast path**: `lambda_item_adopt_scalar_home` begins with a tag test —
   emit the "payload is inline-packed?" test in JIT code and call only on the number-homed
   slow path. Most int results skip the call entirely.
4. **Audit helper scalar classes in the import registry**: helpers whose results are
   provably inline-packable (comparisons, `ord`, small-int arithmetic) but registered
   `DYNAMIC` force the full home-donation dance per call for nothing.

**B2.2 — native scalar call ABI for statically-bound calls (generalize A3; the dual-func
step):** whole-program per-callee analysis is already in place for self-recursion; extend it
to any statically-bound `pn`/`fn` whose params and success return are declared scalars:
callee gets a native-ABI entry (raw i64/double in, raw out), caller skips the entire home
apparatus on the native edge; guard failure/dynamic call/error merge keeps the boxed entry
[D3.3.1–D3.3.2, D8.3.3; DF8 callee-side check, DF9 entry-equivalence]. Include
**typed-array witness pass-through** (F4): a declared `float[]`/`int[]` param whose argument
carries the caller's proved witness enters without `ensure_typed_array`/`ensure_sized_array`.
B2.2 subsumes B2.1 on native edges; B2.1 keeps paying off on every edge B2.2 can't reach
(dynamic calls, error lanes, `any` returns).

Targets: spectralnorm (48% boxed math; expect −60%+), nbody, deltablue (14% ensure), the
whole micro family (towers/permute/list/bounce/queens/storage — their 30–74x C2MIR gap is
per-call overhead), and the adopt/restore tax on every row.
Acceptance: B2.1 alone — `lambda_restore_number_frame_top` and
`lambda_item_adopt_scalar_home` leave every MIR sample's hot leaves; measurable win on fib/
tak/towers/list without ABI changes; forced-GC + poison sweeps green (the debug helper keeps
the poison invariant) [D8.6.3]. B2.2 — fn_div/fn_mul/fn_add leave the spectralnorm sample;
spectralnorm ≤2x Node; **all twelve ≤5%-bar violations cleared or attributed to F3**; ack
does not regress.

### B3 — Fixed-shape record lowering + Name-Identity W1/W2

For declared map/record types (`TState`, deltablue's constraint records, splay nodes):

1. lower field access to **fixed slot offsets** behind a one-time shape guard — no
   ShapeEntry walk, no `strcmp`/`memcmp`/`strlen` (land `ShapeEntry.name_id`, NI W1/W2);
2. a **statically-conforming write to a declared field skips the deep validator** — the
   proof is the declared contract (`towers`: `validate_against_*` must leave the sample);
3. a `var`-bound uniquely-owned record **writes in place** — no `map_set_cow` copy, no
   `pool_calloc`/`memset` per write (C4 value semantics preserved: the elision requires the
   static uniqueness the `var` binding already gives) [D3.2.2, D8.3.2–D8.3.3].

Targets: deltablue −40%, towers −50%, splay −30%, gcbench, json_gen, cd's record share.
Acceptance: named-contract rejection tests unchanged; the F3 leaf family leaves all three
samples.

### B4 — String builder + packed fill (A5 entry gate now met)

- `base64`: internal owned builder for loop-carried `++` accumulation, one immutable
  finalize at the observable boundary; Unicode semantics and precise roots preserved
  [S1.4–S1.6]. Expect −40% on the string family's ~24% plus reduced GC pressure.
- `fill(n, scalar)` allocates a packed ArrayNum directly (no per-slot boxed store) —
  pairs with B1.3 for `brainfuck` (~21% measured ceiling).

### B5 — nbody/raytrace3d codegen audit (investigation gate)

Dump the typed `advance()` MIR (debug build → `temp/mir_dump.txt`) and diff instruction
counts per loop iteration against the C2MIR-compiled transpiled C. Decide afterwards whether
the residue is (a) B2's call ABI, (b) per-element code shape (extra moves/guards), or
(c) MIR register allocation — only (b)/(c) would justify a codegen phase. mandelbrot's 1.64x
is the control: tight native loops are already fine.

### B6 — splay/gcbench GC cadence (conditional)

splay is the only MIR row in this set with a double-digit GC share (~14%). After B3 removes
the per-op map churn, re-profile; enter GC tuning only if collection share is still ≥10%
(gcbench's 25% LJS mark residual from Tune14 L2 remains parked with it).

## 5. LambdaJS note (out of scope this round, carried forward)

Tune14's L1 landed only the safe static-key slice; the measured §2.5 (Tune14) dynamic-key
family — `js_map_get_fast(char*)`, `well_known_name_id` per access, numeric keys
round-tripping through `snprintf`/`sscanf` — is still the top LJS item and still owns the
tail (havlak 448x, cd 267x, hashmap 210x, sha1 197x). The integer/NameId fast path for
dynamic numeric keys remains the highest-leverage LJS change and should be its own round
(LJS may use ICs — D8.4.1 restricts Lambda script only).

## 6. Gates and acceptance (house rules, unchanged)

- `make test-lambda-baseline` 100% and `make test262-baseline` 40,261/40,261 after each
  retained phase; MIR emission ratchet updated in the same commit for justified growth
  [D8.6.1]; `mir-check` coverage for every new witness/elision/native-ABI edge [D8.6.2];
  forced-GC + poison sweeps for every ownership/root/representation change [D8.6.3].
- Release-build timing only; a change is retained only with its measured win on the fixed
  56-row population (three runs, workload-only `__TIMING__`, headline + matched geomeans).
- The categorical bar stands: **an annotation may never make a row more than 5% slower** —
  B1+B2+B3 are collectively accountable for clearing the twelve-row ledger in §1.
- Non-goals carried forward: no flex-int revival, no Lambda-script inline caches [D8.4.1],
  no vendored-dep edits [D1.6], no C2MIR-path changes (frozen, rule 14).

Round target: typed/Node geo 1.68x → **≤1.3x**; typed/C2MIR 8.19x → **≤5x**; every
annotation-regressed row cleared.

## 7. Profiling protocol

Identical to Tune14 §5, with samples under `temp/prof15/` and the symbol table at
`temp/prof15/nm_v24_sorted.txt` (add 0x100000000 to sample offsets before nm bracketing).
Looped prof variants for rows without one: `temp/prof15/{brainfuck,splay,spectralnorm,
nbody,pnpoly,deltablue,cd}_prof.ls` (outer loop scaled only). Discount
`heap_finalize_gc_objects`/`gc_heap_destroy` teardown leaves on short rows.

## 8. Implementation and verification record (2026-08-07)

Tune15's implementation slices are landed in the MIR-direct runtime. The implementation
keeps the semantic and ownership boundaries required by S4.1.2, S4.5.3, D2.2.2, D3.2.2,
D3.3.1–D3.3.3, D5.2–D5.3, and D8.3.2–D8.3.3; no C2MIR or vendor code was changed.

| Slice | State | Evidence |
|---|---|---|
| B1 residual boundary admission | Landed safe slices | Native `int`/`float`/`trunc` conversion lowering, comparison boxing, witness-producing `fill`, and a checked native integer-expression tree at float consumers; `tune15_scalar_conversions`, `tune15_fill_witness`, `tune15_native_int_tree`, and `tune15_string_builder` fixtures. |
| B2.1 frame/adopt elision | Landed | Release-mode inline number-top restore, scalar-home adoption fast path, and number-frame elision; `tune15_number_frame_elision` plus normal and forced-GC/poison runs. |
| B2.2 native scalar calls | Landed | Statically-bound native scalar call ABI, native scalar returns, and typed-array witness pass-through; `tune14_native_return`, `tune15_native_scalar_call`, and `tune15_typed_array_witness` fixtures. |
| B3 fixed-shape records and COW | Landed | Fixed-shape field path and uniqueness-protected in-place update; `tune15_fixed_shape_record` fixture and typed-array guard ratchet. |
| B4 string builder and packed fill | Landed | Owned geometric string builder and packed scalar fill; the B4 entry gate is covered by the fill and string-builder fixtures. |
| B5 nbody codegen audit | Completed as audit | `temp/tune15_nbody2_lambda_witness.mir` records the typed body at 2,283 instructions/168 calls versus 102/1 for C2MIR. The remaining difference is not a safe basis for an arbitrary codegen rewrite, so no unsupported optimization was retained. |
| B6 GC cadence | Profiled; no new tuning required | The splay sample contained 94 `heap_gc_collect` leaves out of 807 samples (~11.65%); the existing adaptive threshold remains in place. A direct splay run recorded 5 mark collections, 145.906 ms marking, and 2,001.56 ms total (~7.3%), so no independent cadence change was justified. |

### Verification gates

| Gate | Result |
|---|---|
| `make test-lambda-baseline` | 3,620/3,620 (2,104 input + 1,516 Lambda runtime) |
| `make test262-baseline` | 40,261/40,261; 0 failures, 0 non-fully-passing tests, 0 retries |
| MIR focused fixtures | 10/10 Tune14/Tune15 fixtures |
| MIR emission ratchet | 15/15 |
| Forced GC + freed-home poison | Normal and `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` runs exit 0 |
| Release fixed population | 56/56 MIR and typed-MIR rows completed successfully, three samples each |

The final release run measured typed/MIR geometric mean **0.545**. Typed/Node measured
**1.35x** on the full 56-row set (**1.03x** on the 44 rows also present in the C2MIR
comparison), so the ≤1.3x target is met on the matched population but not yet on the
full population. Typed/C2MIR measured **6.31x**, an improvement over 7.02x after the
integer-tree slice, but the aspirational ≤5x target is not met. The remaining
typed-vs-untyped rows above the 5% bar are `r7rs/nqueens`, `awfy/bounce`, `awfy/list`,
`beng/knucleotide`, `kostya/brainfuck`, `kostya/json_gen`, and `jetstream/splay`.
The prior spectralnorm arithmetic residue is cleared: its typed release time is now
2.62 ms and its MIR contains no `fn_add`/`fn_mul`/`fn_div` calls in `eval_A`.
These residual ratios are measured follow-up opportunities, not hidden acceptance claims.
The implementation is complete for the Tune15 slices; any further reduction of the
C2MIR gap requires a new root-cause round under D8.6.1–D8.6.3.
