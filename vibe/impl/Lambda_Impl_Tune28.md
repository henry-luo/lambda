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


### 9.3 D8.3.4v3 — `var` positions admitted to raw variants (LANDED, subset)

The user ruled on 2026-09-16 that `var` parameters and methods leave the
boxed-only exclusion list. Spec revised in place (D8.3.4v3, formal design
8.1.0); deliberation recorded as O11v2 in
`vibe/Lambda_Design_Compiling_Dual_Func.md`.

**What shipped.** `mir_callsite_exact_raw_key` no longer rejects a `var`
position outright. It admits one whose value travels in the *same carrier on
both edges* -- records, maps and arrays, which are boxed Items either way -- so
the CW33 home transport the ordinary argument loop already emits applies
unchanged and the borrow's write-back is literally the `_b` path's
(D8.1.1v10). A `var` position whose contract would take a raw scalar lane is
still refused: that is O11's original hazard, a raw carrier replacing the
caller-owned location, and it has no transport yet.

**Evidence.** `test/lambda/proc/type_binder_var_record_raw.ls` gives
`pn bump(var c: Counter, step: number as T) T` two exact keys. Before the
change the emitter produced no raw body for it; after, `_bump__raw0` and
`_bump__raw1` are emitted and selected. Output is byte-identical on interp,
jit and auto, and under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` on
all three. Emission 142/142, ratchet 19/19, and the 229 binder/`cow_var`/proc
tests pass.

**Two things the ruling does not by itself unlock.**

- **Methods are the larger half.** A method call builds a closure rather than a
  direct edge, so the call-site collector never records it
  (`mir_ident_local_func` resolves an identifier callee; a method callee is a
  member expression). Admitting methods means statically resolving the target
  from the receiver's type and emitting a direct edge -- new work, not a lifted
  check. Recorded as ruled-but-unimplemented.
- **The benchmark rows still need binder-free keys.** Raw variants are planned
  only when `signature->binder_count` is non-zero, and no benchmark uses a
  binder.

### 9.4 Why raw variants are the wrong lever for the widest rows

A raw variant's value is that its key *tells the body what the argument's exact
type is*. deltablue's hot procedures declare their parameters
(`pn c_choose_method(var w: World, cid: int, mark: int)`), so the body already
knows every type statically. The key adds nothing there. That part stands.

**But the follow-on claim in the first revision of this section was wrong, and
the measurement that refutes it is below.** It asserted that the per-access cost
is `emit_array_rep_cert_guard` re-emitted at every access to `w.cons`, and that
hoisting that proof was the next lever. The count it rested on came from
grepping a mask constant out of a MIR dump without checking which emitter
produced it. A census settles it instead (§9.6).

### 9.5 T28-6 — a folded constant is proven in band (LANDED)

`mir_int_lane_interval` knew a *spelled* literal's value but not a *folded*
one, so after T28-1 made `K_EDIT` an immediate, `k == K_EDIT` still paid the
two-instruction int53 band test on both operands. The oracle now reads the
settled const-pool word through one shared accessor, `mir_const_folded_item`,
which `mir_emit_const_folded_value` was rewritten to use as well so the two
cannot disagree about a node's folded value (rule 13).

Measured against a control built from the same HEAD (`temp/t28/lambda-t28-v3.exe`):

| | control | T28-6 |
|---|---:|---:|
| `c_choose_method` instructions | 3340 | 3334 |
| band-test pairs in it | 16 | 14 |
| deltablue2 module instructions | 35,546 | 35,342 |

deltablue2 output identical.

**Corpus and timing.** 152 benchmark scripts byte-identical against the
same-HEAD control; the 3 that differ are the `ls_micro` rows that print their
own elapsed times, and their computed results are equal.

Per-row timing is **not resolvable on this host** and the static counts are the
honest signal. deltablue2 read 1.078 at 7 runs and 1.043 at 11 runs with the
control first, then **1.018 the other way** with the order reversed -- i.e. each
binary looks slower when it runs second, so the difference is ordering drift,
not the change. Two unrelated `test_js_mvp_gtest.exe` processes were still
spinning at 100% CPU throughout. T28-6 strictly removes instructions (6 in
`c_choose_method`, 204 module-wide, two band-test pairs) and cannot cost time;
re-measure the row on a quiet host before recording any figure for it.

## 10. State at the end of this round

| Item | State |
|---|---|
| T28-1 module-constant fold | landed, committed, pinned by `tune28_module_const_fold` |
| T28-6 folded constants proven in band | landed, working tree |
| D8.3.4v3 `var` positions in raw variants | landed (same-carrier subset), working tree, fixture `type_binder_var_record_raw` |
| D8.3.4v3 methods | **ruled, not implemented** -- needs static receiver resolution and a direct edge, because a method call builds a closure today |
| binder-free raw keys | not started; permitted by D8.3.1v2, but raw variants replace the conventional native body, so code size needs care |
| certificate-proof hoist (obligation 6) | built, never fired, **reverted** (§9.6) |
| null-sentinel receiver-arm elision | landed (§9.8): deltablue ccm 3334→3254, module −685 insns |
| T28-3 `cow_prepare_write` fast path | **skipped**: 1.1% of richards (§9.9) |
| T28-4 binder-walk short-circuit + key-load inline | landed (§9.10): prettier 0.896 |
| T28-5 memchr `split` + string admission fast path | landed (§9.11): three_way_merge ~0.78, log_pipeline ~0.76–0.89 |
| T28-7 emission diet | **landed** (§9.17): `run_cube` 17,329 -> 9,655 (target met; cube3d2 wall -46%); `c_choose_method` 2,739 -> 1,997 (target 1,200 not met: per-access record/array layout proofs, N2/N3) |
| T28-8 / CW36 store-back | **landed** (§9.16) after a revert (§9.14); splay2 map copies 826k→683k, residue is rotation captures of the caller's `var` root (needs a call-site move convention) |
| D4.4.6 write oracle | conformance fix (§9.16): declarations/returns/blocks now seen; pure builtins and known plain `pn` params no longer writers; decisions run at script finalize |
| CW24v3 / D4.4.6 place-copy static mark rule | landed (§9.15), working tree, fixture `cow_place_copy_place_written`; closes the S9.1.2 alias leak present since ≤Result42 |
| open question | why typed code emits ~33M runtime `string` checks |

Gates at the end of the round (v20): `test_lambda_gtest` 923/923, MIR emission
142/142, ratchet 19/19, benchmark corpus identical to the previous build except
four self-timing `ls_micro` rows, forced-GC stress green on the new fixtures
across all three tiers.


### 9.6 Certificate-proof hoist: implemented, measured, REVERTED

**What was built.** A reuse cache inside `emit_array_rep_cert_guard`, so all
eleven call sites would share one invalidation argument. Two facts had to hold
for reuse: the earlier proof must still dominate, answered by a barrier counter
added to the shared emitter that ticks on a call (which may replace the
container) and on a label (a join a jump can enter); and the register must still
hold the proven pointer, enforced by retiring any cached entry whose register an
instruction redefines.

**It fired zero times, and the reason is structural.** A guarded access does not
prove and continue: it proves, zeroes a flag on the miss arm, and joins. So each
access emits its own labels and advances the barrier, and the next access always
sees a stale generation. More importantly the proof genuinely does **not** hold
after that join -- control reaches it from the miss arm too -- so the proof is
carried in the flag, not by dominance. A correct version would therefore be CSE
on the guard *flag* (copy the previous flag), never elision of the guard, since
eliding would assert the proof on the miss path.

**The census says do not build that either.** Counters on the guard emitter,
reported after lowering (the first attempt reported from the const-fold pass,
which runs *before* lowering, and printed zeros):

| Row | guard sites | repeat same (func, reg, contract) |
|---|---:|---:|
| awfy/deltablue2 | 197 | 3 |
| awfy/havlak2 | 0 | 0 |
| jetstream/cube3d2 | 0 | 0 |
| jetstream/splay2 | 0 | 0 |
| jetstream/hashmap2 | 10 | 0 |
| beng/nbody2 | 28 | 22 |

Three of the four widest rows emit **no** array certificate guard at all, so it
is not their cost. deltablue emits 197 with an upper bound of 3 reuses, about
1.5%. Only nbody2 shows real repetition, and it is already near C2MIR parity.

**Disposition.** The cache and the emitter's barrier counter are reverted:
inert code around a correctness-critical guard is a liability, and the premise
that motivated it is refuted. What survives is the census method and the ruling
that a flag CSE, not a guard elision, is the only sound shape if this is ever
revisited.

**What this leaves as the open question for the widest rows.** The verified
per-function counts for `c_choose_method` are the ones this plan should reason
from, not the grep that produced §9.4's first version: 64 tag dispatches, 38
unbox calls, 57 lane re-boxings, 16 `item_at` miss arms, 123 null-sentinel
materializations, 12 `lambda_map_path_set_checked_fixed` calls and 14 int53 band
pairs, in 3,334 instructions. Attributing *those* to specific emitters, one at a
time and with a census rather than a grep, is the next step.


### 9.7 Null-sentinel simplification: drop the receiver arm on a proven binding (LANDED)

**What the sentinels were.** The largest single count in `c_choose_method` was
123 materializations of `ItemNull`. Reading one field of a record emits two
null-absorption arms: one for a null *receiver* (`null.f` is `null`) and one for
an empty packed *slot*. Each ends in materializing the null value, which is why
the count ran to roughly twice the 113 pointer-mask strips in the same body.
`skip_null_guard` controls the receiver arm only; the empty-slot arm is separate
and is untouched here.

**Why the receiver arm was always emitted.** It was hardcoded
(`bool skip_null_guard = false; // typed variables can still hold null`) even
though the receiver's static type was a declared, non-optional record that the
call boundary had already admitted. Tune27's M7 recorded this residue and did
not close it.

