# Lambda Impl Proposal: Tune28 — Typed Lane vs C2MIR after Result44/45

- **Date:** 2026-09-16
- **Status:** IN PROGRESS. T28-1 implemented (§9.1); T28-2 partly blocked by D8.3.4 (§9.2); other tracks open. Evidence in §2–§4 was taken on the
  archived Result45 release binary `test/benchmark/exe/lambda-v45-714d448fb2`
  (commit `714d448fb2`, the type-binder merge). Raw captures: `temp/r45prof/`
  (`*.sample.txt` = `sample -wait` at 1 ms, `*.cow.tsv` = `COW_EXEC_PROFILE`,
  `*.mir` = `LAMBDA_MIR_DUMP_PATH`, JIT-pinned throughout).
- **Formal authority:** S1.6 (specialization unobservable), S4.1.2 (int
  saturation), S9.1.3 (plain-parameter snapshot), S11.4.1v3 (proof reuse);
  D2.4.1–D2.4.3 (lanes), D3.2.2 / D3.2.4v3 (validator deep on first
  crossing; elision test), D3.3.3v3 (carrier certificates), **D4.4.4v3** / D4.4.5
  (borrows: unobservable under S9.1.2 is the ruling, path shape is impl detail), D8.1.1v10 (tier admission), **D8.3.1v2–D8.3.4**
  (bounded immutable raw variants; check in the callee; two proof paths;
  visibility), D8.4.1v2 (no inline caches).
- **Vibe authority:** `Lambda_Design_Type_Generics.md` rev 15 (TG8, TG20v2),
  `impl/Lambda_Impl_Type_Binder.md` (TG-P3.4, TG-P4),
  `impl/Lambda_Impl_Tune27 (done).md` §3 (M1–M8) and §8 (obligations 1–8).
- **ID series:** `T28-#` tracks, `N#` new mechanisms (M1–M8 keep their
  Tune27 numbers).

## 1. Objective

Result44 measures MIR (typed) at **4.41x C2MIR** geomean over 63 rows
(4.35x on the 59-row Result38 population) and **0.86x Node**. Result45 is a
LambdaJS round (LambdaJS/Node 21.0x → 18.7x; JC23 stack guard, call flatten)
and carries no C2MIR column; its typed Lambda rows are within noise of
Result44 except matmul, whose Result44 3.0x typed penalty was fixed
post-report (17.7 → 5.78 ms; Tune27 §12). Result44's C2MIR column is
therefore the valid reference for this proposal.

This round answers three questions with evidence rather than the Tune27
model: where the remaining 4.4x is, which part of it the newly landed type
binder (D8.3.1v2 raw variants) can reach, and what the next tracks are.
Gate: typed/C2MIR ≤ 3.0x on the 63-row population with outputs identical on
all 138 benchmark scripts across the three tiers.

## 2. Result44/45 read

**Where typed already meets C2MIR (≤ 1.5x):** fib 1.16, tak 1.03, cpstak
1.05, sum 0.99, sumfp 0.84, ack 0.82, mandelbrot 1.36, collatz 1.40,
diviter 1.02, divrec 0.25, binarytrees 0.71, gcbench 1.16, regexredux 1.11,
matmul 0.95 (post-fix). These are scalar-loop and allocation rows: the
scalar lane is done and the M8 band-test floor (≈1.4x) is what remains
there.

**Widest gaps (typed / C2MIR, Result44):**

| Row | typed ms | C2MIR ms | ratio | shape of the port |
|---|---:|---:|---:|---|
| text/hyphen | 69.5 | 1.48 | 47.1x | untyped core module, string append/join loop |
| awfy/deltablue | 41.2 | 1.14 | 36.0x | `pn f(var w: World, …)` records in typed arrays |
| awfy/havlak | 62.9 | 1.80 | 35.0x | same shape, 57 `pn`, 64 `var` params |
| awfy/cd | 490.2 | 15.1 | 32.4x | untyped `array` fields, linear key table |
| text/microdiff | 57.1 | 2.61 | 21.9x | `any` structural diff, `fn_len`/`fn_join` |
| beng/knucleotide | 5.28 | 0.280 | 18.8x | string keys, hashing; typed slower than untyped |
| jetstream/cube3d | 9.06 | 0.528 | 17.2x | 15.5k-insn `run_cube`, compile-bound |
| text/prettier_ast | 666.9 | 41.4 | 16.1x | recursive `Doc` union, 6.6M admits |
| kostya/base64 | 8.77 | 0.555 | 15.8x | string build |
| jetstream/splay | 295.6 | 18.9 | 15.6x | 826k map copies (nested store-back) |
| jetstream/hashmap | 42.9 | 2.80 | 15.3x | 450k unique-mutation checks |
| awfy/richards | 389.5 | 29.1 | 13.4x | `tasks: array` untyped fields, `fn_map_set` |

