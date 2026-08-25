# Tune 20: Result34 — the Container Tail (Untyped Maps, Boxed Dispatch, Per-Element Admission)

- **Date:** 2026-08-25
- **Status:** PROPOSAL — not started.
- **Input:** `test/benchmark/Overall_Result34.md` / `benchmark_results_v34.json`
  (commit `697b5191b5`, archived release binary
  `test/benchmark/exe/lambda-v34-697b5191b5`); fresh **sample profiles of 8 of
  the 12 widest typed/C2MIR rows**, taken 2026-08-25 on the archived v34 binary
  with looped workload copies — sources, raw `sample` outputs, and the `nm -n`
  symbol map are under `temp/prof34/`.
- **Related:** `vibe/impl/Lambda_Impl_Tune19 (done).md` (T19-#; **§8 is the
  negative-results ledger — its entries are decisions, not to-dos, and several
  directly constrain this round**), `vibe/impl/Lambda_Impl_Tune15 (done).md`
  (B3 fixed-shape records, B4 string builder), `vibe/impl/Lambda_Impl_Tune17
  (done).md` (lane unification, same-facts-same-code),
  `vibe/Lambda_Design_Compiling_Lane.md` (ValueRep),
  `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (Result18 M1–M8),
  `vibe/Lambda_Impl_Tune13.md` (typed-array store cloning).
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S5.4 (map equality),
  S7.1, S8.1–S8.2 (key space); `doc/Lambda_Formal_Design.md` D2.4 (value
  representation discipline), D2.5.3, D2.6.1–D2.6.3, D3.3 (inference),
  **D3.4.1–D3.4.5 (shapes; D3.4.3 note: "runtime-constructed maps do not
  intern today — they rebuild per transition")**, D4.6 (name identity),
  **D8.4.1v2 (no inline caches anywhere in the Lambda lane or LambdaJS)**,
  D8.6.1–D8.6.3 (emission testing gates).
- **ID series:** `T20-#` (round-prefixed, per the tune-round convention).

---

## 1. Where v34 stands

| Metric (59-row basis) | Result32 | Result33 | **Result34** |
|---|---:|---:|---:|
| MIR (untyped)/Node geo | 1.81x* | 1.41x | **1.35x** |
| MIR (typed)/Node geo | 0.85x* | 0.83x | **0.80x** |
| MIR (typed)/C2MIR geo | 5.46x | 5.34x | **5.23x** |

\* v32 Node-relative figures were quoted on the era's row basis; C2MIR column is
the §7.6 back-patched 59-row series and is comparable throughout.

The geomean is **flat**: 5.34 → 5.23 across Result33→34, and the twelve rows at
≥14x are the *same twelve rows* in both reports, in nearly the same order. The
distribution is fully bimodal now:

- **At the ceiling.** The scalar-lane rows are done: fib 1.18x, tak 1.32x,
  cpstak 1.46x, ack 1.22x, mandelbrot 1.26x, diviter 1.00x, collatz 1.79x,
  matmul 1.88x — and **divrec 0.41x, typed Lambda beats the C port through the
  same backend**. Tune17–19 finished the scalar story; codegen quality is not
  the problem anywhere in this report.
- **The container tail.** Every row ≥14x is maps, strings, or typed-array
  element traffic: deltablue 66.0x, microdiff 53.5x, base64 29.9x, cube3d
  26.8x, havlak 26.2x, list 25.6x, hyphen 24.0x, raytrace3d 21.1x, hashmap
  19.4x, queens 17.2x, knucleotide 16.7x, cd 14.3x.

The typed lane also still violates the Tune16/17 categorical bar on at least
one row: **binarytrees typed 19.0 ms vs untyped 9.04 ms (2.10x tax)**; bounce
(0.113 vs 0.069), splay (151.6 vs 133.1), and ack (14.4 vs 10.9) are smaller
live violations. See T20-6.

## 2. Fresh profile evidence (2026-08-25)

### 2.1 Method

