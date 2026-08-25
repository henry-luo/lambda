# Tune 16: Result25 Analysis, Typed-Source Audit, and the Next Typed Round

- **Date:** 2026-08-08
- **Input:** `test/benchmark/Overall_Result25.md` (Lambda commit `acb46fb4d`, archived binary
  `test/benchmark/exe/lambda-v25-812ddaef0b`), `test/benchmark/Overall_Result24.md`,
  `vibe/impl/Lambda_Impl_Tune15.md` (§8 implementation record), full typed-source audit of all
  55 `test/benchmark/*/[name]2.ls` files (§3, 2026-08-07)
- **Status:** IMPLEMENTED — C0–C7 code, source repair, regression coverage, and verification gates complete; measured target status is recorded in §6
- **Related:** `vibe/impl/Lambda_Impl_Tune15.md`, `vibe/impl/Lambda_Impl_Tune14 (done).md`,
  `vibe/Lambda_Tune_Typed_Vs_C2MIR.md`, `vibe/Lambda_Design_Name_Identity.md`,
  `vibe/Lambda_Design_Type_Enforcement.md`, `vibe/impl/Lambda_Impl_Tune13.md` (R22 typed-store
  dissection), `vibe/impl/Lambda_Issue_Type_Support (retired).md` (TS-3 ANY downgrade)
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1, S4.5.3;
  `doc/Lambda_Formal_Design.md` D2.2.2, D2.4, D3.2.1–D3.2.2, D3.3.1–D3.3.3, D5.2–D5.3,
  D8.3.2–D8.3.3, D8.4.1, D8.6.1–D8.6.3

## 1. What Result25 says

| Metric | Result24 | Result25 |
|---|---:|---:|
| MIR (untyped)/Node geo | 2.65x | **2.32x** |
| MIR (typed)/Node geo | 1.68x | **1.32x** |
| MIR (typed)/C2MIR geo | 8.19x (44 rows) | **7.14x** (47 rows) |
| LambdaJS/Node geo | 15.5x | 16.0x |
| QuickJS/Node geo | 7.22x | 7.30x |

Tune15 essentially met the typed/Node ≤1.3x target (1.32x on the enlarged 59-row set) and
missed the typed/C2MIR ≤5x target. Composition caveat on the ceiling metric: Result25 adds
the Text suite, whose three rows land at 54x/48x/42x C2MIR and pull the 47-row geomean up to
7.14x — on the population Tune15 §8 measured, the gap is ~6.3x, so the metric moved backward
without a regression in the measured rows.

### 1.1 Delivered (v24 → v25 typed)

spectralnorm 15.4→2.65 ms (now **1.01x Node**; the Tune15 §2.2/F2 boxed-math family is
confirmed cleared), permute 1.40→0.131 (−91%), towers 2.08→0.566 (−73%), storage 1.45→0.473
(−67%), collatz 930→440 (−53%), fft −41%, puzzle −37%, and another −25–30% across the
recursion family (fib 0.74x Node, tak 0.19x, cpstak 0.30x, divrec 0.28x).

### 1.2 Targeted but did not move

| Row | v24 → v25 typed | Now | Tune15 slice that claimed it | §3 audit says |
|---|---|---:|---|---|
| crypto_sha1 | 226.8 → 228.2 (flat) | **24.3x Node — worst typed row** | B1.2 conversion lowering | `w`/`x` word arrays untyped (len()-int64 bug); `input_len` untyped ("widens to decimal") |
| nbody | 30.3 → 30.3 (flat) | 20.0x C2MIR | B2.2 witness pass-through; B5 audit | canonical awfy source: inner pair-loop floats `dx..mag` untyped; no return types |
| base64 | 67.1 → 69.7 (flat) | **120x C2MIR — widest gap** | B4 string builder | `TABLE` untyped Item array indexed 4x/iter; source otherwise well typed |
| deltablue | 94.5 → 88.7 (−6%) | 7.23x Node | B3 fixed-shape records | **canonical awfy source has NO record types** (`type Vec = any` no-op aliases) |
| gcbench | 253.6 → 255.8 (flat) | 9.24x Node | B3 | node fields `map?` not `Node?`; no return types on hot recursives |
| levenshtein | 22.8 → 21.9 (flat) | 23.9x C2MIR | B1 | DP rows `prev`/`curr` untyped (ANY re-tag workaround) |

The pattern: every Tune15 slice that worked, worked where its proof engaged. These rows are
where the landed mechanism **exists but has nothing to key on — because the source cannot
safely carry the annotation** (§3). Tune16 is therefore an *engagement* round whose first
phase is compiler-side annotation repair, not new machinery.

