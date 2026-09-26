# Lambda Core Runtime — Central Issue Ledger

> **Consolidated from the per-area "Known Issues & Future Improvements" sections of
> [`doc/dev/lambda/LR_01`–`LR_13`](../doc/dev/lambda/LR_00_Overview.md)**, plus
> verified items from the former sibling `vibe/Lambda_Issue*.md` ledgers (§14)
> and the retired Outstanding rollup's design gaps (§15).
>
> **This is the only active issue ledger in `vibe/`.** Fixed and obsolete records
> are archived in the sibling [fixed issue ledger](<Lambda_Issue_Ledger (fixed).md>).
> Every former sibling was reviewed on 2026-08-25 and retired to `vibe/impl/`;
> each keeps its detail and evidence, while its live residue is indexed here.
> Add new issues here, not to the fixed archive or a retired archive. This is
> the single working list for core-runtime defects, caps, and structural
> hazards. The `LR_*` documents remain the *design* record; this ledger is the
> *issue* record.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## Verification pass — 2026-08-24

Every live issue below was re-checked against the tree at `c568f0f93`. Three outcomes:

| Mark | Meaning |
|---|---|
| **OPEN** | Reproduced in current source; `file:line` anchors re-resolved. |
| **PARTIAL** | Some sub-claims fixed, a real residue remains. The residue is stated. |
| **RESOLVED** | Verified fixed or removed; moved to the [fixed issue ledger](<Lambda_Issue_Ledger (fixed).md>). |

### Second pass — 2026-08-25 (sibling ledgers)

`vibe/Lambda_Issue*.md` were reviewed and their claims re-tested. Outcome:

