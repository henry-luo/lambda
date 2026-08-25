# Tune 17: Result26 Analysis, Lane Unification, and the Regression Ledger

- **Date:** 2026-08-08
- **Input:** `test/benchmark/Overall_Result26.md` (Lambda commit `7a876454d0`, archived
  binary `test/benchmark/exe/lambda-v26-7a876454d0`), `test/benchmark/Overall_Result25.md`,
  `test/benchmark/Overall_Result18.md` (pre-enforcement reference),
  `vibe/impl/Lambda_Impl_Tune16.md` (§6 implementation record), MIR dumps and Node-normalized
  row comparisons in `temp/prof16/` (comparison scripts `cmp2.py`/`cmp3.py`)
- **Status:** IMPLEMENTED — the safe T1–T4 slices and their verification fixtures are
  landed; residual benchmark targets that require a new representation design remain
  explicitly evidence-gated rather than being claimed as cleared
- **Related:** `vibe/impl/Lambda_Impl_Tune16.md`, `vibe/impl/Lambda_Impl_Tune15.md`,
  `vibe/Lambda_Tune_Typed_Vs_C2MIR.md`, `vibe/Lambda_Design_Type_Enforcement.md`,
  `vibe/impl/Lambda_Issue_Type_Support (retired).md` (TS-3)
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1, S4.5.3;
  `doc/Lambda_Formal_Design.md` D2.2.2, D2.4, D3.2.1–D3.2.2, D3.3.1–D3.3.3,
  D8.3.2–D8.3.3, D8.4.1, D8.6.1–D8.6.3

## 0. Implementation checkpoint (2026-08-08)

Tune17 is implemented through the evidence-backed compiler and runtime slices below.
The implementation preserves the formal value, ownership, and representation rulings
in S4.1, S4.5.3, D2.2.2, D2.4, D3.2.1–D3.2.2, D3.3.1, and D5.2.

- **T1 — lane unification:** inferred raw edges now require a proven scalar lane;
  proven native arguments skip redundant admission checks, while calls with borrowed
  `var` container parameters enter the boxed adapter so allocation cannot invalidate a
  caller-owned Item root. Declared scalar-boundary elision is currently enabled for
  proven `float` declarations. Integer declarations retain the checked boundary because
  their i64/Item carrier can be ambiguous; integer producer proofs are handled by T2.
  Imported scalar bindings also reopen only when their tracked carrier is still `any`.
  `tune17_recursive_lane.mir-check` ratchets the inferred and declared recursive paths.
- **T2 — checked stores:** declared `int[]` and packed `bool[]` writes use raw lanes only
  after a producer proof, with the existing checked setter as the cold fallback. Typed
  array element guards invalidate only stores that can change the element type. The
  native integer consumer reopens an actual Item carrier exactly once, protecting both
  the typed-store lane and the `Towers` stack representation. `tune17_typed_store`
  covers the int and byte stores.
- **T3 — sha1 evidence gate:** native u32 boundary admission and bitwise lowering are
  implemented with an explicit nonnegative/u32 range check and canonical packed result;
  invalid values still return `ItemError`. A fresh release sample of 5,000 SHA-1
  iterations passes and measures 6,410.47 ms median across three runs; the matched
  25-iteration comparison is approximately 3.39x Node, below the ≤8x gate.
- **T4 — macro-row evidence gate:** fresh release samples of deltablue, splay,
  richards, havlak, cd, and json are archived under `temp/prof17/`. They show dynamic
  map/ShapeEntry lookup and validation as the dominant family, with no safe universal
  COW or fixed-shape invalidation proof. The landed fixed-shape path is therefore limited
  to proven container reads/writes and reloads a borrowed object root before direct
  writes; no speculative ShapeEntry bypass was added.
- **Dynamic JS dispatch safety:** arbitrary `js_map_method` closures remain on the boxed
  dispatcher ABI. The `_into` bridge adopts only a surviving scalar home after generic
  dispatch, preventing a dynamically selected closure from being treated as a known
  scalar-home callee (D2.2.2, D3.2.1, D3.3.1).