**Typed slower than untyped (Result45):** knucleotide 1.17x, bounce 1.45x
(R44), mbrot 1.32x (R44), fasta 1.13x, microdiff 1.11x, three_way_merge
1.09x, log_pipeline 1.04x. The pattern is the same as Tune27 §11 G7: a typed
annotation adds admissions and lane conversions without giving the body a
raw representation to use them on.

## 3. Fresh census on the Result45 binary

### 3.1 Static: one hot function, instruction by instruction

`deltablue2.ls` `c_choose_method` (60 source lines; the C port's equivalent
is a `switch` over a struct) emits **3,334 MIR instructions** with 1,356
locals. Counted from `temp/r45prof/ccm.mir`:

| what | count | source in the body |
|---|---:|---|
| tag dispatch (`ursh 56` + branch) | 70 | every read of a module `let` constant (`K_EDIT`, `FORWARD`, …) |
| `lambda_item_to_int_lane_c` calls | 44 | slow arm of those constant reads |
| `int2it_lane` (re-boxing a lane) | 57 | passing lanes back to Item ABI (returns, `_b`, path stores) |
| `item_at` calls | 16 | miss arm of every `w.cons[cid]` / `w.vars[o]` index |
| layout-kind guard (`and … 1535`) + certificate identity (`bne … 4318441680`) | 20 + 20 | **per index access**: proves `w.cons` is still the `Constraint?[]` layout |
| null-sentinel materializations | 123 | every field/element read keeps its null arm |
| `lambda_type_check` | 14 | boundaries into `s_stronger`/`s_weaker` and record returns |
| `lambda_map_path_set_checked_fixed` calls | 12 | every `w.cons[cid].satisfied = 1` store |
| `fn_eq` | 7 | compares whose operand came back boxed |
| int53 band tests | 16 | on `k == K_EDIT`-style equality (no arithmetic) |

The `.kind` read itself is already a direct offset load (obligation 1's
read half exists). What costs is everything around it: proving the array
layout per access, the null arm per read, the boxed module constant, the
path-setter call per store, and the re-boxing at every edge. All 49
procedures get a native body plus a `_b` wrapper (`temp/r45prof/deltablue2.mir`:
49 + 49, **0 raw variants**), and in that native body `w: World` is still a
tagged `Item` (`and %p1, 0x00FFFFFFFFFFFFFF`) — the record is boxed even on
the "native" entry, exactly as `is_native_param_type_id` (`lambda.h:1799`,
scalars only) says.

### 3.2 Dynamic: `COW_EXEC_PROFILE` (exact counts, one JIT run)

| Row | ms | unique-mutation checks | shared copies | admissions | strings |
|---|---:|---|---|---|---|
| richards2 | 397 | map 1,821,750 · array 1,725,950 | 1,000 | map 50,700 | — |
| deltablue2 | 42 | array[num] 334,880 · map 55,200 · array 42,760 | array[num] 67,420 | map 24,620 | — |
| hashmap2 | 41 | map 450,004 · array[num] 450,000 | 0 | — | — |
| havlak2 | 63 | map 242,583 · array 180,821 · array[num] 56,234 | map 58,612 · array 35,130 · num 28,117 | map 202,679 | — |
| splay2 | 295 | map 394,822 | **map 825,641** | map 629,306 | — |
| cd2 | 482 | array 329,205 · map 47,745 | array 239,862 · map 132,766 | map 1,164,979 | — |
| prettier_ast2 | 687 | (marks only: array 301,825 · map 333,577) | 0 | **union 6,615,040** (6 misses) | 783k appends, 263k generic joins, 5.3 MB copied |
| hyphen2 | 71 | array[num] 15,972 | 0 | — | 71k appends, 3,964 generic joins |
| brainfuck2 | 191 | — | — | — | 130k appends, 10k freezes |

richards2 runs **3.5 million ownership tests** for 397 ms: every
`w.tasks[tid].input = …` through `var w: World` re-establishes that `w` is
unique (M2). The copies are gone (1,000); the *tests* are the cost. splay2's
826k map copies are the branch-local store-back shape Tune27 §10.12 could
not admit, now ruled in by D4.4.4v3 (T28-8); they also drive its GC (§3.3).

### 3.3 Dynamic: `sample` top-of-stack (self time)

Percentages are of samples inside the run; `???` are stripped statics.

- **cd2** (282): `fn_index` 10%, `fn_len` 9%, `array_push` 7%, `array_set`
  7%, `scalar_storage_read` 7%, `gc_collect_with_root_region` 6%,
  `array_get` 6%, `item_at` 5%. Every top symbol is a *generic* container
  operator on the port's untyped `array` fields (Tune27 §10.17 residue).
- **richards2** (239): `fn_map_set` 15%, `map_shape_field_to_item` 5%,
  `cow_path_set_raw` 4%, `map_field_to_item` 4%, `cow_prepare_write` 4%,
  **`lambda_module_state_for_unit` 4%**, `_platform_memcmp` 4% (key compare
  inside the generic map set), `fn_index`, `fn_member`.