Per the Tune19 §8.4 lessons — *profile before ranking; never read static dump
counts as time; never profile a debug/ASAN binary* — every ranking below comes
from wall-clock samples of the **archived v34 release binary**:

1. Copies of eight `*2.ls` sources in `temp/prof34/` with only the workload
   repeat count raised (4–6 s per run; the timed region is >98% of the
   process).
2. `sample <pid> 4 1` per run; leaf-frame ("top of stack") attribution.
3. The release binary strips `static` functions, so hot `??? + offset` frames
   were resolved two ways and cross-checked: nearest preceding symbol from
   `nm -n` (bisect), and the frames' *parents* in the sample call tree. Both
   agree on every cluster named below. (Gotcha for future sessions: these
   benchmarks are `pn main()` procedural scripts — they must be run as
   `lambda.exe run x.ls`, or they print `null` and time nothing.)

### 2.2 The headline: JIT'd code is the minority everywhere in the tail

Share of leaf samples inside the JIT-compiled Lambda functions themselves
(everything else is runtime helper calls):

| Row | typed/C2MIR | in JIT code | dominant runtime mechanism |
|---|---:|---:|---|
| deltablue | 66.0x | **3%** | map field get/set + generic equality (~90%) |
| list | 25.6x | **6%** | map field get + per-read boxing (~80%), pool_alloc ~9% |
| microdiff | 53.5x | **14%** | boxed `fn_add`/`fn_mul`/`fn_len` + numeric admission (~85%) |
| hashmap | 19.4x | **17%** | `fn_index`/`array_num_*` reads + admission + boxed `&`/`>>>` |
| havlak | 26.2x | **2%** | map get + array index + type_check + GC (mixed) |
| base64 | 29.9x | **28%** | `fn_string`/`fn_add`/`fn_strcat`/`it2s` (~60%) + memmove 10% |
| raytrace3d | 21.1x | **24%** | type_check/`fn_is`/admission + array ops + GC 7.5% |
| cube3d | 26.8x | **37%** | admission/validate + array alloc (`fn_fill`) + GC 8% |

Top resolved symbols per cluster (leaf sample counts; ~2,700–3,400 leaf
samples per row):

- **deltablue:** `map_get_by_name_id` 815 (incl. statics), `fn_map_set` 134,
  `fn_member_by_id` 111, shape walk (`shape_entry_uses_native_lane` 124,
  `map_field_to_item` 118, `map_shape_field_to_item` 75,
  `type_field_storage_type_id` 75), generic compare (`total_cmp`-region
  statics 368, `fn_sym_eq_ptr` statics 234), map-write statics in the
  `cow_path_set_raw` region 354.
- **list:** `map_get_by_name_id` 1,050, `typeditem_to_item` 348 (the single
  hottest symbol — per-read boxing of a field into an Item),
  `fn_member_by_id` 239, shape walk ~590, `pool_alloc` ~300.