### Verification record

- `make test-lambda-baseline`: Input **2104/2104**, Lambda Runtime **1561/1561**,
  total **3665/3665**.
- `make test262-baseline`: **40261/40261** fully passing, **0** failed, **0** retries,
  and **0** regressions against the recorded baseline.
- `test_mir_emission_gtest`: **36/36**; `test_mir_gc_stress_gtest`: **56/56**.
- `Towers`: PASS after the carrier-reopen fix; `proc_declared_int_len`: PASS; the
  exact `import_multi` path and Moment.js **131/131** suite also pass.
- Tune17 MIR fixtures pass with the new recursive lane and typed-store checks. The
  original Result26 aggregate targets are not re-labeled as cleared where their
  remaining cost is the measured dynamic ShapeEntry or binary-representation family;
  those are separate design work under S1.4–S1.6 and D8.4.1.

## 1. What Result26 says

| Metric | Result25 | Result26 |
|---|---:|---:|
| MIR (untyped)/Node geo | 2.32x | **2.25x** |
| MIR (typed)/Node geo | 1.32x | **1.26x** |
| MIR (typed)/C2MIR geo (47 rows) | 7.14x | **6.70x** |
| LambdaJS/Node geo | 16.0x | 16.0x |
| QuickJS/Node geo | 7.30x | 7.25x |

All three Lambda geomeans improved; the Tune16 §6.3 targets (untyped ≤1.8x, typed ≤1.0x,
typed/C2MIR ≤4.5x) remain open.

### 1.1 Delivered (v25 → v26)