- **splay2** (244; call graph): 93 of 338 in-run samples are
  `map_set_cow → cow_prepare_write → gc_heap_alloc → heap_gc_collect`. The
  COW copy allocates, the allocation collects. `lambda_type_nonnull_map_contract`
  6%, `cow_mark_shape_children` 5%, `fn_map_set` 5%.
- **prettier_ast2** (593): `lambda_numeric_boundary_admit` 7%,
  `map_shape_field_to_item` 6%, **`lambda_module_state_for_unit` 6%**,
  `fn_member_by_id` 5%, `lambda_type_check` 4%, **`lambda_module_name_id_at`
  4%**, `fn_len` 4%, `_tlv_get_addr` 3%, `lambda_type_accepts_null` 3%,
  GC 2%. The admission cluster (`numeric_boundary_admit` + `type_check` +
  `accepts_null`) is 14% *after* the union memo hits 100% — M1's validator
  is gone; what remains is the admission call itself, 6.6M times.
- **three_way_merge2** (4,598) / **log_pipeline2** (7,499): `memcmp`
  (platform + dyld stub) **27% / 25%**, `fn_split` 6% / 6%,
  `lambda_type_matches` 5% / 4%, GC 4% / 11%, `array_push`, `fn_index`,
  `fn_substring` / `fn_slice` / `str_find`, `lambda_array_contract_info` 2%.
- **brainfuck2** (48): `fn_fill` **71%**, `fn_string_ascii_at` 19%, `fn_ord`.
- deltablue2 / havlak2 / hashmap2 / hyphen2 / microdiff2 are too short for
  1 ms sampling (≤ 5 samples); §3.1–§3.2 carry them.

## 4. Mechanisms: what the 4.4x is made of

Tune27's M1–M8 stand, with their state after Tune27 noted; N1–N5 are new
findings from §3.

| # | Mechanism | Evidence | Rows |
|---|---|---|---|
| M2 | COW ownership re-tested per store on `var` record/array params; no frame-level exclusivity fact | 3.5M tests richards2, 900k hashmap2, 480k havlak2, 430k deltablue2 | richards, hashmap, havlak, deltablue, navier |
| M1' | admission *call* on every crossing of a recursive union even when the memo hits | 6.6M `union_admit_calls`, 14% self time | prettier_ast, three_way_merge, log_pipeline (`lambda_type_matches` 4–5%) |
| M4 | generic container/string operators on `any`/`array` carriers | cd2 top-8 symbols; hyphen/microdiff `fn_len`/`fn_join`; brainfuck `fn_fill` 71% | cd, richards, microdiff, hyphen, brainfuck, text rows |
| M6 | emission size: 3,334 insns for a 60-line body; `run_cube` 15.5k | §3.1 | cube3d (compile-bound), deltablue, havlak |
| M8 | int53 band tests on ops whose range is known, and on *equality* | 16 in `c_choose_method` | every numeric row (≈1.4x floor) |
| **N1** | **module `let` scalar constants are boxed Items read from module state**: tag dispatch + slow-arm call per use | 70 dispatches + 44 calls in one function; `lambda_module_state_for_unit` 4–6% self time | deltablue, richards, prettier, three_way_merge |
| **N2** | **array layout proven per access, not per function**: layout-kind + certificate-identity + bounds + null arm on every `w.cons[cid]` | 20 + 20 guards, 16 `item_at` miss arms, 123 null sentinels in one body | deltablue, havlak, hashmap, cube3d |
| **N3** | **records have no raw ABI**: `World`/`Constraint` enter the native body as tagged Items; every field store is a `lambda_map_path_set_checked_fixed` call | 12 path-set calls; `map_shape_field_to_item` 5–6% on richards/prettier | deltablue, havlak, richards, splay, prettier |
| **N4** | **module-state prologue call** in every function that reads a module binding, plus `lambda_module_name_id_at` per `fn_member_by_id` | 27 prologue calls in deltablue2.mir; 6% + 4% on prettier | prettier, richards, three_way_merge |
| **N5** | **string primitives call libc per comparison**: `==`, `split`, `find` reach `memcmp` through the dyld stub for short strings; `split`/`substring`/`slice` copy | memcmp 25–27% on the two longest text rows | three_way_merge, log_pipeline, knucleotide, hyphen, base64 |
| GC | allocation-driven collections caused by COW copies (splay) and string copies (text rows) | splay 32%, log_pipeline 11% | splay, log_pipeline, three_way_merge, cd |

Reading the table against Tune27 §8: obligations **1** (typed containers
and records as native ABI values), **4** (ownership as a static fact) and
**6** (certificates per loop, not per access) are where the widest twelve
rows live. Obligations 3 and 5 are partially met; 2, 7, 8 are mechanical
residue. N1 and N4 are new, cheap, and cross-cutting.

## 5. The type binder: what it can and cannot buy here

