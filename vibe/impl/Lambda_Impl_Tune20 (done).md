# Tune 20: Result34 — the Container Tail (Untyped Maps, Boxed Dispatch, Per-Element Admission)

- **Date:** 2026-08-25
- **Status:** IN PROGRESS. **T20-1a, T20-1b, T20-1c, T20-1d, T20-4 and one
  T20-3 slice landed 2026-08-26** — the object-model track (T20-1) is complete
  and is the round's measured result: deltablue −11%, havlak −13%, richards −8%
  cumulative (baseline 3898/3898; no confirmed regression over a 20-row
  release A/B). T20-6 re-measured; **T20-5 measured and CLOSED** (collection is
  ~5%, under its own gate). T20-1b, T20-2 and the general T20-3 item remain open.
  **See §2.5 for the standing status table** — read it before picking up any
  track, and note that two landed items (T20-1a alone, T20-1d) are correct but
  inert on this corpus for reasons recorded with them.
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

## 2.5 Implementation status (2026-08-26)

| Track | State | Measured effect |
|---|---|---|
| **T20-1a** guarded read, inferred shapes | **LANDED** | 1.72x isolated; 0 benchmark sites on its own |
| **T20-1c** shape propagation (param/return) | **LANDED** | contributes to the confirmed havlak −7%; the list −8% / cd −2.7% figures were single-pair |
| **T20-4** array-lane validator short-circuit | **LANDED** | ⚠ cube3d's −8.5% was a SINGLE-PAIR number; at 4 pairs the row is **flat** — see §2.6 |
| **T20-1b** ~~shared shapes~~ → **module-unique field shapes** | **LANDED** | the original premise was FALSE; the real limiter was coverage. deltablue **−9%**, richards **−6%**, havlak **−6%**, all 3-pair confirmed |
| **T20-1d** guarded store | **LANDED + runtime lane check + NULL-slot retag** | sites richards 7 / havlak 13 / fixture 11; richards −1.2% confirmed; GC-stress verified |
| T20-2 strings | **CLOSED** | flattening was already implemented; operand-conversion elision landed (emission verified, timing flat — #3's shr fix had taken base64 already); byte-lane design has no measured customer |
| T20-3 expression ValueRep | **five slices landed; member-result lane REFUSED (soundness)** | module-let lane ~4x microdiff; len() inline; literal-shr: hashmap2 −18%, base642 −31%; guarded witnesses: havlak −12%; #2 needs companion lanes — see the track |
| **T20-5** allocation cadence | **gate NOT met — stays closed** | collection is 4.6–5.5%, under the ≥10% bar; earlier "30%" conflated alloc with collect |
| T20-6 annotation-tax ledger | diagnosed to one site; **blocked on two rulings** | binarytrees resolved; bounce = per-call `int[]` param re-admission, elision unreachable (MAP gate + contract pointer identity) and its own case is a mutable `var` param where the check can legitimately reject; ack (T19-9) unopened |

Verification for everything above: `make test-lambda-baseline` **3899/3899**, both new
fixtures JIT/interp-identical, a forced-GC + poison sweep on the retag path, and a
14-row release A/B (`temp/prof34/before.exe` vs `after_retag.exe`, `LAMBDA_TIER=jit`)
with **no confirmed regression**.

## 2.6 ⚠ Correction: most of this round's "wins" were single-pair noise

Re-measuring at 3–4 alternating pairs on a quiet machine collapsed most of the
numbers an earlier draft of this document reported. **Only two survive:**

| Row | confirmed | reading |
|---|---|---|
| awfy/havlak | **−7%** | 49.0/49.2/49.2 → 45.9/45.7/45.7, three clean pairs |
| awfy/richards | **−1.2%** | 2206/2192/2211 → 2181/2165/2185 |

Everything else — cube3d −8.5%, list −8%, cd −2.7%, gcbench2 −9%, fib2 −21%,
mbrot2 −9% — **does not reproduce**. (T20-1b landed after this correction and
its numbers below WERE taken at 3 pairs; the cumulative session result is
deltablue 80.8 → 72.6, havlak 49.1–49.8 → 42.9, richards 2197 → 2043–2060.) cube3d is the clearest case: the same
`before.exe` that read 12.66 ms during the T20-4 session reads 11.22–11.38 ms
now, so the "win" was the machine, not the change; at four pairs the row is flat
(11.22/11.28/11.38/11.34 vs 11.43/11.30/11.40/11.42). The same applies in the
other direction to the "+7% binarytrees2" and "+2.5% primes2" scares.

This is Tune19 §8.4-2 — *a 5-pair number on a short row is not a measurement* —
and this round proceeded to fall into it anyway, in both directions, because
single runs were being read as results between build steps. **Every timing claim
in the sections below that is not marked as multi-pair should be treated as
unverified.** The correctness work (baselines, differentials, GC sweeps) is
unaffected; only the performance attributions are.

**T20-5: the gate is NOT met, and an earlier draft of this section said it was.**
That claim came from eyeballing a profile and adding allocation to collection.
Counted properly, cube3d2's leaf samples split **collect 4.6% / alloc 15.2% /
validate 16.5%** before T20-4 and **collect 5.5% / alloc 14.6% / validate 18.7%**
after. The track's own rule is a **collection** share ≥10%; at ~5% it stays
closed. `LAMBDA_GC_STATS=1` corroborates: the shipped cube3d2 workload runs
**1 collection** (raytrace3d2: 4), and even the 3000-iteration loop copy spends
2.08 ms of 4129 ms in marking.

What is real is the **allocation rate** — `fn_fill` + `array_num_new_with_extra`
+ zone/data/heap alloc ≈15% — because `mat4_mul` and friends build a fresh
`float[16]` per call. But `fn_fill` is already an optimal packed writer (it
allocates an ArrayNum and stores the lane directly; no boxed per-slot path to
remove), so this is not GC tuning: it is escape analysis / result reuse, which
is a different and much larger track. Recording it here so the next session does
not re-open T20-5 expecting a threshold knob to help.

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

**⚠ Premise corrected 2026-08-26 by reading the emitter — the missing piece is
NOT interning.** An earlier draft claimed literals have no static shape and
proposed interning one. False: `build_ast` already gives **every map literal
its own complete `TypeMap`** — full `ShapeEntry` chain, precomputed
`byte_offset`s, `byte_size`, appended to the module `type_list` with a
`type_index` (e.g. `build_ns_attr_map_from_parts`, and the literal path
generally). `transpile_map` then embeds that `TypeMap*` **as a compile-time
immediate** into the constructed Map header at offset 8
(`emit_insn(... MIR_T_I64, 8, m ... (int64_t)(uintptr_t)map_type)`), and for
shape-matched literals inlines the whole allocation (`heap_calloc_class` +
direct per-field stores). The shape is present, static, per-site, and already
in the object header at runtime.

**The actual gate is one flag: `is_trusted_contract`.** Every fast path tests
it — the field read (`transpile-mir.cpp:14659`
`map_type->is_trusted_contract && has_fixed_shape(map_type)`), the member
contract (`:13388`), and the field store (`:21172`). It is set at exactly
three sites, all *declarations*: `type N { … }` (`build_ast.cpp:6759`) and the
two type-alias paths (`:7217`, `:7238`/`:7258`). An inferred literal shape —
however complete — never gets it, so the gate falls through to
`fn_member_by_id`.

Measured cost of that one flag (release, `LAMBDA_TIER=jit`, 3M-iteration field
loop, identical `{x: int, y: int}` shape — `temp/prof34/shape_*.ls`):

| object expression | time | path |
|---|---:|---|
| `let p: P = {x:3, y:4}` (declared) | **3.7–5.5 ms** | direct `byte_offset` load |
| `let p = {x:3, y:4}` (inferred) | **125–129 ms** | `fn_member_by_id` |
| `{x:3, y:4}.x` (literal in place) | **154 ms** | `fn_member_by_id` |

**33x for the same shape and the same field.** The third row is the decisive
one: the object expression *is* the literal, so its `->type` is unambiguously
the per-site `TypeMap` — no inference, binding, or propagation is involved.
The only condition it fails is the trust flag.

**Why the flag exists, and what has to replace it.** A declared contract is
backed by admission: a value reaching an annotated binding was reified into
that layout, so the static type is a *promise about the runtime bytes*. An
inferred shape promises nothing — the map arriving at a member site may come
from a different literal site, a join of two, a `var` rebind, or a runtime
shape transition after a field store (D3.4.5; Tune19 §7.13 measured exactly
this — retagging a null slot clones the shape per instance, giving 8000 nodes
8000 distinct `TypeMap`s). So the flag is not gratuitous: it stands in for a
proof that inferred shapes do not have. The fix is to supply that proof
*dynamically at zero static cost*, since the header already carries the
answer. Sub-items, in dependency order:

1. **T20-1a — runtime-verified trust for inferred shapes (replaces the
   interning item). IMPLEMENTED 2026-08-26 — mechanism works, but see the
   coverage finding below: it fires on ZERO benchmark rows without T20-1c.**
   Where the object expression's AST type is a real `TypeMap` whose layout is
   addressable but which carries no `is_trusted_contract`, the direct
   `byte_offset` load is emitted **behind a two-instruction guard**: load the
   map header's `type` word (offset 8 — already written as a compile-time
   constant by `transpile_map`) and compare it against that same constant; on
   mismatch, fall through to the existing `fn_member_by_id` arm. A static
   compare against an immediate, nothing patched, so D8.4.1v2 is respected.
   ⚠ `is_trusted_contract` keeps its *unguarded* path — a declared contract
   still needs no compare.

   *What landed* (`transpile-mir.cpp`, `transpile_member`):
   - `mir_shape_layout_is_addressable()` — the layout half of
     `has_fixed_shape()` without its `struct_name` requirement. That name check
     is how a *declared contract* is recognized and is right for its other
     callers; the guarded read needs only addressable slots, which an inferred
     literal shape has. **This was the actual blocker** — the first cut reused
     `has_fixed_shape()` and never fired.
   - `mir_guarded_field_storage_admits()` — the retag-immune storage classes.
     The guard proves the map's *shape*, never a field's *storage class*:
     `fn_map_set` retags a slot in place, same TypeMap and offset, for `NULL→T`
     and among the pointer-like classes (`shape_entry_retag_is_safe`). Admitted:
     INT family / FLOAT / STRING (absent from that set, so any change reaches
     `map_rebuild_for_type_change`, which allocates a NEW TypeMap and fails the
     guard) and real containers (a retag can only move them among pointer-like
     classes of the same width, and the container arm reads the slot as a raw
     `Container*`, 0 ⇒ ItemNull, which stays correct for all of them).
     **Excluded: NULL** (precisely what the in-place upgrade rewrites — a NULL
     slot retagged to INT would read an int64 as a pointer) **and BOOL** (same
     set, different width). JS fixed-slot shapes are excluded too: their
     in-place retag path never rebuilds.
   - Representation is unchanged: the guard only runs where the type oracle
     published no native lane, so both arms yield a boxed Item and no consumer
     sees a new representation [D2.4]. The oracle (`direct_field_result_type`)
     was deliberately NOT widened — publishing the field's compile-time lane
     would make the *slow* arm unbox a value whose type may have changed.

   *Verification*: `make test-lambda-baseline` **3898/3898**; new behavioural
   test `test/lambda/proc/inferred_shape_guarded_read.{ls,txt}` (same-shape
   hit, second construction site, differently-shaped map through one read site,
   width-changing write ⇒ rebuild ⇒ miss, NULL-slot retag, container field,
   null receiver) with **JIT/interp differential parity** on every case — the
   interpreter never runs this path, so it is a free oracle. Isolated A/B on
   `temp/prof34/shape_literal.ls` (release, `LAMBDA_TIER=jit`, 5 alternating
   pairs): **135–154 ms → 78.4–79.0 ms, 1.72x**, and the after-side variance
   collapses.

   ⚠ **Coverage finding — the reason this bought no benchmark row.** Counting
   guard activations on a **debug** build (`log_debug` is compiled out in
   release — a release-build survey silently reports zero everywhere and is
   worthless): deltablue, list, havlak, cd, richards, splay, hashmap, deriv,
   gcbench all report **0 guarded sites**, and the A/B over those rows is flat
   (deltablue 81.6→81.8, list 0.741→0.742, havlak 50.0→49.4, cd 816→807,
   richards 2216→2232 ms). deltablue alone has **219** ANY-typed member reads.
   The shape is only visible where a literal-initialized local is read in the
   same scope; in every benchmark the object arrives through a parameter or out
   of an array, so the site has no constant to compare against. **T20-1c is not
   the third sub-item — it is the unlock, and should be taken next.**