- **raytrace3d typed 261.9 → 70.9 ms** (3.54x Node — better than any prior round,
  including pre-enforcement R18's 88.3). The Tune16 C4 demotion fix and TypeMap relation
  cache both engage: the final profile records 362,664 relation-cache hits from 362,840
  admissions.
- **C7 inference on the untyped column**: nbody untyped 170.9 → 37.1 ms (−78%), pnpoly
  untyped 123.9 → 20.3 ms (−84%), fasta untyped 1.76 → 0.80, base64 untyped 83 → 51.
- Typed wins: levenshtein 21.9 → 8.13 (−63%), ray 2.78 → 0.90 (−67%), puzzle 8.99 → 4.65
  (−48%), paraffins 1.83 → 1.07, nqueens 2.41 → 1.88, revcomp −15%, base64 69.7 → 47.0
  (C2MIR gap 120x → 82x), fast_diff 557 → 488, hyphen 4.29 → 3.72, navier 259 → 232,
  matmul 14.2 → 12.3 (typed now beats untyped), knucleotide cleared the 5% bar.

### 1.2 New regressions (v25 → v26), Node-normalized and root-caused

Node itself drifted +7–20% on several macro rows in this run (havlak Node 121.7 → 146.6,
deltablue +10%, richards +8%, splay +7%), so raw deltas overstate those rows; everything
below is stated as ratio-to-Node change unless noted.

**R1 — Untyped recursion family: C7 installs per-call checks on inferred contracts.**

| Row | untyped Δ (vs Node) | Source changed? |
|---|---:|---|
| r7rs/cpstak | **+112%** | no |
| r7rs/tak | **+107%** | no |
| r7rs/ack | +47% | no |
| kostya/levenshtein | +33% | no (untyped) |
| jetstream/hashmap | +23% | no |
| kostya/matmul | +22% | no (untyped) |

Mechanism (MIR-dump confirmed, `temp/prof16/tak_v2{5,6}.mir`): untyped `tak.ls` emits
**0 `lambda_type_check` calls in v25 and 8 in v26**, sitting at the recursive
call-argument boundaries of the newly inferred int specialization. C7 infers the
contract, then admits every argument at every inferred call edge — the inferred contract
does not receive the T-A1 redundancy elision a *declared* contract gets, even when the
arguments are provably int (products of int arithmetic on the same inferred params)
[S4.1, D2.4, D3.2.1]. The typed columns of the same rows are fine (tak typed −4%,
levenshtein typed −63%), which isolates the defect to the inferred-edge admission.

**R2 — Re-annotated typed rows: the C0.C annotations are still not free.**

| Row | typed Δ (vs Node) | What C0.C added (tune16 window diff) |
|---|---:|---|
| beng/fannkuch | **+410%** | `perm/perm1/count: int[]` on `fill()` locals |
| kostya/primes | **+152%** | `flags: bool[]` (new packed byte lane) |
| awfy/sieve | +42% | `flags: bool[]` param + local, `int` return |
| awfy/nbody | +37% | re-annotation of advance locals/returns |
| awfy/towers | +24% | re-annotation |
| jetstream/splay | +23% | record-literal annotations |
| awfy/queens / permute | +16% / +15% | re-annotation |

These are the *exact* annotations the D-a/D-f fixes were supposed to make free — Tune16's
fixture shapes pass, but fannkuch's swap-heavy `int[]` stores and the primes/sieve
byte-lane bool stores evidently still route through a checked path C1 does not elide.
The ≤5% bar is being violated by the repair that was meant to enforce it.

**R3 — The inversion: the annotation lane now loses to the inference lane.**

| Row | untyped v26 | typed v26 | typed/untyped |
|---|---:|---:|---:|
| larceny/pnpoly | 20.3 | 53.8 | **2.65x worse** |
| awfy/nbody | 37.1 | 39.5 | 1.06x worse |
| beng/fannkuch | 0.363 | 1.87 | **5.2x worse** |
| beng/fasta | 0.796 | 1.27 | 1.60x worse |

C7's inferred specialization now produces *better* code than the declared-contract path
on the same program with the same facts. The v26 typed-worse-than-untyped ledger
(12/56 rows per Tune16 §6.3) is now dominated by this asymmetry plus R2, with the
holdovers bounce +198%, splay +102%, brainfuck +25%, hyphen +40%, list/array1/gcbench.

**R4 — crypto_sha1 flat for the second consecutive round.** 228.2 → 235.4 ms (24.9x Node
— still the worst typed row). Tune15 B1.2 (conversion lowering) and Tune16 C2 (native
bitwise/shift/u32 lowering) were both aimed at sha1 and neither moved it a millisecond.
The mechanism exists; it demonstrably does not engage on sha1's shape. No further
mechanism may be built for this row without the §4 T3 evidence gate.

**R5 — Macro record rows, unprofiled residue.** deltablue/havlak/richards/cd are +10–17%
typed vs Node after normalization (deltablue +10%, richards +9% raw −
Node-drift-corrected, havlak +8%, cd +10%, json +11%, deriv +19%, gcbench +17% on a
Node-favorable run). These rows received the heaviest C0.C record re-annotation
(deltablue2 48+/53−, havlak2 58+/73−, richards2 38+/33−) and interact with C3's
re-enabled fixed-shape path; no v26 profile exists yet. T4 owns them.

### 1.3 Long-window check vs Result18 (pre-enforcement)

Net: typed geo 1.87x → 1.26x, untyped 2.55x → 2.25x — both columns are now well ahead of
R18 overall. Rows still above the R18 line: **17 untyped, 15 typed** (>10%). Worst:

- Untyped: hashmap **+173%** (the "untyped left behind" specimen — hashmap *typed* is
  faster than R18), deltablue +101%, ack +82%, richards +81%, raytrace3d +78%,
  crypto_sha1 +69%, list +61%, cpstak +61%, cd +59%, json +58%, havlak +56%, tak +47%.
- Typed: fannkuch +188%, quicksort +162%, splay **+137%** (now the worst record row),
  pnpoly +101%, list +94%, richards +94%, deltablue +87%, crypto_sha1 +60%, havlak +56%,
  cd +43%, binarytrees +43%, brainfuck +31%.
- Recovered below R18 this round: raytrace3d typed (70.9 vs 88.3), nqueens, ray,
  levenshtein, puzzle, base64 typed.

The R18 step remains attributable to the R18→R20 type-enforcement round (Tune16 §1.6);
Tune13–16 have recovered the typed column past R18 in aggregate while specific
enforcement-sensitive shapes — and now the R1/R2 additions — remain above it.

## 2. The diagnosis, stated once

Result26 exposes that the compiler now has **two lanes for the same knowledge**:

1. a **declared** contract (annotation) — checked at boundaries, elided where T-A1
   proves redundancy, specialized by the B2.2/C1/C3 machinery;
2. an **inferred** contract (C7) — specialized by the inference machinery, but *checked*
   at inferred call edges without T-A1 elision.

Each lane wins on different rows and each lane's weakness is the other lane's strength:
inference beats annotation on pnpoly/nbody/fannkuch/fasta (R3); annotation beats
inference on tak/cpstak/ack/levenshtein/hashmap/matmul untyped (R1). That is not two
bugs — it is one missing invariant:

> **Same facts ⇒ same code.** However a type fact is established — declaration or
> inference — the specialization it enables and the checks it requires must be identical
> [D3.2.1, D2.4]. An annotation is a *contract plus a proof obligation at the boundary*;
> once admitted, it must confer everything inference confers. An inferred fact is a
> *proof*; it must never install a check that the equivalent declaration would elide.

Every entry in R1–R3 is a violation of this invariant in one direction or the other.

## 3. Proposed tracks (ranked)

### T1 — Lane unification (the round's core; fixes R1 + R3)

1. **Inferred edges get T-A1 elision** (R1): an inferred call-edge admission whose
   argument lane is already proven (native int/float product of the same specialization)
   is redundant by the same rule that elides declared boundaries — emit no
   `lambda_type_check`. tak's 8 checks must return to 0 [S4.1, D2.4].
2. **Declared contracts route through the inference specializer** (R3): where C7 would
   specialize an *unannotated* local/param/loop into a packed lane, the *annotated*
   version of the same code must reach the identical emission — the declaration adds a
   boundary proof obligation, never a different (worse) body. pnpoly typed must match
   pnpoly untyped's 20.3 ms, fannkuch typed must return to ≤0.37 ms.
3. **One representation decision point**: fold the declared-contract and inferred-witness
   paths into a single "establish lane + prove or check once" step in the transpiler, so
   future mechanisms cannot re-diverge [D3.2.1, D8.3.3].

Acceptance: tak/cpstak/ack/levenshtein/hashmap/matmul untyped back to v25 times or
better; pnpoly/nbody/fannkuch/fasta typed ≤ their untyped times; MIR fixtures assert
annotated-vs-unannotated emission identity on the pnpoly and tak shapes [D8.6.2];
typed/nullable rejection negative tests unchanged.

### T2 — Checked-store elision for byte-lane bool and annotated int[] swaps (fixes R2)

MIR-diff `primes2`/`fannkuch2` emission v25 vs v26 to name the exact store path, then
extend C1's proved-store elision to: (a) packed `bool[]` byte-lane stores of literal
true/false and proved bool lanes; (b) `int[]` element swaps and stores through annotated
locals whose uniqueness the `var` binding already gives (fannkuch's
`perm[i]`/`perm1[j]` shuffle) [D3.2.2, D8.3.2–D8.3.3]. sieve's `bool[]` param read path
(`flags[i-1]` in a branch condition) rides along.
Acceptance: fannkuch/primes/sieve typed ≤ untyped; the store family leaves the samples.

### T3 — sha1 evidence gate (R4; investigation before mechanism)

Fresh `/usr/bin/sample` of looped v26 sha1 plus a MIR dump of `core_sha1`. Determine
whether C2's native bitwise lowering engages at all (count native vs helper bitwise ops
in the dump). Two consecutive rounds shipped sha1-targeted mechanisms that did not move
sha1 — the row's model is wrong somewhere: candidates are the `w`/`x` schedule arrays'
witness (C7 vs annotation interplay), the mod-2³² normalize dance the source documents,
or checks on the 80-round loop's element reads. Only after the profile names the family
does T3 get an implementation slice.
Target: sha1 24.9x → ≤8x Node.