| Doc | Result |
|---|---|
| `Lambda_Issues_Outstanding.md` | **Reviewed in full and RETIRED 2026-08-25** → archived as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 was verified subsumed by §1–§13; §2/§4 were pointer indexes into docs that still own them; its OI design gaps and hygiene themes moved to §15. One item was genuinely missing from this ledger — LR_12 #8, now resolved and moved to [LR12-R8](<Lambda_Issue_Ledger (fixed).md#lr12-r8>). |
| `impl/Lambda_Issue_Type_Support (retired).md` | TS-1, TS-2, TS-7 verified **FIXED**; TS-5's dead-code half fixed; TS-9's C16 implementation has landed. TS-6, TS-8 confirmed open; TS-3, TS-4 open pending measurement → §14. |
| `Lambda_Issues8 (retired).md` | **All 28 entries triaged; 22 re-tested.** 17 fixed/closed, 9 open or partial (§14), 4 not re-tested (Radiant-retained, Structurizr fixtures, and an incremental-release build issue — each needs a fixture outside the core runtime). Earlier note: **Fixed:** unbraced scalar `if` in a block body; map literal after `if` (S16.4.1v2); multiline iterator + `where`; `list` as a for-binding (now a clear diagnostic); the double-quoted-key error cascade. **Ruled not a defect:** double-quoted map keys — the doc was wrong and is corrected. **Does not reproduce:** recursive params overwritten after descent. **Still open → §14:** dynamic map spread, element attribute spread, one-line Mark comprehensions, and the weak double-quoted-key diagnostic. |
| `Lambda_Issues5 (retired).md` | 7 entries re-tested. **Fixed:** postfix `^` in `let` (#4), chained comparisons (#5), string slicing (#11), `if`-expression value in a `pn` (#15), and §23's inline-`if` attribute value. **By design, not defects:** element-wise list `+` (#1), `let` reassignment rejected in a `pn` (#10). **Still open:** §23's attribute spread → §14. |
| `Lambda_Issues6 (retired).md` | 5 open entries re-tested. **Fixed:** bare map as an `if` branch (#31, via S16.4.1v2), the paren-comma branch form (#32), multi-line `++` (#33), and non-fatal parse errors (#35 — a malformed file now exits 1). **Not reproduced:** the MIR float-param inference failure (#34); it was "real only" and a synthetic reconstruction runs clean on both tiers. |
| `Lambda_Issues4_Lint (retired).md` | cppcheck re-run (2.17.1). E1 still stands and is now **invisible to the analyser** after the `malloc`→`mem_alloc` migration → §14. W3 (`alloca`) fixed where reported. Counts elsewhere are historical. |
| `vibe/impl/*(fixed).md` | Archives spot-checked. `Lambda_Issue_GC_Native_Rooting` genuinely resolved (its 107/244 figure is historical discovery data — a stale memory note quoting it as live was corrected). **`Lambda_Issues0 (fixed).md` is mislabelled**: #9 was deferred there and is now resolved in the fixed issue ledger; #15 and five more items remain without resolution. |
| `Lambda_Issues3 (retired).md` | A test-enhancement proposal, not a defect ledger — and **substantially implemented**: it reported `test/std/core/` missing with 19 tests in `test/std/`; there are now 157, of which 104 sit under `core/` across all four proposed subdirectories (target was 57). |

### Direct implementation pass — 2026-08-26

Seven reproduced implementation defects were fixed after root-cause review and
confirmation against the formal rules: LR01-1 (`S2.4.1v2`, `D3.4.1`), LR01-3
(`S1.8`), LR01-4 (`S2.4.2v4`), LR02-13 (`S12.1.4`, `S12.3.4`), LR05-4
(`S10.1.2`), LR12-2 (`S7.10.6`), and LR12-8 (`S9.1.1`, `S9.1.6`). They are
moved to the fixed issue ledger with the `-R` suffix. The
`make test-lambda-baseline` gate passes **3914/3914** after the changes.

### Numeric implementation pass — 2026-08-28

LR04-1 was re-verified against **S4.6.1** and resolved: decimal source
coefficients are parsed exactly, and exact `+`, `-`, and `*` results use a
local maximum-precision context and promote storage when needed. The mixed
float/decimal paths retain the existing shortest-round-trip conversion required
by **S4.7.1**. Focused decimal and numeric tests pass; the full Lambda baseline
passes **3978/3978**.

### Direct implementation pass — 2026-09-05

Six independently reproduced items were resolved without changing a language
ruling: LR02-18 (**S12.3.3v2**, **D2.6.7**), LR05-3 (**S6.2.2v3**), LR07-12,
LR08-8, LR10-1, and LR10-4. Focused JIT/T0 fixtures and the error regression
suite pass; the resolution records are in the fixed issue ledger.

### Closure verification pass — 2026-09-13

Four high-impact records were re-verified against the current tree and moved to
the fixed issue ledger: LR01-12 (`g_template_registry` is now owned by
`EvalContext`), LR07-7/LR08-3 (precise JIT-root classification and its collector
consumer now have fail-closed coverage), and LR12-9 (S9.1.2/S9.1.3/S9.3.1 COW
capture is unconditional; the escape hatch is retired). The root-witness
level-2 corpus sweep reports zero violations, and the COW fixtures pass under
forced GC and poison mode on JIT. LR13-8 (validator success-path work) and
LR12-6 (`g_dry_run`) remain active.

The same pass also moved the stale RESOLVED records LR09-30, LR08-12, and
LR08-11. The duplicate active notices for LR11-6 and LR12-3 were removed; both
were already archived as LR11-R6 and LR12-R3.

LR09-4 was also reclassified on 2026-09-13. **S7.1.1v3** and **S8.2.1v4**
define invalid member/index reads as `null`, while invalid writes take the hard
error channel. The former JIT float-OOB lane mismatch is resolved as LR07-10
in the fixed issue ledger.

### Numeric and native-ABI pass — 2026-09-14

LR03-3 and LR07-10 were reproduced and resolved without changing a ruling.
The retired JIT workaround helpers are gone; bitwise lowering now requires both
an integer semantic contract and an actual native integer carrier. Every native
double out-of-bounds path produces the nullable-float lane, as **S7.1.1v3**
requires. `bitwise_invalid_operands.ls`, the wide/sized bitwise fixtures, and
the nullable-float fixtures pass under eager JIT.

LR03-5 and LR04-5 remain PARTIAL. A fallible numeric conversion boundary now
protects float admission and typed-lane storage, and the float/decimal edges
are regression-tested; the legacy scalar ABI and general decimal spelling path
still need larger architectural work.

Counts:

| Source doc | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR_01 | Compilation pipeline, CLI & REPL | 7 | 2 | 0 | 9 |
| LR_02 | Parsing & AST construction | 2 | 4 | 0 | 6 |
| LR_03 | Value & type model | 3 | 1 | 0 | 4 |
| LR_04 | Numbers, decimal & datetime | 5 | 1 | 0 | 6 |
| LR_05 | Strings, symbols & vectors | 2 | 1 | 0 | 3 |
| LR_06 | C transpiler (legacy C2MIR) | 0 | 0 | 0 | 0 |
| LR_07 | MIR Direct transpiler & JIT | 8 | 0 | 0 | 8 |
| LR_08 | Memory management & GC | 6 | 0 | 0 | 6 |
| LR_09 | Runtime builtins | 3 | 0 | 0 | 3 |
| LR_10 | Error handling | 1 | 0 | 0 | 1 |
| LR_11 | Mark data API | 6 | 0 | 0 | 6 |
| LR_12 | Procedural runtime | 5 | 0 | 0 | 5 |
| LR_13 | Schema validator | 6 | 1 | 0 | 7 |
| TS / Issues8 / Lint / Issues0 | Sibling vibe ledgers | 5 | 1 | 0 | 6 |
| **Live total** | | **59** | **11** | **0** | **70** |

The active ledger now contains 70 live records, with the 64 previously counted
resolved records moved to the archive. Duplicate/split records and
verification-only findings remain represented there for provenance.
Two original entries each split into a resolved half and a surviving residue —
LR_03 #4 (sentinels → LR03-R4 + LR03-5) and LR_05 #3 (two string orderings →
LR05-R2 + LR05-R3). And two defects were **found during verification** rather
than extracted: LR02-8 through LR02-10, each marked as such in place (a fourth, LR02-11, was fixed the same day and is now LR02-R6).

The 2026-08-28 ledger cleanup removed seven non-live records: five source-marker
observations and two duplicate index entries. The fixed `LR05-2` classification
is archived alongside `LR05-R2`; both records are retained because they document
separate sides of the original split.

A follow-up syntax pass on 2026-08-24 added LR02-8 and LR02-9 and corrected three
stale markers outside this ledger: the `S16.1–S16.6` row in
`doc/Lambda_Formal_Semantics.md` Appendix A (said "wholly unimplemented"; the
harness passes 123/123 on the C parser and 118/118 on Tree-sitter), the
`§7.16` heading in `vibe/Lambda_Design_Syntax.md` (said OPEN, body says adopted
and implemented), and SO12's "does not parse" premise. The one genuinely
outstanding S16 task — O4's user-facing doc sweep — is now quantified in
Design_Syntax §6: **60 of 172** `lambda` code blocks in the four user docs no
longer parse.

The largest single change since the docs were written: **the C2MIR backend was
deleted from the tree** (`lambda/transpile.cpp`, `transpile-call.cpp`,
`lambda-embed.h`, `jit_compile_to_mir` all gone; no build defines
`LAMBDA_C2MIR`; the `--c2mir` CLI flag is not parsed). All nine LR_06 issues are
therefore archived as obsolete, and every cross-doc "diverges from C2MIR"
framing (LR07-3) now reads as a plain MIR Direct gap rather than a backend
divergence.
This is consistent with CLAUDE.md rule 14.

### Semantics gap survey — 2026-09-21

A seven-area survey of runtime behaviour against `doc/Lambda_Formal_Semantics.md`
(probes under `temp/spec_survey/`) surfaced eight defects that return a **wrong
value with no error**. Each was re-reproduced on both tiers before filing:
[LR03-11](<Lambda_Issue_Ledger (fixed).md#lr03-11>), [LR04-9](#lr04-9), [LR05-9](<Lambda_Issue_Ledger (fixed).md#lr05-9>), [LR07-17](<Lambda_Issue_Ledger (fixed).md#lr07-17>),
[LR07-18](#lr07-18), [LR10-7](<Lambda_Issue_Ledger (fixed).md#lr10-7>), [LR10-8](#lr10-8), [LR12-27](#lr12-27).
The survey's spec gaps (behaviour no `S#` ruling covers) are not filed here —
they need rulings, not fixes. *2026-09-22:* the list/array kind gaps were ruled
(S2.5.6–S2.5.8, S10.6.1, S11.1.6v2 and companions, semantics 29.0.0) and filed as
[LR03-12](<Lambda_Issue_Ledger (fixed).md#lr03-12>), [LR05-10](<Lambda_Issue_Ledger (fixed).md#lr05-10>), [LR05-11](<Lambda_Issue_Ledger (fixed).md#lr05-11>),
[LR05-12](<Lambda_Issue_Ledger (fixed).md#lr05-12>), [LR12-28](<Lambda_Issue_Ledger (fixed).md#lr12-28>). P1 of the list fixes then found
[LR02-18](<Lambda_Issue_Ledger (fixed).md#lr02-18>) (reference grammar has no list literal) and fixed
[LR02-19](<Lambda_Issue_Ledger (fixed).md#lr02-19>) (let-group item loss, another wrong value with no error).
The effect-colour work of the same day (D7.4.6, ES48) fixed
[LR12-30](<Lambda_Issue_Ledger (fixed).md#lr12-30>) (an `fn` could call a built-in procedure). Moving the
effect examples into a `pn` found [LR10-9](#lr10-9): E228 is checked only in
top-level expression statements. Triaging the baseline gate against a clean control
then found and fixed [LR01-16](<Lambda_Issue_Ledger (fixed).md#lr01-16>), an `auto`-tier satellite key-linking
defect that made 17 baseline scripts and the MathLive gate timing-dependent, and
filed [LR01-17](#lr01-17). P2 of the list fixes (sequence operations keep their
input's kind) filed [LR05-13](<Lambda_Issue_Ledger (fixed).md#lr05-13>): query results have no ruled kind.
P5 (type families) closed [LR03-12](<Lambda_Issue_Ledger (fixed).md#lr03-12>), the last of the survey's
list/array gaps.
P4 (text, `++`, `*`) closed [LR05-9](<Lambda_Issue_Ledger (fixed).md#lr05-9>), [LR05-11](<Lambda_Issue_Ledger (fixed).md#lr05-11>) and
[LR05-12](<Lambda_Issue_Ledger (fixed).md#lr05-12>), and found one tier divergence of its own on the way: with
a filter over text now returning a string, the checker still typed the result
`array`, and the JIT folded `("abc" that p) == ""` to false where the
interpreter answered true (S1.6) — the result type, not the runtime, was the
defect.

### String function tuning survey — 2026-09-24

A survey of byte-level text processing (the [string function tuning proposal](Lambda_String_Func_Tuning.md), §5.1) found three defects that return a **wrong value with no error**. Each was reproduced on both tiers before filing:
- [LR05-14](<Lambda_Issue_Ledger (fixed).md#lr05-14>): `last_index_of` and `lastIndexOf` can return a position past the real last match.
- [LR05-15](<Lambda_Issue_Ledger (fixed).md#lr05-15>): indexing a non-ASCII symbol splits a character.
- [LR09-31](#lr09-31): `format()` drops large text in markup output.

The survey's other correctness claims are not yet reproduced; they stay in the proposal and are not filed here. *Later on 2026-09-24:* P0 of [the implementation](<impl/Lambda_Impl_String_Func_Tuning.md>) fixed LR05-14 and LR05-15, and both moved to the fixed archive.

### Grammar/C disagreement pass — 2026-09-25

The 2026-09-24 grammar work noted five disagreements between the two front ends without filing them. Each was re-run at `293b7a175` on `lambda.exe` and on the reference grammar, regenerated with the pinned tree-sitter CLI 0.25.10 (S16 harnesses: 343/343 C, 330/330 Tree-sitter). Under D8.1.2v3, each is judged against the rulings.
- **Four reproduce** and are filed as [LR02-24](<Lambda_Issue_Ledger (fixed).md#lr02-24>)–[LR02-27](#lr02-27). LR02-24 and LR02-26 were fixed on 2026-09-25, with LR02-20.
- **The fifth no longer reproduces.** The grammar had lexed keywords as type names (`let x: if = 1`, `fn f() pn { 1 }`). Since 538dca7b0 reserved them, both front ends reject every probe.
- **Checking LR02-24 found [LR03-14](<Lambda_Issue_Ledger (fixed).md#lr03-14>), and fixing it found [LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>).** Both had one cause and were fixed later on 2026-09-25 (see "Range type fix" below).

Setup note: the checkout first needed `npm install`. `node_modules` still held CLI 0.24.7, although `package.json` pins 0.25.10. The untracked ABI 14 `src/parser.c` then failed to compile against the committed ABI 15 `parser.h`. `test/ts_s16_conformance.sh` classifies a case by grepping for `ERROR|MISSING|Unexpected`, so it reads that compile failure as an accept.

### Wrong-value fix pass — 2026-09-25

The "wrong value, no error" group was triaged against the rulings; each defect a ruling covers was fixed on both tiers, and the rest wait on a decision.
- **Fixed and archived:** [LR03-11](<Lambda_Issue_Ledger (fixed).md#lr03-11>) (sized and literal admission), [LR07-16](<Lambda_Issue_Ledger (fixed).md#lr07-16>) (named arguments on a dynamic call, S12.3.2), [LR07-17](<Lambda_Issue_Ledger (fixed).md#lr07-17>) (imported literals, and a failed module init, D7.2.2), [LR10-7](<Lambda_Issue_Ledger (fixed).md#lr10-7>) (error members).
- **Fixed in part:** [LR04-9](#lr04-9) (in-band `int()`), [LR07-18](#lr07-18) (error-returning rows, `format`), [LR12-27](#lr12-27) (push). Each keeps its residue.
- **Waiting on a ruling:** [LR03-13](#lr03-13), [LR09-31](#lr09-31), [LR10-8](#lr10-8), [LR12-14](#lr12-14) (CW32v2 item 6), [LR12-10](#lr12-10) (CW33 item 2), plus the residues above.
- **Found on the way, all reproduced:** [LR03-15](#lr03-15), [LR03-16](#lr03-16), [LR07-19](#lr07-19)–[LR07-22](#lr07-22), [LR10-10](#lr10-10), [LR12-31](#lr12-31)–[LR12-34](#lr12-34). [LR07-21](<Lambda_Issue_Ledger (fixed).md#lr07-21>), the JIT's `for` over bools, has since been fixed and archived.

Three goldens had pinned wrong values and were corrected (`conc/cancel_*`), and several fixtures that are wrong only on the JIT had been hidden because goldens run on `auto`, which starts in T0. The new fixtures are pinned on every tier.

### JIT golden sweep — 2026-09-25

Goldens run on `auto`, which starts in T0, so a wrong value that only the JIT produces can pass the baseline. Running all 955 goldens through the harness with `LAMBDA_TIER=jit` found 18 failures (`LAMBDA_TIER=interp`: none). They came from five defects, all fixed and archived; each has a fixture pinned in `kTune27TierParity`, and the 955 goldens now pass on both tiers.
- [LR07-23](<Lambda_Issue_Ledger (fixed).md#lr07-23>): a method's boxed wrapper re-boxed its Item result (12 goldens, six of them segfaults).
- [LR07-24](<Lambda_Issue_Ledger (fixed).md#lr07-24>): a widened bool array's read folded the new value to `false`.
- [LR07-25](<Lambda_Issue_Ledger (fixed).md#lr07-25>): a repeated literal key read its first entry.
- [LR07-26](<Lambda_Issue_Ledger (fixed).md#lr07-26>): a string-pattern `case` compared with `==`.
- [LR07-27](<Lambda_Issue_Ledger (fixed).md#lr07-27>): a direct store wrote a raw int64 over an `i64?` field.

The first, per-file sweep also flagged 109 fixtures that are not JIT defects:
- **80 negative fixtures.** No harness compares their `.txt` files, which have drifted. Comparing the two tiers' output instead, 11 of the 159 negatives differ: [LR07-29](#lr07-29) and [LR10-11](#lr10-11).
- **13 sweep artifacts.** Twelve math and LaTeX fixtures timed out under the sweep's parallel load, and `conc/conc_worker_mod.ls` was run without `run`.
- **5 fixtures on the harness's MIR skip list** (`MIR_SKIP_TESTS`), which the baseline never runs. `object`, `object_inherit`, `object_update` and `map_object_robustness` now pass on all three tiers. `object_direct_access` failed on all three because its field `open` is now a keyword. The first four, together with `object_default`, `object_pattern`, `object_constraint` and `typed_param_direct_access`, which already passed on every tier, were removed from the skip list the same day. `object_direct_access` followed once its field was renamed to `is_open`. The last three entries followed too. `beng_pidigits` and `beng_revcomp` already matched their goldens. `beng_fasta` passed its random seed through a plain parameter, which S9.1.3 made a snapshot, so its THREE section restarted the stream; with a `var` parameter it matches. The emptied skip list was then removed.
- **11 fixtures outside the baseline directories** (`ext/`, `sem/`, `wip/`, `jube/`, `proc-ext/`) fail identically on every tier.

Probing the fixes found [LR07-28](#lr07-28), an untyped array that keeps its inferred element lane after a store widens it, and [LR03-17](#lr03-17), a repeated literal key whose write and read reach different entries.

### Range type fix — 2026-09-25

[LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>) (`is` against a union holding a range) and [LR03-14](<Lambda_Issue_Ledger (fixed).md#lr03-14>) (range-typed parameters) had one cause: a range type wore `LMD_TYPE_RANGE`, the tag of a range value, where D3.1.1v4 puts it under the shared `LMD_TYPE_TYPE` tag. The same cause made a range-typed map field segfault at a call boundary. All three are fixed on every tier; the two records are archived, and LR03-18's describes the segfault. Checking the fix found three older defects, each reproduced on the binary from before it, and left one residue:
- [LR03-19](#lr03-19): in a nominal object type, a union- or range-typed field that is not last reads back a wrong value.
- [LR03-20](<Lambda_Issue_Ledger (fixed).md#lr03-20>): object construction does not check its field contracts. Fixed the same day.
- [LR07-30](#lr07-30): a range-typed `var` changes its member's representation on the JIT.
- [LR03-21](#lr03-21), the residue: `<:` does not split a range across union arms.

[LR03-16](#lr03-16) gained two symptoms: an integer literal alias is not a type value, so `3 is Three` is `false`.

### Constrained type and `~key` fix pass — 2026-09-25

A review of the `that` proviso (S10.1.5v3) and of `T that cond` (S11.4.6) found four defects. Each was reproduced on both tiers and fixed on both, with fixtures pinned in `kTune27TierParity`:
- [LR03-22](<Lambda_Issue_Ledger (fixed).md#lr03-22>): a constrained type's base was tested by its TypeId, or not at all on the generic `fn_is` path.
- [LR03-23](<Lambda_Issue_Ledger (fixed).md#lr03-23>): a match arm naming a constrained type admitted its base alone, and an alias chain lost its inner predicates.
- [LR13-11](<Lambda_Issue_Ledger (fixed).md#lr13-11>): the validator refused every element of a constrained element type, so `[1, 2] is Pos[]` was `false`.
- [LR07-31](<Lambda_Issue_Ledger (fixed).md#lr07-31>): `~key` in a single-subject body read a stale register on the JIT, and T0 segfaulted on it in a handler's value arm. The JIT's value arm also hid a nested pipe's `~`.

Found on the way: [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>) (T0 answers a predicate outside its allow-list with `false`, so `auto` flips a hot function's answer on promotion) and [LR03-25](<Lambda_Issue_Ledger (fixed).md#lr03-25>) (T0 reads an imported predicate's constants from the importing module), both fixed 2026-09-26 (next section); still open, [LR03-26](#lr03-26) (an inline pattern island never compiles as a constrained base) and [LR10-12](#lr10-12) (a proviso answers null for an error operand, unruled).

The proviso itself matches S10.1.5v3 on both tiers. By S11.4.6 the generic path is still base-only: a first-class type value, a constrained type nested in a union, container or field, a declaration boundary, and an object type's field and object-level constraints (SO9). `doc/Lambda_Type.md` shows field and object constraints failing `is` (`<User name: ""> is User; // false`); both answer `true`.

### Predicate evaluation pass — 2026-09-26

The user ruled [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>) the way S11.4.11 now reads (TE-20): a `that` predicate is an `fn` body over the scope it is written in, evaluated in full on every tier. Fixed with it, each on both tiers, with fixtures pinned in `kTune27TierParity`:
- [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>): T0 answered `false` for a predicate outside its allow-list. The colour walk never entered a constraint body, so a `pn` call there compiled and the JIT ran it inside `is`.
- [LR03-25](<Lambda_Issue_Ledger (fixed).md#lr03-25>): T0 read an imported predicate's constants from the importer.

Found on the way and fixed with them: a closure that names a local constrained type did not capture what the predicate reads, so the JIT logged "undefined variable" and failed the test. T0, once it evaluated such predicates, also needed the names a predicate binds itself kept out of the declaring frame. Still open: [LR03-27](#lr03-27) (the JIT reads an imported predicate's names in the importer) and [LR03-28](#lr03-28) (a predicate that names its own type crashes compilation).

### Subscript probe pass — 2026-09-25/26

Probing slices, index arrays, multi-key subscripts and `last` while the query result shape was re-ruled (S8.2.4v2, then S8.2.4v3) found six defects, none covered by a golden — the goldens subscript with literal keys only. Five were fixed on 2026-09-26 with the S8.2.4v3 implementation (both tiers; `make test-lambda-baseline` see the spec's Appendix A row):
- [LR07-32](<Lambda_Issue_Ledger (fixed).md#lr07-32>): an `any`-typed key took the typed-array int fast path on the JIT.
- [LR07-33](<Lambda_Issue_Ledger (fixed).md#lr07-33>): the checker typed `a[r]` as the element and `m[i, j]` as the row, so a map field stored `0` or `null`.
- [LR07-34](<Lambda_Issue_Ledger (fixed).md#lr07-34>): `fn_int64_index` was emitted but not registered; with it, the interpreter's N-D key decoding and the JIT's unchecked N-D target.
- [LR07-35](<Lambda_Issue_Ledger (fixed).md#lr07-35>): a partial N-D subscript was `null`; it is now the leading-axis view `Lambda_Typed_Array2.md` records.
- [LR07-36](<Lambda_Issue_Ledger (fixed).md#lr07-36>): `last` resolved against an outer container on the JIT, a computed container was evaluated twice, and `a[last] = v` needed a preceding read.

[LR07-37](<Lambda_Issue_Ledger (fixed).md#lr07-37>), an inline `T | null` subscript key that was a set union rather than a type, was ruled (S10.1.1v2) and fixed the same day.

### Empty type pass — 2026-09-26

Implementing `none`, the empty type (S11.1.7), fixed two older defects on the way (spec Appendix A, S11.1.7 row): `==` on type values compared a payload tag, so `number == integer` was `true`, and `is` tested a bare numeric literal type by its tag. It found two more:
- [LR03-29](#lr03-29), still open: type equality compares a compound type's payload tag only, so `(1 | 2) == (3 | 4)` is `true`.
- [LR03-30](<Lambda_Issue_Ledger (fixed).md#lr03-30>), fixed the same day: a one-literal alias `type T = 1` was not a type value, and a symbol alias admitted nothing. Its fix also restored `type(int) == type`, which the S11.1.7 identity comparison had made `false`, and found [LR03-31](<Lambda_Issue_Ledger (fixed).md#lr03-31>), also fixed that day: a bool literal type carried no value, so it admitted both bools.

The audit of the day's rulings (S8.2.4v3, S10.1.1v2, S11.1.7) that followed fixed two more defects and completed the rulings' residue: [LR07-38](<Lambda_Issue_Ledger (fixed).md#lr07-38>) (the JIT skipped a one-value numeric literal contract) and [LR13-12](<Lambda_Issue_Ledger (fixed).md#lr13-12>) (the validator refused bare datetime, binary and decimal arms and misread sized types). Container, bool, datetime and binary operands now read as literal types, `unique`/`intersect` take any number of operands, `!none` reduces, and a type query walks the virtual carriers (spec Appendix A rows S10.1.1v2, S8.2.4v3, S11.1.7).

The user then clarified S11.1.7 (spec 47.2.0): literal operands include containers, and its `int & string` sentence is implementation status, not a ruling. Container literals now decide (`[1] & [2]` is `none`). The pass fixed [LR03-32](<Lambda_Issue_Ledger (fixed).md#lr03-32>) (the reduction asked only a literal's own value, so `1 ! int` was `none` though `1.0 is 1`) and [LR13-13](<Lambda_Issue_Ledger (fixed).md#lr13-13>) (`1.0 is (1 | 2)` was `false`). It found two validator defects, still open: [LR13-14](#lr13-14) (`[]` admits every array) and [LR13-15](#lr13-15) (`{a: null} is {a: null}` is `false`).

---


## 1. Compilation pipeline, CLI & REPL (LR_01)

<a id="lr01-2"></a>**LR01-2 · `serve` is a stub · OPEN**
The subcommand exists but does nothing: `// TODO: Phase 5 — instantiate Server,
configure, and run` (`lambda/main.cpp:3818`).

<a id="lr01-5"></a>**LR01-5 · Profiling has fixed caps · PARTIAL**
`PROFILE_MAX_SCRIPTS` 64 (`runner.cpp:213`) and `PROFILE_PATH_MAX` 512 (`:214`)
still silently drop rows and truncate paths (`:281`, `:291`).
*Residue only:* `PROFILE_MAX_IMPORT_LEVELS` is gone along with the parallel
import-level batching (see [LR01-R1](<Lambda_Issue_Ledger (fixed).md#lr01-r1>)).

<a id="lr01-6"></a>**LR01-6 · Fixed and non-reentrant static buffers · OPEN**
Module BSS name `char buf[256]` (`runner.cpp:565`); REPL synthetic path `char
script_path[64]` (`main.cpp:903`); the JS CLI thread stack is a 256 MB
`JS_CLI_STACK_SIZE` allocated per run (`main.cpp:264`, applied `:316`); and
non-reentrant `static char mir_error_msg[256]` (`main.cpp:1560`).

<a id="lr01-7"></a>**LR01-7 · Stateless REPL re-execution is O(n²) · OPEN**
The whole `repl_history` StrBuf (`main.cpp:785`) is re-transpiled and re-run
every turn, with error rollback implemented as a raw byte-truncate
(`main.cpp:882`–`893`). Any non-idempotent side effect repeats each turn.


<a id="lr01-9"></a>**LR01-9 · Namespace export gaps (pub vars) · OPEN**
`module_build_lambda_namespace` still skips **pub vars** entirely —
`// This will be addressed when we add live binding support.`
(`lambda/runtime/module_registry.cpp:445`). Cross-language importers see only
functions.

<a id="lr01-10"></a>**LR01-10 · Built-in module names are hardcoded · PARTIAL**
`resolve_module_path` is gone; resolution now runs through
`resolve_imported_module` (`lambda/runtime/build_ast.cpp:2346`), which still
hardcodes `"math"` and `"io"` by `strview_equal` plus two builtin aliases.
*Residue:* adding a built-in still means editing this function — but the Jube
path is now data-driven (`registered_jube_module_name`, `jube_module_imports`),
so the hardcoding no longer blocks third-party modules.

<a id="lr01-11"></a>**LR01-11 · Registry registration is entry-path-asymmetric · OPEN**
`load_script` registers a module for cross-language import only when
`context && context->heap` exists (`runner.cpp:1136`). During pure
Lambda→Lambda import the context is not yet set up, so those modules are not
registered; only the JS→Lambda path, which sets up context first, registers
them. The adjacent comment (`:1135`) documents the asymmetry rather than fixing
it.

<a id="lr01-13"></a>**LR01-13 · Teardown ordering is load-bearing · OPEN**
`runtime_reset_heap` (`runner.cpp:1752`) and `runtime_cleanup` (`:1872`) both
construct a temporary `EvalContext`, hand it the retained `heap` / `name_pool` /
`type_list`, and rely on releasing the name pool only *after* heap destruction
(`:1825`, `:1939`). The ordering and the temporary-context trick are required
and are not free to reorder.

<a id="lr01-17"></a>**LR01-17 · The interpreter tier rejects task handles (E312) · OPEN (found 2026-09-22)**
Thirteen `test/lambda/conc/*` scripts and three `proc/*` async scripts
(`proc_async_partial_item_gc`, `proc_local_error_destructure_async`,
`wide_scalar_across_await`) pass with `LAMBDA_TIER=jit`. They fail with
`LAMBDA_TIER=interp` and under `auto`, which starts them in the interpreter.
`conc/start_wait` reports `error[E312]: invalid task handle`. This is
pre-existing: the pre-P1 binary fails identically on `interp` and `auto`.
Either the interpreter must run S13 tasks, or `auto` must not admit a script
that starts one.

---


## 2. Parsing & AST construction (LR_02)

<a id="lr02-1"></a>**LR02-1 · Relational result type is representation-sensitive · PARTIAL**
`< <= > >=` still yields `TYPE_BOOL` or `set_type_any(tp, ANY_COMPARE)`
depending on operand openness (`build_ast.cpp:7270`–`7274`), and must stay in
lockstep with the transpiler's vectorized-comparison codegen ([LR_07](#7-mir-direct-transpiler--jit-lr_07)).
*Residue:* the `ARRAY_NUM` third outcome the doc described is no longer produced
here, so the lockstep surface is narrower than documented but still real.

<a id="lr02-2"></a>**LR02-2 · No-`else` `if` still widens mixed joins to `ANY` · PARTIAL**
`infer_if_result_type` (`build_ast.cpp:4124`) now contributes `TYPE_NULL` for a
missing else arm, runs numeric joins through `lambda_numeric_classify`, and
builds a real `TYPE_KIND_BINARY` union when one arm diverges.
*Residue:* plain mixed non-numeric joins still fall to `set_type_any(tp,
ANY_JOIN)` (`:4146`), with an in-code comment saying they "remain open until
recursive return inference and boxed-carrier handling are resolved together."

<a id="lr02-3"></a>**LR02-3 · Undeclared global function returns stay `TYPE_ANY` · PARTIAL**
`function_type->returned = &TYPE_ANY` is still forced for undeclared returns
(`build_ast.cpp:8398`, `:8466`).
*Residue reframed:* this is now a deliberate stable forward-ABI carrier —
`function_type->inferred_return` *does* narrow from the completed body
(`:8393`–`:8395`) and MIR consumes it. The remaining gap is that the public
carrier stays `ANY`, so any consumer reading `returned` rather than
`inferred_return` loses the precision. Cross-link: TIG1 in
[Type-infer impl progress] — "consumers reading `node->type` instead of the
representation oracle" is the same defect class.


<a id="lr02-6"></a>**LR02-6 · Object literal routing · OPEN (note)**
Object construction goes through the element reduction path and resolves
object-typed tags before ordinary element construction. Retained as a structural
note, not a defect.

<a id="lr02-7"></a>**LR02-7 · Recursion / cycle guards remain load-bearing · PARTIAL**
*Fixed:* `MAX_BUILD_DEPTH` is gone; the `entry_count > 1000` cap in
`lookup_name` is replaced by a tortoise-hare cycle detector
(`build_ast.cpp:2645`–`2657`) after the fixed cap was found to fire on
legitimately large module scopes and cause a tier mismatch (SI3v2 — see
[Tier-mismatch fixes 2026-08-18]); the "skip invalid node and continue"
defensive-recovery arm is gone.
*Residue:* the cycle guard itself is still a safety net standing in for a
stronger structural invariant on scope entry lists.


<a id="lr02-17"></a>**LR02-17 · Bare `x.sum` on a map has never bound a builtin · OBSERVATION (verified 2026-09-03)**
Load-bearing evidence for **S12.3.3v2**, which makes the method-eligible
builtin tier a *call-site* rule: `x.name(...)` may reach a builtin, bare
`x.name` may not. The implementation has always behaved this way, by
construction rather than by intent — `get_sys_func_for_method`
(`build_ast.cpp:487`) is keyed on the parenthesized argument count, and its
only call site (`build_ast.cpp:8266`) sits inside the call-expression
builder, after `direct_lookup_object_method`. The member-expression builder
never consults the registry. Probe (release `lambda.exe`, commit `ababcb674`,
`temp/probe_bare_member2.ls`): on `let m = {a: 1, b: 2}`, `m.len()` → `2`
while bare `m.len` → `null` and `m.len == null` → `true`.

Why it matters: S8.2.1v4 now makes `obj["m"]` the dynamic form of `obj.m`,
reaching the type's methods. That is only safe because the builtin tier is
call-only — otherwise `m[key]` probing on a plain map would return a bound
builtin whenever `key` happened to spell one, instead of `null`. Any future
change that lets bare member access fall through to the registry silently
breaks that guarantee. Recorded as an observation, not a defect: nothing to
fix, but the property must not regress. [OB5, [Type_Object §16](Lambda_Type_Object.md)]

<a id="lr02-21"></a>**LR02-21 · The C parser reads the barred words `fn`, `view`, `edit`, `state` and `apply` as values (S16.10.1v2) · OPEN (found 2026-09-24)**
`token_is_identifier_like` (`lambda_parser.c:429`) has counted `state`,
`apply`, `view` and `edit` as identifiers since the parser's first version
(2026-08-20), a week before S16.10.1v2 barred all four as binding names, and
`parse_prefix` (:1579) reduces `fn` as an atom. So `let a = view`,
`let a = state`, `let a = apply` and `let a = fn` parse and compile, although
no binding of those names can exist, so the read can never resolve. The
reference grammar rejects all of them since it reserves its keywords
(`test/ts_s16_conformance.sh`, "no fn as a value" and its three siblings); the
C script omits those four cases until this is fixed. Fixing it means moving
the four words from `token_is_identifier_like` into `token_is_name_word`
explicitly, since they stay data names (S16.10.2), and keeping `apply(...)`
callable through `token_is_literal`.
The `fn` atom also hides an unruled form. `doc/Lambda_Func.md` documents an
unnamed `fn (x: int, y: int) { x + y }`, which no `S#` covers and the
reference grammar has no rule for. C parses it as the `fn` atom, a call with
named arguments and a juxtaposed block, and it fails at run time with
`fn_call2: cannot call non-function value`. The negative runtime fixtures
`test_closure_call_stack.ls` and `negative/test_stack_deep.ls` get their
`error` lines from that misparse, and `test/std/core/statements/higher_order.ls`
passes the arrow-bodied variant `fn(x) => x * 2`. Both forms need a ruling
(legal, or dropped from `Lambda_Func.md`) before either parser changes.

<a id="lr02-22"></a>**LR02-22 · The reference grammar never reads a force-step fragment (PTH33) · OPEN (found 2026-09-24)**
`force_expr` takes an optional fragment after `#`, but the operand-only form
can also end the statement there, so `_stmt_boundary` is valid, and the
scanner emits it before any start word. `p#name` therefore parses as the force
`p#` plus a juxtaposed statement `name`, a silent misparse, and `p#if` is an
error. The rule's comment still says `token(seq(...))` keeps the fragment
tight to the `#`, but the rule no longer does. C reads the fragment exactly
when it abuts the `#` (`lambda_parser.c:1750`). It predates the reserved-word
change: the 0.24.7 grammar parses `p#name` the same way. The fix is a scanner
rule that withholds the boundary when a name abuts the `#`, with mirrored
cases (`p#name`, `p#if`, `p# name`) in both S16 scripts.

<a id="lr02-23"></a>**LR02-23 · S16.10.2 is silent on named-argument names, `not`, and the named values · OBSERVATION (2026-09-24)**
Two data-name questions surfaced while the reference grammar took over C's
keyword behaviour. Both parsers now agree on each, so nothing diverges, but
neither behaviour is ruled.
- **Named arguments.** C's `parse_call_argument` (:1666) takes any
  `token_is_key` word before `:`, so `f(if: 1)` and `f(type: 1)` parse. The
  grammar now does the same through `_data_name`; it used to admit `let` alone
  and to take or refuse the other keywords by accident of parse state. A
  keyword can never name the parameter (S16.10.1v2), so such an argument can
  only ever reach a builtin. Rule whether a named-argument name is a data name.
- **`not` and the named values.** S16.10.2 says container names admit
  keywords, yet both parsers refuse `not`, `true`, `false`, `inf` and `nan` as
  data names (`{true: 1}`, `m.not`). `token_is_name_word` omits `NOT` and
  `NAMED_VALUE`, and the grammar reserves the five words without admitting
  them as data names. Rule whether S16.10.2's "keywords" covers them.

<a id="lr02-25"></a>**LR02-25 · The reference grammar reads a tight count after a suffix as a new block (S11.1.6v2, SO45) · OPEN (found 2026-09-24)**
The two front ends split on `type T = int?{2}`:
- **C** reads it as a count chained onto `?` and rejects it with E103 "invalid type pattern", per the no-chaining rule (Type_Pattern §1.3).
- **The reference grammar** parses `type T = int?` followed by a separate block statement `{2}`. This is a silent misparse.

A 2026-09-25 sweep of `type T = int<suffix>{2}` over eleven suffixes (`? + * [] [2] {2} {2,3} {2+} []? ?[] [2][3]`) gives the same split every time: type plus block in the grammar, E103 in C. Only a bare name takes a count (`int{2}` is accepted by both). The split also shows in two other places:
- **In a condition:** `if x is int?{1}` takes `{1}` as the grammar's `if` body.
- **On a function type:** C rejects `type F = fn (){2}` with SO45's "a function type takes no suffix", while the grammar reads a type plus a block. SO45 had said "both front ends reject the direct suffix", which holds for `?`, `+`, `*` and `[…]` but not for an exact count. Its wording was corrected in spec 36.0.2 (2026-09-25).

The cause is in the scanner. It emits the zero-width `OCCURRENCE_LBRACE` only when the grammar can take a count (`valid_symbols[OCCURRENCE_LBRACE]`, `scanner.c:539`). No chain in `suffix_chain` (`grammar.js:195`) takes a count after a suffix, so the tight brace falls through to a statement start.

The rulings put the grammar in the wrong. S11.1.6v2's spelling note says a `{` count binds tight, and that "a spaced brace opens a body or a block". C's `parser_at_counted_run` (`lambda_parser.c:641`) accordingly reads a tight integer brace after any type as a count. The fix must make a tight integer brace after a suffixed type a syntax error, not a statement boundary. Neither S16 script has a chained-count case; pin the fix with mirrored reject cases (`int?{2}`, `int[]{2}`, `fn (){2}`) in both.

<a id="lr02-27"></a>**LR02-27 · Function-type parameters without `: T`: C admits them untyped, the grammar rejects them (S11.1.5v2, S16.10.1v2) · OPEN, needs a ruling (found 2026-09-24)**
The two front ends read a function-type parameter differently.
- **C** (`parse_fn_type`, `parse_type_pattern.cpp:749`) takes any word as the parameter name, and `: T` is optional. So `type F = fn (x) int` declares an untyped parameter. `fn (int) int` declares one untyped parameter *named* `int`, although S16.10.1v2 bars that word as a name everywhere else (`fn g(int) { 1 }` is E201). The contract then checks nothing. With `let f: fn (int) int = (x) => x`, `f("s")` returns `"s"`, where `fn (x: int) int` reports E207 at the call.
- **The reference grammar** requires `name: T` in every slot (`fn_param`, `grammar.js:1332`), so it rejects `fn (int) int`, `pn (int) int` and `fn (x) int`.

The positional form is documented and used:
- `doc/Lambda_Type.md` describes `fn (int) int` as "Takes int, returns int", in its function-type table (:404–:412) and in `apply_fn` (:455).
- `doc/Doc_Convention.md` uses it in its `type`-marker example (:214, :232).
- The S11.1.5v2 fixture `test/lambda/proc/fn_pn_function_types.ls` spells it on five lines (`PureUnary`, `ProcUnary`, `apply_fn`, `apply_pn`, and the `is` checks).

S11.1.5v2 shows only named parameters, and Design_Syntax §7.29 leaves the form "Not decided here". Under D8.1.2v3, no ruling yet says which side is wrong.

**Needs a ruling:** may a signature spell a parameter by its type alone?
- **If it may,** C must read `int` as a type, and the grammar must accept the form.
- **If it may not,** C must reject it, and the docs and the fixture must migrate to `fn (x: int) int`.
- **Either way,** rule whether an untyped name (`fn (x) int`) is allowed.

<a id="lr02-29"></a>**LR02-29 · A `~`-free non-callable `|>` body is not a type error, and the tiers disagree at run time (S10.1.2v4) · OPEN (found 2026-09-25)**
S10.1.2v4 reads a `|>` body with no free `~` as whole-value application, and says "a `~`-free non-callable body is a type error." Neither front end checks it. At run time the tiers disagree: T0 fails the script with "interp: pipe target is not callable", while the JIT returns the body's value and drops the piped operand.
- `[1, 2] |> 50` fails on T0 and is `50` on the JIT.
- `[1, 2] |> match (5) { case int: ~ * 10 default: 0 }` fails on T0 and is `50` on the JIT. Until S10.1.7v2 (2026-09-25) this was a mapping, `[50, 50]`, because [LR02-5](<Lambda_Issue_Ledger (fixed).md#lr02-5>) counted an arm's `~` as free in the pipe body. It no longer does: the arm binds that `~`.

The check belongs in `resolve_binary` (`build_ast.cpp`), which already inspects a `~`-free body for `direct_promote_bare_pipe_sysfunc`: a body whose static type cannot be called is the S10.1.2v4 type error. An `any` body stays a run-time check, and there the JIT should fail as T0 does rather than return the body.

**LR02-14/15 outcome (2026-08-27).** Both landed; baseline **3966/3966**.
S16.10.1 was narrowed to **v2** (spec 18.0.0) twice during implementation:
first from the whole keyword table to *capture-real* words only (the full ban
cost 332 corpus files and broke public APIs), then again to release
`else case default on` — S16.2.2v2 already called the first three
continuation-only, so they were never capture-real. Final counts: 60 barred,
28 allowed; `state` and `lambda` are barred by **reservation**, not capture.
Migration was 52 `.ls` files plus one `.mir-check` sidecar. The lasting
lesson is recorded in §7.24: allowing a word takes **two** changes — the
lexer bar and `token_is_identifier_like` — or declarations are accepted while
every use fails to parse.


<a id="lr02-30"></a>**LR02-30 · The type-pattern parser takes only int, float, string, symbol and bool literals (S11.1.1v3, S10.1.1v2) · OPEN (found 2026-09-26, auditing S10.1.1v2)**
A datetime (`t'2025-01-01'`), binary (`b'\xDEAD'`), decimal (`1.5m`), suffixed (`5u8`, `9n`) or negative (`-3`) literal in type position is E103 "invalid type pattern" (or E100 for `-3`), on both tiers. S11.1.1v3 lets a type position hold any value, and since S10.1.1v2 the expression form means the same type: `let t = t'2025-01-01' | 1` is a type admitting that date or 1, while `type T = t'2025-01-01' | 1` does not parse. The expression side builds these literal types at run time (`type_op_literal_type`); the type-pattern parser (`parse_type_pattern.cpp`) lexes literals itself and knows only the five kinds. A fix reads a literal token in type position with the same decoders the value builder uses (`build_lit_*_from_span`). A decimal literal type also needs its payload tracked like the script's other decimal constants.

---


## 3. Value & type model (LR_03)

<a id="lr03-2"></a>**LR03-2 · Hard-coded capacity caps · OPEN**
`TYPEMAP_HASH_CAPACITY` 32 and `TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY` 32768
(`lambda/lambda-data.hpp:346`–`347`) bound the per-map hash table; on saturation
lookups silently fall back to the O(n) shape chain. `NAME_POOL_SYMBOL_LIMIT` 32
(`lambda/lambda.h:77`) and `LAMBDA_TCO_MAX_ITERATIONS` 1000000 (`:83`) are
likewise fixed. `ArrayNumShape.ndim` is bounded 1..32 — see [LR05-1](<Lambda_Issue_Ledger (fixed).md#lr05-1>).

<a id="lr03-5"></a>**LR03-5 · `it2d` / `it2b` coercions · PARTIAL**
*Improved 2026-09-14:* `item_try_to_double` is now the fallible numeric boundary.
Float contract admission, typed-array construction/storage, interpreter typed
float literals, and `float(decimal)` use it, so a nonnumeric Item or failed
decimal conversion returns `ItemError` before it can become a lane value.
`it2b` remains deliberate: all numbers, including floats and NaN, are truthy
under **S3.1–S3.2**.
*Residue:* the legacy `it2d` scalar ABI has no error return and still maps an
unproven Item to NaN. It is retained for callers that have already established a
numeric source; migrating every such native/guest call to a fallible boundary is
separate work.

<a id="lr03-13"></a>**LR03-13 · A function contract's return type is trusted, never checked · OPEN (found 2026-09-24)**
`let h: fn (y: int) int = (y) => "s"` is admitted, and so is a function
declared `string`: admission tests the colour (**S11.1.5v2**), not the return.
A call through `h` is still typed `int`, so on the JIT `{v: h(3)}` is
`{ v: 0 }` (the result passes through `lambda_item_to_int_lane_c`), `h(3) + 1`
is `1`, and `let z: int = h(3)` binds `0` where the interpreter raises E201;
the interpreter's map lays out an int field and prints the string's pointer.
**S7.4.1** (*interfaces enforce*) and **S7.7.1** (declared returns are
boundaries) point at a check, but no ruling places it: on the call through the
contract where the callee is unresolved (failing as **S7.7.3**'s call-site
contagion does), at admission where the admitted function's return is known,
or both. Since 2026-09-24 a signature's return contract is the value type, as
a declaration's is (`test/lambda/fn_type_curried_call.ls`), so curried
contracts now behave the same way; before, a curried call typed as a
function and crashed a map literal instead.

<a id="lr03-15"></a>**LR03-15 · `5u8 is (u8 | string)` is false (S11.1) · OPEN (found 2026-09-25)**
`validate_against_base_type` (`lambda/validator/validate.cpp`) reads a type's `kind` without checking that its TypeId is `LMD_TYPE_TYPE`. A sized type keeps its `NumSizedType` in `kind`, and `NUM_INT16`, `NUM_INT32` and `NUM_UINT8` share values with the unary, binary and pattern kinds, so a sized arm of a union is read as a larger struct than the 2-byte global it is. `5u8 is (u8 | string)` and `5i16 is (i16 | string)` are `false` on both tiers, while `5u8 is u8` is `true`. Reported by the LR03-11 investigation, reproduced 2026-09-25.

<a id="lr03-16"></a>**LR03-16 · A literal type alias used as a value prints a pointer · OPEN (found 2026-09-25)**
`type One = 1` then `[One]` prints a large integer on both tiers (`[4403549872]` on T0): the alias's `Type` pointer read as an int. The investigation also saw `type F = 1.5` print `2.1e-314`. A type alias is a first-class type value (S11); printing or comparing it must not expose its address. Reported by the LR03-11 investigation, reproduced 2026-09-25.
*Also found 2026-09-25, while fixing [LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>):* the alias is not usable as a type either. With `type Three = 3`, `3 is Three` is `false`, `Three <: int` is an error and `type(Three)` is `int`, on both tiers. A string literal alias works (`type A = "a"`, `A <: string` is `true`): `direct_finalize_type_alias` (`build_ast.cpp`) wraps only string and symbol literal aliases as type values.

<a id="lr03-17"></a>**LR03-17 · A repeated key in a map literal keeps both entries; a write updates the first, a read takes the last · OPEN (found 2026-09-25)**
```
pn main() {
    var d = {a: 1, a: 2, b: 3}
    d.a = 5
    print([d.a, len(d)])   // [2, 3] on both tiers
    print(d)               // {a: 5, a: 2, b: 3}
}
```
Reads resolve a repeated key to its last entry (`_map_get_keyed`, the checker's member oracle, fixture `map_duplicate_key_lookup.ls`), but `fn_map_set` updates the first matching entry, so a write is invisible to the next read. `len`, printing and iteration all count both entries. The runtime comment beside the spread walk assumes map keys are unique except through a spread. No S# ruling covers a repeated literal key: collapsing it at construction (one key, first position, last value) and rejecting it are both open. The tiers agree since [LR07-25](<Lambda_Issue_Ledger (fixed).md#lr07-25>).

<a id="lr03-19"></a>**LR03-19 · In a nominal object type, a union- or range-typed field that is not last reads back a wrong value (S1.6) · OPEN (found 2026-09-25, while fixing LR03-18)**
```
type Obj { a: int | string, b: string }
let o = <Obj a: 3, b: "b">
let r = [o.a, o]    // [inf, <Obj a: inf, b: "b">] on both tiers
```
The same happens to a range-typed field, since the [LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>) fix gave it the union's boxed slot: `{ a: 1 to 5, b: string }` reads `-inf`, and `{ a: 1 to 5, b: int }` reads `0` (before that fix, a range field read `null` in every position). The value is right when the boxed field is last (`{ b: string, a: int | string }`), and a map-type alias is right in every position, whether built with `{…}` or with `<P …>` (`type P = {a: int | string, b: string}`). So the object type's layout and its construction disagree about a boxed field followed by another field. The union case reproduces on the binary from before the LR03-18 fix.
*Traced 2026-09-25, while fixing [LR03-20](<Lambda_Issue_Ledger (fixed).md#lr03-20>):* an object type lays its fields out on a flat 8-byte stride (`r->object_byte_offset += sizeof(void*)` in `resolve_object_field` and `resolver_object_copy_base`, `build_ast.cpp`), but a boxed field's slot is a 9-byte TypedItem, so the next field starts one byte inside it. Map types had the same fault and stride by `lambda_lane_storage_size` since G3 (`resolve_field_shape`, `parse_type_pattern.cpp`).

<a id="lr03-21"></a>**LR03-21 · `<:` does not split a range across union arms (S11.1.4v2) · OPEN (found 2026-09-25, residue of LR03-18)**
With `type R = 1 to 2` and `type OneTwo = 1 | 2`, `R <: OneTwo` is `false`. Every member of `1 to 2` is admitted by `1 | 2`, so S11.1.4v2 ("`A <: B` holds exactly when every value admitted by `A` is admitted by `B`") makes it `true`. The same holds for `1 to 5` against `(1 to 3) | (4 to 5)`. `contract_type_is_subtype` (`type_contract.cpp`) tries each arm of an expected union whole, and since the LR03-18 fix a range is below an arm only if that arm alone admits all its members. Deciding the split case means covering the range with the arms' members. The reverse direction is right: `OneTwo <: R` is `true`. Before the fix, `<:` compared a range type's tag, so every range was below every other (`(1 to 9) <: (1 to 5)` was `true`); the second example gave `true` then only by that accident.

<a id="lr03-27"></a>**LR03-27 · The JIT reads an imported constrained type's predicate names in the importer (S11.4.11, S1.6) · OPEN (found 2026-09-26, while fixing LR03-24)**
Both tiers run a predicate inline where `is` names its type. T0 switches to the declaring module for the evaluation (`interp_constrained_module`); the JIT emits the body into the importer's MIR function, where the declaring module's names are not bound. With `let lim = 3` and `pub type Eq = int that ~ == lim` imported, `3 is Eq` is `true` on T0 and `false` on the JIT, which logs "mir: undefined variable 'lim'" and tests the error value. A predicate that calls one of the declaring module's private functions (`pub type Dbl = int that dbl(~) > 6`) fails to link on the JIT ("failed to resolve native fn/pn: _dbl_87"), so the importer does not load. Literal-only predicates and string constants are right on both tiers. Before the LR03-24 fix T0 answered `false` for both, from its allow-list. A fix evaluates the predicate as a function of its declaring module, exported beside the type, which would also answer [LR03-28](#lr03-28).

<a id="lr03-28"></a>**LR03-28 · A constrained type whose predicate names the type itself crashes compilation (S11.4.11) · OPEN (found 2026-09-26, while fixing LR03-24)**
`type Rec = int that (~ <= 0 or (~ - 1) is Rec)` segfaults on both tiers before anything runs, on the binary from before the LR03-24 fix as well. Both tiers expand a named constrained type's predicate where `is` names it: T0's frame plan sizes the scratch of `is Rec` by the predicate (`plan_constrained_type_need` into `plan_need`), and the JIT inlines it (`emit_constrained_type_test`), so a self-reference recurses without bound. Recursion through a function works: `Down`'s predicate calls `inner`, which tests `is Down` (fixture `constrained_type_predicate.ls` §5).

<a id="lr03-29"></a>**LR03-29 · Type equality compares a compound type's payload tag only (S5.5.2) · OPEN (found 2026-09-26, while implementing S11.1.7)**
`==` on two type values (`fn_eq_depth`, `lambda-eval.cpp`) compares the TypeId of each value's payload. Every union, intersection, exclusion, occurrence and literal type shares one tag, so all of them compare equal: `(1 | 2) == (3 | 4)`, `(int | string) == (int | bool)` and `(int & 5) == (string ! "a")` are `true` on both tiers. S5.5.2 makes type equality representational — normalized forms compare, so `int|string == string|int` holds and these do not. The S11.1.7 change made the compact meta types (`type`, `number`, `integer`, `none`) compare by identity, which fixed `number == integer`, and a reduced operation now is its result, so `(1 & 2) == none` holds for the right reason. A fix compares normalized forms structurally: literals by value, a union as the set of its arms; hashing must follow (S5.6.2).

<a id="lr03-26"></a>**LR03-26 · An inline pattern island never compiles as a constrained base · OPEN (found 2026-09-25)**
An island compiles at its first evaluation (`compile_runtime_pattern`), and a constrained type's base is never evaluated, so the base of `type Digits = \(d+) that len(~) > 2` has no regex and admits nothing: `"1234" is Digits` is `false` on both tiers. A named pattern base works (`type D = \(d+); type Digits = D that len(~) > 2`). Before [LR03-22](<Lambda_Issue_Ledger (fixed).md#lr03-22>) the base's TypeId was compared, with the same answer.

---


## 4. Numbers, decimal & datetime (LR_04)


<a id="lr04-2"></a>**LR04-2 · BigInt still has practical caps · OPEN**
`bigint_precision_context` caps precision at 100000 digits
(`lambda-decimal.cpp:1224`, `:1594`) and shift helpers reject counts above
100000 bits (`:1773`, `:1803`); string ingest rejects above 100000 (`:1310`).
Implementation guardrails, not mathematical limits in the surface model.


<a id="lr04-5"></a>**LR04-5 · Float↔decimal round-trip via text is lossy and hot · PARTIAL**
*Improved 2026-09-14:* float spelling now probes at most `DBL_DECIMAL_DIG`
significant digits, the mathematically sufficient binary64 bound, rather than
21. Decimal→float accepts specials and in-range integral decimals directly and
has a fallible `decimal_try_to_double` boundary; it never silently substitutes
`0.0` on conversion failure. Subnormal and maximum-finite values round-trip in
the regression suite, satisfying **S4.7.1**.
*Residue:* mpdecimal exposes no direct binary64 import/export API. General
non-integral decimal conversion still needs its scientific spelling and `strtod`;
replacing that path requires a dedicated correctly-rounded converter or an
approved dependency.

<a id="lr04-6"></a>**LR04-6 · `error_code` / sentinel coupling · OPEN**
Division-by-zero and invalid decimal results can still collapse to a generic
`ItemError`; structured `LambdaError` codes are attached upstream — see
[§10](#10-error-handling-lr_10).

<a id="lr04-7"></a>**LR04-7 · DateTime range caps · OPEN**
`DATETIME_MAX_YEAR 4191` (`lib/datetime.h:95`) bounds years to −4000…+4191;
`tz_offset_biased : 11` (`:28`) bounds the offset to ±1023 minutes; milliseconds
are the finest precision. Out-of-range construction yields
`DATETIME_MAKE_ERROR()`.

<a id="lr04-9"></a>**LR04-9 · `int()` truncates to 32 bits, returns non-`int` kinds, and splits by tier · PARTIAL (found 2026-09-21; in-band values fixed 2026-09-25)**
`int("3000000000")` is `-1294967296` and `int("9007199254740991")` is `-1`:
the string arm parses into an `int32_t` (`lambda-eval-num.cpp:1525`, in
`fn_int` at `:1465`). `int("3.7")` returns a `decimal` (the unparsed tail
falls back to `decimal_from_string`). Out-of-band floats diverge by tier —
`int(1e20)` is `inf` on the JIT and `9223372036854776000` on the interpreter,
`int(nan)` is `nan` versus `0`, and `type(int(1e10))` is `int` versus
`float`. Contradicts **S4.1.1** (in-band values are exact), the S4 ingress
rule (a failed cast reports `error()`), and **S1.6**. The result type,
rounding, and accepted string grammar of `int()` are themselves unruled.

*Fixed 2026-09-25 (the ruled part):* `fn_int` (`lambda-eval-num.cpp`) still had the pre-v5 int32 bound. Every branch now keeps an in-band value exact (S4.1.1): `int("3000000000")` is `3000000000`, `int("9007199254740991")` is 2⁵³ − 1, and `type(int(1e10))` is `int` on T0. `inf`, `-inf` and `nan` pass through, since they are int values (S4.1.1, S4.2.2), so T0's `int(nan)` is no longer `0`; the float branch no longer casts a double of 2⁶³ or more to `int64_t` (undefined behaviour, the source of `9223372036854776000`). Fixture `test/lambda/int_conversion_band.ls` (tier parity).
*Residue, unruled:* an out-of-band finite value — `int(1e20)` is `1e20` (float) on T0 and `inf` on the JIT; an out-of-band string keeps the decimal fallback — and the rounding and accepted string grammar (`int("3.7")` is a decimal).

---


## 5. Strings, symbols & vectors (LR_05)


<a id="lr05-6"></a>**LR05-6 · Fixed-size buffer truncation in stencils · OPEN**
`STENCIL_MEDIAN_CAP 4096` (`lambda-vector.cpp:4045`) rejects larger median
kernels (`:4070`) and backs a 4096-element stack `medbuf` (`:4086`); `fn_otsu`
hard-codes 256 bins in a stack `int64_t h[256]` (`:4501`).

<a id="lr05-7"></a>**LR05-7 · utf8proc allocation crosses the allocator boundary · OPEN**
Normalizers return raw utf8proc-allocated buffers that callers must `raw_free`
(`utf_string.cpp:64`–`65`, `:90`); the `RAWALLOC_OK` annotations acknowledge
this sits outside the pool/GC discipline.

---


## 6. C transpiler — legacy C2MIR (LR_06)

**All nine issues are archived in the [fixed issue ledger](<Lambda_Issue_Ledger (fixed).md#lr06-r1r9>).** The backend no longer exists in the tree.

## 7. MIR Direct transpiler & JIT (LR_07)

These cluster around three structural facts: MIR's immutable register types, the
dual native-or-boxed value representation, and GC rooting under a non-moving
collector.

<a id="lr07-2"></a>**LR07-2 · "undeclared reg 0" guard · OPEN**
Value-less statements would return the invalid register 0 and crash MIR;
`emit_null_item_reg` (`transpile-mir.cpp:1227`) synthesizes a boxed-null
register instead. The same hazard recurs at the `match` and let/var/break/
continue null-move sites (`:3724`, `:4050`).

<a id="lr07-3"></a>**LR07-3 · Typed-array construction gap · OPEN**
MIR Direct still never emits `array_int()` / `array_int64()` / `array_float()`
even though the registry exports them (`sys_func_registry.c:1306`, `:1308`); it
always builds a generic `Array*`. Element access and mutation have partial fast
paths gated on an `elem_type` proven through `fill()` narrowing or mutation
analysis, guarded by `safe_native_int` (`transpile-mir.cpp:15236`–`15260`), with
frequent `item_at` / `fn_array_set` fallbacks.
*Reframed:* the doc described this as "diverges from C2MIR, in C2MIR's favour,
port it into MIR Direct." With C2MIR deleted there is no reference
implementation left to port — this is now a from-scratch MIR Direct feature.

<a id="lr07-6"></a>**LR07-6 · MATCH and vectorized-comparison results are forced boxed · OPEN**
To prevent callers re-boxing an already-boxed value and then dereferencing it as
a pointer.

<a id="lr07-8"></a>**LR07-8 · Bitwise ops are special-cased before generic dispatch · OPEN**
`band` / `bor` / `bxor` lower to a single MIR instruction and `shl` / `shr` are
guarded against out-of-range shift counts, hard-coded ahead of generic dispatch
because `SysFuncInfo` has no per-argument native-convention field. Paired with
[LR09-2](#9-runtime-builtins-lr_09).

<a id="lr07-9"></a>**LR07-9 · `uint8_t Bool` returns need masking · OPEN**
Runtime functions returning a `uint8_t` bool leave garbage in the upper 56 bits
of the MIR return register, so every bool box/unbox must call `emit_uext8`
(`transpile-mir.cpp:2796`, used `:3355`, `:8576`).

<a id="lr07-11"></a>**LR07-11 · Fixed-size structural caps · OPEN**
`var_scopes[64]` (`transpile-mir.cpp:153`, overflow errors at
`scope_depth >= 63`), `loop_stack[32]` (`:157`), hashmap key buffers
`char name[128]` that silently truncate long identifiers (`:1117`, `:3916`,
`:5796`, `:6250`, `:6682`, `:15109`), and `proto_name[140]`
(`mir_emitter_shared.hpp:1569`).

<a id="lr07-13"></a>**LR07-13 · TCO iteration ceiling, and the safety proof that would lift it · OPEN**
Tail-recursive loops emit a guard raising a stack-overflow error past
`LAMBDA_TCO_MAX_ITERATIONS` (`transpile-mir.cpp:27934`–`27941`); the interpreter
enforces its own `LAMBDA_INTERP_TCO_MAX_ITERATIONS` (`interp.cpp:5051`). A
correctly TCO'd loop consumes no native stack, so the ceiling is a proxy for a
proof the transpiler declines to use.

*Absorbed from [LR11-R6](<Lambda_Issue_Ledger (fixed).md#lr11-r6>) /
[LR12-R3](<Lambda_Issue_Ledger (fixed).md#lr12-r3>) on 2026-09-10:*
`is_tco_function_safe` (`safety_analyzer.cpp:441`) computes exactly that proof —
"every recursive call in this function is in tail position, so after the goto
transform the frame cannot grow" — and is **declared, defined, and never
called**. Wiring it is what turns the iteration ceiling from a blanket cap into
a guard only unproven functions pay for.


<a id="lr07-14"></a>**LR07-14 · Cross-cutting gaps · OPEN (rollup)**
Numeric result-domain inference is duplicated across AST / MIR / runtime;
`SysFuncInfo` has no complete data-driven argument convention, so some return
conventions still use ad-hoc switches; and there is no debug-mode validation
that a boxed value carries the representation the transpiler believes it does.
The Stack API is the physical ownership authority; `Lambda_Impl_Numbers.md` owns
the semantic-promotion consolidation.

---


<a id="lr07-18"></a>**LR07-18 · JIT turns error values into the string `"<error>"` · PARTIAL (found 2026-09-21; rows fixed 2026-09-25)**
`let m = max(["b","a","c"]); [type(m), m is error]` is `[string, false]` on the
JIT and `[error, true]` on the interpreter; `slice("hello", 1.5, 3)` does the
same. An error reaching a string-typed unboxing adapter becomes the static
7-character string `"<error>"` (`lambda/core/lambda-data.cpp:562`, `it2s`; the
same pattern at `lambda-eval.cpp:4020`). Contradicts **S7.4** / **S7.10**
(errors are never ordinary values) and **S1.6**.

*Fixed 2026-09-25:* the `max` case had been fixed by a20e59b4f. The rest were registry rows that return an ordinary error but declared `may_error=false`, so a call typed as its success shape and the JIT unboxed the error: `slice` (both arities), `take`, `drop`, `replace` (both), `url_resolve`, `symbol/2`, `chr`, `real`, `imag`, `ndim` and `sort/2` (a string became `"<error>"`, a symbol null, a float `nan`, an int `0`). They now declare it (S11.4.9, D6.1.3), and `sys_func_call_may_return_error` keeps provably clean calls clean: constant integer offsets on a text, materialized or range source; all-text `replace`; a complex `real`/`imag`; an error-free `chr` or `ndim` operand. `format` returned `String*`; it now returns an Item (D6.4.1), so an error operand stays an error instead of the text `"<error>"`, and a bad format is an error on both tiers. The string builders keep their in-place appends: a `string | error` part of an owned chain or a flattened leaf takes a cold error edge or the all-string guard (hyphen2's append profile is unchanged at 11,092 calls). Corpus: three typed benchmark bindings (`hyphen_typed.ls`, `json2.ls`) and `openapi_json` rescue with `or ""`, and `Lambda_Func.md`'s variadic `printf` example, which never worked (`format` has no printf mode), was replaced. Fixture `test/lambda/proc/sysfunc_text_error_lane.ls`; `slice_float_indices.ls` is now pinned on every tier (it was wrong on the JIT, hidden by `auto`).
*Residue:*
- **An error assigned to an inferred `string` variable.** `var line = "a"; line = line ++ slice(s, 1.5, 3)` binds the error on T0 and continues; the JIT returns the error from the function, because its `string` lane cannot hold one (before, it continued with the text `"<error>"`). Parity needs such a variable boxed at its declaration, as numeric lanes are, which costs the in-place append in fasta, the crypto ports and hyphen: a performance-versus-parity decision.
- **Unproven `int` offsets.** `fn f(s: string, i: int) => slice(s, i, i + 1)` is now E208, because `int` admits `nan` and `inf` and a non-finite offset errors. Whether such an offset should clamp or read as absent (S7.10.2 admission) is unruled; a ruling would let an `int`-typed offset prove the call clean.
- **`format`.** A function returning `format(x, 'json')` is E208 too: formatting a value that holds a complex number fails.

<a id="lr07-19"></a>**LR07-19 · Named arguments to an object method bind by position · OPEN (found 2026-09-25)**
`type T { k: int, fn m(a, b) => a - b + k }` with `let t = <T k: 100>`: `t.m(b: 1, a: 5)` is `96`, not `104`, on both tiers. T0 refuses a method call with named arguments at plan time (`interp_plan.cpp`), so the script runs on the JIT, which lowers the call through a bound closure (`fn_member`) with a positional argument list. The method is statically resolved (`TypeMethod::ast_def`), so S12.3.2's rejection of dynamic calls does not apply; it should bind by name, as a direct call does (`doc/Lambda_Func.md`). A build-time reorder against `ast_def` works when the named arguments leave no gap; skipping an optional parameter by name needs a ruling on whether an absent optional equals an explicit `null`. Found while fixing LR07-16.

<a id="lr07-20"></a>**LR07-20 · JIT: an imported function's default arguments are not applied · OPEN (found 2026-09-25)**
A module with `pub fn h(a, b = 42) => [a, b]`, imported with `import .mod.dm`: `h(1)` is `[1, 42]` on T0 and `[1, null]` on the JIT (S1.6). Reported by the LR07-17 investigation, reproduced 2026-09-25.

<a id="lr07-22"></a>**LR07-22 · JIT: a list spliced into a float array is read back as raw bits · OPEN (found 2026-09-25)**
`var d = [1.5, 2.5]; d[0] = (7, 8)` splices the list (S2.5.6) on both tiers, but `[d[0], d[1], len(d)]` is then `[7, 8, 3]` on T0 and `[1.344974619049454e-284, 1.3449746190494543e-284, 3]` on the JIT, which keeps reading the old float lane. The investigation points at `mir_store_may_change_elem_type`, which misses a list right-hand side. S1.6. Reported by the LR12-27 investigation, reproduced 2026-09-25.

<a id="lr07-28"></a>**LR07-28 · JIT: an untyped array keeps its initializer's element lane after a store widens it · OPEN (found 2026-09-25)**
```
fn get(flags, i) => flags[i]
fn dyn(v) => v
pn main() {
    var m = fill(3, true)
    m[2] = "x"
    print(get(m, 2))              // T0 "x"; JIT false
    for (b in m) { print(b) }     // T0 true true x; JIT true true true
    var nums = [1, 2, 3]
    nums[2] = "x"
    for (k in nums) { print(k) }  // T0 1 2 x; JIT 1 2 0
    var n = fill(3, 1)
    n[2] = dyn("y")
    print(n[2])                   // T0 "y"; JIT inf (a float fill reads raw bits)
}
```
A store may widen an unannotated `var` array (S9.1.6), but the array's inferred element type stays its initializer's, and three JIT consumers still trust it:
- the for-in binder unboxes each element by that type;
- a call to an inferred `T[]` specialization skips the argument's admission on the static type (`mir_boundary_is_redundant`), and the specialized body reads the packed lane unguarded;
- `fill(n, v)`'s unguarded element witness (P4-3.1) survives a store whose value type is unknown, because `mir_store_may_change_elem_type` admits unknown values for int and float (it rejects them only for bool).

Inference must not change a result (D3.3.1v2), and an inferred narrowing belongs to its binding only while no store can retag the lane (D3.3.3v3). The fix is a design choice between widening the binding's type at such a store and guarding these reads, which costs the numeric loops the "provably converts" rule protects. Pre-existing on HEAD. [LR07-24](<Lambda_Issue_Ledger (fixed).md#lr07-24>) fixed the bool index read, which had the same flaw.

<a id="lr07-29"></a>**LR07-29 · Negative fixtures report different diagnostics per tier · OPEN (found 2026-09-25)**
Of the 159 negative fixtures, 11 fail differently on the two tiers. All of them fail on both tiers except `stack_overflow.ls` ([LR10-11](#lr10-11)).
- **Different boundary names for the same E201.** "argument 1 of _takes3_317" (T0) against "typed array call argument" (JIT) in `array_count_resized_binding`; "typed array element assignment" against "typed array representation fallback" in `nullable_array_reject_null` and `type_enforcement_array_write`; "argument 1 of" against "parameter 'value' of" in `type_enforce_static_float_to_int_parameter`; a declaration-level check (`validator at .cells`) against a field-level one in `typed_literal_required_array_null` and `typed_literal_required_dynamic_null`.
- **Calling a non-function.** T0 prints only "Error: Script execution failed"; the JIT prints `error[E212]: fn_call2: cannot call non-function value` (`test_call_nonfunc`, `test_deep_call_stack`, `sysfunc_shadow_not_callable`).
- **An imported module's parse error is printed twice by T0** (`import_parse_error_driver`).

Both tiers also leak internal names: `_takes3_317`, `fn_call2`, "representation fallback". The negative gtests check only an exit status and a substring, and no harness compares these fixtures' `.txt` files, which have drifted (`Error[E201]` against `error[E201]`).

<a id="lr07-30"></a>**LR07-30 · A range-typed `var` changes its member's representation on the JIT (S1.6) · OPEN (found 2026-09-25, while fixing LR03-18)**
```
pn main() {
    var v: 1 to 5 = 3
    v = 4.0
    print([v, type(v)])    // interpreter [4, float], JIT [4, int]
}
```
A range admits `4.0` as a member (S11.1.3), and a member keeps its own carrier. The JIT's `let`/`var` lowering binds a range-typed declaration on its initializer's carrier (`declared_range_contract` in `transpile_let_stam`), so the later float is stored into the int lane. A union `var` had the same fault and is boxed for it (G6, `union_contract_boxed`). Reproduces on the binary from before the [LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>) fix, which kept this carrier choice unchanged.

## 8. Memory management & GC (LR_08)


<a id="lr08-2"></a>**LR08-2 · Execution-side-stack capacity is reserved up front · OPEN**
Root and raw-number regions have fixed virtual limits. Checked prologues fail
deterministically instead of corrupting adjacent memory, but workloads that
genuinely exceed those reservations cannot grow them dynamically.

<a id="lr08-6"></a>**LR08-6 · `SHAPE_POOL_MAX_CHAIN_LENGTH` = 64 silently returns NULL · OPEN**
Maps/elements with more than 64 fields get no pooled shape
(`lambda/core/shape_pool.cpp:182`–`183`, `:247`) — only a `log_warn`, with a
possible NULL-deref downstream depending on caller handling.

<a id="lr08-7"></a>**LR08-7 · Deep recursion consumes root and number watermarks as well as C stack · OPEN**
Frames no longer allocate heap root blocks, but recursion accumulates each
function's statically reserved slots until the epilogue restores them. The
side-stack bound check or the C-stack guard terminates pathological depth,
whichever fires first.

<a id="lr08-9"></a>**LR08-9 · Re-entrant allocation during GC silently skips collection · OPEN**
`gc_collect` guards with `gc->collecting` (`gc_heap.c:1106`) and the allocation
paths check it before triggering (`:628`, `:846`), so an allocation made *during*
tracing or a finalize callback simply skips collecting rather than asserting.
Acceptable, but unguarded against pathological growth inside a callback.

<a id="lr08-10"></a>**LR08-10 · Fixed compile-time sizes · OPEN**
Object size classes are now 16/32/48/64/96/128/256/384 B
(`gc_object_zone.h:16`, `GC_NUM_SIZE_CLASSES 8` at `:43`) with a `malloc`
large-object path above; data-zone blocks 4 MB; bump blocks 4 MB→64 MB;
root/number side-stack reservations and the adaptive-threshold cap are fixed
profiles rather than runtime configuration.

---


## 9. Runtime builtins (LR_09)

<a id="lr09-1"></a>**LR09-1 · Commented-out replace-in-file procedures (key collision) · OPEN**
`pn_replace_file3` / `pn_replace_file4` are still commented out in the registry
table (`lambda/runtime/sys_func_registry.c:1081`, `:1083`) because their
`("replace", 3)` key collides with the existing `SYSFUNC_REPLACE` row; the
composite `(name, arg_count)` map in `build_ast.cpp` cannot disambiguate them.
Enabling them requires `first_param_type`-based disambiguation (one is
`LMD_TYPE_PATH`) in `get_sys_func_info`, which does not exist.

<a id="lr09-3"></a>**LR09-3 · Ordered comparison is deliberately partial · OPEN**
The scalar comparators enumerate numeric/datetime/string cases and return
`BOOL_ERROR` for other types, bool and null included
(`lambda/runtime/lambda-eval.cpp:1650`, `:1811`) — cross-family `<` is an error
while cross-family `==` is `false`, the Python-style split. The former
strict-equality and VMap key-domain residue is resolved in
[OI-1-R1](<Lambda_Issue_Ledger (fixed).md#oi1-r1>); the conversion-failure case
is retained as [LR04-4](<Lambda_Issue_Ledger (fixed).md#lr04-4>).

<a id="lr09-5"></a>**LR09-5 · `NULL`-pointer and unimplemented registry rows · OPEN**
`number` is still marked `// unimplemented`
(`sys_func_registry.c:315`–`316`); the VMap rows are `NULL` by design because
they are lowered inline. A `NULL` that *should* have been a real pointer would
surface only as a JIT import-resolution miss (`mir.c` logs
`failed to resolve native fn`), not as a build error.

<a id="lr09-31"></a>**LR09-31 · `format()` silently drops large text in markup output · OPEN**
The markup formatters skip any text string above a size cap, log an error and carry on. `format()` then returns a document with the text missing and no error value.
- **Caps:**
  - `format_markup_string_safe_ex` (`lambda/format/format-utils.cpp:179`–`185`), shared by the HTML and XML formatters, drops text longer than 1 MiB. Attribute values are kept up to 32 MiB.
  - The JSX formatter drops text longer than 10,000 bytes and JS expressions of 10,000 bytes or more (`lambda/format/format-jsx.cpp:17`, `:46`).
  - The LaTeX formatter drops strings of 64 KiB or more (`lambda/format/format-latex.cpp:56`).
- **Reproduced 2026-09-24 on both tiers, for HTML and XML:** a paragraph of 1,200,000 characters followed by `<p>tail-marker</p>` formats to 79 characters of HTML and 120 of XML. The paragraph is gone and the marker survives. The only trace is an `[ERR!] html_string_guard: skipping suspicious string` log line.
- **Likely intent:** the log wording suggests a defence against corrupt string pointers, not an intended size limit.
- **Why filed here:** the formatters sit outside LR_09's scope (`lambda/format/`, which `lambda convert` also uses), but the wrong value is observed through the `format()` builtin.
- **Fix:** needs a decision — remove the caps, or make an oversized string an error.

## 10. Error handling (LR_10)

<a id="lr10-8"></a>**LR10-8 · JIT: `raise` of a non-error value escapes the declared return type · OPEN (found 2026-09-21)**
`fn f(x) int^ { if (x < 0) raise "s" else x }; let v = f(-1) ^ { 0 }` binds
`v = "s"` (`string`) on the JIT. The interpreter rejects the value at the
function return (E201) and the handler yields `0`. A binding declared `int`
holds a string — violates **SI14** and **S1.6**. What `raise` may accept is
itself unruled.

<a id="lr10-9"></a>**LR10-9 · E228 is checked only in top-level expression statements · OPEN (found 2026-09-22)**
**S7.5.1** requires a call with a `^` channel to be engaged at the immediate
expression everywhere, and **S7.5.2** says a bare `let x = a()` never
acknowledges. The E228 walk (`validate_enforcing_calls_in_expression`,
`build_ast.cpp`) is entered only once per top-level item. It has no case for
`let`/`var`/`pub` statements (the `VARIABLE_DECLARATOR` case is unreachable
from a script) or for `for` in either form, and it returns at every `fn`/`pn`
node. `if` bodies are walked. Inside an `fn`, E208 catches only an error that
reaches the return value. So all of these compile with no diagnostic:
`let x = risky()` at the top level, `for i in xs { risky() }`,
`io.mkdir("out")` as a statement in `pn main()`, and `let a = risky(); 5`
inside an `fn`. The existing E228
fixtures (`type_e228_acknowledgment.ls`,
`negative/semantic/unhandled_error_expression.ls`,
`std/negative/unhandled_error.ls`) test only top-level expression statements,
and the positive fixture's in-function cases are never walked. Closing the gap
has a wide reach: by **S7.4.5** `input` raises, so every
`let data = input(...)` would become E228. That is 165 sites in 109
test/package files and 63 in the docs. `doc/Lambda_Error_Handling.md`
"Handling System Function Errors" (`error=E228`) fails `check_doc_blocks.py`
until this is fixed: its `io.mkdir` example moved into a `pn` with LR12-30, and
neither remaining ❌ line is reported.

<a id="lr10-10"></a>**LR10-10 · S7.4.4's other error members and the two-argument constructor are missing · OPEN (found 2026-09-25)**
Found while fixing LR10-7, both tiers (S7.4.4: an error carries code, message and source location; constructors `error(msg)`, `error(msg, source)`, `error({...})`):
- `error(msg, source)` is not registered (`sys_func_registry.c` has arity 1 only): the JIT calls a non-function value (E212), and T0 logs "call target is not a function".
- `.source`, `.file`, `.line` and `.column` fall through to returning the error itself, so `error("outer").source is error` and `.line is error` are both `true`. The `^.source.message` example in `doc/Lambda_Error_Handling.md` cannot work.
- A payload-less sentinel error still reads `context->last_error`: after `error("outer")`, `int("abc").message` is `"outer"`. Producers that return the bare `ItemError` need real payloads.
- `context->last_error` is not a GC root, yet it can hold a GC-heap error.

<a id="lr10-11"></a>**LR10-11 · T0 binds a stack overflow into a `let` instead of faulting · OPEN (found 2026-09-25)**
```
fn f(n) => n + f(n + 1)
pn main() {
    let x = f(0)
    print("after ")
    print(type(x))
}
```
T0 prints `after error` and exits 0; the JIT stops with `error[E308]: Stack overflow` and exits 1. A stack overflow is a fault, never a call result (S7.11.1v2), and faults pass through `fn` frames to their boundary (S7.11.2). T0 instead turns it into an ordinary error value, so an unused binding hides it: `negative/runtime/stack_overflow.ls` (`let x = f(0)` at top level) exits 0 on T0. `RuntimeError_StackOverflow` checks only that the script does not crash; `RuntimeError_StackOverflowJit` pins the JIT.

<a id="lr10-12"></a>**LR10-12 · A proviso answers null for an error operand when its predicate touches `~` (S10.1.5v3, S7.9) · OPEN (found 2026-09-25, waiting on a ruling)**
`x that p` binds `~` to `x` whatever it is. With `let e = error("boom")`, `e that true` is the error, but `e that ~ > 3` is `null`: the comparison propagates the error (S7.9.3), an error is falsy, so the proviso fails. S10.1.5v3 rules a failed proviso absence and says nothing of an error operand. S7.9 asks whether a result can be mistaken for a successful computation, and a null proviso can. Both tiers agree.

## 11. Mark data API (LR_11)

<a id="lr11-1"></a>**LR11-1 · Reader traversal is stubbed · OPEN**
`MarkReader::ElementIterator`'s destructor leaks `state_`
(`lambda/core/mark_reader.cpp:50`, `// TODO: Free traversal state`), and
`next()` only linear-scans *direct* children (`:57`, `// TODO: Implement proper
tree traversal for nested elements`). There is no real descendant or CSS-like
matching, so any caller expecting deep selection gets silently wrong results.

<a id="lr11-2"></a>**LR11-2 · `render_map` iterates while it mutates · OPEN**
The retransform loop calls `fn()` inside the iteration, and that can reach
`render_map_record()` → `hashmap_set()`, resizing the very map being iterated.
The current code defends by snapshotting the entry before re-execution
(`lambda/runtime/render_map.cpp:323`–`326`) and by refreshing the root reverse
mapping (`:135`–`142`), but the iterate-while-mutate pattern remains and is easy
to break with any change to retransform ordering.

<a id="lr11-3"></a>**LR11-3 · The `ui_mode` arena-provenance landmine · OPEN**
Inline `map_rebuild_with_new_shape` must **not** `pool_free` the old data buffer
in `ui_mode_`, because in ui_mode that buffer was arena-allocated by the JIT
(`context->arena`), and freeing it through the editor's pool would corrupt
rpmalloc. The guard is present and correct at both sites
(`lambda/io/mark_editor.cpp:834`–`837`, `:1356`–`1359`), but any path that flips
`ui_mode_` incorrectly corrupts the heap with no diagnostic.

<a id="lr11-4"></a>**LR11-4 · Hard-coded caps with mixed failure modes · OPEN**
All four caps survive, and so does the inconsistency in how they fail:

| Cap | Where | Failure mode |
|---|---|---|
| `SHAPE_BUILDER_MAX_FIELDS` 64 | `lambda/core/shape_builder.hpp:6` | **silent truncation** in `shape_builder_import_shape` (`shape_builder.cpp:132`, `:141` — `log_warn` only), so maps/elements with >64 fields cannot be edited correctly |
| `MAX_BATCH_UPDATES` 64 | `lambda/io/mark_editor.cpp:13` | **errors out** above 64 (`:941`–`945`) |
| `MAX_DEPTH` 2000 / `MAX_FIELD_COUNT` 10000 | `lambda/core/print.cpp:12`–`13` | **clamps** deep/wide structures with a `[MAX_DEPTH_REACHED]` marker (`:139`, `:208`, `:650`) or a bail (`:142`) |
| `EDIT_SOURCE_PATH_MAX` 32 | `lambda/runtime/edit_bridge.h:31` | **fails** source paths deeper than 32 (`edit_bridge.cpp:72`–`75`, `"source path too deep"`) |

Truncate vs. error vs. clamp vs. fail, for four caps in one subsystem, is itself
the hazard.


<a id="lr11-7"></a>**LR11-7 · `createSymbol` pooling-comment divergence · OPEN**
The header comment still claims symbols ≤32 chars are pooled
(`lambda/io/mark_builder.cpp:12`), but `createSymbol` (`:126`–`:134`) is
**unconditional `arena_alloc`** — no pooling branch exists. Anyone relying on
symbol pointer-identity for short symbols will be surprised.

<a id="lr11-8"></a>**LR11-8 · `push` / `splice` are not in the Mark editor · OPEN (note)**
They are runtime builtins in `lambda/runtime/collection_runtime.cpp` (registered
as `SYSPROC_PUSH` / `SYSPROC_SPLICE`) and belong to
[§12](#12-procedural-runtime-lr_12), not the editor surface.

---


## 12. Procedural runtime (LR_12)

<a id="lr12-1"></a>**LR12-1 · `fetch_response_to_item` returns a bare String · OPEN**
`// TODO: Implement proper map structure when the complex type system is
working` (`lambda/runtime/lambda-proc.cpp:513`–`514`) — `pn_fetch` hands back
only the response body as a String; status, headers, and metadata are dropped
(`:510`, consumed `:663`). A proper `{status, headers, body}` map is pending
type-system work.

<a id="lr12-4"></a>**LR12-4 · `push` is generic-`Array`-only · OPEN**
`pn_push` rejects `ArrayNum` (`collection_runtime.cpp:213`), so there is no
in-place append for typed numeric arrays; growing a typed array still requires a
rebuild.

<a id="lr12-5"></a>**LR12-5 · `splice` cannot touch views or N-D arrays · OPEN**
The `is_view` / `is_ndim` guard
(`collection_runtime.cpp:243`–`245`, "copy()/ravel() first") is correct but a
usability cap: in-place removal on a strided or shared typed buffer requires an
explicit copy.

<a id="lr12-6"></a>**LR12-6 · `g_dry_run` is a process-global · OPEN**
Declared `extern bool g_dry_run` (`lambda/lambda.h:63`), set once from the CLI
(`lambda/main.cpp:4920`), read from IO paths (`lambda/core/path.c:769`). A
single non-thread-local flag: concurrent compilation/execution that wants
per-run dry-run semantics has no per-context override. Cross-link: RG1–RG14 in
[Runtime globals audit].

<a id="lr12-10"></a>**LR12-10 · A copy of a `var` parameter saw later writes through it · PARTIAL (fixed 2026-09-17 for homed parameters; place borrows open)**
S9.1.2 / S9.1.3. `pn f(var b: Box) { var saved = b; b.cells[0].k = 8; ... }`
left `saved.cells[0].k == 8` on both tiers (and on the Result46 binary): the
callee's own copy observed the borrow's write. The same held for a root stored
into a container, a copy returned after the write, a reassigned alias, a typed
array, and a copy followed by a `var` re-borrow that writes. Three defects
combined:
1. An alias bind marked its source only when the source was `cow_owned`, and
   a `var` parameter never is (T0 declaration bind,
   `mir_expr_is_owned_binding_alias`). T0's plain reassignment `s = b` marked
   nothing even for owned locals -- a T0-only divergence (`s = b; b.x = v`
   showed `v` through `s`).
2. Stores through a `var` parameter write the root in place (CW33: the
   chain-root prologue leaves it unique), so a root shared *after* entry was
   mutated under its other holder.
3. T0 re-borrowed a `var` parameter without the prepare MIR's call site emits
   for a `cow_marked` root.

**Fix.** (1) both tiers mark an alias of a `var` parameter, and T0 reassignment
marks like MIR's `cow_binding`. (2) `lambda_ast_note_var_root_sharing`
(script finalize, shared by both tiers) decides per `var` parameter whether the
body may share its root -- a bind, return, container element, or a call
argument the callee can keep (a plain parameter whose root it retains, CW29
bare scan `cow_param_root_retained`; a `var` parameter that itself may share,
iterated to a fixpoint); comparisons and truth tests are reads -- and flags each
store a sharing use may precede (`AstAssignNode::var_root_unshare`,
flow-sensitive like MIR's `cow_marked`; an `if` arm that exits contributes
nothing, a loop counts as a whole). A flagged store detaches a shared root and
republishes the binding (`interp_read_store_owner`,
`mir_emit_var_root_unshare`, also used by the T29-1 writing bind); the CW33
epilogue publishes it, and such parameters are marked publishable so callers
transport the home. (3) a T0 activation that marked a `var` parameter prepares
it before re-borrowing (`InterpFrame::var_marked_mask`). Size cost: jetstream
deltablue2 unchanged, awfy deltablue2 +4 instructions, splay2 +53 (its
rotations really do share their roots). Regression
`test/lambda/proc/var_param_snapshot.ls` (identical on interp, jit, auto and
default; forced-GC clean). Two goldens pinned the defect and were corrected:
`proc/interp_typed_var_param.txt` (`let keep = x` then writes through `x`) and
`proc/cow_rmw_borrow.txt` (`shared_root`: `snap.xs[2]` is 0, not 3).

**Residue (OPEN):** the detach is gated on a publish channel. A place borrow
argument (`f(b.other)` into `var c: Cell`, CW25) transports no home on either
tier, so `var s = c; c.k = 50` still shows 50 through `s`: detaching would lose
the caller's write instead. Needs the place-borrow home of CW33 cost item 2
(`vibe/Lambda_Design_Runtime_COW.md` §11.10). Repro: `cell_alias` in
`temp/varparam_more.ls` (expected 550, prints 5050).

*Scoped 2026-09-25:* the residue is broader than `cell_alias`. Seven place-borrow shapes fail in three ways — a copy sees the write (a record, `int[]` on T0), a rebind is lost (`c = {…}`, `int[]`), or the caller loses the write (`int[]` on the JIT, untyped and `Cell?` parameters) — and a scalar place (`inc(b.size)`) is skipped on T0 and errors on the JIT. CW33 item 2's mechanism cannot be built as written: `&parent->items[i]` is not a stable home under D4.3.1 (array items and map field buffers move) or CW37/D4.4.4v4 (data-zone pointers are never facts), and record fields are packed typed lanes, not Item words. It needs a designer amendment (CW33 item 2 v2: a caller-owned rooted Item home, with a reinstall after the call) and a ruling on scalar places. Record: `temp/wv/LR12-10/`. Found on the way: [LR12-32](#lr12-32), [LR12-33](#lr12-33).

<a id="lr12-14"></a>**LR12-14 · A row copy of a matrix aliases it both ways · OPEN**
S9.1.2 / D4.4.6. Found while testing T29-5; present on the post-T29-3 binary,
both tiers, for a loop-built and a packed matrix alike:
```
var m = [fill(3, 1), fill(3, 1), fill(3, 1)]
var row = m[0]
m[0][1] = 9      // row[1] reads 9
row[2] = 4       // m[0][2] reads 4
```
The place-copy rule should mark `row` because its place is written while the
copy is alive, but both writes are visible through the other name. Repro:
`temp/t29/packed_probe.ls` (`row_copy_loop`, expected 9110, prints 9944).

*Investigated 2026-09-25:* only a packed matrix leaks. `[fill(3, 1), …]` is promoted to a 2-D ArrayNum, and `m[0]` returns a mutable view onto its buffer (`make_leading_axis_view`, `is_mutable_view = 1`). The place-copy facts are right and both tiers mark the bind, but a view owns no storage: `clone_mutable_array_num` hands back the view itself, so the detach writes the base. A push-built matrix, a generic array of rows and a map of arrays are correct; `row_copy_loop`'s expected value is 9141, not 9110. The fix direction, copying a view into an owned array at a place-copy bind (S9.2.2), is the eager view-alias clone CW32v2 item 6 lists, but item 6 leaves mutable views OPEN/TODO by designer ruling, so it waits on that ruling. The other value boundaries (capture, push, parameter snapshot, call result, reassignment) leak the same view. Record: `temp/wv/LR12-14/`.

<a id="lr12-24"></a>**LR12-24 · TE-15 defect containment — declaration and reassignment boundaries FIXED 2026-09-18; the remaining origination classes are OPEN**
S7.1.3v2, S7.4.2, S7.4.3, TE-15, TE-18. Four entries were filed separately as
JIT defects — [LR12-15](<Lambda_Issue_Ledger (fixed).md#lr12-15>), [LR12-16](<Lambda_Issue_Ledger (fixed).md#lr12-16>), [LR12-18](<Lambda_Issue_Ledger (fixed).md#lr12-18>)
and [LR12-22](<Lambda_Issue_Ledger (fixed).md#lr12-22>). They are one missing feature, not four bugs, and this
entry supersedes them.

**The ruling.** TE-15 (`vibe/Lambda_Design_Type_Enforcement.md`, decided
2026-08-01), as narrowed by TE-18 (2026-08-06), governs a failed deferred type
check — exactly the E201 all four symptoms raise. Skip is a
**declaration-boundary** mechanism: the error skips to the end of the block
that *declares* the binding whose establishment (case 1) or assignment (case 7)
failed, and that block evaluates to the error. `while`, `if`, plain blocks and
`for` contribute no region of their own. The fn body is the outermost block, so
an uncontained defect **becomes the function's result and crosses a plain `T`
return on the unenumerated system channel** — inference must never widen a
signature. The cross-function ABI is named in the ruling: boxed-returning calls
carry the error in the result Item; **native-returning calls check an error
lane — "one load-and-branch after the call, the Swift-`throws` shape"**, an
emission-time effect bit that is *transitive in the implementation, invisible
in types*.

**What was wrong.** The JIT lost the defect entirely when the return lane was a
native scalar: `emit_function_error_return` fell through to
`emit_function_return`, republishing the error Item's bits through the value
lane — `inf` from an int lane, `nan` from a float one. TE-15 anticipated
exactly this: value-propagation through unboxed lanes was rejected because
"today's accidental out-of-band i64 *is* [an in-band sentinel], and its
consumer-dependent meaning is the measured divergence".

**Correction to this entry's earlier text: T0 does not over-contain.** Measured
2026-09-18 with `temp/lr1224/probe2.ls`: T0 binds the error to the caller's
unannotated `let` (an acceptor per TE-15), answers `r is error` = true and
`type(r)` = `error`, and runs the rest of the caller's block. The earlier claim
that "`print("s=" ++ straight(a, 5))` emits nothing" is real but is a *different*
question — `print` of an error-valued `++` chain renders nothing on **both**
tiers — not a containment failure. Recorded as [LR12-25](#lr12-25), together
with the more serious finding beside it: `string(<contained error>)` re-raises
it as a top-level fault and terminates the script, on both tiers.

**Fixed (2026-09-18) — TE-18 cases 1 and 7.** The mechanism the ruling asks for
already existed for `T^E` bodies: `RETURN_SHAPE_NATIVE_ERROR` (shape 4) returns
`[native, error]`, and the call site merges lane 2 into a boxed value-or-error
join. It was gated on `TypeFunc::can_raise` alone. Three changes:
1. `function_body_may_originate_defect` (`transpile-mir.cpp`) walks the whole
   body — not just its top-level statements, which is all
   `function_body_may_check_boundary` ever scanned, and which is blind for a
   `pn` whose body is a BLOCK rather than a LIST — and reports every
   declaration (case 1) and reassignment (case 7) boundary that emits a check.
2. `lambda_body_return_lane` and `analyze_lambda_mir_variants` select the error
   lane on `can_raise || may_originate_defect`, from **one** shared
   `carries_error_lane` fact so the body and its forward-declared contract
   cannot disagree (RV2). `em_return_shape`'s parameter was renamed off
   `can_raise` for the same reason.
3. The call site reads `call_variant->result.shape == RETURN_SHAPE_NATIVE_ERROR`
   from the callee's descriptor instead of re-deriving the rule from the
   signature (RV10: read the transport, never recreate it).

The scan asks the emitter's own gates — `mir_boundary_is_redundant` and the
newly extracted `mir_assignment_boundary_applies`, which `transpile_assign` now
also calls so the two cannot drift. Asking a looser AST-level question instead
cost real code: `declaration_may_check_boundary` answers "may check" for
`var total: int = 0` (a literal carries its own `is_literal` Type), which gave
every counted loop a lane — and with it the number-frame scratch slot the
shape-4 epilogue spills its native result into.

Pinned by `test/lambda/proc/defect_native_return_lane.{ls,txt}` — both classes
and both native lanes (`int_acc`/`float_acc` for case 7, `read_sum` for case 1),
plus the happy path and a clean call after the defect, identical on both tiers
— and by `test/mir/lambda/tune26_terminal_oob_boundary.mir-check`, whose
`expect_seq` previously required `lambda_type_check` to be followed immediately
by `it2d` — **the sidecar was pinning the NaN republication itself**. It now
forbids `it2d` on that arm outright.

**Cost.** Static: `lambda_corpus_deltablue` module_insns 8773 → 8998 (+225,
+2.6%), `_constraint_choose_method_#` 1701 → 1744 (+43); no other ratchet probe
moved. Runtime: **within noise** — A/B from one release binary with the
predicate behind a temporary switch, median of 7, nine rows between −0.6% and
+0.3% (deltablue +0.1%/+0.2%, nbody −0.1%, richards −0.5%, splay −0.2%). The
hot path gains one compare-and-branch per native call to a defect-capable
callee, which is what TE-15 budgeted for. TE-15 still names the `may_defect`
call-graph fixed point (D6.1.3) as what removes it, and says it "must be built
before, not after, the routing work".

Verified after the fix: baseline 5631/5634 (Lambda runtime 3527/3530), MIR
emission 156/156, ratchet 20/20, forced-GC stress 203/203, Lambda runtime suite
941/941. The three remaining failures are JS (`tune12_array_access`,
`dynamic_call_invoke_entry`, `regex_bt_legacy_octal_assertion`); they are red at
HEAD — no JS source is modified here, and the unpulled upstream commits touch
only Radiant, CSS and the build.

**Still OPEN — the other origination classes.** `emit_return_if_item_error` has
~40 call sites; this fix covers two. Parameter admission, element and field
stores (TE-18 **S1**, which also requires a *report* that this fix does not
emit), and literal construction still reach a native return lane and still
republish raw bits. An assertion placed there during this work fired on 75 of
156 emission fixtures, which is the honest measure of the remainder;
`fn twice(tree: Tree) int => total(tree) + total(tree)` is a minimal example.
The residue now logs `mir-defect-residue` at each such emission, so
`grep mir-defect-residue log.txt` enumerates it over any corpus instead of it
being silent.

Two further blockers for the remainder, both already ruled on:
- **TE-17 I3 lane eligibility.** A defect-capable call's result is
  `T | error` and therefore *not lane-eligible*, so `f(x) + 1` must be computed
  boxed. The emitter does not know this yet: forcing a lane onto
  `tune14_native_return`'s accumulator produced
  `mir-value: unavailable representation transition 1 -> 2` — an Item the
  consumer demanded as an int lane. Until I3 lands, the lane can only be given
  to bodies whose callers can still consume the merged join.
- **TE-18 S1's report.** A failed element store leaves an aliased container
  partially mutated; scope exit cannot undo it, so the ruling requires a
  runtime report using case 7's three tiers. Neither tier emits one.

Repros: `temp/t29/packed_probe.ls`, `temp/t29/oob_int.ls` (`param_oob` still
diverges: T0 true, JIT false — the S1 store class), `temp/t30/h/err_prop.ls`,
`temp/t30/h/tier_divergence_probe.ls`, `temp/lr1224/oob_read.ls` (now agrees).

**Where the rest of the work belongs.** The design tracks it in
`vibe/impl/Lambda_Impl_Type_Enforce.md`, but the file on disk is
`Lambda_Impl_Type_Enforce (done).md`, so the tracking pointer is stale and the
remaining round-2 work reads as finished.

<a id="lr12-25"></a>**LR12-25 · A contained error is invisible to `print` and escapes containment through `string()` · OPEN**
S7.4.2, TE-18 case 5. Measured 2026-09-18, **identical on both tiers**, with a
contained defect bound to `r` (`temp/lr1224/print_err3.ls`,
`temp/lr1224/print_err4.ls`):

| form | behaviour |
|---|---|
| `print(r)` | prints an empty line; execution continues, exit 0 |
| `print("x " ++ r ++ "\n")` | prints **nothing at all**; execution continues |
| `string(r)` | **terminates the script** — the contained error is re-raised as a top-level `error[E201]`, exit 1 |

The first two are a rendering gap: expression composition correctly does not
skip (TE-18 case 5 — "the result type is `T | error`; the error flows as a
value"), so `"x " ++ r` is an error value, but the output surface then discards
it silently. Every contained defect is therefore invisible in a program's own
output, which is what made TE-15 containment look like a lost statement.

The third is a **containment escape**, and the more serious of the two: a value
that `is error` answered true for, and that the block legitimately holds,
becomes an uncatchable process-level failure the moment it is converted. The
three failure channels (S7.4.2) do not permit a soft error to promote itself to
a fault at a conversion boundary.

Split from [LR12-24](#lr12-24), where the `print` half was mis-diagnosed as T0
over-containment. It is not: T0 contains correctly.

<a id="lr12-7"></a>**LR12-7 · The procedural surface is thin and ad hoc · OPEN**
IO procedures are a hand-curated set in one file with bespoke validation per
procedure; there is no general effect/capability system, so adding a
network-write or process-spawn procedure means another bespoke `pn_*` plus a
registry row.

---


<a id="lr12-27"></a>**LR12-27 · `push` onto an unannotated number array is a silent no-op; out-of-range writes do not raise · PARTIAL (found 2026-09-21; push fixed 2026-09-25)**
In a `pn`, `var a = [1, 2, 3]; push(a, 4)` leaves `a` as `[1, 2, 3]` on both
tiers: the literal is an uncertified `ArrayNum` and `push` returns a soft error
that nothing surfaces (`collection_runtime.cpp:240`, "an uncertified ArrayNum
has no append contract"). `int[]`, string, and mixed arrays append correctly.
Likewise `var b = [1]; b[5] = 2` only logs "index 5 out of bounds"
(`lambda-eval.cpp:8200`) and the procedure continues with exit 0. Both
contradict **S7.1.3v2** (writes are checked and raise through the `T^`
channel).

*Fixed 2026-09-25 (push):* S9.1.1 makes `push(b, v)` mean `b' = b ++ [v]`, and index writes already keep an open packed array's lane or widen it in place. `array_num_push_open` (`lambda-eval.cpp`) does the same for push: it appends in the lane when every item fits and otherwise converts to a generic Array, then spreads a list (D2.6.5v3); a view or an N-D array refuses, as splice does. `pn_push_cow` no longer checks an open binding against a past admission's certificate (SI3v2): after `pn g(x: int[])` had admitted `u`, `push(u, "s")` had failed. `proc/jit_tail_if_push_error` now fails its push on a matrix, and `proc-ext/list_var_mutation`, green at last, moved to the baseline. Fixture `test/lambda/proc/push_open_packed.ls`.
*Residue (blocked):* `b[5] = 2` still only logs. Raising it on both tiers needs [LR12-24](#lr12-24)'s store class: a callee with a native return lane would publish the error as `inf` on the JIT while T0 raises, a new split. And whether a plain `pn`'s write error survives a statement-position call is unruled (S7.4.3 against TE-18 "What pn adds").

<a id="lr12-31"></a>**LR12-31 · A row assignment into a packed matrix flattens it · OPEN (found 2026-09-25)**
`var q = [[1, 2], [3, 4]]; q[0] = [9, 9]` leaves `q` as `[[9, 9], 2, 3, 4]` on both tiers, where `[[9, 9], [3, 4]]` is right: the literal is packed into a 2-D ArrayNum, and the write widens it through `convert_specialized_to_generic`, which flattens N-D storage. Reported by the LR12-27 investigation, reproduced 2026-09-25.

<a id="lr12-32"></a>**LR12-32 · T0 corrupts the caller on a tail place borrow (S9.1.3) · OPEN (found 2026-09-25)**
`pn walk(var n, d: int) { if (d > 0) { n.k = d; walk(n.next, d - 1) } }`: `walk(x, 2)` on a three-node chain gives `x.k=2 x.next.k=1 x.next.next.k=0` on the JIT, while T0 replaces `x` with the deepest node (`x.k=0`, the rest empty). A bare tail re-borrow is fine. The LR12-10 scoping suggests `plan_mark_tail_calls` (`interp_plan.cpp`) must not mark a self-call that passes a place at a `var` position. S1.6. Probe `temp/wv/LR12-10/p_tco.ls`.

<a id="lr12-33"></a>**LR12-33 · A plain parameter sees a `var` write through the same argument (S9.1.3) · OPEN (found 2026-09-25)**
`pn h(var x: Box, p: Box) { x.size = 50; print(p.size) }` called as `h(b, b)` prints `50` on both tiers. A plain parameter is a snapshot taken before any borrow's mutation, so it should print `5`. Reported by the LR12-10 scoping, reproduced 2026-09-25.

<a id="lr12-34"></a>**LR12-34 · Reassigning from a place aliases it; the JIT does not capture a place stored into a field · OPEN (found 2026-09-25)**
- `var row = [0]; row = m[0]; m[0][1] = 9` shows the 9 through `row` on both tiers, even for a push-built (generic) matrix. D4.4.6's place-copy rule covers declarations only, and both tiers mark reassignments from plain names only, so the fix needs the rule extended to reassignment (S9.1.2).
- `r.x = m[0]; m[0][1] = 9` shows the 9 through `r.x` on the JIT only: T0 captures the row (`ast_expr_insertion_needs_capture`), while `mir_emit_value_capture` returns early unless the value is a plain name (S9.3.1, S1.6).
Reported by the LR12-14 investigation, reproduced 2026-09-25.

<a id="lr12-35"></a>**LR12-35 · A binding from an expression that returns its operand aliases it (S9.1.2) · OPEN (found 2026-09-25)**
`var xs = [1, 2, 3]; var y = xs or null; y[0] = 99` leaves `xs[0] == 99` on both tiers; so do `if (true) xs else null`, `match (1) { case 1: xs default: null }`, and the S10.1.5v3 proviso `xs that true`. A plain `var y = xs` marks its source and detaches on the write. The alias-bind test (`mir_expr_is_owned_binding_alias`, and T0's declaration bind) recognises only a bare name, so an operand returned through `or`, a branch, an arm, or a proviso is never marked. The same root as LR12-34's reassignment half. Found while implementing `|:` (S10.1.6), reproduced 2026-09-25.

## 13. Schema validator (LR_13)


<a id="lr13-2"></a>**LR13-2 · Inconsistent `max_depth` defaults · OPEN**
`SchemaValidator::create()` sets 1024 (`doc_validator.cpp:139`),
`default_options()` sets 100 (`:779`), and the CLI sets 100
(`ast_validate.cpp:444`, `:464`, `:626`) — three ceilings for one bound.

<a id="lr13-3"></a>**LR13-3 · Fragile root-type selection · OPEN**
The schema root type is chosen by raw text-scanning of the schema source plus a
hard-coded filename map, with fixed `char cwd_path[1024]`-class path buffers
that truncate (`ast_validate.cpp:324`ff).

<a id="lr13-4"></a>**LR13-4 · Hard-coded caps with silent truncation · OPEN**
`MAX_UNION_TYPES = 32` silently drops members of larger unions
(`validate_pattern.cpp:478`–`482`); the reporting path array is `[100]`
(`error_reporting.cpp`); `type_info[]` is assumed size 32 so any `TypeId ≥ 32`
renders `"unknown"` (`validate_helpers.cpp`).

<a id="lr13-5"></a>**LR13-5 · Unenforced options · OPEN**
`strict_mode`, `allow_unknown_fields` / `--allow-unknown`, and
`allow_empty_elements` are parsed and printed
(`ast_validate.cpp:198`, `:201`, `:442`–`457`, `:593`) but largely not acted on;
the fixed-length array check is commented out; warning merging exists
(`doc_validator.cpp:474`) but no code path ever emits a warning.

<a id="lr13-6"></a>**LR13-6 · Placeholder helpers · OPEN**
`extract_type_from_ast_node` is "Phase 1, basic type extraction"
(`doc_validator.cpp:266`–`271`); `is_item_compatible_with_type` is a bare
`item.type_id() == type->type_id` (`:431`–`436`); `format_type_name` returns the
literal `"unknown"` (`error_reporting.cpp:341`–`344`).

<a id="lr13-8"></a>**LR13-8 · Repeated successful validation of an unproven union member · PARTIAL (re-scoped 2026-09-14)**
*Resolved portion:* Tune27 removed the measured hot-path regression. A plain
unary `T[]` crossing now certifies its admitted carrier instead of re-walking
the whole array; a map whose trusted layout proves a union arm returns through
`runtime_union_map_rep_proves_cached` (`lambda-eval.cpp:10693`–`:10728`). The
current release evidence reports 6,615,034 cache hits from 6,615,040
`prettier_ast` union admissions, and the accepted-path validator cost is gone
from the former hot rows. `tune27_array_contract_union.ls` passes on interpreter,
auto, and eager JIT.

*Residue:* the memo records `MAP_CONTRACT_UNION_MEMBER_PROVEN` only. When a
dynamic/foreign map has no physical-layout proof but the schema validator still
accepts it, `runtime_union_map_rep_proves_cached` returns false without
recording that result; `runtime_type_admit_value` then reaches
`lambda_type_matches` (`:10842`), which invokes
`runtime_validate_value_against_type` (`:1665`). The first deep validation is
required by **D3.2.2**; subsequent crossings of that unchanged accepted value
still revalidate it. **S11.4.1v3** permits reuse, and **D3.2.4v3** defines the
physical-proof condition. The remaining work is a sound negative-proof or
post-validation certificate for stable untrusted shapes, with invalidation on
shape transition. [Tune27 M1/T27-1](impl/Lambda_Impl_Tune27 (done).md) records
why this residue was not measurable on the frozen benchmark rows.

<a id="lr13-7"></a>**LR13-7 · `printf`/emoji output in production paths · OPEN**
Contrary to CLAUDE.md rule 4, `ast_validate.cpp` has 59 direct `printf` calls
and `error_reporting.cpp` 6, writing to stdout with emoji rather than through
`log_*`. Also `error->actual.item` truthiness treats a `0`/null actual as
"absent", which can misreport a legitimately-null value.

<a id="lr13-9"></a>**LR13-9 · The validator test surface is not a baseline gate · PARTIALLY RESOLVED 2026-09-05 (found 2026-09-03)**
Filed as LR12-1, an ID the `fetch_response_to_item` record already held; renumbered 2026-09-25 into the validator family.

The hosted `lambda-runtime-full` DSO resolves `ItemNull` and `g_lambda_home`
from its executable host. Every test target that links that DSO now compiles
retained references to both globals, so static-archive extraction supplies them.
All ten current validator executables load and enumerate their GTests. The
post-startup SIGSEGV was a second link-closure defect: the direct AST reducer
calls `parse_type_pattern_text_span`, but `lambda-runtime-full` omitted
`runtime/parse_type_pattern.cpp`; macOS dynamic lookup therefore supplied a
null reduction target. Including that translation unit restores the call, and
the enabled `test_validator_gtest` cases complete without a crash.

The selected-root lookup is also repaired. The loader still expected a retired
`AST_NODE_ASSIGN`, while the direct parser canonically emits an
`AST_NODE_VARIABLE_DECLARATOR` marked `is_type_definition`; it now registers
that form (and named patterns) after shared `TypeType` unwrapping. The shipped
`validate` invocation reaches genuine validation rather than `REFERENCE_ERROR`.
It currently reports its independent `element`-versus-`map` mismatch. Root
*selection* remains the separate open LR13-3 CLI-contract issue: it still uses
the raw textual scan and filename map (`validator/ast_validate.cpp:270`–`312`).

**Why it went unnoticed:** the validator gate runs from `test-lambda-full` (`Makefile:1624`), not `test-lambda-baseline`, so both baselines stay green over this unexercised surface. That is the finding worth keeping — a gate outside the baseline is a gate nobody runs.

Not attributed. The `elmt code clean up` commit (2cdcc1ea1) touches none of the files involved — not `build_lambda_config.json`, not `lambda-data.cpp`, not `runner.cpp`, not any validator or test source. The two remaining candidates are the object-redesign commit (da7a97b13), which reshaped `lambda.h`/`lambda-data.cpp` heavily, and the merged upstream `DOM: seven linkage lies and a dead local` (dbe7b7dba). Confirming which needs a from-scratch build of an older tree; a worktree attempt stalled on re-fetching vendored `re2`.

**Consequence for the specs:** the D2.6.6v2 content-arity claim can only be read from the code, not run. `validate_against_element_type` does enforce `content_length`, and an element-kinded nominal type reaches that arm by tag, so the old "not implemented" note is wrong — but "conformant" cannot be asserted until this is fixed. Both conformance rows now say exactly that.


<a id="lr13-14"></a>**LR13-14 · The empty bracket pattern `[]` admits every array (S11.1.6v3) · OPEN (found 2026-09-26, extending S11.1.7 to container literals)**
`[1, 2] is []` and `[[1]] is [[]]` are `true` on both tiers. S11.1.6v3 reads `int[0]` as `[]`, the empty array. A zero-slot pattern has no `item_patterns` (`fill_sequence_pattern_slots`), and `validate_against_array_type` reads that as "no per-slot pattern", then finds no `nested` type and admits the array. A fix gives a bracket pattern its length check even with no slots, keeping the homogeneous carriers (a `TypeArray` with only `nested`) apart. Until then the S11.1.7 reduction decides `[]` by its kind only.

<a id="lr13-15"></a>**LR13-15 · A present `null` map field is admitted only by a run type: `{a: null} is {a: null}` is `false` (S11.1.6v3) · OPEN (found 2026-09-26, extending S11.1.7 to container literals)**
A map keeps a `null` field (`len({a: null})` is 1), and the validator admits it only when the field's type is a run with a zero minimum (`type_admits_null_value`, `validator_internal.hpp`). So `{a: null} is {a: int?}` is `true`, while `{a: null} is {a: int | null}` and `{a: null} is {a: null}` are `false` on both tiers, though S11.1.6v3 makes `T?` the same type as `T | null`. A fix asks the field's type whether it admits `null` (`lambda_type_matches`) and leaves absence to `f?:`. The S11.1.7 reduction does not decide on a field that may be `null`, so it is not affected.

## 13.1 Ledger hygiene observations (not issues)

These records are retained for provenance but are excluded from the counts
above. The absence of source markers is not evidence that a structural defect
is absent; active rows must be found by behavior and ownership analysis.

<a id="lr03-7"></a>**LR03-7 · Latent, not annotated · OBSERVATION**
The core value-model files carry no `TODO`/`FIXME`/`HACK`/`XXX` markers; the
issues above are structural and will not surface in a tag grep.

<a id="lr04-8"></a>**LR04-8 · No literal `TODO`/`FIXME` markers · OBSERVATION**
The number and datetime concerns are expressed only as "for now" /
"far more than needed" comments.

<a id="lr05-8"></a>**LR05-8 · No `TODO`/`FIXME` markers · OBSERVATION**
The string and vector concerns have no source-level marker.

<a id="lr09-7"></a>**LR09-7 · No tags in source · OBSERVATION**
The registry caveats carry no `TODO`/`FIXME`/`HACK`; they are discoverable only
by reading the commented-out block, the "transpiler special case" notes, and
the `NULL` pointers.

<a id="lr10-6"></a>**LR10-6 · No source-level markers · OBSERVATION**
The error-handling concerns have no source-level marker.

---


## 14. Sibling vibe ledgers (TS, Issues8)

> Issues raised in the sibling `vibe/Lambda_Issue*.md` docs rather than in an
> `LR_*` Known-Issues section. **IDs are kept as their owning doc assigns them**
> (rule 17: no new ID series); those docs stay the detail record and this section
> is the index. Verified 2026-08-25 unless noted.

<a id="ts-3"></a>**TS-3 · `int[]`/`float[]` on a *local* is a 3–5x regression · OPEN (needs re-measurement)**
`impl/Lambda_Issue_Type_Support (retired).md`. The cited cause has moved — the
`var_tid = LMD_TYPE_ANY` assignment and its *"treat as ANY"* comment are gone
from `transpile-mir.cpp` — but the regression itself was not re-measured, which
needs a release build and the typed benchmark column.

<a id="ts-6"></a>**TS-6 · Binding a map literal to a local kills region allocation · OPEN**
`impl/Lambda_Issue_Type_Support (retired).md`. Structurally unchanged, only relocated:
`mir_region_producer_candidate` is now `transpile-mir.cpp:888` and delegates to
`mir_region_producer_node`, whose switch handles only CONTENT/LIST/BLOCK,
IF_EXPR, RETURN_STAM and MAP — with no `AST_NODE_VAR`/`AST_NODE_LET` case, a
`var`/`let` in the body still falls to `default:` and disqualifies the function.


<a id="s16-9-5-gap"></a>**S16.9.5 · `a?: T` optional-field marker · PARTIAL (parsing fixed 2026-08-25)**
Found while trying to write a schema for the Issues8 explicit-null attribute
entry. S16.9.5 says the marker "applies in every type-field position", and the
spec shipped it **unmarked** on a 2026-08-22 spot-check — but two of the three
named positions did not parse:

```
fn f(a: int, b?: int)     parameter        OK
type R = {a?: int}        map-type field   error[E103]  <- now fixed
type E = <e a?: int>      element attr     error[E103]  <- now fixed
```

**Fixed:** both field sites now read the marker through one shared helper in
`parse_type_pattern.cpp`, and the validator honours it — the fixture
`test/validator_test_data/maps.ls`, which contains `optional?: int`, no longer
reports an invalid-type-pattern error against itself. Covered by
`test/lambda/optional_field_marker.ls` on both tiers. Note the subtlety that
made the first attempt silently useless: `is_type_optional()` reads `type->op`
on the `TypeUnary`, not the AST node's `op`; a wrapper that sets only the AST
side parses but is invisible to every consumer.

**Residue (why this stays PARTIAL):**
1. The marker is carried by wrapping the field type in `OPERATOR_OPTIONAL` — the
   same representation `a: T?` produces — so the two spellings S16.9.5 calls
   *distinct* ("field may be absent" vs "field present, value nullable") are
   indistinguishable downstream. Separating them needs a field-level flag on
   `ShapeEntry`; that was not invented here.
2. The declaration binding checker treats an optional field as required:
   `type Rec = {name: string, opt?: int}` with `let v: Rec = {name: "a"}` gives
   `error[E205]: missing required field 'opt'`. This is **pre-existing and not
   specific to the new marker** — `opt: int?` behaves identically — so the
   binding path ignores field optionality for every spelling.

`Lambda_Formal_Semantics.md` is v15.1.2 with the footnote narrowed to this
residue. Fixing the parse also closed the Issues8 entry it was blocking
("Explicit null Mark attributes fail optional schema type checks"): with an
element schema using `fontname?: string`, explicitly-null attributes validate,
absent attributes validate, and a real type violation still fails.


<a id="i8-genafterlet"></a>**Issues8 · A comprehension generator may not follow a `let` clause · OPEN (design question, not a defect)**
Clause order is fixed: **all generators, then all `let`s**. Measured
2026-08-25:

| Form | |
|---|---|
| `for (v in vs, let k = v)` | accepted |
| `for (v in vs, w in vs, let k = v)` | accepted |
| `for (v in vs, let k = v, let j = k)` | accepted — lets chain, each sees the previous |
| `for (v in vs, let k = v, w in vs)` | `error[E100]: expected let clause` |
| `for (let k = 1, v in vs)` | `error[E100]: expected a for binding name` |

**This is documented and deliberate**, contrary to this entry's first draft,
which called it unruled after checking only S14.1 (group-by and joins).
`doc/Lambda_Expr_Stam.md:726` gives the grammar —
`for (<bindings> [, let <name> = <expr>, ...] [where <cond>] [order by <spec>]
[limit <n>] [offset <n>]) <body>` — and states the model: *"Clauses are
processed in logical order: bindings → let → where → order by → offset → limit
→ body."* The parser enforces exactly that pipeline.

**What remains open.** The fixed order cannot express one shape: compute a key
from the current item, then iterate what that key yields —
`[for (v in vs, let k = f(v), e in entries(v, k)) e]`. The workaround is a helper
(`entries_for(v)`) that exists only to satisfy clause order. Two answers are
coherent; the choice is a design call, not a bug fix.

---

**Option A — Keep it.** The fixed phases are the feature. `bindings → let →
where → order by → offset → limit` is a pipeline, and each stage having a single
well-defined input is what makes `where`, `order by` and `limit` compose
predictably: `where` filters *after* every binding exists, `order by` sorts a
settled row set, `limit` counts settled rows. Interleaving generators with `let`s
makes "what does `where` see?" depend on clause position rather than clause kind,
and the logical order stops being statable in one line. The cost is a helper
function in the one dependent-iteration case — real, but small and local.

*If chosen:* promote the reference-doc prose to an `S#` ruling so the order is
normative rather than descriptive, and extend the diagnostic from
`expected let clause` to name the rule and the repair — the shape the element
and map-key diagnostics now use (see [i8-semidiag](<Lambda_Issue_Ledger (fixed).md#i8-semidiag>),
[i8-dqdiag](<Lambda_Issue_Ledger (fixed).md#i8-dqdiag>)).

**Option B — Relax it.** Allow a generator to follow a `let`. The dependency
direction is already strictly left-to-right *within* the pipeline — `let j = k`
proves a clause may read an earlier one — so a generator reading an earlier `let`
introduces no new kind of dependency, only a new position for an existing one.
On that reading the restriction is a grammar artefact rather than a semantic
boundary, and the helper function it forces is pure ceremony. The logical order
would be restated per-clause ("each clause sees every clause to its left")
instead of per-phase, which is arguably simpler, not more complex.

*If chosen:* the `where`/`order by`/`limit` tail must stay phase-ordered — only
the `bindings`/`let` prefix interleaves — or the composability argument in
Option A genuinely breaks. Rule that boundary explicitly; do not let it be
inferred from the parser.

---

**Either way**, two things are owed: there is currently **no `S#` ruling** for
clause ordering (only `doc/Lambda_Expr_Stam.md` prose), and the diagnostic
reports *what* but not *why*. Option A makes both a small documentation and
message change; Option B makes them a grammar change plus the same ruling.

**Half already fixed:** the diagnostic used to point at the *first* generator
(`Unexpected syntax` at `value in values`); it now names the expected clause kind
and points at the generator that actually conflicts.

## 15. Design gaps inherited from the retired Outstanding rollup (OI)

> `vibe/Lambda_Issues_Outstanding.md` was **retired on 2026-08-25** and archived
> as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 (Lambda core,
> `LR_01–13`) was verified fully subsumed by §1–§13 above — every MAJOR item it
> bolded resolves to a ledger entry, and the ledger carries more per section than
> the rollup listed. Its §2 (JO1–JO13) and §4 (JS_01–16) were pointer indexes
> into docs that still own them: `vibe/Lambda_Design_Stack_Frame_JS.md` (18 JO
> references) and the `doc/dev/js/JS_*.md` Known-Issues sections (present in all
> 17). What had **no other home** were the OI design gaps and the cross-cutting
> themes, indexed below; the archived file remains the full argument for each.
>
> **IDs keep their `OI-n` spelling** (rule 17). These are design gaps needing an
> ADR before code, not point defects — none was re-verified in the 2026-08-25
> pass unless noted.

- **OI-2 · JS object model: internal metadata + GC lifetime.** (a) The
  marker→shape-flag migration is half-done — class identity, accessors,
  iterators and Promise branding still ride `__class_name__`/`__ctor__`/`__arr__`
  string keys beside the typed `JsClass`/`ShapeEntry` scheme; (b) pools are never
  GC-reclaimed (JsFunction wrappers, generator pool with index collision on
  churn, promise pool with reactions capped at 8), and WeakMap/WeakSet/WeakRef
  have no weak semantics. Rider: map-field tombstones and sparse-array holes are
  two conventions over one concept — unify when the representation work happens.
- **OI-3 · ESM correctness.** Named-import live bindings are snapshot-only;
  circular ESM sees placeholder `undefined` instead of a TDZ ReferenceError; TLA
  is first-await-only; `js_await_sync` busy-drains. The Lambda side has the same
  gap from the other direction — cross-language import skips `pub` vars
  ([LR01-11](#lr01-11)). One cross-language design.
- **OI-5 · MIR value-representation contract (MIR Direct) · RESOLVED 2026-09-14.**
  The canonical boundary is `MirValue`: it carries the full `Type*` contract and
  actual `ValueRep`; consumers request a carrier through `em_require_rep()`.
  Lambda expression lowering has no semantic `MIR_reg_type()` probe or raw-register
  expression shim. The historical truncation, boxed-result, and error-unboxing
  failures are resolved by the implementation records, including
  [LR07-1](<Lambda_Issue_Ledger (fixed).md#lr07-1>) and
  [LR07-4](<Lambda_Issue_Ledger (fixed).md#lr07-4>), under **D2.4.1–D2.4.3**.
- **OI-6 · Codegen quality cluster (JS).** Destination-passing lowering
  (66–88% of emitted MIR is MOVs); shape-based polymorphic inline caching;
  de-pointered relocatable MIR (~59 baked realm pointers) blocking artifact
  caching. The **PIC design record** is the substantive part and survives in the
  archive: the single-tier/no-patching/no-deopt constraints force a *data-driven*
  side-table cache (`{TypeMap* shape, void* target, uint32_t guard_version}` × 2
  ways, module-owned so it is realm-scoped by construction — the flaw in the
  reverted process-global prototype cache). Open decision: invalidation
  granularity, per-realm version (cheap, thrashes under test262 prototype
  mutation) vs per-shape counters (+8 B per TypeMap; recommended). Companion:
  re-key duplicate-class-name deopt by constructor/`TypeMap` identity rather than
  class-name strings.
- **OI-7 · Node compatibility gaps · RE-SCOPED 2026-09-14.** `vm` is
  intentionally absent: `require('vm')` / `require('node:vm')` return
  `MODULE_NOT_FOUND`, after the non-isolating implementation was retired. It is
  therefore not an outstanding sandboxing defect. The remaining compatibility
  work is that callback-style `fs` operations can run synchronously and invoke
  callbacks inline; stream internals remain partial (K27 shared stream core is
  the settled fix); and crypto lacks asymmetric primitives and complete
  Node-style error behavior. See `doc/dev/js/JS_14_Node_Compat.md` §11.
- **OI-8 · DOM fidelity.** No on-read layout flush, so mutate-then-read
  `offsetWidth` sees stale pixels; framework-blocking API gaps fail as silent
  `undefined`; O(n) listener/wrapper storage degrades quadratically; no text
  segmentation or Bidi.
- **OI-9 · Unboxed scalar storage in maps and arrays · DEFERRED by decision.**
  Shaped slots and array elements as a guaranteed, inline-addressable raw
  representation. Two rulings were **decided 2026-07-16 and must not be
  relitigated casually**: (1) **no in-band tombstones in unboxed scalar storage,
  for any type** — absence is always out-of-band, delete/uninitialized forces a
  transition back to boxed, holey arrays stay boxed; (2) **adopt the ArrayNum
  raw-storage discipline** (element-width-aware compaction, data buffers never
  scanned as Items) rather than inventing new rules. Open: transition policy on a
  non-conforming write, write-path blast radius, GC/shape coherence during
  transition, and whether scope is fields-only or fields + elements-kind.

### 15.1 Cross-cutting hygiene themes

One policy each, not per-site fixes.

- **Silent fixed caps with inconsistent failure modes** — closure captures 16,
  generator states 63, promise reactions 8, TypeMap hash 32, union types 32,
  module vars 2048/1024, regex groups 256, and more. One grow-or-error doctrine
  retires the class. Ledger instances: [LR01-5](#lr01-5), [LR11-4](#lr11-4),
  [LR13-5](#lr13-5).
- **Layout-coupled raw offsets** — resolved for module binding by removing the
  unreachable `init_module_import` walk ([LR01-8](<Lambda_Issue_Ledger (fixed).md#lr01-8>)); GC
  trace/compaction is resolved by [LR08-5](<Lambda_Issue_Ledger (fixed).md#lr08-5>).
- **One masked memory-safety bug** — the event-loop SIGSEGV band-aid remains;
  the `sys://` map-walk segfault workaround was replaced by the shape-aware
  traversal in [LR01-R3](<Lambda_Issue_Ledger (fixed).md#lr01-r3>).
- **`SysFuncInfo` registry expressiveness** — data-driven argument/return
  conventions would delete inline special-casing ([LR09-1](#lr09-1),
  [LR09-2](<Lambda_Issue_Ledger (fixed).md#lr09-2>)).

### 15.2 Settled designs awaiting implementation

No new decisions needed; each has an owning design doc.

| Work | Design doc | Unblocks |
|---|---|---|
| Unified AST Phases 0–5 | `Lambda_Design_Unified_AST.md` (U1–U26) | shared emitter/inference and guest ports; Lambda MIR Direct OI-5 is resolved |
| K27 shared stream core | `Lambda_Design_Concurrency.md` §11 | OI-7 streams, fs/event-loop integration |
| De-pointered MIR P1–P5 | `Lambda_Design_MIR_Cache.md` (MC1–MC8) | OI-6 artifact caching |
| JS threading P1–P3 | `Lambda_Js_Thread.md` (JT1–JT7) | worker isolation/watchdog; feeds `vm` realm isolation |
| Concurrency Stage A/B | concurrency v3 (K11–K18) | real suspension; actor/mailbox K20 |
| Stack-frame Python port | `Lambda_Design_Stack_Frame_Python.md` (PS1–PS10) | PO1–PO6 |

---


# Appendix B — Cross-cutting clusters

Several ledger entries are one defect wearing different masks. Fix them
together, not individually.

| Cluster | Entries | Root |
|---|---|---|
| **TCO safety proof residue** | LR07-13 | The former root-classification faces LR07-7/LR08-3 are resolved and archived. The surviving TCO face is the unused `is_tco_function_safe` proof, now tracked independently under LR07-13. |
| **Representation ↔ semantics coupling** | LR07-14 | Remaining container and result-domain cases. Lambda expression lowering carries `MirValue`; see resolved [LR07-1](<Lambda_Issue_Ledger (fixed).md#lr07-1>). |
| **Silent-truncation caps** | LR01-5, LR01-6, LR03-2, LR05-6, LR07-11, LR08-6, LR08-10, LR11-4, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency (LR11-4) is the clearest statement of the pattern. |
| **Surface syntax (S16) residue** | S16.9.5, i8-genafterlet, SO36, O3, §7.17, LR02-25, LR02-27 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). LR02-24–LR02-27 (2026-09-25) are four such type-sublanguage splits between the front ends: a range after `is`, a chained count, a line-start `?`, and signature parameters without `: T`. The harnesses stand at 343/343 C and 330/330 Tree-sitter at `293b7a175`, and neither covers these forms. |
| **Process globals** | LR12-6 | `g_template_registry` is now context-local; `g_dry_run` remains process-global and blocks per-run dry-run semantics. See RG1–RG14 in [Runtime globals audit], RC1–RC8 in [Radiant concurrency design]. |

---

# Appendix C — Maintaining this ledger

1. **This file is the working list; `LR_*` sections stay as design record.**
   When an `LR_*` "Known Issues" section changes, mirror the change here with
   the same `LRnn-k` ID. IDs are stable — a resolved issue keeps its number and
   moves to the fixed archive with an `-R` suffix rather than being renumbered.
2. **Re-verify before acting.** Every `file:line` in this document was resolved
   against the dated verification passes above and will drift. Grep the quoted identifier,
   not the line number.
3. **Cite rulings by formal-spec ID** (CLAUDE.md rule 17): `S#` from
   `doc/Lambda_Formal_Semantics.md`, `D#` from `doc/Lambda_Formal_Design.md`;
   vibe ledger IDs (OI-#, TE-#, RG-#, RC-#, TIG#) only where no formal ruling
   covers the point.
4. **Do not close an issue from a doc edit alone.** A resolution needs a
   verified source anchor, as every fixed-archive entry has.