**The proof, and its deliberate limit.** `mir_receiver_binding_non_null` admits
only a **named binding** whose declared contract is a non-optional record: its
declaration or parameter boundary admitted the value and a rebind must re-admit
it (D3.2.4v3, S9.1.2), so null was never admissible. It refuses a binding whose
contract was widened by reassignment, and a declared type that merely *contains*
a record (an optional, a union) rather than being one.

It is **not** extended to an arbitrary expression of non-optional record type,
and that restraint is the safety argument: a container field whose packed slot
is empty is reconstructed as `ItemNull` by `emit_mir_direct_field_read` itself,
so `a.b.c` can meet a null `a.b` whose static type says otherwise. That is the
case the old hardcode was defending and it stays guarded.

**Measured** (control `temp/t28/lambda-t28-v6.exe`, same HEAD):

| | control | after |
|---|---:|---:|
| `c_choose_method` instructions | 3,334 | 3,254 |
| `ItemNull` materializations in it | 123 | 107 |
| deltablue2 module instructions | 35,342 | 34,657 |

Each removed arm is a branch, a sentinel materialization, a jump and two
labels -- and the labels were splitting otherwise straight-line code, which is
why the saving (80 instructions in the body, 685 module-wide) exceeds the 16
sentinels the count alone suggests. deltablue2 output identical.

**Gates.** `null_safe_member`, `agg_null_absence`, `oob_read_null`,
`cow_var_nullable_record`, `cow_var_nullable_record_typed_handle`,
`guarded_store_null_slot_retag` and `type_binder_var_record_raw` byte-identical
on interp/jit/auto; three of them re-run under `LAMBDA_GC_FORCE_EVERY=1
LAMBDA_GC_POISON_FREED=1` on all three tiers. A probe
(`temp/t28/nullprobe.ls`) covering the two excluded shapes -- a chained read
through a declared non-optional field, and a genuinely nullable receiver behind
a test -- matches the control exactly. MIR emission 142/142, ratchet 19/19.

Kept on the user's instruction that a clear simplification is worth landing on
its own terms: it removes work the compiler can prove is unnecessary, and the
emission reduction matters independently because MIR's generator is super-linear
in function size (M6).


### 9.8 T28-4 — the binder-use walk ran on every admission (LANDED)

**The plan's design was sized first and set aside.** T28-4 proposed inlining
the union memo hit at each admission site. On the current release,
`print_node` has 33 admission sites; inlining a six-arm descriptor chain at each
would grow a 4,528-instruction function by roughly 15%, against M6. It stays
unbuilt pending the result below.

**What the profile found instead.** `runtime_type_admit_value_env` opens every
admission by calling `runtime_contract_uses_binder(expected)`, which recurses
into every map field and every array element type, bounded only at depth 64.
prettier's `Doc` is a recursive six-arm union, so the walk re-traversed the
whole contract on each of 6.6M admissions -- to answer `false`, because the
program declares no binders. It arrived with the type-binder work and charged
every program for a feature most never use.

**The fix.** Every `TypeBinder` and `TypeBoundRef` is created through one
allocator, `alloc_type_kind`, which now sets a process-wide monotonic flag the
first time it creates either. `runtime_contract_uses_binder` returns `false`
immediately while the flag is unset: nothing can reach a binder that was never
allocated. The flag uses `lib/atomic.h` (rule 3), and its only transition is
0 -> 1, so a racing reader can see an older `false` only for a node not yet
reachable from any published contract.

Soundness was checked before relying on it: both binder creation sites
(`build_ast.cpp`) and the only bound-reference site (`parse_type_pattern.cpp`)
go through `alloc_type_kind`; nothing assigns `TYPE_KIND_BINDER` or
`TYPE_KIND_BOUND_REF` directly; nothing clones types.

**Measured.** A/B against `temp/t28/lambda-t28-v7.exe`, same HEAD, min of 5
interleaved JIT-pinned runs:

| Row | control | after | ratio |
|---|---:|---:|---:|
| text/prettier_ast2 | 1,271 | 1,139 | **0.896** |
| text/log_pipeline2 | 10,374 | 9,749 | **0.940** |
| text/three_way_merge2 | 5,537 | 5,598 | 1.011 |
| jetstream/splay2 | 463.0 | 455.2 | 0.983 |
| awfy/richards2 | 600.3 | 616.8 | 1.028 |

prettier's 10.4% clears the ~5% noise floor established earlier on this host;
the other three are within it. On prettier, `lambda_type_accepts_null` dropped
out of the top of the profile, and top-of-stack samples fell from 763 to 542
over the same run. Outputs identical on all five rows.

**Binder programs still take the binder path.** Every `type_binder*` fixture
(`type_binder`, `type_binder_leading`, `type_binder_raw_variants`,
`proc/type_binder_proc_raw`, `proc/type_binder_var_record_raw`) is
byte-identical on interp/jit/auto -- the flag is set whenever a binder exists.

**What remains of the admission cluster.** After the fix prettier still spends
about 7% in `lambda_type_check_env` and `lambda_numeric_boundary_admit`. The
larger cost is now plainly the member-read cluster at about 21%:
`map_shape_field_to_item` 5.0%, `fn_member_by_id` 4.4%,
`lambda_module_name_id_at` 4.2%, `lambda_module_state_for_unit` 4.2%,
`map_field_to_item` 3.0%. Two of those five resolve a property-key id through
module state on every member read -- the N4 finding in §4 -- and look like the
next contained target.


### 9.9 T28-4 follow-on — inline the module property-key load (LANDED)

**A correction first.** §4's N4 described `lambda_module_name_id_at` as a
per-*member-read* cost. It is not: `emit_module_property_key_load` already
hoists each key into the function-entry chain and caches it per (key, function),
and `emit_module_state` does the same for the state pointer. Both run once per
*invocation*. The cost on prettier comes from `print_node` being recursive: each
call resolves all 13 of its keys up front, whether or not that call reads them.

**What the call did.** Two loads and two defensive checks:
`state->property_keys[index]`, returning `NAME_ID_NONE` when the table is
missing or the index is out of range.

**The change.** For the module's own code the call becomes two plain loads,
inserted at the same prologue cursor. That is sound because the key table is
linked from exactly this transpiler's key list
(`lambda_module_state_link_property_keys_for_state`), and the link fails loudly
when the sealed count changes -- so the index is in range and the table is
non-null by construction. A `static_assert` ties the emitted 32-bit read to
`sizeof(NameId)`.

**Where it stops.** Satellites keep the checked call. Their keys are appended by
`lambda_module_state_append_property_keys`, which **reallocates** the table, and
their index is offset by a base that the append establishes. For the same
reason the inlined path reads the table pointer fresh, back-to-back with its
use, rather than caching the pointer. `lambda_module_state_for_unit` (the other
4% on prettier) is left alone: inlining it needs a runtime branch for the
logical-unit flag, and the entry chain is built by inserting after a cursor
while `em_emit_label` appends at the end of the function, so a label there
would land in the wrong place.

**Measured** against `temp/t28/lambda-t28-v8.exe`, min of 5:

| Row | ratio |
|---|---:|
| text/prettier_ast2 | 0.975 |
| text/log_pipeline2 | 0.995 |
| awfy/deltablue2 | 0.994 |
| awfy/richards2 | 0.974 |

No row clears the noise floor alone, and none regresses. `print_node` loses all
13 `lambda_module_name_id_at` calls; its instruction count *rises* by 13 (each
call became two loads) and the module by 49. That is a cheaper function by work
done even though it is larger by count -- a call carries argument setup, the
call and the return.


### 9.10 T28-5 — `split` scanned with a library call per byte (LANDED)

**Attribution first, by call graph.** On three_way_merge2 `_platform_memcmp`
plus its `DYLD-STUB$$memcmp` stub is 28.3% of top-of-stack samples. Walking each
`memcmp` leaf up to its nearest named caller attributes **1,269 samples to
`fn_split` and 88 to `fn_str_eq_ptr`** -- about 93% of the `memcmp` time is
`split`, and `fn_split` itself is another 7.5%. The plan's part (a), inline
string equality, is therefore not where these rows spend their time.

**The cause.** Three loops scanned for a literal separator by calling
`memcmp` at *every* byte position -- `split_literal_match_count` (the pass that
sizes the result array), the `fn_split` loop, and the `fn_split3`
keep-delimiter loop. Splitting on one character paid a library call per source
byte, twice.

**The change.** One kernel, `split_literal_find`, returns the leftmost match at
or after a position: `memchr` jumps to the next occurrence of the separator's
first byte (libc vectorizes it), and `memcmp` runs only over the remaining bytes
at a candidate; a one-byte separator needs no compare. All three loops use it
(rule 13). Matching stays leftmost-first and non-overlapping, and both split
loops still re-read the string's bytes after every allocating round -- slicing
and pushing may relocate them -- so the scan is kept in indices.

**Correctness.** `temp/t28/splitprobe.ls` covers leading, doubled and trailing
separators; overlapping repeats (`split("aaaa","aa")`, `split("aaa","aa")`); a
separator longer than every candidate and longer than the subject; a
first-byte near miss (`"aXbXXc"` on `"XX"`); multi-byte UTF-8 in the subject and
in the separator; the keep-delimiter form; and the empty subject. Output is
byte-identical to the control on interp, jit and auto. Outputs are identical on
three_way_merge2, log_pipeline2, knucleotide2, hyphen2, base642 and
text_search2.

**Permanent regression pin (2026-09-19).** The temporary probe is now the
checked-in `test/lambda/proc/tune28_split_literal_kernel.ls` fixture and
`LambdaOptStrings.LiteralSplitKernelAvoidsBytewiseComparisons`. On the JIT
profile it asserts S17.1.1 output for a multi-byte near miss, the
keep-delimiter form, and a one-byte delimiter. The test also checks the
runtime kernel's structure: `memchr` finds the next first-byte hit, a
one-byte delimiter returns without `memcmp`, and a multi-byte delimiter
compares only the remaining candidate suffix. The semantic fixture ensures the
checked shape preserves S17.1.1; this direct source check is necessary because
`split` is a runtime builtin rather than a MIR lowering. It adds no work to
the release path.

**Measured** against `temp/t28/lambda-t28-v9.exe`, same HEAD, min of 3
interleaved runs, taken twice (the runs partly overlapped each other, so each
is read only for direction and the pair for agreement):

| Row | run 1 | run 2 |
|---|---:|---:|
| text/three_way_merge2 | **0.812** | **0.796** |
| text/log_pipeline2 | **0.775** | **0.749** |
| beng/knucleotide2 | 1.088 | 0.926 |
| text/hyphen2 | 0.967 | 0.983 |
| kostya/base642 | 0.973 | 0.943 |

The two long rows gain roughly 20% and 25%, far above the noise floor and in
agreement across both runs. knucleotide calls `split` once, to break its input
into lines, and its two readings point in opposite directions -- the signature
of noise on a 7 ms row, not of the change. hyphen and base64 are not
`split`-bound, which matches the attribution.