- **microdiff:** `fn_add` 544 **plus 891 in statics of
  `lambda-eval-num.cpp`** (nearest anchor `fn_ushr_item`; call-tree parents are
  `fn_add+436` — this is fn_add's out-of-line slow path), `fn_len` 241,
  `fn_mul` 217, `lambda_numeric_boundary_admit` 211+232, `lambda_type_check`
  55+150. Note: Tune19 §7 classed microdiff under "strings" from static dump
  reading; the profile says its time is **boxed arithmetic dispatch**, and per
  §8.4-1 the profile wins.
- **base64:** `fn_string` 522, `fn_add` 478, `fn_strcat` 333,
  `_platform_memmove` 274, `it2s` 199, fn_add slow-path statics 502.
- **hashmap:** `fn_index` 263, `array_num_read_item` 179, `item_at` 150,
  `array_num_get` 144, `lambda_numeric_boundary_admit` 109 + 222 in its
  statics, `fn_band_item` statics 328, `total_cmp` statics 245,
  `fn_sym_eq_ptr` statics 170, `cow_path_set_raw`-region statics 385.
- **cube3d / raytrace3d:** `lambda_type_check`(+statics) 172 / 257,
  `lambda_type_matches` 123 / 90, `validate_against_type` /
  `validate_occurrence_type` 198 / —, `ensure_typed_array` 68 / 119,
  `lambda_numeric_boundary_admit` — / 100, `fn_is` — / 245 (8.7% of
  raytrace3d), `gc_collect_with_root_region` 135 / 121, `fn_fill` 87 / —,
  `array_num_new_with_extra` 97 / —, `array_push`+`expand_list` — / 97.
- **havlak:** `map_get_by_name_id` 284 + named map/shape 13.6%,
  `fn_sym_eq_ptr` statics 186, `lambda_type_check` 183+64,
  `gc_collect_with_root_region` 147, `fn_index`/`item_at`/`array_get` 252,
  `fmod` 66 (hash via `%`).

### 2.3 The structural finding: the tail rows *cannot* use the declared-record machinery

This is the fact that reorders the round. Tune15 B3 landed fixed-shape record
lowering for **declared** map types, and Tune19 §11.5 made recursive record
contracts adopt at native speed. Yet `map_get_by_name_id` still owns deltablue
and list. The reason is in the sources:

- `deltablue2.ls` declares `type Variable = any`, `type Planner = any` — every
  object is an untyped map on purpose;
- `list2.ls` traverses via `pn list_length(node: map?)` — bare nullable map;
- `havlak2.ls` declares no types at all.

**Why (corrected 2026-08-25 — the first framing was wrong).** An earlier
draft claimed these rows are "semantically barred" from typing by the
mutability model. That is false: annotations are value contracts and do not
change S9's mutation rules, and since the untyped scripts run correctly,
mutability cannot be the discriminator. The actual mechanism was probed on the
v34 binary (`temp/prof34/alias_probe*.ls`):

- **The untyped map path aliases observably today.** `var v = {val:1};
  var c1 = {out:v}; var c2 = {out:v}; c1.out.val = 5` → `c2.out.val == 5`
  and `v.val == 5`. Construction does *not* capture by value. This is the
  documented **C4.1 bug catalog** state ("uniform reference — sites alias
  today", `Lambda_Design_Runtime_COW.md`), a known divergence from S9.1.2/
  S9.3.1, whose closure is a CW3-C prerequisite for thread-mode sharing.
- **A declared record *holder* detaches at admission.** The same probe with
  `type C = {out: V}` on the holders prints `c2.out.val == 1, v.val == 1`:
  admission into shaped storage reifies into the packed layout (D3.4.1) and
  severs sharing. A typed *node* inside untyped holders keeps aliasing;
  the detach line is the shaped-field boundary.

So typed and untyped **currently differ in observable sharing** — an
implementation divergence between two storage paths, not a property of the
semantics (under closed-C4.1 spec behavior both would copy). The graph
benchmarks' mutations lean on the untyped aliasing (which is what makes them
match their reference-semantic JS originals), so typing their holders today
would change program behavior, and historically also paid the reification
cost (cd2 arena→direct-refs 2.21x, splay O(n²) pre-§11.5). Hence the sources
stay `any`/`map?`, and they are exactly Tune19 §2's "annotation buys nothing"
cohort (deltablue, havlak, microdiff within ±5% of untyped in v34 too).

⚠ Corollary worth recording: when the C4.1 catalog is closed, *untyped*
deltablue2/havlak2 as written will change behavior too — the aliasing they
lean on is scheduled to disappear. A spec-conformant rewrite needs explicit
identity (id/index + owner store + `var` borrows) and *that* version is fully
typeable. The ports and their goldens will need revisiting in the same round
that closes C4.1.

**Consequence for this round:** the dominant rows run — and for now must run
— on the untyped/inferred map path, so extending declared-contract fast paths
cannot reach them. The untyped path itself has to get fast — statically,
since D8.4.1v2 forbids inline caches. That is this round's center of gravity.
(This also future-proofs: dynamic-shaped data from JSON ingestion and guest
languages stays on this path regardless of how the benchmarks evolve.)

## 3. Tracks (ranked; each separately land-able and gate-able)

### T20-1 — Static shape resolution for runtime-constructed maps (the object-model track)

**Owns:** deltablue 66x, list 25.6x, havlak 26.2x, cd 14.3x, splay 8.2x,
richards 7.4x, and the map-side ~40% of hashmap. Largest unblocked lever in
the report.

The gap versus C is one load per field access versus: `fn_member_by_id` →
`map_get_by_name_id` (ShapeEntry chain walk keyed by name_id) → lane decision
(`shape_entry_uses_native_lane` + `type_field_storage_type_id`) → boxing
(`map_field_to_item` / `typeditem_to_item`). All four stages are visible as
separate hot symbols in §2.2. The write side (`fn_map_set` + the
`cow_path_set_raw`-region statics) repeats the walk.

The static structure to exploit: a map literal at a given construction site
always produces the same shape, and D3.4.1 already makes the packed layout ABI
with precomputed `byte_offset`. What is missing (D3.4.3's own note) is that
**runtime-constructed maps do not intern** — so nothing downstream can name
their shape statically. Sub-items, in dependency order:

1. **T20-1a — transpile-time shape interning for map literals.** Intern the
   shape of each literal construction site into the module's shape pool at
   transpile time (structural identity per D3.4.2); construction tags the map
   with the interned `TypeMap`. No semantic change — this is the same interning
   Input already does at `final()` (D3.4.3), moved to compile time for
   script-constructed maps.
2. **T20-1b — member-site slot lowering behind a shape-signature test.** Where
   the AST's inferred type names an interned shape, lower `.field` to a direct
   `byte_offset` load guarded by one shape-pointer compare (fallback: the
   generic call). This is B3's fixed-slot path with the *proof source widened
   from declared contract to inferred literal shape*. The guard is a static
   compare against a compile-time constant — not an inline cache; nothing is
   patched (D8.4.1v2 respected). D3.4.5 keeps it sound: type-compatible writes
   never transition the shape, and the guarded fallback covers `var`-bound maps
   that did transition.
3. **T20-1c — closed-caller shape propagation.** deltablue/list pass nodes
   through `map?` parameters, so site-local inference sees only `map?`. The
   T19-4 closed-caller machinery (every call site known ⇒ join the argument
   facts into the parameter) applies unchanged with "fact" = interned shape
   instead of scalar lane. ⚠ The T19-4 ledger entries carry over: the
   *transitive* edge was built, measured, and rejected (geomean 1.02) — propagate
   one level from literal constructors only, and treat "unknown yet" joins as
   generic, not as a veto to re-litigate.
4. **T20-1d — `fn_map_set` fast path (the T19-6 remainder).** Tune19 measured
   `fn_map_set` at ~46% of richards2 and its O(1) `field_index` attempt was
   **rejected at 1.060/1.065 with 0/11 wins** (§8.1). Its stated precondition —
   *a precomputed site hash or a NameId-keyed table* — is exactly what T20-1a/b
   provide: at a site with a statically-known shape, the store lowers to
   `byte_offset` directly and no index is needed at all. Do not rebuild the
   dynamic index; land the static form.

**Acceptance:** deltablue typed ≤10x C2MIR, list ≤6x, havlak ≤10x, cd ≤6x;
`map_get_by_name_id` and `typeditem_to_item` leave the deltablue/list top-10
samples; `mir-check` fixture asserting the guarded direct load (with `expect_seq`
adjacency per §8.4-6); named-contract rejection tests and S5.4 map-equality
tests unchanged.

### T20-2 — Strings: land the T19-7 design (base64, hyphen, knucleotide, fast_diff)

Carried from Tune19 §7.5, where it was promoted to "design now" and estimated
as the biggest single geomean lever (4.0 → 2.8 on the v33 numbers *by
itself*). The v34 profile sharpens the implementation targets the design doc
must serve:

- base64 is **not** dominated by codepoint indexing but by *accumulation*:
  `fn_string` (Item→string of an int, 522) + `fn_add` dispatch (478) +
  `fn_strcat` (333) + `it2s` (199) + `memmove` (274 — quadratic-ish rebuild of
  the loop-carried accumulator). The Tune15 B4 owned-builder shape (internal
  mutable builder for loop-carried `++`, one immutable finalize at the
  observable boundary [S1.4–S1.6]) addresses precisely this and should be the
  doc's first deliverable, ahead of the byte-lane storage work.
- hyphen (24.0x) and knucleotide (16.7x) add the read side: per-codepoint
  boxed indexing that the element-width-aware byte lane addresses.

**Acceptance:** design doc ratified first (implementation stays gated on it,
per Tune19's ruling); then base64 ≤8x, hyphen ≤8x; `memmove` and `fn_strcat`
leave the base64 top-10.

### T20-3 — Expression-result representation for boxed arithmetic (microdiff, hashmap's hash loop)

microdiff spends ~85% in generic dispatch: `fn_add`+slow-path statics (1,435
combined), `fn_mul`, `fn_len`, plus numeric admission — on values that are
statically `int` in the source. hashmap's probe loop pays the same through
`fn_band_item`/`fn_ushr_item` statics (bitwise ops on boxed items). This is
the Tune19 §4 root cause — *expression results still have no representation* —
in its remaining habitat: results of calls (`fn_len`, sys-funcs) and of
map/array reads feeding arithmetic, where the lane machinery currently
re-boxes between every producer and consumer [D2.4, D3.3].

Scope for this round: give call-result and container-read expressions a
ValueRep so int-typed results stay in the int lane through arithmetic chains,
starting with the sys-func registry rows whose `success_type` is precise
(⚠ T19-B: `int()`'s registry row lied — audit `success_type` precision as step
zero, it is the cheapest part of the track).

**Acceptance:** microdiff ≤15x (from 53.5x); `fn_add` leaves microdiff's top-3;
no scalar-recursion row regresses >5% (the T19-2 window charged exactly those
rows — watch fib/tak/ack with ≥15 pairs).

### T20-4 — Hoist per-element typed-array admission (cube3d, raytrace3d, hashmap's array side)

The typed-array rows pay `lambda_numeric_boundary_admit`,
`lambda_type_check`, `ensure_typed_array`, and `validate_against_*` **per
element access**, plus `fn_index`/`item_at` generic entry. C2MIR does a raw
indexed load. Distinct from the refuted T19-5: that track was ranked from
static `fn_array_set` counts and profiling showed 0–3% (§8.2); this one is
ranked from the profile itself — the admission family is 20–25% of cube3d and
raytrace3d *measured*.

Mechanics: an `int[]`/`float[]` binding whose producer is statically conforming
(e.g. `fill(n, 0)`, an ArrayNum-producing sys-func, or a same-typed array) gets
its witness established **once at the binding**, and element reads/writes inside
a loop whose subject's witness is loop-invariant use the direct
`array_float_get`/`set` form with no per-element re-admission [D2.6.2–D2.6.3].
This also retires the T19-B demotion for good (the declared witness unsatisfied
by `fill`'s producer). raytrace3d's `fn_is` (8.7%) joins here: a type test
whose operand has a static witness folds to a constant.

**Acceptance:** cube3d ≤8x, raytrace3d ≤8x, hashmap ≤8x;
`lambda_numeric_boundary_admit` leaves all three top-10s; forced-GC + poison
sweeps on the witness lifetime (D8.6.3) — a hoisted witness must die with the
binding, not the loop.

### T20-5 — Allocation cadence on the 3D/graph rows (conditional, investigation gate)

`gc_collect_with_root_region` is 7–8% of cube3d/raytrace3d/havlak, with
`fn_fill`/`array_num_new_with_extra` allocating fresh frame arrays per
iteration and `array_push`/`expand_list` growing worklists. Rule (same as
Tune15 B6): **do not enter GC tuning first** — T20-1/T20-4 remove most of the
per-op allocation (boxed field reads, admission temp boxes); re-profile after
they land and open this track only if collection share is still ≥10% on any
row. gcbench (3.03x — the one row where even C2MIR is slow, allocation *is*
the workload) parks here too.

### T20-6 — The v34 annotation-tax ledger (categorical-bar cleanup)

binarytrees typed is **2.10x its own untyped row** (19.0 vs 9.04 ms) — the
worst live violation of the bar (*an annotation may never cost >5%*). bounce,
splay, and ack are smaller. Process per §8.3: reproduce the emission on a
two-line probe before editing the emitter; per §8.4-2, confirm each row with
≥15 paired runs before and after.

**REGRESSION found 2026-08-25 — FIXED 2026-08-25: self-referential type names
no longer resolved.** On v34 and pre-fix HEAD, `type Node = {left: Node?, …}`
emitted `type-pattern: unresolved type name 'Node', using ANY`
(`lambda/runtime/parse_type_pattern.cpp:914`) — the recursive fields silently
degraded to ANY. v33 (`lambda-v33-8705d85c5a`) resolved the same declaration
cleanly; the regression entered with the v33→v34 syntax-migration commits.
**Root cause:** the C parser reduces bottom-up, so the `TYPE_SLOT` body parsed
(and resolved names) *before* the `FORM_TYPE_ALIAS` declaration reduction
registered the alias name — the retired CST builder's pre-registration
(`build_assign_expr`'s pre-bound placeholder map) was lost in the port, while
object-form `type N { … }` kept its `TYPE_OBJECT_BEGIN` pre-binding. **Fix:**
new `LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN` context reduction emitted after
`=` and before the type slot parses; the direct sink pre-registers the alias
node with a placeholder `TypeType→TypeMap` and, at the declaration reduction,
publishes the completed shape through that same map identity
(`direct_type_alias_begin` / `direct_adopt_pending_alias_map` in
`build_ast.cpp`), mirroring v33 semantics exactly. Pattern islands skip the
pre-binding (own registration path). Coverage: `test/lambda/type_selfref.ls`
asserts mismatched recursive fields fail `is` (ANY degradation would answer
`true`, as v34 demonstrably did); output parity vs the archived v33 binary
confirmed; `make test-lambda-baseline` 3884/3884.
Consequences now unblocked:
(a) binarytrees2's `Node?` fields were ANY-degraded when Result34 was measured
— its 2.10x tax and the R34 binarytrees typed cell carry this caveat and need
re-measurement on the fixed build;
(b) §11.5's adoption gate (`mir_map_contract_storage_valid` refuses
ANY-bearing contracts) could never fire on a degraded contract — the recursive
fast path is reachable from source again. Re-measure binarytrees/splay before
any emitter work.

**Internal coverage added 2026-08-25** (closes the D8.6.2 gap the §11.5
re-land left):
- `test/test_lambda_opt_gtest.exe` (baseline, js_opt-style): runs one fixture
  per child with `COW_EXEC_PROFILE=1` and pins the `map_admit_*` counters —
  recursive contract under `--tier=jit` = ZERO runtime admissions (fully
  static), under `--tier=interp` = 20 trusted classifications with 0
  reifications/copies; ANY-bearing union contract = refused and reified on
  both tiers (the §11.3 misaddressing guard). Validated against the archived
  v34 binary via `LAMBDA_JS_OPT_EXE`: 3 of 6 tests fail on it (regression
  signature 20 calls / 20 reifications).
- `test/mir/lambda/recursive_record_adoption.{ls,mir-check}`: emission-level
  pin — `_build_#` must lower to direct allocation + field stores
  (`heap_calloc_class` / `map_with_region*`), forbidding the degraded
  signature (`map_with_tl` + `map_fill` + `lambda_type_check`);
  `_total_#` capped at ≤1 `fn_member_by_id` call.
- Noted while pinning: on the interp tier, `let a: Node = {val: 7, next:
  null}` (null recursive field) takes the runtime path and reifies once where
  `next: <var>` adopts statically — pinned at 11 calls / 1 reification in
  `RecursiveShapeIdentityInterpExactHits`; ratchet to 10/0 when construction
  learns this literal.

**Second probe drift (needs its own minimization):** `var b: N = …;
b.next = a; a.k = 99` — the store **aliases on v33/v34** (`b.next.k == 99`)
but **detaches on current HEAD** (`== 1`), while plain untyped stores alias on
both. A behavioral change to declared-`var` member stores entered the last few
commits (`temp/prof34/alias_probe8.ls`). Related to the §2.3 C4.1 discussion:
the alias/copy line is moving between builds, which is exactly the state the
C4.1 catalog is supposed to pin with fixtures.

**Acceptance:** no row where typed > 1.05x untyped on the fixed population.

## 4. What NOT to do (inherited decisions)

- **No inline caches, no patched code** — D8.4.1v2 covers both lanes. Every
  fast path in T20-1 is a static lowering behind a compile-time-constant guard.
- **No dynamic O(1) shape index** over `TypeMap.field_index` (Tune19 §8.1:
  1.060 at 0/11 wins). Its precondition is met only by the *static* form
  (T20-1d).
- **No transitive closed-caller edges** (§8.1, geomean 1.02). One level from
  construction sites.
- **No typed contracts on the graph benchmarks' sources this round.** Not a
  semantic law (see §2.3's correction) — but typing the holders *today*
  changes observable sharing (probe7) and therefore program behavior; the
  sources stay as they are until the C4.1 catalog closes. (list2 is the
  exception: its list is immutable after construction, so a recursive record
  type is behavior-preserving post-Tune19 §11.5 — permissible, but keep a
  dynamic-map variant as untyped-path coverage.)
- **No flex-int revival, no C2MIR-path changes** (frozen, CLAUDE.md rule 14),
  **no vendored-dep edits** (rule 16).
- crypto_sha1 pre-v34 MIR cells time a defective computation (Tune19 §8.5) —
  never trend that row across sessions older than v34.

## 5. Round targets

| Metric | v34 | target | stretch |
|---|---:|---:|---:|
| MIR (typed)/C2MIR geo, 59 rows | 5.23x | **≤3.8x** (T20-1 + T20-4) | ≤3.0x (with T20-2 impl) |
| MIR (typed)/Node geo | 0.80x | **≤0.68x** | ≤0.60x |
| MIR (untyped)/Node geo | 1.35x | **≤1.15x** | ≤1.05x |
| Rows >20x C2MIR | 5 | **0** | — |
| Typed rows >1.05x untyped | 4+ | **0** | — |

The untyped/Node target leans on T20-1 too: the object rows run the same map
path on both lanes, so the untyped column gains everything T20-1 buys
(deltablue untyped 79.6, cd untyped 774 today). Tune19's unopened T19-8
(closed-caller float lanes; untyped ray 30x, spectralnorm 13.6x) and T19-9
(scalar-recursion call boundary) remain open behind these — pull them in only
if the round has room after T20-1/T20-4.

## 6. Gates and acceptance (house rules, unchanged)

- `make test-lambda-baseline` 100% and test262 40,261/40,261 after each
  retained phase; MIR emission ratchet updated in the same commit for justified
  growth [D8.6.1]; `mir-check` coverage for every new witness/guard/lowering
  edge, with adjacency assertions, mangled-name scoping, and unique step-0
  per Tune19 §8.4-5/6 [D8.6.2]; forced-GC + poison sweeps for every
  representation/lifetime change [D8.6.3].
- Release-build timing only (verify ~21 MB binary — `make test-lambda-baseline`
  clobbers `lambda.exe` with a debug build); one benchmark run at a time;
  three runs, workload-only `__TIMING__`; **≥15 paired runs before believing
  any short-row delta** (Tune19 §8.4-2); A/B against an archived control
  binary for any broad claim.
- The categorical bar stands and T20-6 is accountable for clearing it.
- Profiling protocol for this round's re-measurement: the `temp/prof34/`
  looped-copy + `sample` + `nm`-bisect method of §2.1 is cheap and release-safe;
  prefer it over debug-build MIR dump reading for *ranking* (dumps remain the
  tool for *dissecting* a chosen site).