2. **T20-1b — make the guard hit more often: stop cloning shapes per
   instance.** The guard only pays if instances share a `TypeMap`. Tune19
   §7.13 Route 1 (route Lambda's field retag through the existing
   `TypeMapTransition` / `is_transition_shared_shape` table already built for
   JS objects) is the prerequisite, and it independently cuts allocation:
   8000 per-instance `TypeMap`s → a handful. Land 1a first (it pays on
   non-transitioning maps immediately), then 1b to extend it to mutated
   graphs.
2b. **T20-1b — the premise was wrong, and measuring it produced the round's
   best result. LANDED 2026-08-26.**

   The track was written as "make instances share shapes", on Tune19 §7.13's
   finding that 8000 splay nodes got 8000 distinct `TypeMap`s. **That does not
   happen here.** Counting calls into `map_rebuild_for_type_change` — the only
   path that clones a shape — over full benchmark runs: **havlak 0, richards 0,
   splay 0, deltablue 40**. Instances built at one literal site already share the
   compile-time `TypeMap` that `transpile_map` embeds in their header, and the
   T20-1d retag mutates that shared entry in place rather than cloning. The
   §7.13 cloning was the *declared recursive contract* path, which §11.5 fixed.

   The second hypothesis was polymorphism — deltablue has four constraint
   constructors, so a site reading `c.kind` might see four shapes. Also mostly
   false: logging distinct candidates per propagated parameter position gave
   **`distinct=1` for all but one position in deltablue and one in havlak**.

   What the numbers actually said: deltablue ran **17 guarded reads against 219
   ANY member reads** — 8% coverage — and still made **2.2M**
   `map_get_by_name_id` calls. The limiter is not shape identity or shape
   count, it is that propagation only reaches receivers traceable to a
   construction site. Objects fetched out of an untyped array (`vec_at(v, i)`)
   or through a nested field read hand back ANY, and the chain dies.

   **The fix follows from what the guard is.** Because a header compare decides
   correctness, a candidate does not have to be *proven* to reach a site — it
   only has to be a good guess, and a wrong one costs a failed compare. The
   module's own literal shapes are exactly that evidence:
   `mir_module_unique_shape_for_field` scans `mt->type_list` and, when precisely
   one addressable literal shape declares the field name being read or written,
   offers it as the candidate. Restricted to a UNIQUE match on purpose —
   several shapes sharing a name would need a guard chain, which the
   `distinct=1` measurement says is not worth building here.

   *Effect*: deltablue guarded reads 17 → **63**, stores 2 → **13**; havlak
   23 → **50** / 13 → **22**; richards 5 → **18** / 7 → **14**; `map_get_by_name_id`
   on deltablue 2.2M → 1.6M.

   *Measured* (release, `LAMBDA_TIER=jit`, 3 alternating pairs each):
   **deltablue 78.3–80.5 → 71.6–73.5 (−9%)**, **richards 2128–2157 → 2009–2012
   (−6%)**, **havlak 44.6–45.6 → 42.1–42.5 (−6%)**, cd flat. Baseline
   **3899/3899**; both fixtures tier-identical; the retag fixture and deltablue
   pass the `FORCE_EVERY=1` + poison sweep with the widened coverage. No
   regression on fib2/cube3d2/binarytrees2/primes2/microdiff2/gcbench2/
   mandelbrot2/hashmap2.