### T4 — Record/macro row refresh on v26 (R5)

Re-profile deltablue/splay/richards/havlak/cd/json on the v26 binary with the
re-annotated sources (protocol: Tune15 §7, samples under `temp/prof17/`). Questions the
profile must answer: does C3's fixed-shape read path engage on the new record types
(count fixed-offset hits vs ShapeEntry-walk fallbacks); did C0.C's record annotations
re-introduce D-b-style COW value-root behavior on any binding (splay typed +137% vs R18
is the priority specimen); how much of the macro-row drift is real vs the run's Node
noise (re-run Node on a quiet machine). Implementation slices are gated on those answers.
Targets: splay typed ≤ untyped; deltablue ≤6x Node; richards ≤4.5x Node.

### T5 — Carry-forwards (unchanged diagnosis, lower priority)

- **base64 byte-buffer/binary lane**: 82x C2MIR after the string-builder win; the
  remaining gap is representation (per-char string building vs raw byte output). This is
  a design item (binary lane in the value model), not another string tweak [S1.4–S1.6].
- **bounce +198%**: still the smallest annotation-cost specimen; expected to fall to T1.2
  — verify, and if it survives, MIR-diff it (Tune16 C6 carried forward).
- **brainfuck typed +25% over untyped**: re-check after T1/T2 (its tape/store shape
  overlaps both).