### 1.3 Regressions inside the Tune15 window — ROOT-CAUSED (2026-08-07)

**raytrace3d typed 159.3 → 261.9 (+64%): caused by `a6aabd467 tune15 impl`, mechanism
confirmed = witness-demotion to the boxed wrapper.** Bisect evidence: a build at
`903b737d3` (the commit immediately before tune15 impl) reproduces v24 exactly —
`map_admit_calls` 362,688 and ~167 ms — while v25 measures **631,736 admissions (+74%)**
and 234 ms on the identical script (`COW_EXEC_PROFILE=1`, counters in
`temp/prof16/cow_*.tsv`). MIR dumps from both archived binaries
(`LAMBDA_MIR_DUMP_PATH`, `temp/prof16/raytrace_v2{4,5}.mir`) show the chain:

1. B2.2's typed-array witness pass-through added an `i64:_array_witness` parameter to
   the raw fast entries of `triangle_intersect` and `scene_intersect`.
2. Call sites whose `float[]` argument witness is not statically provable no longer
   satisfy the raw-entry contract and **silently demote to the boxed `_b_` wrapper**:
   v24 had raw calls on all edges (triangle 3 raw/0 boxed, scene 2/0); v25 flipped the
   per-pixel `render_scene → scene_intersect` edge and the per-shadow-ray
   `scene_intersect → triangle_intersect` edge — the two hottest edges — to boxed
   (2/1 and 1/1). The demoting arguments are the untyped `vec3()` vector locals the
   §3 audit flags (D-a workaround).
3. The boxed wrapper re-admits **every** declared parameter per call, so each demoted
   call pays a full `lambda_map_contract_relation` walk on its Triangle/Scene record
   params. Top leaf in both profiles, 319 → 446 samples; static `lambda_type_check`
   site count is unchanged (159 in both dumps) — the regression is pure edge routing,
   not new check emission.

Aggravator (pre-existing, now the dominant cost): `map_admit_exact_shape_hits` is **0**
in both versions — all 631k admissions conclude `MAP_CONTRACT_STORAGE_COMPATIBLE` after
a full shape walk, and the relation is never memoized. See C4.1/C3 fixes.

**Secondary regressions:**
- raytrace3d **untyped** +18% (295 → 328): mostly the dedup commits (the `903b737d3`
  build already measures ~320 ms), small tune15 residue; different mechanism (untyped
  code never enters map admission). Unattributed beyond commit range; lower priority.
- pnpoly typed +11%: same pre-existing `lambda_numeric_boundary_admit` family (623 vs
  688 leaf samples); v25 adds `it2d`/`lambda_type_lane_storage_desc` leaves — a modest
  widening of the F1 residue, not a new mechanism.
- splay typed +8%: map admissions identical (71,995 both) — not admission-related;
  likely alloc/GC cadence noise; re-measure after C1/C3.
- quicksort typed +11%, cd untyped +9%: not yet root-caused; fold into C4.1's
  verification sweep.

### 1.4 The ≤5% annotation-bar ledger (9 rows in v25)

| Row | typed/untyped | §3 audit correlate |
|---|---:|---|
| awfy/bounce | **+173%** (0.795 vs 0.291) | no return types on `random_next`/`benchmark`; RNG state = 1-elem `int[]` checked per call |
| text/hyphen | +63% (4.29 vs 2.64) | hot string accumulator untyped; `hyphenate` no return type |
| kostya/brainfuck | +41% (452 vs 321) | tape untyped (deliberate); typed `jumps: int[]`/`output: string` cross checked boundaries |
| jetstream/splay | +41% (221 vs 157) | node literals bare despite declared `SplayNode`; zero return types |
| r7rs/nqueens | +35% (2.41 vs 1.78) | no return types on hot recursives; `fill(1,0)` per recursive call |
| beng/knucleotide | +10% (5.18 vs 4.71) | `counts = map()` untyped; `k`/`entries` params untyped |
| kostya/json_gen | +10% (21.5 vs 19.5) | fully typed — genuine runtime cost (string accumulator path) |
| awfy/list | +9% (0.924 vs 0.845) | node params only `map?`; bare `{val,next}` literals |
| kostya/matmul | +9% (14.2 vs 13.0) | fully typed — genuine runtime cost (checked `float[]` stores) |

Seven of nine correlate with source-level typing defects or workarounds; only json_gen and
matmul are cleanly-typed programs where the runtime itself still charges for the annotation
(the C1 checked-store family). The ledger cannot be closed by runtime work alone.

### 1.5 Worst remaining typed rows