3. **T20-1c — carry the shape to the access site. LANDED 2026-08-26.**
   deltablue/list/havlak pass
   nodes through `map?`/`any` parameters, so the *site-local* AST type at the
   member access is `map?`, and 1a's guard has no constant to compare against.
   The T19-4 closed-caller machinery (every call site known ⇒ join the
   argument facts into the parameter) applies unchanged with "fact" =
   per-site `TypeMap` instead of scalar lane. ⚠ The T19-4 ledger carries over:
   the *transitive* edge was built, measured, and rejected (geomean 1.02) —
   propagate one level from construction sites only, and treat "unknown yet"
   joins as generic, not as a veto to re-litigate. A join of two distinct
   shapes yields no constant, and the site simply keeps the generic call.
4. **T20-1d — `fn_map_set` fast path (the T19-6 remainder). LANDED 2026-08-26,
   and it is INERT on this corpus — read the finding before extending it.**

   *Why it was reachable at all.* The trusted-contract writer is gated on the
   binding's declared `full_type`, and instrumenting richards2's stores showed
   **every one of them with `full_type == NULL` and `cow_marked == 0`** — the COW
   uniqueness proof the fast path needs was already there; only the type was
   missing. richards2 declares `type Packet`/`type TCB` at the top and then never
   uses them on a binding: `var pkt = { link: null, identity: 0, … }` is an
   ordinary literal.

   *What landed.* Where the root binding has no declared type, is not
   `cow_marked`, and `mir_expr_candidate_shape` names a shape, the store is
   emitted behind the same four-step guard as the read (tag, non-null, container
   kind, header identity) with `fn_map_set` as the fallback arm. Both arms yield
   ItemNull, so only control flow converges. Stricter than the read by design: a
   store that changes a slot's storage class must run `fn_map_set`'s retag
   bookkeeping or GC stops tracing the right words, so this admits only writes
   whose value **already carries the slot's lane** (`exact_lane`) in a class an
   in-place retag cannot reach.

   ⚠ **And that requirement is why it never fires on the benchmarks.** In an
   untyped program the *values* are ANY too: richards' `pkt.identity = identity`
   assigns a parameter, deltablue's `variable.determinedBy = 0` assigns through
   `any`. The slot's lane is known (the literal wrote `identity: 0`, so INT); the
   incoming value's is not. Guard sites: **0 in richards, richards2 and
   deltablue**, 1 in a probe where the value is a literal. Verified correct
   there, and in the extended fixture (same-shape hit, differently-shaped map
   through one store site, and a retagging `r.v = "text"` write that must fall
   back and still read correctly), JIT/interp identical.

   **The runtime value-lane check LANDED 2026-08-26**, replacing `exact_lane`.
   `emit_item_runtime_lane_guard` walks the same ladder `Item::type_id` does —
   inline-double test first (its high bits alias the tag space), then the tag
   byte, then a container's own kind byte — and branches to the slow arm on a
   mismatch; `emit_raw_field_store_from_item` then decodes and stores. Two
   consequences worth stating: the RHS is now boxed **once, before the receiver
   is loaded**, and the slow arm reuses that register (evaluating it twice would
   run its side effects twice), and the boxed value is rooted across the receiver
   load because the RHS may allocate and move the map (D5.2). Out-of-band floats,
   sized numerics and decimals fail the test and take the generic setter —
   conservative in the safe direction.

   *Effect:* store guard sites 0 → **4 (richards), 5 (havlak), 1 (deltablue), 11
   (fixture)**, and time flat at that stage. Correctness verified including the
   retagging fallbacks, JIT/interp identical.

   ⚠ **Why it is still flat, measured rather than guessed.** Instrumenting all
   **60** of richards' member-assign sites: every one passes the outer gate
   (`cow_root` present, `full_type` NULL, `cow_marked` false, key an ident) **and
   every one now resolves a candidate shape**. The inner gate is what rejects
   them, and the dominant reason is `stype=1` — **the slot's lane is NULL**.
   That is not incidental: richards' hot mutations are exactly the link fields
   (`pkt.link`, `tcb.input`, `tcb.queue`), and an untyped literal writes them as
   `link: null`, so the shape records NULL and the first real write is a
   **retag**, which is the one thing this path must not do. deltablue's
   `variable.determinedBy`/`constraints` are the same story. Secondary rejects:
   `err=1` (the RHS may yield an error Item) and a few misaligned offsets from
   ANY-carrying neighbours.

   **The retag from the fast arm LANDED 2026-08-26**, which is what makes the
   NULL-lane link fields reachable. Three facts make it sound, and each was
   checked rather than assumed:
   - **The retag is always an UPGRADE.** `shape_entry_retag_is_safe` refuses only
     the downgrade *to* null (on a still-shared shape that would make GC skip
     live pointers held by siblings); every non-null target returns true
     unconditionally. Null values never reach this arm — they fail the lane
     guard — so the path can only upgrade.
   - **Widths do not move.** `type_info[LMD_TYPE_NULL]` is deliberately
     pointer-sized, commented in the table as being so "for NULL↔container
     transitions", so the packed layout is unchanged and no offset shifts.
   - **There is no refcount to maintain.** `map_field_decrement_ref` is a no-op
     in every arm — Lambda containers are GC-managed — so skipping the runtime
     setter's bookkeeping drops nothing.

   `emit_shape_entry_retag` publishes `type_info[kind].type` into the ShapeEntry,
   guarded by a compare so the shared cache line is only dirtied once; the lane
   guard admits the pointer-sized storable containers as one tag range
   (RANGE..OBJECT) and hands back the kind register the retag indexes with.

   *Verification.* Sites 4 → **7 (richards)**, 5 → **13 (havlak)**, 1 → 2
   (deltablue). New fixture `test/lambda/proc/guarded_store_null_slot_retag.{ls,txt}`
   builds a 300-node chain through exactly these writes and sums it back.
   **D8.6.3 sweep: `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`** — a
   collection at every allocation with dead payloads poisoned — returns the
   correct sum, which is the direct test that the retag published the pointer
   lane; the fixture and deltablue pass the same sweep, and havlak/richards/list
   pass it at `FORCE_EVERY=5`. Baseline **3899/3899**.

   *Measured* (release, `LAMBDA_TIER=jit`, 3 alternating pairs each):
   **havlak 49.0–49.2 → 45.7–45.9 (−7%)**, **richards 2192–2211 → 2165–2185
   (−1.2%)**, deltablue/cd/list/splay/gcbench flat.