**What landed (D8.3.1v2–D8.3.4, 2026-09-15).** A `fn` whose signature
carries `as T` binders gets up to four immutable `__rawN` bodies keyed on the
exact argument-type tuple plus the bound types; `_b` runs the exact-key
guard chain (scalar tags, normalized container kinds, **descriptor-pointer
identity for named/nominal maps and concrete elements**) and falls back to
the complete boxed body; a statically exact local edge calls `__rawN`
directly. `LAMBDA_MIR_TG8_HOIST_GUARDS=1` hoists the same chain to the
caller.

**What it contributes to the benchmark corpus today: nothing.** No
benchmark source uses `as T` (grep over all 63 typed rows: 0), and the
planner refuses everything the hot rows are made of
(`mir_callsite_exact_raw_key`, `transpile-mir.cpp:35795–35818`):

- `!signature->binder_count` → no raw variants for a function without a
  binder, i.e. every existing typed function;
- `callee_as->node_type == AST_NODE_PROC` → no `pn` (the twelve widest rows
  are 100% `pn`: deltablue2 49/0, havlak2 57/0, splay2 19/0, richards2_core
  20/0, cd2 32/0, cube3d2 15/0);
- `parameter->is_var_param` → no `var` parameter (80 in deltablue2, 64 in
  havlak2, 30 in splay2, 52 in cube3d2);
- `callee->captures` → no closures.

The deltablue2 dump confirms it: 49 `_b` + 49 native bodies, **0 `__raw`**.

**Why the machinery is still the right lever.** Three of its parts are
exactly what obligations 1, 4 and 6 need, and they exist now with tests
(`type_binder_raw_variants.ls`):

1. The **descriptor-identity guard** in `_b` is the record-ABI proof. Once
   `_b` has matched `w`'s `TypeMap*` against `World`'s descriptor, the raw
   body may read `w.cons` as `i64:8(base)` with no null arm on a
   non-optional field and no layout re-proof — D8.3.3 gives the raw entry
   exactly two proof paths (exact static edge or `_b`), so the per-access
   guards of N2 are the "third path" that ruling forbids.
2. The **multi-key plan** generalizes the closed-caller single-key
   inference specialization (`specialization_types[pos]`,
   `transpile-mir.cpp:35770`), which today joins conflicting call sites to
   `ANY` and pays — per its own comment — geomean 1.038 with gcbench +31%,
   nqueens +30%, binarytrees +28% (T19-4). D8.3.1v2's text keys a variant on
   "the exact argument-type tuple **plus** every selected binder-slot type";
   a function with zero binders has an empty binder part and is a
   degenerate, fully valid case of the same rule.
3. **TG20v2 exact bindings** (TG-P3.4) are the elision licence at
   typed→typed edges that M7/obligation 2 still lacks for records.

What binders do **not** buy: any of the string rows (N5 is representation,
not dispatch), the COW ownership tests of M2 (a per-frame escape fact, not
a type), splay's copies (T28-8, a borrow-shape extension), or the M8 floor.

## 6. Tracks

Ordered by (expected geomean gain) / (risk). Each track names its ruling,
its mechanism, its pilot row, and its gate. Every track keeps the standing
gates: `make test-lambda-baseline` at its current pass set, 138 benchmark
outputs byte-identical across interp/jit/auto, MT7 ratchet, and the
tier-matrix gtest.

### T28-1 — Module scalar constants as immediates (N1, N4)

An immutable module-level `let` bound to a scalar literal (`let K_EDIT = 1`,
`let NONE = -1`, `let FORWARD = 1`) is a compile-time constant under
S1.6/D2.4.1; today it is loaded from the module slab as an Item and
tag-dispatched at every use (70 times in one function). Fold it: the
emitter already has the RC8 folded-constant path for literals
(`emit_sysfunc_abi_arg`), and Tune27 §10.x made "immutable module ints
native index leaves" — extend that leaf rule from index positions to every
lane consumer (compare, arithmetic, call argument, store RHS). Then remove
the prologue `lambda_module_state_for_unit` call from functions whose only
module reads were folded; for the rest, cache the state pointer in a fixed
`Context` slot so the prologue is one load (N4). `fn_member_by_id`'s
`lambda_module_name_id_at` lookup becomes a constant NameId operand the
same way.

- Pilot: deltablue2 (`c_choose_method` 3,334 → target ≤ 2,400 insns),
  prettier_ast2 (−10% self time expected from §3.3).
- Gate: `test/mir/lambda/tune28_module_const_fold.mir-check` — no `ursh …
  56` on a module-constant read; zero `lambda_item_to_int_lane_c` calls in
  `c_choose_method`; `--emit-ast-dump` shows the constant's literal type.