vs Node: crypto_sha1 24.3x, navier_stokes 16.7x, fast_diff 14.3x, cd 13.8x, brainfuck
13.2x, raytrace3d 13.0x, pnpoly 11.0x, splay 9.49x, gcbench 9.24x, deltablue 7.23x.
vs C2MIR: base64 120x, microdiff 54x, hyphen 48x, list 42x, fast_diff 42x, paraffins 35x,
pnpoly 35x, quicksort 32x, bounce 31x, queens 28x, levenshtein 24x, nbody 20x.

Cross-check against §3 verdicts: of the ten worst typed/Node rows, **seven are UNDER-TYPED
sources** (sha1, cd's tree layer, brainfuck, raytrace3d, splay, gcbench, deltablue-awfy).
The typed column's tail is substantially measuring untyped code.

### 1.6 Long-window check: rows slower than Result18 (2026-07-29, pre-enforcement)

Against R18 (commit `e406aa9b87`, the last report before the type-enforcement round),
**21 untyped and 19 typed rows are >5% slower in v25** despite both headline geomeans
improving (untyped 2.55x→2.32x, typed 1.87x→1.32x). Worst offenders:

- Typed: quicksort +201% (source also changed `arr: int[]` → `var arr: int[]`),
  raytrace3d +197%, pnpoly +161%, list +94%, splay +80%, richards +66%, deltablue +56%,
  crypto_sha1 +55%, nqueens +51%, binarytrees +46%, brainfuck +43%, ray +41%, base64 +36%,
  cd +30%, json_gen +21%, havlak +20%, fasta +15%, knucleotide +10%, cube3d +6%.
- Untyped (sources unchanged unless noted): hashmap +119%, raytrace3d +103% (source
  tweaked), crypto_sha1 +72%, richards +66%, deltablue +62%, list +57%, cd +43%,
  json +42% (source tweaked), ack +25%, havlak +21%, permute +18%, deriv +18%,
  towers +16%, pnpoly +13%, navier_stokes +12%, gcbench +9%, json_gen/base64 +8%,
  sieve/fasta/primes +5–6%.

Attribution: the step lands almost entirely in the **R18→R20 window** — the
type-enforcement round (first-bad commit `274625d56` per the 2026-08-01 bisect:
unconditional `emit_checked_boundary` + ANY downgrade). Trace: hashmap untyped
76→159 ms and richards untyped 1.45s→2.62s at R20, flat since. Tune13–15 recovered the
*typed* column net-positive but left specific shapes above the R18 line — precisely the
C-track families: checked stores (quicksort/brainfuck/matmul → C1), boundary admission
(pnpoly/sha1 → C2/F1), record validation+COW (richards/deltablue/splay/cd/havlak/list/
json → C3), and the Tune15 demotion (raytrace3d → C4.1). The clean "untyped left behind"
specimen is hashmap: untyped +119% while typed is now *faster* than R18 (−12%) — the
enforcement-era cost was tuned out of the typed lane only (C7's case in miniature).
Caveat: R18 is not a pure like-for-like baseline — enforcement is semantics-bearing
(TE-15/17/18 correctness), so part of the delta is bought correctness, and ~30 sources
took small annotation edits in the window.

## 2. Attribution: what the deltas prove

The proposal began from the v24 `temp/prof15/` samples, the v24→v25 row deltas, and the §3
source audit. The retained implementation re-profiled the release binary before each
performance-sensitive slice; the final measured evidence is recorded in §6 (the
spectralnorm delta was the warning that the v24 attributions were partially stale).

1. **sha1 unchanged ⇒ its ~50% admit share is not conversions.** B1.2 lowered
   `int()`/`float()` on proved operands; sha1 didn't move. Per §3 the word-schedule arrays
   `w`/`x` are *untyped* (the len()-int64 defect D-d forced it), so every one of the
   80-round word ops runs generic `item_at`/`fn_array_set` plus admission — and the
   bitwise/wraparound family (`band`-equivalents, shifts, mod-2³²) crosses the boxed helper
   ABI regardless [S4.5.3, D2.2.2]. Both halves need fixing (C0 + C2).
2. **brainfuck still typed-worse after the packed-fill witness ⇒ the cost moved to writes.**
   Reads through the witness got cheap; `tape[dp] = …` still routes through the checked
   store path the Result22 dissection measured at clone+validate per store
   (`lambda_array_set_checked`; `vibe/impl/Lambda_Impl_Tune13.md` R22-1). matmul (fully typed,
   still +9%) is the clean specimen: the annotation's only remaining cost is the store
   path. That is C1.
3. **deltablue −6% / gcbench flat / splay +8% ⇒ B3 never engaged — and §3 shows why.** The
   canonical awfy deltablue2 has no record types (`type X = any` aliases); splay2 declares
   `SplayNode` but builds every node as a bare literal; richards2 (awfy) and havlak2
   declare zero types. **B3 had nothing to lower.** Separately, raytrace3d2's comment
   states the "Phase 3 direct-offset path is disabled in transpile-mir.cpp" — so even
   annotated field reads go through `fn_member`. Fixing engagement means fixing the sources
   (C0.C) *and* the two compiler defects that made authors strip the annotations (D-b,
   D-f), then re-enabling the direct-offset path (C3).
4. **nbody unchanged ⇒ two stacked causes.** The B5 audit stands: typed `advance()` =
   2,283 instructions/168 calls vs C2MIR 102/1 (`temp/tune15_nbody2_lambda_witness.mir`).
   Per §3 the canonical awfy source leaves the innermost pair-loop floats
   (`dx/dy/dz/d_squared/distance/mag`) untyped and declares no return types — so a large
   share of those 168 calls may be source-attributable. Re-type first (C0), re-dump, then
   run the call census (C4).
5. **base64 unchanged ⇒ B4's builder never engages on its pattern.** The accumulation is
   `result = result ++ (a ++ b ++ c ++ d)` with pieces from indexing the *untyped* `TABLE`
   (an Item array of strings — no packed layout exists for it, defect D-f adjacent).
   Builder pattern-match plus TABLE representation are both in scope (C5).

## 3. Typed-source audit: are the "typed" scripts actually typed?

Full audit of all 55 `*2.ls` typed variants (2026-08-07, four parallel sweeps). Question:
does each script read like a statically-typed program at the places that matter — hot
params, returns, locals, containers, records? Answer: **no.**

**Verdicts: 11 FULLY-TYPED, 26 MOSTLY-TYPED, 18 UNDER-TYPED.**

| Suite | Fully | Mostly | Under-typed |
|---|---|---|---|
| r7rs (10) | — | ack, cpstak, fib, fibfp, nqueens, sum, sumfp, tak | fft, mbrot |
| awfy (14) | — | bounce, cd, json, list, mandelbrot, nbody, permute, sieve, storage, towers | **deltablue, havlak, queens, richards** |
| beng (10) | mandelbrot, pidigits | binarytrees, fannkuch, nbody, regexredux, spectralnorm | fasta, knucleotide, revcomp |
| kostya (7) | collatz, json_gen, matmul | base64, levenshtein, primes | brainfuck |
| larceny (12) | diviter, divrec, paraffins, quicksort, ray | array1, deriv, pnpoly, primes, triangl | gcbench, puzzle |
| jetstream (9) | — | cube3d, deltablue, navier_stokes, nbody | **crypto_sha1, hashmap, raytrace3d, richards, splay** |
| text (3) | microdiff | fast_diff | hyphen |

Notable: the FULLY-TYPED set (diviter, divrec, paraffins, quicksort, ray, collatz,
matmul, json_gen, mandelbrot, pidigits, microdiff) contains most of the best typed/Node
ratios; the UNDER-TYPED set contains seven of the ten worst (§1.5).

### 3.1 Why the sources are under-typed: six annotation-hostility defects

The corpus is not lazily typed — it is *defensively* typed. The workaround comments name
the defects explicitly, with repros. These are typed-lane compiler bugs every typed user
would hit; they gate everything else in this round.

- **D-a — Bracket annotation on a local re-tags it as ANY.** `var a: int[] = fill(n, 0)`
  (or `= [literal…]`) loses the packed-ArrayNum witness and the direct-index path that the
  *unannotated* binding gets by inference. Cited verbatim in 8 files (array12, pnpoly2,
  triangl2, cube3d2, navier_stokes2, nbody2-js, fannkuch2, levenshtein2) — and directly
  contradicted by matmul2/quicksort2/spectralnorm2, which annotate `fill()` results and
  keep packed storage. Either the defect is shape-dependent or half the corpus is carrying
  a stale workaround; both possibilities must be resolved. This is TS-3's ANY-downgrade
  wearing a new coat [`vibe/impl/Lambda_Issue_Type_Support (retired).md` TS-3; D3.2.1].
  Consequence: sha1's `w`/`x`, levenshtein's DP rows, fannkuch's three permutation arrays,
  triangl's five boards, navier's six grids, nbody-js's seven body arrays, hashmap's
  bucket arrays — every hot array in the tail — are deliberately bare.
- **D-b — Declared record type on a `var` local creates a COW value root; reads then
  deep-copy.** raytrace3d2 L7–13 verbatim: "A declared map type on a `var` here makes the
  local a fresh COW value root instead of a borrow, so every triangle/light read in the
  intersect loops deep-copies its map — this file did not finish in 120 s with those
  annotations and runs in ~85 ms without them. … The Phase 3 direct-offset path is
  disabled in transpile-mir.cpp." A **>1,400x** annotation penalty, and confirmation that
  the fixed-offset field path B3 was supposed to deliver is switched off. splay2 keeps its
  node literals bare for the same reason (undocumented). [D3.2.2, D8.3.2; C4 value
  semantics — the fix is borrow-not-root on read paths, exactly the distinction splay2's
  L50–54 comment describes manually.]
- **D-c — `string` return type on a `pn` segfaults the MIR JIT when the result spans a
  GC** (`temp/repro_string_return_segv.ls`). Cited in 7 files (spectralnorm2, fasta2,
  revcomp2, nbody2-beng, regexredux2, levenshtein2, crypto_sha12). Partially stale:
  brainfuck2 (`run_bf … string`), base642, fast_diff2, and sha1's own `binb2hex` declare
  string returns and pass. Verify the repro on v25; if it still fires, it is a plain
  correctness bug (P0); if fixed, sweep the stale comments and re-annotate.
- **D-d — Declared `int` local initialized from a `len()` expression miscompiles/narrows**
  (`temp/repro_declared_int_len_concat.ls`), and `len()`-derived arithmetic **widens to
  decimal** so an `int` param rejects it (sha1's `input_len`: "the caller passes
  `len(s) * CHRSZ`, which the compiler widens to decimal"). Cited in 7 files. This is an
  int-lane inference defect squarely in v5 int53-total territory
  [`vibe/Lambda_Semantics_Int_Type.md`; S4.5.3] — `len()` should produce an int-lane value
  whose products stay int, not decimal.
- **D-e — A sole map assignment inside an `if` block is dropped by the transpiler.**
  Documented only in awfy/richards2 L271–272; worked around by **49 undocumented
  `var _dN: int = 0` dummy statements** across cd2 (19), havlak2 (15), json2 (10),
  deltablue2-awfy (5). A silent wrong-code bug hiding behind dummy statements in four
  benchmark sources — P0 correctness.
- **D-f — No packed `bool[]` layout** — only int/float/int64/uint64 have packed ArrayNum
  layouts, so sieve/primes/queens/triangl/puzzle keep boxed boolean arrays (and puzzle
  types them `any` explicitly). Either add a packed bool (byte) lane or define the idiom
  (int 0/1) and type the sources accordingly.

### 3.2 Systemic gaps beyond the defects

1. **Return types are almost entirely absent.** Zero non-main return types in all 10 r7rs
   files; awfy has them only in list2; cube3d2/raytrace3d2/splay2/navier_stokes2/nbody2-js
   declare essentially none; richards2/deltablue2-js nearly none. Every undeclared return
   is a boxed Item crossing the call ABI — precisely the F2 family — and **B2.2's native
   scalar call ABI keys on declared scalar returns, so the corpus opts itself out of the
   optimization Tune15 built.** The micro family (bounce/nqueens/list/queens/towers
   helpers) is dominated by this.
2. **Record types absent or unapplied where B3 needs them.** havlak2 and richards2-awfy:
   zero `type` declarations, all bare maps. deltablue2-awfy: fake `type X = any` aliases.
   splay2/raytrace3d2: types declared but constructor literals bare (D-b forced).
   Well-typed counterexamples exist in the same corpus (cd2's `RbtTree`/`DrawCtx`, json2's
   `Parser`, richards2-js's `Packet`/`TCB`/`Scheduler`, deltablue2-js) — the pattern is
   known and applicable once D-b/D-e are fixed.
3. **Hot inner-loop scalars untyped in otherwise-typed files**: fft2's butterfly temps
   (`tempr`/`tempi`, n·log n executions), awfy/nbody2's `dx..mag`, hyphen2's string
   accumulator, cd2's collision-loop locals, mbrot2's untyped `matrix` param (the only
   untyped param in r7rs).
4. **Duplicate-source skew**: the canonical timed row is awfy for
   nbody/deltablue/richards, and the awfy copies are *worse-typed* than their
   beng/jetstream siblings (beng/nbody2's advance is fully typed; jetstream/richards2 has
   real record types). The typed column is penalized by which copy happens to be
   canonical.

### 3.3 Consequence for the benchmark methodology

The typed column claims to measure "what annotations buy." Today it measures a corpus that
(a) strips annotations to dodge compiler bugs, (b) strips them because they *pessimize*
(D-a, D-b — the very defects the ≤5% bar exists to catch, baked into sources as
workarounds), and (c) never declares return types. Where the source is under-typed the
column under-reports the typed lane; where a workaround restructured the program the
untyped comparison is polluted too. **No Tune16 acceptance number is trustworthy until the
sources are repaired — and the sources cannot be repaired until D-a/D-b/D-e (at minimum)
are fixed.** The compiler fixes are the perf fixes.

## 4. Implementation tracks (proposal map, ranked)

### C0 — Annotation repair: make annotations safe, then apply them (prerequisite)

- **C0.A — fix the pessimization defects (compiler):**
  1. D-a: an element-typed bracket annotation on a local must *keep* the inferred packed
     witness (`var a: int[] = fill(n, 0)` ≡ untyped binding + declared contract), never
     re-tag ANY. Resolve the matmul2-vs-fannkuch2 contradiction first — if the defect is
     already partially fixed, this is a comment-sweep plus re-annotation.
  2. D-b: declared-record `var` locals bind as borrows on read paths, not fresh COW value
     roots; re-enable the transpile-mir direct-offset field path behind its shape guard
     (folds into C3). Acceptance: raytrace3d2 fully annotated runs ≤ its bare-literal
     time.
  3. D-f: packed bool byte lane (or the documented int-0/1 idiom applied corpus-wide).
- **C0.B — fix the correctness bugs (compiler):** D-e dropped-assignment (P0, silent wrong
  code); D-c string-return segv (P0 if it still reproduces on v25); D-d len()-int lane
  (int products of `len()` stay int — aligns with v5 int53-total).
- **C0.C — re-annotate the corpus:** return types on every non-main `fn`/`pn` (55 files —
  the single largest mechanical gap); record types applied at literal sites in
  deltablue2-awfy/havlak2/richards2-awfy (port from their well-typed siblings);
  the §3.2.3 hot-local list; remove the 49 `_dN` dummies and the stale workaround
  comments. Annotations only — no workload restructuring.
- **C0.D — re-baseline:** re-run the fixed population; re-issue the ≤5% ledger and the
  C2MIR gap table on corrected sources. All later acceptance is measured against this
  baseline.

### C1 — Proved-store elision on typed containers (the write-path twin of B1)

When the stored value's lane statically conforms to the declared element/field type and
the binding is uniquely owned (`var`), emit a raw store behind the witness — no validator,
no COW clone, no `lambda_array_set_checked` [D3.2.2, D8.3.2–D8.3.3; C4 value semantics
preserved by the same static-uniqueness argument the B3 in-place-update slice already
uses]. matmul and json_gen — the two *fully-typed* rows still violating the 5% bar — are
the acceptance specimens; brainfuck/knucleotide/splay/hashmap follow once C0 types them.
Acceptance: `fn_array_set`/`lambda_array_set_checked` leave the matmul/brainfuck samples;
matmul and json_gen typed ≤ untyped; typed-rejection negative tests unchanged; forced-GC +
poison sweeps green [D8.6.3].

### C2 — Native bitwise/wrapped-int lowering (sha1)

Lower the bitwise builtin family (`band`/`bor`/`bxor`/shifts/rotates) and the mod-2³²
wraparound idiom to native MIR ops on proved int lanes [S4.5.3, D2.2.2]. Depends on C0
(sha1's arrays and `input_len` must first be typeable — D-d). Gate: fresh v25 sha1 profile
after C0.C confirming the residual share sits in the bitwise family. Target: crypto_sha1
24.3x → ≤8x Node; hashmap and levenshtein secondary.

### C3 — Fixed-shape record lowering, re-enabled and engaged

Sequenced after C0.A.2 (D-b borrow fix) and C0.C (real record types in the canonical
sources): re-enable the direct-offset field path in `transpile-mir.cpp` behind its shape
guard, land `ShapeEntry.name_id` (Name-Identity W1/W2) so guard checks are id compares,
and keep the B3 statically-conforming-write and in-place-update slices in the engaged
path [D3.2.2, D8.3.2–D8.3.3]. Then instrument engagement: count fixed-offset hits vs
ShapeEntry-walk fallbacks on deltablue/splay/richards/gcbench — the count, not a fixture,
is the acceptance evidence. Targets: deltablue −40%, splay typed ≤ untyped, richards ≤3x
Node, gcbench −30%.

### C4 — Fix witness demotion (raytrace3d, root-caused) + nbody call census

1. **Fix the §1.3 demotion defect** (bisected and mechanism-confirmed; no further bisect
   needed). Invariant: **adding a witness parameter to a raw entry must never make a
   previously-raw edge take the boxed wrapper.** When the caller cannot statically prove
   the array witness, emit a caller-side `ensure_typed_array` once and still call the
   raw entry — the fallback must cost no more than the pre-witness arrangement
   [D3.3.1–D3.3.3; DF9 entry-equivalence]. Add a `mir-check` guard asserting
   raw-vs-boxed edge counts for the raytrace3d shape [D8.6.2].
   Acceptance: raytrace3d typed ≤159 ms; `map_admit_calls` back to ≤362,688.
2. **Memoize the map-contract relation** (pairs with C3): raytrace3d does 362k–631k
   `lambda_map_contract_relation` walks per run with `map_admit_exact_shape_hits` = 0 —
   every walk re-proves the same (candidate TypeMap, expected TypeMap) pair. Cache the
   relation on the TypeMap pair (or stamp the candidate on first admission). This is
   pure win even at the v24 baseline, where the walk is already raytrace3d's top leaf.
3. After C0.C re-types awfy/nbody2 (inner-loop floats, return types): re-dump `advance()`
   and classify every residual call (ensure residue? adopt/restore? boxed element access?),
   then extend the corresponding landed mechanism (B2.2 native edges, witness
   pass-through) until the loop body is call-free. Only a call category with *no* landed
   mechanism justifies new codegen work [D3.3.1–D3.3.3].
Targets: nbody 20x → ≤5x C2MIR; raytrace3d ≤159 ms then onward with C0.A.2 and C4.2.

### C5 — String round (Text suite + base64 + levenshtein)

1. **Builder engagement**: B4's owned builder must recognize nested `++` trees feeding a
   loop-carried accumulator (base64's `result = result ++ (a ++ b ++ c ++ d)`), not just a
   bare binary `++` [S1.4–S1.6].
2. **O(1) char access on ASCII-proved strings**: fast_diff (14.3x Node) and levenshtein
   are dominated by `s[i]` compares C2MIR does as byte loads. An ASCII witness carried
   like the array witness makes indexed access a byte load.
3. **hyphen +63%**: expected to fall to C0.C (untyped accumulator + missing return type);
   re-measure after re-annotation before scheduling runtime work.
Longer term the honest answer for base64's 120x is a byte-buffer/binary lane; out of
scope this round.

### C6 — Post-C0 annotation-cost diagnosis (bounce and residue)

After C0 re-annotates them, MIR-diff typed vs untyped emission on whichever of
bounce/nqueens/list still violates the bar. These are the smallest specimens of "what does
an annotation still cost categorically"; whatever survives C0+C1 here names the next
runtime mechanism.

### C7 — Untyped track: inference reuse

The untyped column's worst rows are exactly where typed proves the lowering works:
spectralnorm untyped 45.6 vs typed 2.65, richards 2.40s vs 240 ms, collatz 1.20s vs
440 ms, mbrot 12.3 vs 0.705, navier 1.07s vs 259. The facts the annotations assert are
locally inferable (literal-initialized locals, monomorphic helpers, witness-producing
builtins — B1.3 generalized). Let inferred lanes reuse the same B1/B2/C1 machinery.
Note D-a's flip side: unannotated bindings *already* get better inference than annotated
ones in half the corpus — C0.A.1 and C7 are two faces of one invariant: **annotation and
inference must reach the same representation** [D3.2.1]. Target: untyped/Node geo
2.32x → ≤1.8x.

**Order: C0.A + C0.B (compiler) → C0.C/C0.D (sources + re-baseline) in the same phase as
(C4.1 raytrace3d bisect) → C1 + C3 in parallel → C2 → C4.2 → C6 → C5 → C7.**

## 5. Gates and acceptance (house rules, unchanged)

- `make test-lambda-baseline` 100% and `make test262-baseline` 40,261/40,261 after each
  retained phase; MIR emission ratchet updated in the same commit for justified growth
  [D8.6.1]; `mir-check` coverage for every new witness/elision/native-ABI edge [D8.6.2];
  forced-GC + poison sweeps for every ownership/root/representation change [D8.6.3].
- D-c/D-e are correctness bugs: fixes land with regression tests in
  `test/lambda/` (with expected `.txt`) regardless of benchmark impact.
- Fresh `/usr/bin/sample` profiles of the **v25 binary** (or the post-C0 baseline) before
  any perf slice is coded; a change is retained only with its measured win on the fixed
  population (three runs, workload-only `__TIMING__`, headline + matched geomeans),
  release build only.
- The categorical bar stands: **an annotation may never make a row more than 5% slower.**
  C0 makes the bar *measurable* (today the sources dodge it); C1+C3+C6 are accountable
  for clearing what remains on corrected sources. D-a and D-b are themselves categorical
  bar violations baked into the compiler — their fixes are the bar's enforcement.
- Non-goals carried forward: no flex-int revival, no Lambda-script inline caches [D8.4.1],
  no vendored-dep edits, no C2MIR-path changes (frozen, rule 14). LambdaJS remains its own
  round (Tune15 §5 carry-forward; havlak 397x, cd 264x, hashmap 208x, sha1 199x).

Round target: on repaired sources — typed/Node geo **≤1.0x**; typed/C2MIR **≤4.5x** on
the 47-row matched set; untyped/Node **≤1.8x**; the annotation ledger cleared; D-a–D-f
closed with tests.

## 6. Implementation record (2026-08-08)

Tune16 is implemented as a compiler/runtime and corpus-repair round. The implementation
follows the normative rulings in `doc/Lambda_Formal_Semantics.md` S4.1 and S4.5.3 and
`doc/Lambda_Formal_Design.md` D2.2.2, D2.4, D3.2.1–D3.2.2, D3.3.1–D3.3.3, D5.2–D5.3,
D8.3.2–D8.3.3, D8.4.1, and D8.6.1–D8.6.3.

### 6.1 Landed tracks

- **C0:** preserved inferred packed witnesses across declared array boundaries; added the
  packed `bool[]` byte lane; repaired string-return GC rooting, `len()`-derived int-lane
  propagation, and the dropped sole-member-assignment case; repaired the 55 typed source
  variants with return/record/container annotations and removed the 49 assignment dummies.
- **C1:** added proved int/float/bool ArrayNum stores, unique/in-place publication, checked
  fallback, post-COW cache rebinding, dense-loop bounds proofs, and heterogeneous-store
  conflict tracking. A write-only inferred array parameter now remains boxed unless all
  call-site and store evidence agrees.
- **C2:** added native MIR lowering for proved integer bitwise operations, shifts, unsigned
  32-bit operations, and the existing wrapped-int lane, with negative/oversized shift
  guards and native-emission fixtures.
- **C3:** re-enabled fixed-shape scalar field reads and retained the uniqueness-guarded
  direct field-write path; typed map construction now publishes packed fields directly.
- **C4:** fixed open typed-array caller admission so witness-bearing raw edges do not
  silently demote to boxed wrappers; added the TypeMap relation cache and open-witness
  `mir-check` coverage. The final raytrace profile records 362,664 relation-cache hits
  from 362,840 admissions, with 176 misses and zero copied bytes.
- **C5:** flattened nested loop-carried string joins, added the ASCII indexed-string
  helper, and covered both MIR emission and GC-spanning string-return cases.
- **C6:** added the post-annotation regression fixtures for fixed records, native bitwise
  code, float stores, string builders, and ASCII indexing.
- **C7:** generalized call-site element inference and reused typed-array specialization
  for unannotated homogeneous locals and parameters, including caller write-back and
  recursive/mutable procedure cases.
- **Acceptance repair:** await-rejection completion now routes through `finally` when
  present and through the function-level exceptional exit otherwise; this preserves the
  pending-exception invariant across async-function and async-generator resumes. The
  focused 15-case Test262 cluster that exposed the defect passes after the repair.

### 6.2 Verification gates

The final release/debug verification completed with:

- `make test-lambda-baseline`: **3,651/3,651** — input 2,104/2,104, Lambda runtime
  1,547/1,547, MIR emission 34/34, ratchet 15/15, forced-GC 49/49, and JS 324/324.
- `make test262-baseline`: **40,261/40,261**, 0 failures, 0 retries, and 0 regressions;
  the verified Test262 commit remained `673e9bacbe28590f501e2dcd817aadcc31899191`.
- Fresh release benchmark matrix: all **56 workload rows** completed with `OK` status;
  three-run workload medians were used for the headline ratios. A native
  `/usr/bin/sample` snapshot was also captured for the release Richards workload.
- Tune16 MIR fixtures: **7/7** focused fixtures pass, and the full MIR emission suite is
  **34/34**. All newly added Lambda scripts have checked-in expected `.txt` results.

### 6.3 Measured target status

The implementation is complete, but the original performance aspirations are not all
achieved by the repaired corpus. On the fresh 56-row release matrix:

| Metric | Measured | Target | Status |
|---|---:|---:|---|
| MIR untyped / Node geomean | 2.295x | ≤1.8x | not met |
| MIR typed / Node geomean | 1.252x | ≤1.0x | not met |
| MIR typed / C2MIR geomean, 44 matched rows | 5.832x | ≤4.5x | not met |
| typed rows >5% slower than untyped | 12 / 56 | 0 | not met |
| raytrace3d typed workload | 71.9 ms median of 3 final release runs | ≤159 ms | met |

The residual gap is therefore recorded rather than hidden: the largest typed/Node residues
are nbody, crypto-sha1, cd, splay, navier-stokes, and raytrace3d, while base64 and the
Text-family workloads remain dominated by boxed string/binary lanes. These are follow-up
optimization targets, not unimplemented Tune16 mechanisms; C2MIR remains frozen as required
by D8.4.1 and the repository agent rules.