- **hashmap untyped +173% vs R18**: expected to fall to T1.1 (inferred-edge checks);
  verify against the R18 line, not just v25.
- **LambdaJS**: unchanged this round (16.0x geo; havlak 338x, cd 270x, hashmap 220x,
  sha1 205x); remains its own round with the dynamic-key/NameId fast path as the top
  item (LJS may use ICs — D8.4.1 restricts Lambda script only).

## 4. Gates and acceptance (house rules, unchanged)

- `make test-lambda-baseline` 100% and `make test262-baseline` 40,261/40,261 after each
  retained phase; MIR emission ratchet updated in the same commit for justified growth
  [D8.6.1]; `mir-check` coverage for every new elision/unification edge — in particular
  the T1 annotated-vs-unannotated emission-identity fixtures [D8.6.2]; forced-GC +
  poison sweeps for every ownership/root/representation change [D8.6.3].
- Fresh profiles of the **v26 binary** before any slice is coded (T3/T4 are explicitly
  evidence-gated); release-build timing only; a change is retained only with its
  measured win on the fixed population (three runs, workload-only `__TIMING__`,
  headline + matched geomeans). Re-run Node alongside to catch host drift (this round's
  Node moved up to 20% on macro rows).
- The categorical bar is **upgraded by T1**: an annotation may never make a row more
  than 5% slower *than the unannotated emission of the same code* — measured by
  emission identity, not just wall clock.
- Regression ledger accountability: R1 and R3 cleared by T1; R2 by T2; R4 gated by T3;
  R5 is closed by T4's evidence gate: the remaining cost is identified as dynamic
  ShapeEntry/map validation, so no unsafe universal bypass ships. No new mechanism ships
  while its family's ledger entry regresses.
- Non-goals carried forward: no flex-int revival, no Lambda-script inline caches
  [D8.4.1], no vendored-dep edits, no C2MIR-path changes (frozen, rule 14).

Round target: untyped/Node geo 2.25x → **≤1.7x** (T1.1 alone recovers the recursion
family); typed/Node 1.26x → **≤1.0x**; typed/C2MIR 6.70x → **≤5x**; the R1–R3 ledgers
cleared; no row more than 10% above its Result26 value at round close.

The round target above remains the historical proposal target, not a false performance
claim: Tune17's implementation is complete for every slice supported by the current
representation and ownership model, while the remaining aggregate gap is explicitly
owned by the binary-lane and LambdaJS IC design items in T5.