5. **T20-1d (original text) — `fn_map_set` fast path.** Tune19 measured
   `fn_map_set` at ~46% of richards2 and its O(1) `field_index` attempt was
   **rejected at 1.060/1.065 with 0/11 wins** (§8.1). Its stated precondition —
   *a precomputed site hash or a NameId-keyed table* — is met by 1a's guard:
   behind the same header compare, a store lowers to a `byte_offset` write and
   needs no index at all. ⚠ The store side additionally owes the COW/uniqueness
   check the `:21172` path already performs (`cow_root->cow_marked`) — the
   guard proves *shape*, never *ownership*. Do not rebuild the dynamic index;
   land the static form.

**Acceptance:** deltablue typed ≤10x C2MIR, list ≤6x, havlak ≤10x, cd ≤6x;
the `shape_*.ls` inferred rows come within 2x of their declared counterparts
(from 33x); `map_get_by_name_id` and `typeditem_to_item` leave the
deltablue/list top-10 samples; `mir-check` fixture asserting **guard +
adjacent direct load** (`expect_seq` per §8.4-6) and a second asserting the
fallback still emits `fn_member_by_id`; a fixture pinning that a shape
transition takes the fallback arm (build a map, retag a field, re-read);
named-contract rejection tests and S5.4 map-equality tests unchanged.

### T20-2 — Strings: land the T19-7 design (base64, hyphen, knucleotide, fast_diff)

⚠ **SCOPE AUDIT 2026-08-26 — do not start from the plan below; two of its three
premises are already implemented and its third row is not string-bound.**
Profiled on the current build (`temp/prof34/b64.sample.txt`, `hy.sample.txt`):

1. **The owned builder already exists.** `String` carries `is_buffer`
   ("exclusively owned GC string-builder storage") and `transpile-mir.cpp`
   maintains `MirVarEntry::string_buffer_owned` for `s = s ++ x`; Tune18's
   E-slice landed it (base64 46.0 → 17.9). The "first deliverable" named below
   is done.
2. **The ASCII read side already exists.** `vibe/Lambda_Type_String_Tuning.md`
   records #3 `is_ascii` flag, #4 interned single-char ASCII table, #5 pointer
   identity in `fn_eq` as **Done** (json2 ~113x). `fn_string_ascii_at` shows up
   in the hyphen profile doing exactly the O(1) index that work bought.
3. **What is actually hot now:**
   - base64 — `fn_string` 68, `fn_strcat` 47, `memmove` 45, `it2s` 37. The
     call-tree parent of `fn_string` is **`fn_strcat` itself**: concat converts
     its operands through the generic Item→string path even when both operands
     are statically strings. The accumulator is fine; the **nested RHS chain**
     (`TABLE[a] ++ TABLE[b] ++ TABLE[c] ++ TABLE[d]`) materializes three
     temporaries per iteration before the owned append ever runs.
   - hyphen — **`lambda_type_matches` 449 and `is_truthy` 325 are the top two,
     ~31% combined, and neither is a string operation.** `fn_string`+`it2s` 440
     is Item→string conversion; the actual indexing (`fn_string_ascii_at` 150,
     `utf8_decode` 65) is a minority. hyphen is boundary-check-bound, i.e.
     T20-3/T20-4 territory, not byte-lane territory.

   So the two real string items are **(a) concat operand conversion** — teach
   `fn_strcat`/the `++` emission that a statically-typed string operand needs no
   `fn_string`, and **(b) concat-tree flattening** — build `a ++ b ++ c ++ d`
   into one buffer instead of a left-nested chain of temporaries. Both are
   representation questions of the same family as T20-3, not new storage design.

**Both resolved 2026-08-26.**
- **(b) was already implemented.** `mir_emit_string_concat_item` flattens a
  nested RHS join into the same exclusive buffer (its own comment records the
  earlier one-level lowering that froze each inner temporary). No work needed;
  the audit's reading of the profile was right but the fix predates it.