- Risk: low. Semantics unchanged; only `let` (never `var`) module bindings
  with a literal initializer qualify. Do **not** fold through a call or
  through `import` (const-pool serialization is TGO12's problem).

### T28-2 — Raw record ABI through the D8.3.1v2 guard (N3, obligation 1)

Lift the three refusals in `mir_callsite_exact_raw_key` for the record
case, in this order and each independently gated:

1. **`binder_count == 0` → allowed.** A function with no binder has an
   empty binder part; the key is the argument tuple (D8.3.1v2 text, TG8
   text). Keep the cap at four and the source-order rule. This alone
   replaces the single-key `specialization_types` join-to-ANY for
   scalar-tuple conflicts and is the T19-4 recovery (pilot rows gcbench,
   nqueens, binarytrees, list, storage — the ones its comment names).
2. **Named/nominal map parameters as raw pointers.** A key element whose
   type is a declared record (`World`, `Constraint`, `Node`) is admitted
   when `mir_raw_variant_requires_shape_guard` is true and `_b` performs
   the descriptor-identity match (already implemented for binder
   functions). Inside `__rawN`, the parameter is a raw base pointer with a
   proven `TypeMap*`: field reads are `i64:off(base)` with no null arm when
   the field's declared type is non-optional (retire the
   `skip_null_guard = false` hard-code at `transpile-mir.cpp:21286` for
   proven descriptors), and field stores are raw `mov i64:off(base), v`
   plus the store's own contract lane conversion — no
   `lambda_map_path_set_checked_fixed` call. Reads of `w.cons` yield the
   array carrier *with its certificate proven once* (D3.3.3v3): the layout
   guard pair of N2 is emitted once per function at first use, not per
   access; a rebinding of the field or a call that may retain `w`
   invalidates the fact (D4.4.4v2's escape rule).
3. **`pn` and `var` positions.** A `var` record parameter travels through
   the CW33 home exactly as D8.1.1v10 already does for the `_b` adapter;
   the raw body receives the home's raw pointer and the epilogue publishes
   it back. The COW exclusivity question is T28-3's, not this track's: the
   raw body still calls `cow_prepare_write` where the frame fact is
   unknown, but does so **once per frame** because the descriptor and the
   home are function-invariant.

- Pilot: deltablue2 (36x → target ≤ 12x), then havlak2, hashmap2, splay2,
  richards2_core (only its `World` scalar fields; `tasks: array` stays
  generic — a port choice).
- Gate: `test/lambda/type_binder_raw_variants.ls` extended with a
  binder-free record function proving `__raw0` is emitted and selected on
  the exact edge and `_b` falls back on a derived shape;
  `tune28_record_raw_field.mir-check` — no `item_at`, no
  `lambda_map_path_set_checked_fixed`, at most one layout guard pair per
  `w.cons` per body; `LAMBDA_ROOT_WITNESS=1` and `LAMBDA_GC_FORCE_EVERY=1`
  green on the pilot (the raw base pointer must stay rooted through its
  boxed source — D8.3.1v2's "pointer lanes retain a boxed source root").
- Risk: medium. The hazards are D8.3.3 (a raw body must never be reached by
  an unproven value — every direct edge must be statically exact or go
  through `_b`) and S9.1.3 (a *plain* record parameter still needs its
  snapshot on first write; only `var` and provably-unaliased locals skip
  it). Export ⇒ `_b` mandatory (D8.3.4) is unchanged.

### T28-3 — Frame exclusivity for `var` containers (M2, obligation 4)

Not a binder item; listed because it caps T28-2 on richards/hashmap. A
`var` parameter or local whose value no callee retains and no binding
captures is exclusive for the frame (Tune26 §9's capture/escape result).
Compute it as a per-function escape fact over the AST (calls whose callee
signature borrows the position via `var` do not retain; a plain-parameter
pass, a `let` capture, a container store of the value, or a return
does). With the fact, the raw body prepares the write **once at first
store** and never re-tests; without it, current behaviour. The ruling
already exists (D4.4.4v2, D4.4.5): this is the implementation of its
"handle for the rest of the frame" reading.

- Pilot: richards2 (3.5M tests → ≤ 50k), hashmap2, deltablue2.
- Gate: `LambdaOptCow` census pins (`unique_mutations` on the pilots), the
  `cow_*` tier-matrix fixtures, and a new fixture where a callee retains
  the argument and the fact must be **false**.
- Risk: medium; correctness is S9.2.2 — the census tests are the proof.

### T28-4 — Admission on proven recursive carriers is a pointer compare (M1', obligation 3)

prettier_ast2 pays 6.6M `union_admit_calls` that all hit the memo. D3.2.4v3
says admission of a *proven physical layout* is a compare; the memo hit is
already that compare but it sits behind a call, a null-unwrap
(`lambda_type_accepts_null`), a numeric-boundary probe and a TLS read
(`_tlv_get_addr`). Inline the fast path in the emitter: for a contract
whose non-null arm is a nominal record or a memoized union, emit
`descriptor == expected` (or the 8-way probe) inline and call
`lambda_type_check` only on the miss. The `Doc` recursion is reached
through the non-null arm as D3.2.4v3 requires (the memory note's
"never publish a nullable lane as full_type" trap).

- Pilot: prettier_ast2 (−12–15%), three_way_merge2 / log_pipeline2
  (`lambda_type_matches` 4–5%).
- Gate: `union_admit_calls` unchanged (the *semantics* are), a new
  `union_admit_inline_hits` census row ≥ 99% on prettier; `mir-check`
  forbidding `lambda_type_check` on the `print_node` mandatory path.
- Risk: low.

### T28-5 — Strings: a small-string compare/scan kernel (N5)

Not a typing item and not reachable by binders; recorded because it is
25–27% of the two longest rows and the whole of hyphen/base64/knucleotide.
Three emitter/runtime changes, no representation change yet: (a) string
`==`/`!=` inline: pointer-equal, length-mismatch, then a word-wise compare
for `len ≤ 32` before falling to `memcmp`; (b) `split` with a one-char
delimiter scans bytes, never `memcmp`; `find`/`str_find` use the same
kernel; (c) `substring`/`slice` on an immutable ASCII string return a
borrowed view when the result is consumed by `==`, `len`, or another
`substring` (the escape analysis of T28-3, reused). The proven-ASCII
exclusive-buffer representation Tune27 §8 named stays a separate round.

- Pilot: three_way_merge2, log_pipeline2, knucleotide2 (typed must stop
  being slower than untyped), hyphen2.
- Gate: string fixtures byte-identical on all tiers; `memcmp` ≤ 5% of
  samples on the two pilots.

### T28-6 — Band tests only where a value can leave the band (M8)

Equality and ordering compares on two `int` lanes never need the band test
(`k == K_EDIT` emits 16 of them in `c_choose_method`); a folded constant
(T28-1) is in-band by construction; an induction variable bounded by a
literal or a `len()` is in-band (Tune27 §10.16 proves this for loop
counters — extend the fact to *uses* of the counter, not only the dense
proof). Keep every test whose operand's range is genuinely unknown
(S4.1.2 is the semantics; this changes only where it is *tested*).

- Pilot: collatz2, mandelbrot2 (Tune27 G10: ≤ 1.15x C2MIR), deltablue2.
- Gate: `tune27_*` band-test sidecars unchanged; new
  `tune28_compare_no_band.mir-check`.

### T28-7 — Emission diet for record bodies (M6, obligation 7)

Follows from T28-1/T28-2 rather than a separate mechanism: fewer tag
dispatches, no path-setter calls, one layout proof per body. Record the
per-function budgets (`c_choose_method`, `run_cube`, `print_node`) as MT7
probes *before* T28-1 lands so the ratchet measures each track. Target:
`c_choose_method` ≤ 1,200 instructions, `run_cube` ≤ 10,000 (Tune27 G9,
still open).

### T28-8 — Branch-local store-backs and nested handles (CW36, D4.4.4v3)

Ruled 2026-09-16: a place handle borrows wherever the borrow is
unobservable under S9.1.2; the path shape is an implementation detail
(`vibe/Lambda_Design_Runtime_COW.md` §11.13). Implement the structured-region
shape there: arms classified non-writing / writing+stored-back /
writing+not-stored-back (refuse), final store-backs skip capture, non-final
ones keep the ordinary capture and share-mark, nested handles run the spine
test through the outer leaf, `var`-call rebinds keep the borrow, a root
rebind ends the region for that path. Runtime unchanged.

- Pilot: splay2 (826k map copies → near 0; the 32% GC share follows), then
  havlak2 (58k map copies) and cd2 (133k map copies) where the same
  branch-local shape recurs.
- Gate: `test/lambda/proc/cow_rmw_branch_store_back.ls` (both branches, the
  no-write path, a pre-call second holder, an error exit in a branch, a
  non-final store-back followed by a write that must copy), byte-identical on
  three tiers and against the pre-CW36 binary; `LambdaOptCow` copy-count
  pins; forced-GC and poison-freed runs green.
- Risk: medium. The invariant's condition 4 (share-mark on non-final
  store-back) is the safety net; the fixture's "write after non-final
  store-back" case is the proof it fires.

### Deferred, with the reason

- **cd / richards `array`-typed fields**: the port substituted untyped
  containers for typed ones (`tasks: array`, `keys: array`). Typing them is
  a port change with its own golden; the language-side lever (generic
  operators are correct on `array`) is nil. Recommend re-porting under
  the same workload with `TaskControlBlock[]` / `int[]` once T28-2 gives
  records a raw ABI, then re-measuring.
- **cube3d compile time** (172 ms auto vs 9 ms exec): MIR's generator is
  super-linear in function size; T28-7 is the only lever short of splitting
  `run_cube`.
- **`fn_fill` at 71% on brainfuck2**: a per-iteration `fill()` in the
  interpreter loop; it is the workload as ported, but `fill(n, 0)` under a
  proven `int[]` contract could bump-allocate and `memset` — one runtime
  fast path, worth a probe after T28-5.

## 7. Measurement procedure

- Reference binary for every A/B: `test/benchmark/exe/lambda-v45-714d448fb2`
  (its typed rows equal Result44's within noise; matmul is the post-fix
  value). Never time a debug build; `make test-lambda-baseline` overwrites
  `lambda.exe` and `make release` deletes `test/*.exe`.
- One benchmark process at a time; JIT-pinned `LAMBDA_TIER=jit` for part-1
  numbers, min of 5 interleaved runs (`temp/t27/sweep.py`), 9 runs for any
  sub-3% call.
- Static census per track: `LAMBDA_MIR_DUMP_PATH`, then the §3.1 counts
  (`ursh … 56`, `and … 1535`, `item_at`, `lambda_map_path_set_checked_fixed`,
  `lambda_type_check`, null sentinel `72057594037927936`) on the pilot's hot
  function — these are the numbers each track must move.
- Dynamic census: `COW_EXEC_PROFILE=1` unique-mutation and admission rows;
  `sample <exe> 60 1 -wait -mayDie` started 0.5 s before the run (the
  attach-after-launch form returns zero samples on macOS 26).
- Correctness: outputs of all 138 scripts on all three tiers; the tier
  matrix gtest; `LAMBDA_ROOT_WITNESS=1` and forced GC on every fixture a
  track adds (rule 15: precise roots only).

## 8. Expected outcome

| Track | Rows | expected typed/C2MIR after |
|---|---|---|
| T28-1 | deltablue, richards, prettier, havlak, three_way_merge | −10–20% on each; no row worse |
| T28-2 | deltablue 36x, havlak 35x, hashmap 15x, splay 15.6x, cube3d 17x | deltablue ≤ 12x, havlak ≤ 15x, hashmap ≤ 8x |
| T28-3 | richards 13.4x, hashmap, deltablue | richards ≤ 6x |
| T28-4 | prettier 16x, text rows | prettier ≤ 12x |
| T28-5 | three_way_merge, log_pipeline, knucleotide, hyphen, base64 | typed ≤ untyped on all; −20% on the two long rows |
| T28-6 | collatz 1.40x, mandelbrot 1.36x, all numeric | ≤ 1.15x on the scalar rows |
| T28-8 | splay 15.6x, havlak, cd | splay ≤ 6x |

Together these are the 63-row target of ≤ 3.0x. The residue after them is
what Tune27 §8 named as the language rather than the implementation: GC on
allocation-heavy rows, the side-root prologue on container-holding frames,
and saturating `int` on genuinely unknown ranges.


## 9. Implementation evidence

### 9.1 T28-1 — the const fold never reached a literal initializer (LANDED)

**Two defects, one symptom.** The census said module `let` constants were boxed
slab reads with a tag dispatch and an unbox call at each use. The cause was not
a missing optimization but a fold that could not fire:

1. **`interp_const_init_value` tested the wrong node.** It called
   `ast_unwrap_primary(init)` and then required the result to be a childless
   `AST_NODE_PRIMARY`. `ast_unwrap_primary` loops *while* the node is a PRIMARY,
   so for a bare literal -- which is exactly a childless PRIMARY -- it returns
   **NULL**, and the arm returned false before reaching
   `ast_static_literal_item`. That call was dead code: `let K = 1` folded on no
   tier, ever. Fixed by walking the wrapper chain directly and requiring the
   landing to be that childless PRIMARY (the memory ledger's Tune27 round-7
   lesson: read a literal from the raw operand, never the unwrapped one).
2. **The whole pass declined without a GC heap.** RC7 expects an *evaluation*
   to allocate, so the guard required `context->heap`. The eager pipeline
   (`LAMBDA_TIER=jit`, which every Part-1 benchmark number pins) compiles the
   module before `runner_setup_context()` reaches `heap_init()`, so the pass
   never ran there at all. Split the guard: the binding-copy arm (RC3.3) runs
   with or without a heap because it evaluates nothing, and only the expression
   arm waits for one. A heap-less pass leaves the watermark behind so a later
   full pass can still fold expressions, and a settled node is never
   republished (a second intern would hand one value two pool slots).

**Signed literals.** `let NEG = -1` is a UNARY over a literal, which only the
expression arm could fold. The sign is now applied in the binding-copy arm for
the two kinds whose negation is exact and allocation-free: a packed `int`, and
a `float` whose negation still packs inline (the residue that would need a
boxed double declines). The PRIMARY-chain-plus-optional-sign walk had grown
copies in `build_ast.cpp`, so it was extracted as
`ast_signed_literal_operand()` in `type_build.hpp` and
`ast_constant_integer_value` now calls it (rule 13).

**Measured.** Probe `temp/t28/constprobe2.ls` (`let POS = 3`, `NEG = -1`,
`FLT = 2.5`, `BOOLC = true`, all read in a loop), `main`, JIT-pinned:

| build | insns | tag dispatches |
|---|---:|---:|
| r0 (HEAD) | 229 | 2 |
| r2 (fold reaches literals) | 185 | 1 |
| r3 (+ signed literals) | **174** | **0** |

deltablue2 `c_choose_method`: 3,334 -> 3,225 instructions, 70 -> 64 tag
dispatches, 44 -> 38 unbox calls, and its module-state prologue call is gone.
Outputs identical on `interp` and `jit` for the probes and for deltablue2.

**Scope of the fold (a ratchet caught the first shape).** The first version
folded every immutable binding. `test_mir_ratchet_gtest` failed with cube3d's
`run_cube` **growing** two instructions: `let loop_max: int = 50` is a
function-local constant, already a live register, and folding traded that reuse
for a fresh `mov` immediate at each use. The fold is therefore restricted to
bindings in the script's own module scope, which is the case whose read would
otherwise be a boxed slab load. The test is `entry->scope ==
((AstScript*)tp->ast_root)->global_vars`: scope *kind* is not a usable
discriminator, because a `pn` body's top-level lexicals report the same kind
(both `SCOPE_KIND_MODULE`-only and `+GLOBAL` spellings were measured and
rejected -- the first folded nothing, the second still admitted `loop_max`).

**Gates (all on the final shape).**

| Gate | Result |
|---|---|
| `make test-lambda-baseline` | 5,415 passed / 88 failed |
| those 88 | **all pre-existing**: each script's output is byte-identical on the control binary built from HEAD without this change (`temp/t28/checkfail2.log`, `identical=88 build_diff=0`) |
| MIR emission ratchet | passes (cube3d module and `run_cube` byte-identical) |
| benchmark corpus | 149 scripts identical; 3 differ only in the timings they print themselves (computed results equal); 3 fail identically before and after |
| tiers | probes byte-identical on `interp` and `jit` |

**Timed A/B** (min of 7 interleaved JIT-pinned runs; the host had four unrelated
`test_js_mvp_gtest.exe` processes spinning at 100% CPU, so treat +/-4% as noise
-- cube3d, whose emitted MIR is byte-identical, read 1.038 and calibrates that
floor):

| row | control | T28-1 | ratio |
|---|---:|---:|---:|
| awfy/deltablue2 | 81.5 | 77.2 | 0.947 |
| awfy/richards2 | 759.0 | 738.6 | 0.973 |
| larceny/puzzle2 | 23.7 | 23.5 | 0.993 |
| awfy/queens2 | 0.366 | 0.363 | 0.992 |
| kostya/primes2 | 4.23 | 4.27 | 1.008 |
| jetstream/cube3d2 | 17.9 | 18.6 | 1.038 (identical MIR = noise) |

**Regression pin.** `test/mir/lambda/tune28_module_const_fold.{ls,txt,mir-check}`
requires the folded immediates in `scan` and forbids both the
`lambda_module_state_for_unit` prologue call and the
`lambda_item_to_int_lane_c` unbox on a constant read; its script also carries a
function-local `let` so the module-scope restriction stays visible. Emission
suite 142/142, ratchet 19/19.

**What this did *not* fix, and why it matters for the plan.** Only 6 of
deltablue's 70 dispatches were constants. The other 64 are **field reads** --
`w.cons[cid].kind` returns an Item that is then tag-dispatched into the int
lane. That is N3 (no raw record ABI), not N1, and it confirms the census
reading in §4: the bulk of the widest rows is obligation 1, not constants.

### 9.2 T28-2 — scope correction after reading the planner

Two findings change the track's shape:

- **Raw variants replace the conventional native body.** The planner sets
  `has_native = false` for a binder function and plans its variants instead
  (`transpile-mir.cpp`, the `ft->binder_count` arm). Admitting binder-free
  functions is therefore not additive: it swaps one single-ABI native body for
  up to four keyed bodies, with the code-size consequences that implies. The
  slice worth doing is narrower than §6 states -- admit a binder-free key
  **only when a parameter is shape-bearing** (a named record), which is the
  case the native lane refuses today (`is_native_param_type_id` is scalars
  only).
- **`pn` and `var` parameters are excluded by a ratified ruling.** D8.3.4's
  boxed-only exclusions name closures, methods, variadics, `pn`/`var` params
  and may-await procedures, and the planner enforces them. Every one of the
  twelve widest rows is `pn` with `var` record parameters (deltablue2 49 `pn`
  and 80 `var` positions, havlak2 57/64, splay2 19/30, cube3d2 15/52). So the
  record-ABI slice **cannot reach the pilot rows without lifting that
  exclusion**, which is a ruling for the user, not an implementation choice.

The part of N2/N3 that needs no ruling is separate and stays in scope: a
declared record parameter's descriptor is admitted once by `_b` (D8.3.2), so
the per-access layout-kind and certificate-identity guards on `w.cons[cid]`
are a re-proof the callee already owns. Hoisting that proof to the first use
in the function (invalidated by a write to the field or a call that may retain
the root) is obligation 6 applied to a declared parameter and is the next
concrete slice.