**Deferred, with reasons.** Part (a), inline `==`, measured at 88 samples here
and is not the lever. Part (c), borrowed substring views, changes string
representation and needs its own round.

**Follow-up (2026-09-24): `replace()` and `find()` join the kernel.** Their literal paths still ran the pre-T28-5 loop — a count pass, then a copy (or collect) pass, each calling `memcmp` at every byte position. revcomp's complement is a chain of 18 one-character `replace()` calls, and that chain was 0.79 of revcomp2's 0.99 ms; regexredux's IUPAC expansion is 11 more. The kernel is now `literal_find`: it takes `ignore_case` (ASCII folding probes every position, since `memchr` cannot fold case) and serves `split`, `replace` and `find` (rule 13). `count_literal_matches` absorbs `split_literal_match_count` and counts a one-byte needle with the SWAR `str_count_byte`, because a dense needle ("A" in DNA) would otherwise pay a `memchr` call per match. A one-byte-for-one-byte `replace()` of every match skips the search altogether: `translate_byte` is a branch-free select that clang vectorizes (NEON `cmeq.16b`/`bit.16b`, 64 bytes per iteration in the release binary).

**Correctness.** A 4,000-case differential fuzz of `replace`/`find`/`split` — a dense six-letter alphabet that includes a two-byte UTF-8 letter, one- to three-letter needles, every option kind, and the keep-delimiter split — is byte-identical to the control. `test/lambda/find_replace_options.ls` gains overlapping, near-miss, translate, symbol, window and UTF-8 cases. The regression pin now names `literal_find` and also asserts that `fn_replace_impl` and `fn_find_impl` call it.

**Measured** against the same HEAD built without the change: release, `LAMBDA_TIER=jit`, median of 7 runs (5 for the last two rows):

| Row | before (ms) | after (ms) | speedup |
|---|---:|---:|---:|
| beng/revcomp2 | 1.284 | 0.201 | 6.39x |
| beng/revcomp | 1.263 | 0.305 | 4.14x |
| beng/regexredux2 | 1.181 | 0.607 | 1.95x |
| beng/regexredux | 1.435 | 0.664 | 2.16x |
| text/three_way_merge2 | 1976 | 1846 | 1.07x |
| text/prettier_ast2 | 518.7 | 493.0 | 1.05x |

knucleotide2 and log_pipeline2 read 0.98x on interleaved runs with overlapping ranges; log_pipeline2 calls none of the three builtins, so that is noise. What remains of regexredux is mostly its nine case-insensitive pattern `find()`s, which run in RE2.


### 9.11 T28-4 follow-on — inlining the module-state lookup: built, measured, REVERTED

**What was built.** In the non-satellite prologue, a fast path for
`lambda_module_state_for_unit`: test the logical-unit flag, bounds-check the id
against `EvalContext::module_state_capacity`, then load
`module_states[unit_id]` -- the same dense load the satellite branch already
inlines -- with the call kept on a slow arm. The earlier obstacle, that a branch
at function entry needs a label while `emit_label` appends at the end of the
function, was solved by inserting the labels through
`emit_module_state_load_insn`, which uses `MIR_insert_insn_after`; MIR labels
are instructions. The emitted entry sequence was exactly as designed, with the
join placed before the key loads so the state is defined on both paths.

**It never fired.** After the change `lambda_module_state_for_unit` was still
5.1% of prettier's samples. `script_module_layout_id` (`transpiler.hpp`) marks a
module's layout id **logical** whenever the script is in the process cache --
and every script run through `lambda.exe run` is admitted to that cache. So the
flag test sent every call down the slow arm. Timing agreed: prettier 1.022 with
the control first and 0.990 reversed, richards 1.023 and 1.001, havlak 0.955 and
0.996 -- directions disagree, so the effect is zero.

**Disposition.** Reverted. It added a branch and a join to every function's
entry for no benefit, and unlike the null-sentinel change that is not a
simplification. The rebuild is byte-identical to the pre-change emission on
prettier. The key-load inlining (§9.9) is unaffected and stays: the key table is
indexed the same way whether the unit id is logical or physical, and
`lambda_module_name_id_at` is absent from the post-change profile.

**What would actually help.** The cost on cached scripts is the logical-to-dense
map lookup inside the call. Making it cheap needs a per-context resolution
cache or resolving the dense id once per invocation of the *entry* function and
passing it down -- a new mechanism, not an inline. Recorded, not started.

**Method note.** Check that a fast path is *taken* before timing it: a profile
that still shows the call it was meant to remove is the cheapest possible test.


### 9.12 T28-5 follow-on — a string-lane proof for `string[]`: built, measured, REVERTED

**What the post-`split` profile showed.** With `split` fixed, three_way_merge2
and log_pipeline2 flatten out. `memchr` (5.8% / 3.4%) is the new kernel doing
its work. The shared residue is `lambda_type_matches` (7.3% / 5.4%) plus
`lambda_array_contract_info` (3.0% / 3.1%), and allocation/GC at ~15% on both.

**What was built.** `runtime_array_admit_primitive_contract` already treats an
`ArrayNum` lane as an exact decoder and installs a certificate. A sibling proof
extended that to a boxed `Array` whose native lane is the plain non-nullable
`string` pointer lane, restricted to a bare `string` leaf (a refined leaf has
the same lane and a predicate the lane cannot prove). Soundness was checked
first: `lambda_pointer_lane_accepts_item` admits exactly `LMD_TYPE_STRING`
(no symbols), a non-nullable lane rejects null, a rejected `array_push` widens
the lane away, and a rejected `array_set` is dropped. The private
`array_representation_matches_cert` was promoted rather than copied (rule 13).
Nine one-case probes (split results, `""` elements, refined accept/reject,
widened, mixed, symbol, `a[i] = ...`, empty arrays) were byte-identical to the
control on interp/jit/auto, including the rejection messages.

**It fired on neither target, for two different reasons** -- confirmed by
profile before timing:

- **three_way_merge2.** The hot call is
  `fn word_at(words: string[] as W, index: int)`, three times per inner-loop
  step on `split` results. That contract carries a **type binder**, and every
  array fast path in `runtime_type_admit_array_env` is gated on
  `!binder_dependent` -- by design, because the element walk is what computes
  `W`'s binding (TG18, D3.3.3v3). The row was rewritten to use binders in
  `c912d2d6e` / `19757f474`. So the walk here is the price of the binder, not a
  missing proof. (The T28-4 binder-walk short-circuit correctly does not apply:
  this program has binders.)
- **log_pipeline2.** No binders, four `string[]` uses, and still no change:
  `lambda_type_matches` there is not coming from `string[]` array admission at
  all. `runtime_validate_value_against_type` is 0% on both rows, so the volume
  is cheap scalar checks from some other caller, not the validator.

**Disposition.** Reverted: sound but inert on the measured rows, and it adds
code rather than removing it. The rebuild's emission is byte-identical to the
pre-change build on three_way_merge2.