- **(a) LANDED.** A statically-string operand now decodes through
  `emit_text_pointer_lane` instead of calling `fn_string`. Two gains, one of
  them structural: the call disappears, and because the decode allocates
  nothing the emitter no longer has to treat that point as a safepoint and
  reload the accumulator's root afterwards. ⚠ **Timing is FLAT** at 3 pairs
  (base642 12.2–13.2 → 11.6–12.4, fast_diff2 unchanged): the `shr` fix (#3 of
  the T20-3 set) had already taken base64 from 16.5 to 11.4, and what remains
  is `fn_strcat`'s own `memmove`. Kept because the emission is verifiably
  smaller and the ratchet now pins it, not because it bought time.
  Emission tests: `tune15_string_builder.mir-check` updated (D8.6.1 — a
  justified ratchet *reduction*; `fn_string` moved from `expect` to `forbid`),
  plus new `tune20_concat_operand_conversion.{ls,mir-check}` which pins the
  DISCRIMINATION — `fn_string` must still be emitted for an `int` operand in
  the same loop, so a future over-elision that concatenates raw bits fails a
  test rather than a benchmark. 63/63 emission tests, baseline 3903/3903.

*Original plan (superseded by the audit above):*

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

**One slice landed 2026-08-26: the return-contract boundary elision.**
Instrumenting `emit_checked_boundary`'s site strings on cube3d2 showed what the
crossings actually are — `function return`, `declaration 'm'`, `assignment to
'mqube'`, `argument 1 of _mat4_mul_526`, `parameter 'v1' of _calc_cross_2998` —
every one with `expected_tid=23` (the `float[]` occurrence) and **`val_tid=25`
(ANY)**. The contract is known on both sides; only the *producer's
representation* is unknown, which is this track's root cause in miniature.

A local function with an explicit return contract already emits its own
`function return` boundary, so re-admitting its result at the caller is a second
check on a value the callee proved one frame earlier. `mir_boundary_is_redundant`
now elides it when the callee's `return_contract` is the **same `Type*`** as the
target (contract identity, not structural similarity), the contract is explicit,
and the callee cannot raise. cube3d 11.59 → 11.28 ms on top of T20-4's win.

**Root cause LOCATED for microdiff 2026-08-26 — a 5.4x isolated repro, and an
attempted fix that had to be REVERTED. Read this before starting.**

Profiled on the current build, microdiff is `fn_add` + its out-of-line arm
**50.7%**, `lambda_numeric_boundary_admit` **17.9%**, `fn_len` 9.8%, JIT code
15.7%. The hot line is
`score = score + 19 + type_lengths[slot] * 23 + path_lengths[slot]`, where both
arrays are module-level `let ...: int[]`.

**The repro isolates it to module scope, nothing else.** The identical function,
with the two arrays moved from module scope into the enclosing `pn` — same
source otherwise, same checksum — runs **0.166 ms vs 0.896 ms, 5.4x**
(`temp/prof34/md_fnlocal.ls` vs `md_modlet.ls`). Passing them as `int[]`
parameters is likewise ~6.5x faster than reading them as globals. So microdiff's
53x C2MIR gap is, in large part, one missing representation.

**Where it goes wrong.** `transpile_index` gates every typed-array fast path on
`obj_tid == LMD_TYPE_ARRAY_NUM`. For a module binding the carrier oracle reports
**`LMD_TYPE_ARRAY` (18)** — a declared `int[]` global is published as a *generic*
array — so the read falls to `emit_boxed_index_call`, hands back an Item, and
every arithmetic consumer downstream dispatches through `fn_add`.
⚠ Note `mir_module_binding_has_native_lane(..., ARRAY_NUM)` answers **no** here,
because it compares against the same ARRAY carrier; it is not the hook to use.

**LANDED 2026-08-26 (second attempt), and the first attempt's diagnosis was
wrong about where the crash lived.** The lldb backtrace pinned it:
`fn_numeric_binary` faulted at address 0x6 — a **raw native int passed as an
Item**. The slot load was fine all along; the crash was the emitter and the
carrier oracle disagreeing. The first attempt derived the element at the gate,
inside `transpile_index`, so the fast path produced a native lane while
`mir_expr_carrier_type` — which consults `mir_known_index_element_type` — kept
answering ANY, and every consumer boxed the raw lane as an Item. The comment at
the oracle's INDEX_EXPR arm ("keep this witness aligned with transpile_index")
is the law that was being broken.

*The fix puts the witness in the shared oracle.* `mir_known_index_element_type`
gained a module-binding arm, mirroring its existing PARAM-recovery arm: an
immutable, un-widened module `let` whose declared (or, via `mir_named_contract`,
inferred) contract is `int[]`/`int64[]`/`float[]` answers its element type.
`transpile_index`'s gate then *consumes that same oracle* to admit the ARRAY
carrier with `obj_elem_guarded` — so emission and planning cannot disagree by
construction. BOOL is excluded (its module carrier is a boxed-items Array with
no guarded ArrayNum path). Safety is the elem guard: kind/layout/elem re-checked
on the loaded container, `item_at` slow arm whose result
`emit_index_result_move` converges back into the lane, OOB → `INT_LANE_NULL`.
A drifted representation (the G1 push-on-module-let hazard) takes the boxed arm.

*Measured* (release, `LAMBDA_TIER=jit`, 3 pairs): **microdiff2 0.90–1.41 →
0.23–0.53 ms (~4x)**; untyped microdiff 1.24 → 0.28 (the inferred-contract
fallback covers un-annotated lets); crypto_sha12 −4.5% (single pair,
unconfirmed); fast_diff2's apparent +12% **dissolved at 3 pairs** (§8.4-2
again); deltablue/havlak/base64/fasta/hyphen/brainfuck/json2 flat. Isolated
repro at parity: module variant 1.96 → 0.231 ms vs fn-local 0.25 ms.
Fixture `test/lambda/proc/module_array_lane.{ls,txt}` (declared int[]/float[],
un-annotated let, dynamic index, OOB → null), JIT/interp identical; baseline
**3900/3900**. (Learned en route: module-level `var` is a compile error E224,
so the mutable-exclusion in the oracle arm is defense in depth, not a live
case.)

**Remaining scope, re-profiled post-fix (2026-08-26).** microdiff is now
**63% JIT code** (was 16%) and its entire residual runtime cost is **`fn_len`
35.7%** — `len()` of a module array is still a boxed sysfunc call per loop
iteration; an inline length load under the same module witness is the next
slice. hashmap2's profile is **unchanged by the whole round**: boxed bitwise
(`fn_band_item` 9.8% + `fn_ushr_item` 4.8% — the bitwise family never joined
the int-lane planning that `+`/`-` have), generic index chains through
container-typed record fields (`fn_index` 8.8% + `item_at` 6.4% +
`array_num_get/read_item` 10.3% + admission ~10% — `h.keys` is a declared
generic `array` field, so its element is never known), map write path
(`cow_path_set_raw` region 11.1%), and compares (`total_cmp` 7.4% +
`fn_sym_eq_ptr` 6.1%). And deltablue's remaining gap is the member-read
result lane: T20-1a's fast arm deliberately boxes because the member oracle
publishes ANY — extending the one-oracle pattern to member exprs on guarded
shapes is what converts its 63 firing sites into time.

**Items #1, #3 and #4 LANDED 2026-08-26; #2 (member-result lane) REFUSED on a
soundness argument worth keeping.** All measured at 3 alternating pairs,
baseline **3901/3901**, tier differentials on every new fixture:

- **#1 `len()` inline** (`shr_literal…` sibling fix first found the real
  blocker: the module ident carries the ANNOTATION meta-type, LMD_TYPE_TYPE,
  so none of the existing native len arms fired). Inline length load at offset
  16 under the kind/layout guard; ndim/view bail to the call because their
  `len` is shape[0] (`array_num_iter_count`), as does any non-witnessed
  container. microdiff2 0.24 → **0.16 ms** on top of the module-lane 4x.
- **#3 literal-count `shr`** — the boxing exists for two cold arms a literal
  count 0..63 kills statically (negative-count error; there is no overflow arm
  on a right shift, unlike shl). One predicate,
  `mir_shr_native_literal_count`, consulted by the classifier veto, the
  carrier oracle arm and the lowering — the one-oracle rule, third
  application. The veto had also been declassifying every ENCLOSING bitwise
  call, so `bxor(h, shr(h, 16))` paid two boxed helpers per hash.
  **hashmap2 57–58 → 45–49 ms (−18%)**, **base642 16.5–20.2 → 11.3–11.5
  (−31%)**. Fixture `shr_literal_native_lane.{ls,txt}` pins signed negatives,
  count 0/63, chained consumers, and a dynamic count staying on the guarded
  boxed helper.