**Two real leads for a later round.**

1. **Binder contracts force an element walk on every admission.** For
   `string[] as W` the walk only establishes that `W` is `string[]`, which a
   string-lane array already proves. Letting the binder oracle read the lane is
   the fix -- but it changes freshly landed binder semantics (TG18), so it is a
   design question, not a local optimization.
2. **log_pipeline's scalar `lambda_type_matches` volume** needs attribution to
   its real caller before anything is changed; the stripped binary cannot name
   it, so it needs an instrumented build.


### 9.13 The binder "walk" that was not there, and the real text-row cost (LANDED)

**Correction to §9.12.** §9.12 said three_way_merge2's cost was a binder
contract forcing an element walk, and the follow-up request was to bind a
binder from an array certificate. Both halves turned out wrong:

- **Certificate-based binding already exists.** `binder_narrowest_type` returns
  `value.array->rep_cert->array_contract` whenever the array carries a
  certificate and never walks -- exactly what TG18 permits ("a typed carrier
  whose leaf lane is already proven binds that lane without walking elements").
  `runtime_type_admit_binder` admits against the binder's *bound* in a fresh
  call, where the bound `string[]` has no binder, so the array fast paths are
  not suppressed there either.
- **There was no hot array walk.** Temporary per-call-site counters on every
  `lambda_type_matches` caller showed the array rebuild path's final check ran
  **3 times** in the whole three_way_merge run: the arrays are converted and
  certified once. (Named attribution from the binary was not possible: the
  release strips locals with `strip -x`, and an unstripped relink still has no
  symbol for LTO-internalised statics, so counters were the reliable tool.)

**What the counters did show.** One site -- the general fallback at the bottom
of `runtime_type_admit_value_env`, `if (!binder_dependent &&
lambda_type_matches(value, expected))` -- accounted for 32,936,304 calls on
three_way_merge2 and 33,264,000 on log_pipeline2. A histogram by contract kind
put **100% of them against a scalar `string` contract**. Each was a string
value crossing a `string` boundary and descending the whole ladder (binder
dispatch, unwrap, binder-use walk, `any`, union memo, array certificate, map
relation) to a tag compare in `lambda_type_matches`' final switch.

**The fix.** An early return beside the existing T27-4 `any` fast path: when
the unwrapped contract *is* `&TYPE_STRING` and the value's tag is
`LMD_TYPE_STRING`, admit it unchanged. That is exactly equivalent: the global
singleton cannot carry a literal, pattern or `that` refinement; no probe between
the entry and the fallback converts a string; and the fallback's success branch
returns the value as is. Identity with the singleton is required, so literal
unions, patterns, refined, nullable and union string contracts all keep the
normal route.

**Taken, then timed.** `lambda_type_matches` fell from 5.2% of three_way_merge's
samples to 0.0%. Ten one-case probes -- plain accept; `int`, `symbol` and `null`
rejects; `string?`; a literal union accept and reject; a `that`-refined string;
`string | int`; `string[]` -- are byte-identical to the control on interp, jit
and auto, rejection messages included. Outputs identical on seven text rows.

| Row | control first | reversed |
|---|---:|---:|
| text/three_way_merge2 | **0.784** | **0.772** |
| text/log_pipeline2 | **0.887** | **0.892** |
| text/prettier_ast2 | 0.976 | -- |

(Reversed readings are old/new, shown here inverted.) Both targets clear the
noise floor in both directions.

**Why this matters beyond the two rows.** The earlier admission work in this
round (T28-4) removed a recursive walk from the *top* of the same ladder; this
removes the ladder itself for the most common scalar contract. The broader
question it raises -- why typed code emits tens of millions of runtime `string`
checks at all, rather than proving them statically -- is an emitter question for
a later round; the runtime fast path is the cheap, sound half.


### 9.14 T28-8 (CW36) — built, caught by its own probes, REVERTED; a pre-existing value-semantics bug

**Re-measured target.** splay2 still performs 825,641 map copies (53.7 MB copied)
per run, so the D4.4.4v3 target stood.

**What was built.** A path-aware walk (`rmw_branch_local`, `build_ast.cpp`)
engaged only when the existing rule finds no top-level store-back. Per path it
tracked the handle as CLEAN, DIRTY or STORED: the root may be read while CLEAN
or STORED, never observed while DIRTY, and mutated only once STORED; a DIRTY
path may not leave the region; a store-back is FINAL (skips capture) only when
nothing later on that path writes through the handle, otherwise it keeps the
ordinary capture, whose share-mark makes later writes copy. It was restricted
to member paths and refused loops, `match`, handlers, `raise`, `^`, `start` and
nested functions. Both tiers already lower a borrowed handle's non-release
store-back as an ordinary capture (`interp.cpp`, `transpile-mir.cpp`), so only
the analysis was new.

**Six value-semantics probes caught two violations** (`temp/t28/cw36/`), on
interp, jit and auto, and again under forced GC:

- **b5, read between write and store-back** printed `55` where the control
  prints `25`. That one is the new walk's own bug: it detected writes with
  `rmw_move_handle_cb`, which does not report an assignment *through* the handle
  -- `rmw_moves_out` checks that case explicitly before using the same scan.
- **b4, an alias of the place taken before the bind** printed `777` where the
  control prints `377`. That one is **not** the new walk's fault.

**The pre-existing bug.** A straight-line version of b4, which the existing
D4.4.4v2 rule accepts, prints `777` on the control too -- and so does a program
with no handle and no borrow at all:

```lambda
pn f(var r: N) {
    let old = r.kid
    r.kid.n = 7
    print(old.n)     // prints 7; S9.1.2 requires 3
}
```

Reproductions are kept in `temp/t28/value_semantics_bug/`. Every cell below
should read `3`; the interpreter tier is shown, and the JIT agrees:

| binary | `var` param root | local typed root | local untyped root | `var old = r.kid` |
|---|---|---|---|---|
| Tune28 HEAD | 7 | 7 | 7 | 7 |
| Result45 | 7 | 7 | 7 | 7 |
| Result44 | 7 | 7 | 7 | 7 |
| Result43 | 7 | 7 | 7 | 7 |
| Result42 | 7 | 7 | 7 | 7 |

It violates S9.1.2 ("binding, assignment and construction copy, observably ...
sharing must be unobservable"), on both tiers, in every archived release back to
Result42, and for `let` and `var` place copies alike.

**Likely mechanism.** `NameEntry::place_copy_mutated` gates the bind-time
share-mark: "an unmutated place copy stays a borrow -- read-and-return helpers
(rbt_get) must not share-mark the stored value they hand out." That rule asks
only whether the *copy* is mutated. It does not ask whether the *place* is
written while the copy is alive, so a later write through the root lands in
place and shows through the copy. c2 (a place copy that *is* mutated) behaves
correctly (`373`), which fits: the mutated copy is share-marked.

**Why T28-8 is blocked on it.** The D4.4.4 borrow -- v2 and the CW36 extension
alike -- relies on "the leaf's own share bit still decides whether the handle's
writes detach". This rule leaves that bit unset for exactly the aliases it
should protect, so no borrow analysis can be sound on top of it. The revert
restores emission byte-identical to the pre-change build on splay2, and every
probe back to control behaviour.

**Disposition.** A fix belongs to the place-copy rule itself and is a
core-semantics decision with a performance side (it is what kept `rbt_get`-style
reads free), so it is raised with the user rather than changed here. T28-8
resumes after that fix, with the b5 write-detection bug corrected.


### 9.15 CW24v3 / D4.4.6 — the place-copy bind marks on "place written while live" (LANDED)

**Ruling (user, 2026-09-16).** A place copy is share-marked at its bind iff the
place may be written while the copy is alive, decided statically per binding;
the copy's own mutation still marks. Recorded as D4.4.6 (formal design 8.2.0)
and CW24v3 in `vibe/Lambda_Design_Nested_Mutation.md` §4.3.

**The static fact.** `NameEntry::place_copy_place_written`, decided at
FUNCTION_END by `lambda_ast_note_place_copy_place_writes` for `fn` and `pn`:
walk each statement list (descending `if` arms, blocks, content/seq/list
carriers and loop bodies); for each place-copy declarator, the live range is
the statements from the bind to the last top-level statement of that list
naming the copy (a nested use pulls in its whole enclosing statement). The fact
is set when `ast_body_may_write_entry` -- the CW30 loop gate, which already
counts unknown callees and `var` passes as writes -- reports the root may be
written in that range, or when the copy escapes. Place copies under constructs
the list walk does not descend (`for` bodies, `match` arms) fall back to the
whole body, conservatively. Both bind gates (T0 `interp_bind_declared_value`,
MIR `transpile_let_stam`) now mark on `mutated || place_written`.

**Three things the first version got wrong, each caught by the probes.**

1. **`let` copies were never place copies.** `lambda_ast_mark_place_copy` ran
   only in the `var` reduction, so `let old = r.kid` had no fact to decide.
   The `let` reduction now marks it too; the CW24 mutation diagnostic cannot
   fire on a `let` because the write is rejected as immutable first.
2. **The escape test was the CW29 root-matching scan**, which counts
   `print(old.n)` as passing `old` and returned `escaped=1` for every probe --
   including the read-and-return helper this rule exists to keep free. The
   retention walker now takes a `bare_only` predicate (rule 13: one walker, two
   match rules): a copy escapes only when the bare name is returned, stored,
   rebound, passed or captured.
3. **The JIT chose the raw store at compile time.** With the runtime child
   marked, the interpreter's path setter copied correctly, but MIR Direct picks
   a store form from the root's compile-time facts and an untyped
   `r.kid.n = 7` still took the `fn_map_set` arm, which never reads the child's
   share bit (it is a raw setter shared with LambdaJS, CW21). The bind now
   records `cow_children_may_be_shared` on the root -- the fact a detach already
   records -- so nested stores take the rebuilding path helper.

**Probes** (`temp/t28/cw24v3/`, expectations derived by hand from S9.1.2,
twelve cases): direct root write, typed/untyped local roots, `var` alias, an
alias beside an RMW borrow, `var`-pass to a mutating callee, a copy declared
inside an `if`, a root written in a loop, returning the node itself, and three
shapes that must stay free (read-and-return of a field, no write, write after
the copy's last use). Control (v17) was wrong on eight of twelve; v19 matches
all twelve on jit, interp and auto, and under forced GC. `COW_EXEC_PROFILE`
map share-marks on the three free shapes: 0 before, 0 after.

**Fixture.** `test/lambda/proc/cow_place_copy_place_written.ls` (golden
re-derived segment by segment), identical on all three tiers.

**A fourth trap, found by the copy census, not the probes.** With the
`bare_only` escape in place, cd2's array copies rose 239,862 -> 276,754. A
hypothesis probe (`e2_len_escape.ls`: `let keys = t.keys; return len(keys)`
followed by a push) reproduced it exactly -- 0 -> 1,000 copies -- because a
bare copy handed to `len` counted as an escape. A system function that is not
a procedure and returns a scalar cannot retain an argument; both facts are on
the registry row (`SysFuncInfo::is_proc`, `return_type`), so the bare-name scan
now exempts such callees. cd2 is back to byte-identical with the control, and
the probe to 0 copies.

**Cost census, control vs final (`COW_EXEC_PROFILE`, JIT).** richards2,
deltablue2, havlak2, hashmap2 and cd2: identical marks, unique-mutation checks
and copies. splay2: copies 825,641 -> 825,642 (+1), share-marks +1, but
unique-mutation checks 394,822 -> 613,307 (+218k). The emission diff names it:
`splay_node` gains 4 `cow_bind_var` (the `var branch = left.left` / `left.right`
copies, whose root `left` is written while they are live -- a required mark)
and 4 `cow_prepare_write` (the JIT now records `cow_children_may_be_shared` on
the root at such a bind, so its nested stores take the prepared path instead of
the raw setter); `rotate_left`/`rotate_right`/`splay_remove` gain one to two
`cow_bind_var` each, for copies that are stored into a container (an escape)
and then overwritten, hence no extra copy. Module +42 instructions. Whether the
218k unique checks cost measurable time is the timing question below.

**Final verification (v20 = all four fixes).** Gates: emission 142/142,
ratchet 19/19, runtime 923/923. Corpus identity v17 vs v20: 151 same, 4 diff,
all four `ls_micro` rows that print their own millisecond figures (counts
identical). Timing, min-of-5 interleaved, both orders (loaded machine):

| row | v17→v20 (v17 first) | v20→v17 (v20 first) |
|---|---|---|
| splay2 | 1.018 | 1.002 |
| cd2 | 1.012 | 1.070 |
| havlak2 | 1.088 | 0.950 |
| richards2 | 1.033 | 0.969 |
| deltablue2 | 0.977 | 1.009 |

Every row changes sign between orders, i.e. noise. splay2 alone, min-of-9,
both orders: 0.986 and 1.009 (v20 ≤1% faster either way). The +218k unique
checks are free at this scale; the +1 copy is the required mark. CW24v3 is
correctness-neutral on the corpus and performance-neutral on the five COW-heavy
rows, closing the S9.1.2 alias leak.

**Unblocked.** T28-8 (CW36) can resume on a sound share bit, with its own
write-detection defect (§9.14, b5) corrected first.


### 9.16 T28-8 (CW36) — branch store-backs LANDED, with three prerequisite fixes

**The walk.** Where the straight-line CW34/CW35 rules decline, `rmw_branch_local`
(`build_ast.cpp`) walks the handle's statement list path by path with two
facts, LIVE (the handle may still hold the unmarked leaf) and DIRTY (it may
have written that leaf without storing it back). The root may be observed only
when the path is not DIRTY and, if LIVE, no write through the handle can
follow. A return, or the end of the bind's list, refuses on DIRTY. A store-back
clears both; it is final (`cow_borrow_release`, capture skipped) only when
nothing after it names the handle, otherwise it keeps its capture and the
share-mark makes later writes copy (D4.4.4v3 condition 4). `if` arms join by
union. Loops naming the handle refuse, a plain rebind of the handle refuses,
`h = p(h)` with `h` at a `var` position is a write, and a mutated place copy
bound from the handle is a write at its bind (it may itself borrow). This time
writes are detected with the shared oracle, `ast_body_may_write_entry`, plus
that nested-bind rule. The §9.14 build had used the move scan, which misses a
write *through* the handle (b5).

**First finding: the splay handles were never candidates.** Instrumenting the
candidate check showed `splay_node`'s four `branch` handles reported
`place_copy_mutated = 0`, although each is passed to `splay_node` by `var`.
A call whose callee has no published signature skips build-time argument
validation, and so skips the `var`-pass note. `TypeFunc` parameters are
filled after the body, so this covers both a procedure calling itself and a
procedure defined later. `splay`'s own `root`, passed to the later
`splay_node`, had the same gap. Fix: `place_copy_var_call_cb` records `var`
passes from the callee's parameter nodes. The CW24v3/CW34/CW36 decisions moved
from FUNCTION_END to `lambda_ast_finalize_script`, which every build path runs
(runner, REPL, validator, AST dump) once all signatures exist. The CW24
diagnostic queue is untouched.

**Second finding: a D4.4.6 hole, through the same oracle.** The oracle did not
descend into `var`/`let` initializers, returns, blocks, `if` expressions in
conditional form, map/element items, pipes, handlers or `start`. So on v20:

```lambda
pn f(var r: R) {
    let old = r.kid
    var z = mutate(r)     // writes r.kid.n = 7
    print(old.n)          // printed 7; S9.1.2 requires 3
}
```

The JIT's read-only alias check (`mir_alias_is_readonly_borrow`) uses the same
oracle, so it had the same exposure. The oracle now covers those nodes.

**Third finding: seeing declarations exposed two over-approximations.** With
only the coverage fix, cd2 array copies rose 239,862 → 256,855, havlak2
35,130 → 45,541 (arrays) and 58,612 → 72,250 (maps), and deltablue2 67,420 →
70,380. A per-change switch build attributed all of it to the oracle, and a
trace of its new hits named the statements: `var tail_index = len(keys) - 1`
(every system function counted as a writer) and
`var sat = c_is_satisfied(w, c)` (every plain `pn` parameter counted, under a
comment "until CW29 lands"). A non-procedure system function is pure; the
mutating builtins (`push`, `splice`) are procedures. A known plain parameter's
writes stay in the callee (S9.1.3, CW29 snapshots there). A procedure whose
parameter list is not built yet stays a writer, because its `var` check was
skipped too.

**Copy census (`COW_EXEC_PROFILE`, JIT), v20 → v23.**

| row | kind | copies v20 | copies v23 | bytes v20 | bytes v23 |
|---|---|---|---|---|---|
| splay2 | map | 825,642 | 682,847 | 53.7 MB | 44.4 MB |
| havlak2 | array | 35,130 | 29,926 | 4.57 MB | 4.04 MB |
| havlak2 | map | 58,612 | 48,174 | 3.64 MB | 3.14 MB |
| deltablue2 | array[num] | 67,420 | 38,880 | 10.4 MB | 3.40 MB |
| cd2 | array | 239,862 | 239,663 | 87.6 MB | 87.5 MB |
| richards2 | all | identical | identical | | |

`splay_node` now binds all six handles as borrows, where v20 bound none.

**Why splay is not near zero.** The proposal expected the store-back shape to
be the whole cost; it is about a sixth. The rest comes from the rotations:
`rotate_right` does `left.right = node`, storing the caller's `var` root into
a child. That store must capture, because the caller's variable still holds the
same object until its `node = rotate_right(node)` overwrites it. The
next descent through that node then copies it. Removing the capture needs a
call-site convention, something like "this call's result overwrites the `var`
argument's home, so the callee may move the argument out". That is a new shape
with its own analysis on both sides of the call, so it is recorded here rather
than built under CW36.

**Correctness evidence.** The probe table (`temp/t28/cw36b/EXPECTED.txt`,
31 scripts × 3 tiers = 93 cells) covers the §9.14 probes, the CW24v3 probes,
the new reductions, a recursive `var` pass and the declaration write. v23
passes 93/93, and again under `LAMBDA_GC_FORCE_EVERY=1` with
`LAMBDA_GC_POISON_FREED=1`. v20 fails exactly the declaration-write cells.
New fixture `test/lambda/proc/cow_rmw_branch_store_back.ls` covers the splay
rotation, both arms with a snapshot between the store-backs, a path without a
store-back, an alias taken before the bind, a read between write and
store-back, the caller's view, an error exit after a handle write, the
declaration write and a forward `var` call. It is pinned with
`cow_place_copy_place_written` in the tier-parity list of
`test_lambda_gtest.cpp`.

**Timing (JIT, min of N, same-HEAD control).** The control is the v23 source
with the three changes switched off by environment (it reproduces v20's census
exactly). The machine carried four unrelated test processes at ~100% CPU, so
only rows that agree in both orders are read as results.

| row | N | control first | v23 first | reading |
|---|---|---|---|---|
| awfy deltablue2 | 9 | 0.932 | 0.907 | ~8% faster |
| awfy havlak2 | 9 | 1.008 | 0.981 | neutral |
| jetstream splay2 | 9 | 1.040 | 1.000 | neutral to 4% slower |
| jetstream deltablue2 | 5 | 1.039 | 1.013 | neutral |
| awfy cd2 | 5 | 0.995 | 1.002 | neutral |
| awfy richards2 | 5 | 1.012 | 1.002 | neutral |

(Ratios are v23/control; the second column is inverted from the raw run.)
splay2 does not speed up although 143k map copies disappear: its six borrowed
binds now run the spine test and every store through a borrowed handle
consults the share bit, so unique-mutation checks rose 613k → 756k. The
rotation captures above are the lever left for that row.

**Gates (v23, and again on the final v24 source, a no-behavior refactor that
reuses `direct_pn_callee`).** MIR emission 142/142; ratchet 19/19 after two
re-baselines -- `lambda_tune4_callsite_inference` +8 (the oracle now sees
`return f(a, b)` through a function value, so `apply2` snapshots its plain
parameters at entry, as the bare-statement form already did) and
`lambda_corpus_deltablue` -176, already present at v20 from §9.7; runtime
924/924 including the tier-parity test; `test_lambda_opt_gtest` 24/24;
benchmark corpus against v20: 151 identical, the same four self-timing
`ls_micro` rows differ. `make build-test` exits non-zero on this checkout
because three unrelated test executables fail to link the Node trace-events
module from the latest upstream merge.



### 9.17 T28-7 — emission diet for record and array bodies (LANDED, partly)

Targets from §6: `run_cube` ≤ 10,000 instructions, `c_choose_method` ≤ 1,200.
Instruction counts use the MT7 counter (every tab-led line except `local` and
`endfunc`).

| function | Result45 | v24 (start) | T28-7 |
|---|---|---|---|
| cube3d2 `run_cube` | 15.5k | 17,329 | **9,655** |
| cube3d2 module | | 22,528 | 14,487 |
| deltablue2 `c_choose_method` | 3,334 | 2,739 | 1,997 |
| deltablue2 module | | 28,626 | 24,703 |

`run_cube` meets its target. `c_choose_method` is 40% below Result45 but not at
1,200; the remainder is listed at the end.

Each change is generic; none names a benchmark.

1. **A literal `return` kept procedures off the native return lane.**
   `function_body_result_expr` unwrapped `return 0` with `ast_unwrap_primary`,
   which answers NULL for a childless literal primary, so "no body" denied every
   procedure ending in a literal return its native lane. `s_stronger(a: int,
   b: int) int` returned a boxed Item, boxed its own `1`/`0` with runtime band
   tests, and made every caller compare through `fn_eq`. A new shared
   `ast_unwrap_primary_to_leaf` (ast-core.hpp) replaces five inline copies of the
   leaf-preserving loop (rule 13) and fixes this site.
2. **Fixed-length array locals.** An index store never grows an array
   (`fn_array_set` raises past the end), so only a rebind, `push`/`splice`, a
   `var` pass, an unknown callee or a method receiver can change a local's
   length. `lambda_ast_note_fixed_array_lengths` (script finalize) records
   `NameEntry::fixed_array_length` for locals bound to `fill(N, v)` with a
   constant N or to an N-item scalar literal, and drops it on any of those
   uses. `walk_lambda_ast` now also visits loop init/update clauses, blocks,
   `seq` and conditional expressions, which that invalidation scan needs.
3. **Index proofs in the typed read and store emitters.** `mir_index_proof`
   returns none / non-negative / in-bounds (with the constant when the
   interval is one point). In bounds, a read skips its length load, both tests
   and its out-of-bounds arm; a store skips its bounds test and emits its cold
   arm only if some other guard can still reach it; a constant index becomes a
   load displacement. `mir_index_expr_nonnegative` now consults the interval
   oracle first -- a literal index was a childless primary it reported as
   unknown, so `qv[32]` kept a negative-index test.
4. **Int values with a proven interval skip the store's lane validity test**,
   which was the only branch to the cold arm of `line_drawn[k] = 1`.
5. **One-call boxing in cold arms.** `emit_box_cold_scalar` spells int and float
   boxing as `int2it_lane` / `push_d` (which is `flt2it`), instead of the
   ~10-instruction inline encoders, on store miss arms.
6. **`jmp L` followed only by labels up to `L` is removed** at Lambda function
   finalize (`mir_prune_jumps_to_next_label`). Emitters close arms that way
   whenever a later arm turns out empty; `run_cube` had 165. No code records a
   jump's address.
7. **Int equality against a proven in-band operand is a raw compare.** The
   float arm exists because raw equality is wrong when both sides are
   sentinels; a sentinel against an in-band value compares unequal either way.
   The non-null equality path likewise drops its NaN tests when one side is
   proven.
8. **An int lane entering a plain `int` parameter checks only for null.**
   `int` admits +/-inf and NaN (verified on both tiers), so the box, generic
   check call, error test and unbox (~30 instructions per argument) become one
   `beq` to a deferred terminal rejection with the same E201 text.
9. **The typed record path store keeps int values and index keys native** on
   its hot arm and boxes them only on the cold arm (an int Item never
   allocates, so no root is needed).
10. **An index product with an operand already inside [0, 2^26) skips that
    operand's compare**, so `i * 4` costs one unsigned compare.

**Output identity.** The benchmark corpus is identical to v24 except the four
`ls_micro` rows that print their own timings; the CW36 probe table passes 93/93
on the new build, and targeted probes (out-of-bounds store errors, arrays
resized through `push` or a `var` callee, rebinds) match v24 on all tiers.

**Fixtures.** Four MIR fixtures pinned shapes this track removes by design:
`tune22_int_lane_equality` (a literal operand no longer needs the float arm --
the float arm is now pinned with two nullable reads, and a literal case forbids
it), `tune16_proved_float_store` and `tune13_array_lane` (literal indices into
fixed-length locals have no cold store -- they now use parameter indices),
`tune26_same_owner_store` (the literal `flags[0]` lost its negative test), and
`r37_literal_index_product` (one compare instead of add + compare). New fixture
`tune28_fixed_length_index` pins the proven stores and the `push` counter-case.

**What keeps `c_choose_method` above 1,200.** Each `w.cons[cid].field` read is
still ~50 instructions: the `cons` field load with its zero-slot null arm, the
per-access `Constraint?[]` layout guard (N2), a bounds test, the element's
`item_at` miss arm with spills, then the element's own null and shape tests.
Each store is similar. Removing them needs the record-layout proofs of N2/N3
(one layout proof per body, a raw record ABI), not emission trimming. The
zero-slot arm on a non-null container field was left alone: nothing yet proves
a declared non-null container slot can never hold a zero word (partially
built records, host-built maps), and a wrong guess there is a crash, not a
lost optimization.

**Timing (JIT exec, min of 5, both orders; t7/v24).** The machine carried four
unrelated test processes at ~100% CPU.

| row | v24 first | t7 first | reading |
|---|---|---|---|
| awfy deltablue2 | 0.913 | 0.935 | ~7% faster |
| awfy mandelbrot2 | 0.961 | 0.943 | ~5% faster |
| awfy richards2 | 0.958 | 0.976 | ~3% faster |
| jetstream navier_stokes2 | 0.969 | 0.981 | ~2% faster |
| awfy havlak2 | 1.000 | 0.970 | neutral |
| jetstream deltablue2 | 1.018 | 0.954 | neutral |
| jetstream nbody2 | 0.784 | 0.988 | noise |
| beng spectralnorm2 (min of 15) | 1.076 | 1.001 | noise: 2.8 ms, and the hot loop's only change is removed instructions |

**Wall time, cube3d2 (compile + run, min of 9).** This is the row whose cost is
compilation (§6 "Deferred"): JIT 341-347 ms -> 181-186 ms, auto 347-353 ms ->
186-198 ms, about 46% faster end to end.

**Gates (final source).** MIR emission 143/143 (four fixtures re-pinned as
above, one new); ratchet 19/19 after locking in 23 shrunk metrics across 11
probes (largest: cube3d module 22,528 -> 14,487, jetstream deltablue
`choose_method` 1,823 -> 1,563, prettier module 16,376 -> 16,268; none grew);
runtime 924/924 including tier parity; `test_lambda_opt_gtest` 24/24;
`test_lambda_proc_gtest` 6/6; benchmark corpus identical to v24 except the four
self-timing rows; CW36 probe table 93/93.