- **#4 guarded element witnesses generalized** — `mir_guarded_array_num_witness`
  consolidates three sources no local lane proof covers: (a) module `let T[]`
  (the #1/module-lane case), (b) `rec.field[i]` where a trusted record declares
  `field: T[]`, (c) a local declared FROM such a member read
  (`var vs = hm.values`), installed guarded at the declaration. Consulted by
  `mir_known_index_element_type` (so the carrier oracle agrees), the
  `transpile_index` gate, and the `len()` arm. **havlak 42.6–43.0 → 37.8–38.0
  (−12%)**. ⚠ Retyping hashmap2's record to `keys: int[]` to feed arm (b) was
  attempted and **REVERTED — it changes behavior on both tiers** (result
  −450000 vs 18900000): a `T[]` field contract reifies on admission, detaching
  `var hm_keys = hm.keys` from the record, and the algorithm's writes vanish —
  the same aliasing law as the object graphs (§2.3). Arm (b) is therefore
  correct but has no benchmark customer; arm (c) fires off the generic `array`
  fields' runtime ArrayNum representation instead.

**#2 — why the member-result lane is NOT landed.** The offset and the result
type have different proof obligations. An offset claim is guardable per access
— a wrong candidate shape fails the header compare and takes the generic arm.
A RESULT-TYPE claim is a static fact consumers cannot check: if the oracle
says INT on a guessed candidate and the guard misses onto a shape whose field
holds a string, the slow arm must still produce an int lane — a forced
mis-decode with no fallback arm, i.e. a wrong answer, an S1.6 violation. The
index path never has this problem because its witnesses are CONTRACT-backed
(a declared `T[]` makes a wrong-typed element an upstream contract violation);
T20-1b's member candidates are guesses, sound only for offsets. The sound
subset (static-receiver literals) covers nothing the benchmarks do. The real
mechanism is the **companion-lane pair** (return-convention v3's value+lane
form) — carry both representations to the consumer — which is the general
ValueRep design, not a slice. Do not re-attempt #2 as a slice.

The rest of the general item — giving call results and container reads a
ValueRep so int chains stay in the int lane — is otherwise untouched. The sys-func
`success_type` audit is still the other cheap opening (⚠ T19-B: `int()`'s row
published the looser `&TYPE_NUMBER`, which made every store look
representation-changing).

*Original proposal follows.*

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

### T20-4 — Typed-array admission (cube3d, raytrace3d, hashmap's array side) — **LANDED 2026-08-26, and the name was wrong**

⚠ **The track was mis-scoped and the profile corrected it.** "Hoist the
per-element admission out of the emitter" assumed the cost was one admission per
element ACCESS. It is not: the cost is one admission per BOUNDARY CROSSING, and
that admission then walks every element itself. `lambda_type_matches` →
`validate_against_type` → `validate_occurrence_type` on a `float[]` contract runs
the schema validator's element loop, so admitting a 16-element matrix costs 16
checks, and `mat4_mul(m1: float[], m2: float[]) float[]` pays it three times per
call. The emitter was never the site (§8.3's repeated lesson, third instance this
round).

**Fix: an ArrayNum IS its element representation.** Every slot is stored in the
packed lane the header's `elem_type` names, so walking the elements to learn what
the header already states cannot discover a disagreement. `validate_against_array_type`
now short-circuits when a plain (non-ndim, non-view) ArrayNum's lane equals a
simple nested contract's TypeId — fast mode only, because the reporting validator
owes a per-element diagnosis rather than a verdict [D2.6.1, D3.2.1]. This is the
same argument as Tune19 §7.13's map shape-identity short-circuit, applied to the
array side. Compact sized lanes (int8/uint16/…) keep the walk.

**Measured**: cube3d 12.66 → 11.59 ms (**−8.5%**); raytrace3d, hashmap, nbody,
spectralnorm, ray all flat. The modest result is itself the finding — see §2.5:
after this, cube3d is **allocation**-bound, not validation-bound, which is what
opens T20-5.

*Original proposal text follows; the per-element emitter work it describes is
still unstarted and now looks lower-value than T20-5 for these rows.*


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

At Result34 time, binarytrees typed was **2.10x its own untyped row** (19.0 vs
9.04 ms) — the worst live violation of the bar (*an annotation may never cost
>5%*). **RESOLVED by the T20-6a chain without emitter work:** re-measured
2026-08-25 post-`ad7a5664b` (release, `LAMBDA_TIER=jit`, 3 runs), binarytrees2
typed is 4.3–6.1 ms vs untyped 8.9–9.6 ms — the tax inverted into a **~0.55x
win**; the R34 cell was measured with its `Node?` fields ANY-degraded (defect
1 below) and both admission defects live. Remaining ledger: bounce (0.113 vs
0.069), ack (14.4 vs 10.9), splay's residual 1.14x — re-measure on a post-fix
build with §8.4-2's ≥15 pairs before touching the emitter; per §8.3, reproduce
each emission on a two-line probe first.

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

**Status after the resolution fix (`7348aa111`, re-tested 2026-08-25):**
resolution works, but the recursive-record path behind it was broken on HEAD:
(1) minimal 14-line recursive type (`temp/prof34/rec_min.ls`: `type N = {val:
int, next: N?}` + build/sum a 100-node list) **SIGSEGVed the release MIR JIT**
(exit 139); (2) splay2 with `SplayNode?` links was reported to hang;
(3) list2 with a recursive `ListElem` type was reported 14.7x slower than
`map?` — adoption/shape-identity not engaging. ⚠ The interpreter tier masks
all of this, so verify on `LAMBDA_TIER=jit` release.

**T20-6a — recursive-record JIT segfault: ROOT-CAUSED AND FIXED 2026-08-25.**

**Root cause — one bug, spelled twice.** `N?` is a `TypeUnary` whose OWN
`type_id` is `LMD_TYPE_TYPE`, not `LMD_TYPE_MAP`. Five places tested the
wrapper's `type_id` against `LMD_TYPE_MAP` and therefore dropped the map arm of
every *optional* named contract — and a self-referential record is
**necessarily** optional, since its recursion has to terminate on null. So the
crossings that REIFY a value into the contract's packed layout were all
skipped:
- `transpile-mir.cpp`: the return firewall, both call-argument sites, and the
  assignment site (`mir_decl_type_id(...) == LMD_TYPE_MAP` / `contract->type_id
  == LMD_TYPE_MAP`);
- `lambda-eval.cpp` `runtime_type_admit_value`: the map-contract block guarded
  on `expected->type_id == LMD_TYPE_MAP`, so a `T?` value fell through to the
  `lambda_type_matches` shortcut and was admitted **structurally, unchanged**.

Meanwhile `emit_mir_direct_field_read` *does* index by the contract's
`byte_offset`. So `make()` returned a map built from the literal's own inferred
shape — in which the `N?` field classifies ANY and occupies a 9-byte
`TypedItem` (1-byte tag + 8-byte payload) — while `total()` read that slot as a
bare 8-byte `Container*`. The value it produced was literally `(ptr & mask) <<
8 | 0x13` (tag byte `0x13` = `LMD_TYPE_MAP` plus seven pointer bytes); the next
`lambda_type_check` dereferenced it in `get_type_id` and died. Debug and
release both fault (ASan `BUS` at `lambda-eval.cpp:1705`) — the "debug works"
reading in the brief does not reproduce.

**Fix.** One shared predicate `mir_map_contract_needs_reification()` replaces
the four open-coded transpiler tests, and `runtime_boundary_nonnull_map_arm()`
reaches the contract through `N?` / `N | null` on the runtime side. Two
refinements matter and are load-bearing:
- the predicate compares the non-null **arms**, not the wrappers: `Node?` on a
  field, a parameter and a return are three occurrence objects around one
  TypeMap, so wrapper identity would re-admit every hop of an adopted structure;
- the open `map` singleton is excluded — it implies no packed layout, so
  reifying there is pure cost (it cost list2 1.7x before exclusion).

This makes the invariant inductive: a value acquires the contract's static type
only by adopting it at construction or by crossing a boundary that reified it.

**T20-6b — nullable pointer-lane locals (pre-existing, found by the new test,
FIXED).** `mir_can_inline_null_item_test` compares against `ItemNull`, but
`transpile_box_item`'s IDENT arm boxed through the contract lane only when the
binding had a `full_type` — which an UNANNOTATED local never has. So `var walk
= node` off a `Node?` parameter held lane null as raw `0`, `walk != null`
compared `0` to `ItemNull` and answered **true**, and every idiomatic `while (n
!= null) { n = n.next }` walk ran one step past the end. Fixed by falling back
to the node's static type for the **boxing decision only**. Publishing the same
fact as the binding's `full_type` also reroutes the redundancy, direct-read and
null-guard gates that read it — measured at **3.6x on list2**, so do not.

**Measured after the fix** (release, `LAMBDA_TIER=jit`, quiet machine, equal
9000-rep drivers):
| row | before | after |
|---|---:|---:|
| `rec_min.ls` | SIGSEGV (139) | `sum = 5050` |
| list2 (`map?`) | 6208 ms | 6599 ms (+6%, the null-lane decode) |
| list2_rec (`ListElem?`) | SIGSEGV | **1826 ms — 3.6x faster than `map?`** |
| splay2_typedlinks (8000) | 155.6 ms | 149.8 ms |

So (3) inverts: the recursive record is now **3.6x faster** than the `map?`
original, not 14.7x slower — adoption/shape identity is engaging. Symptom (2)
**does not reproduce at HEAD**: both `splay2_typedlinks.ls` (8000 nodes) and
`splay2_tl_small.ls` complete on the pre-fix release binary too.

**Sources retyped 2026-08-25** (the §4 "do not retype" hold is lifted for
these two; the parked variants were adopted verbatim):
- `test/benchmark/awfy/list2.ls` — all five signatures `map?` →
  `ListElem?` over `type ListElem = {val: int, next: ListElem?}`. Same release
  binary, 9000-rep driver: **6597 → 1726 ms (3.8x)**. Golden `List: PASS`
  unchanged; `awfy_list2` green. §4's "keep a dynamic-map variant as
  untyped-path coverage" is satisfied by `test/benchmark/awfy/list.ls`, which
  is fully unannotated and still builds dynamic maps.
- `test/benchmark/jetstream/splay2.ls` — one line: `left`/`right` `map?` →
  `SplayNode?`. `value` stays `map?` deliberately (arbitrary payload, no fixed
  layout). 8000 nodes: 141.8 → 136.7 ms, i.e. **flat within noise** — do not
  quote this as a win without the §8.4-2 ≥15 paired runs. splay2 has no `.txt`
  golden and jetstream is not in the auto-discovered dirs, so it is
  benchmark-only; PASS verified by hand on both tiers.

`make test-lambda-baseline` 3897/3897 after both edits. The R34 list2/splay2
cells were measured on `map?` sources and now describe a different program —
re-measure before trending either row.

**Coverage.** `test/lambda/proc/recursive_record_links.{ls,txt}` (behavioral,
chain + tree + annotated root + nullable reassignment) and two **tier-pinned**
tests in `test_lambda_opt_gtest.cpp` —
`UnadoptedRecursiveLiteralReifiesOnJit` / `...MatchesInterp`. The tier pin is
essential: the auto tier answers correctly on the pre-fix binary, so the `.ls`
test alone would not have caught this. The pinned pair crashes the archived
pre-fix binary and passes now. `make test-lambda-baseline` 3897/3897.

⚠ `RecursiveContractJitFullyStatic`'s old `map_admit_calls == 0` assertion is
gone: it encoded "the runtime admission path is never entered", which was only
true because `T?` crossings silently bypassed the classifier via
`lambda_type_matches` — the bypass that caused this segfault. The counters now
show 20 calls / 20 `exact_shape_hits` / 0 reifications / 0 bytes copied on JIT
and 60 / 60 storage-compatible on interp: every crossing classified trusted and
free. Assert **trusted-and-zero-copy**, never zero-calls.

**Second probe drift — RESOLVED with T20-6a, fixture still owed.** `var b: N
= …; b.next = a; a.k = 99` aliased on v33/v34 (`b.next.k == 99`), **detached**
on the interim HEAD between the resolution fix and T20-6a, and **aliases again
(99) post-`ad7a5664b`** — consistent with T20-6a's inductive invariant: `a`
adopts `N`'s shape at construction, so the store is a same-shape trusted
crossing (zero-copy), not a reifying boundary. ⚠ The alias/copy line has now
moved twice across builds within one week; `temp/prof34/alias_probe8.ls` (and
probes 5/7) should be promoted into pinned C4.1 fixtures so the next drift
fails a test instead of a benchmark investigation.

**Acceptance:** no row where typed > 1.05x untyped on the fixed population.

**Ledger re-measured 2026-08-26** (release, `LAMBDA_TIER=jit`, post-T20-1/T20-4):

| Row | untyped | typed | tax | vs R34 |
|---|---:|---:|---:|---|
| beng/binarytrees | 9.14 | 4.70 | **0.51x** | was 2.10x — RESOLVED |
| awfy/bounce | 0.092 | 0.117 | 1.27x | was 1.64x — improved, still violating |
| r7rs/ack | 10.68 | 14.30 | 1.34x | was 1.32x — unchanged |

Two rows still violate. Neither is new: bounce is T19-B/T19-C (a declared `int[]`
read demoted to a guarded call, and an int-lane compare lowered through `double`)
and ack is T19-9, the call boundary on scalar recursion, which Tune19 left
unopened.

**Investigated 2026-08-26; NOT fixed, and the blocker is now precise.** Release
profiles of the two bounce variants: untyped is 77% JIT code with a clean tail
(fmod 11%, fn_abs 4%); typed is 61% JIT code plus a **contract/admission family
worth ~14%** — `lambda_type_check` 5.7%, `cow_path_set_raw` 3.0%,
`lambda_numeric_boundary_admit` 2.2%, `cow_item_is_container` 1.7%,
`validate_against_type` 1.5%. Instrumenting the boundary site strings names the
recurring one exactly: **`parameter 'seed_arr' of _random_next_68`**, an `int[]`
re-admitted on every call in the hot loop, against **zero** boundaries in the
untyped build. That is the annotation tax, one site.

The elision that should cover it — the array-witness arm of
`mir_boundary_is_redundant` — is **unreachable for two independent reasons**,
both now recorded at the code: (1) it sits inside a block gated on
`target is LMD_TYPE_MAP`, and an `int[]` target unwraps to the occurrence TYPE;
(2) it requires `declared == expected`, POINTER identity on the contract, which
holds for named map aliases (one shared `Type*`) but never for two separately
written `int[]` annotations. Hoisting it past (1) was tried and reverted — (2)
still blocks it, verified on a probe where even an immutable `let a: int[]`
passed to a same-spelled `int[]` parameter is not elided.

⚠ And the fix cannot simply relax to structural equality for bounce's own case:
`seed_arr` is a **`var` parameter that is `push`ed**, so its representation can
genuinely change and `lambda_type_check` there may legitimately REJECT —
eliding it would skip a real error, not redundant work. So the fix is two
rulings, not a tweak: a structural container-contract equality (same occurrence
kind, same simple element TypeId) for the immutable case, and a separate
decision about mutable borrowed containers. ack (T19-9) was not opened.

⚠ Also confirmed and closed while here: **`int()`'s `&TYPE_NUMBER`
`success_type` is HONEST, not the T19-B defect it was filed as.** `fn_int`
returns `push_d(dval)` — a float — above INT32_MAX and `box_int64_value` for
out-of-range sized ints, so the row accurately describes the function and
tightening it to `&TYPE_INT` would be unsound. The identity case
(`int(x)` where x is already INT) is *already* handled in both the carrier
oracle and the emitter. Do not re-file this as a sys-func audit item; the real
question underneath is whether `fn_int` should return int across the v5 int53
range at all, which is an S4 semantics ruling.

## 4. What NOT to do (inherited decisions)

- **No inline caches, no patched code** — D8.4.1v2 covers both lanes. Every
  fast path in T20-1 is a static lowering behind a compile-time-constant guard.
- **No dynamic O(1) shape index** over `TypeMap.field_index` (Tune19 §8.1:
  1.060 at 0/11 wins). Its precondition is met only by the *static* form
  (T20-1d).
- **Do not "add shape interning" for map literals.** They already have
  complete per-site `TypeMap`s in the module type list, and the pointer is in
  the object header at offset 8 (verified 2026-08-26 — see T20-1). The work is
  the trust/guard, not the shape.
- **Do not set `is_trusted_contract` on inferred literal shapes.** The flag
  means "admission guarantees these bytes"; setting it without admission would
  make the direct read unsound the moment a differently-shaped map reaches the
  site. Widen the gate with a runtime guard instead (T20-1a).
- **No transitive closed-caller edges** (§8.1, geomean 1.02). One level from
  construction sites.
- **No typed contracts on the graph benchmarks' sources this round.** Not a
  semantic law (see §2.3's correction) — but typing the holders *today*
  changes observable sharing (probe7) and therefore program behavior; the
  sources stay as they are until the C4.1 catalog closes. (list2 is the
  exception: its list is immutable after construction, so a recursive record
  type is behavior-preserving post-Tune19 §11.5 — permissible, but keep a
  dynamic-map variant as untyped-path coverage. **list2 and splay2 were
  retyped 2026-08-25 once T20-6a landed — see §T20-6.** The rest of the hold
  stands.)
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
