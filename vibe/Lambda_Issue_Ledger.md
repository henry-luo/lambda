# Lambda Core Runtime — Central Issue Ledger

> **Consolidated from the per-area "Known Issues & Future Improvements" sections of
> the former core-runtime design set (areas `LR01`–`LR13`)**, plus
> verified items from the former sibling `vibe/Lambda_Issue*.md` ledgers (§14)
> and the retired Outstanding rollup's design gaps (§15).
>
> **This is the only active issue ledger in `vibe/`.** Fixed and obsolete records
> are archived in the sibling [fixed issue ledger](<Lambda_Issue_Ledger (fixed).md>).
> Every former sibling was reviewed on 2026-08-25 and retired to `vibe/impl/`;
> each keeps its detail and evidence, while its live residue is indexed here.
> Add new issues here, not to the fixed archive or a retired archive. This is
> the single working list for core-runtime defects, caps, and structural
> hazards. Design lives in the formal specs and the vibe records; this ledger is the
> *issue* record.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## Sync implementation runtime findings — 2026-11-11

The local-repository follow-up fixed and archived [LR07-68](#lr07-68),
[LR12-45](#lr12-45), [LR12-46](#lr12-46) and [LR12-47](#lr12-47), with the final **7,458/7,458**
baseline and tier/GC evidence. The general builtin dynamic-call adapter gap
[LR07-69](#lr07-69) remains open; it is independent of the direct APIs used by sync.

Registered two defects found and fixed during CRDT implementation:
[LR07-66](<Lambda_Issue_Ledger (fixed).md#lr07-66>) preserves actual logical
operand outcomes before choosing a native Boolean lane (S3.4, D4.3.1);
[LR07-67](<Lambda_Issue_Ledger (fixed).md#lr07-67>) retains the imported
caller's success-domain proof after caret propagation (D4.3.1). Both are
resolved and archived with reductions, fixes and qualification evidence.
The separate timeout/cleanup crash [LR01-24](#lr01-24) remains open. The
workspace member-access observation below retains its original probe; its
semantics and cause remain unconfirmed. Those two earlier archived records add no live issues.

## Recovery/scale integration findings — 2026-11-12

<a id="sync-recovery-scale-safety"></a>

**Status:** FIXED for the named script integration defects; qualification residue
is retained below. The [implementation record](<impl/Lambda_Impl_Sync_Recovery_Scale (done).md>)
records the selected bounded profile (S12.1.1v3, S12.4.4v4, D11.1.2).

The new reuse protocol initially chose a cancelled maintenance job's physical
storage generation again. Its uploaded keys remained in the registry: an open
publication could reference those future keys, or the next deletion job could
stop because a candidate used its current generation. Reproduction:
`sync_reuse_fencing.ls` aborts generation 1 after upload, starts a new cut, and
replays old requests. Allocation now advances `next_epoch` even on abort;
publication rejects abandoned generations. The next cut skips to generation 2
and can reclaim the abandoned keys. Later cuts/reapers cannot affect retained
old copies or newly uploaded paths. This is a new script integration fix;
the committed legacy profile already filtered future keys from its old-key list.

The existing scheduled client could also treat an aborted job as a committed
cut, then repeatedly try to prune it. It now returns to selection after seeing
its job's explicit abort. The real-S3 recovery gate covers this transition on
AUTO, interpreter and JIT with unchanged deadlines. No lease-based
cancellation or automatic release of another writer's pending uploads is added.

A release 16 MiB capture/external-edit/restore workflow exposed excessive typed
codec allocation: about 5.88 GB maximum RSS / 10.34 GB peak footprint before the
fix. Native hash streaming alone used about 59.8 MB maximum RSS, and incremental
script chunking about 76.8 MB. Per-scalar Mark parsing and text-encoded chunk
payloads repeatedly created Input-owned values; D5.1.1–D5.1.3 deliberately keep
those outside GC. Batch scalar validation preserves canonical wire bytes, and
new streamed `chunk_raw` objects use raw plaintext and raw domain contexts.
The revised JIT workflow passed exact restores with about 489 MB peak footprint;
interpreter passed with about 298 MB. Final source/binary identities and all-tier
qualification are recorded in the implementation record. This is a confirmed package hot
path fix; the cause of the earlier concurrent JIT document SIGKILL remains
unconfirmed. No native/vendor lifetime change or general SIGKILL fix is claimed.

The large interpreter peer test also exposed a pre-existing scheduling problem:
initial server capture ran after handshake, consuming a peer's fixed deadline.
The agent now captures before advertising readiness; subsequent scans reuse only
exact unchanged inode/revision observations. Deadlines are unchanged.

Aggregate verification returned 7,573/7,574. `eps_fonts`
(`test/lambda/eps/fonts.ls`) returned script status 1 in its baseline batch;
three isolated GTest retries passed with unchanged fixture, golden and deadline.
Cause is unconfirmed; no EPS/input/runtime fix or complete aggregate pass is
claimed. This observation does not block the sync milestone. Evidence:
`temp/sync-recovery-scale/baseline.log`, `eps-fonts-retry*.log` and final
source/binary identities. All 70 sync goldens and five focused GC/poison cases
pass on every tier; real-S3 recovery and retention/cache/crash gates also pass.


## Retention integration findings — 2026-11-12

<a id="sync-retention-safety"></a>

**Status:** FIXED in the script integration; no new native-runtime defect is
claimed. Specification boundaries remain S12.1.1v3, S12.4.4v4 and D11.1.2.
The [retention implementation record](<impl/Lambda_Impl_Sync_Retention (done).md>) carries
qualification and bounded scope. Findings and reproductions:

- A partial workspace checkpoint after retirement still held the old replica.
  Rechecking its dependencies against the new retired set rejected offline return.
  Validate changed accepted replicas, retaining partial-frame progress. The
  four-device retention process gate merges unsaved edits after base purge.
- Equal-frontier concurrent saves could bind different timestamps to one saved
  ID. New IDs bind timestamp and unique persisted intent (`lambda-save-2`);
  history tests require distinct valid endpoints and complete ancestry heads.
- Opening a deferred SQLite scope did not acquire writer ownership before file
  deletion. A real pending head write now acquires the lock before effects;
  cursor commit follows deletion. The process gate pauses after one removal,
  rejects another writer, kills the first process and resumes the exact plan.
- Unindexed recovery state and rolled-back cache eviction could lose dependency
  provenance. Reachability now traverses each recovery state's own retention
  context; locator rebuild quarantines orphans; verified durable cache intent
  reconstructs missing-chunk availability. Offline/repository/process fixtures
  cover orphan snapshots, resumed pruning and cache recovery.
- Missing publication catalogs could otherwise recreate epoch zero. A durable
  protocol marker and signed bootstrap profile now fail closed. Immutable
  catalog backups allow explicit owner recovery with dependency validation;
  destructive maintenance remains audit-blocked. `sync_retention_cloud.ls`
  removes the catalog, requires rejection, restores its known backup, and
  requires the audit barrier. No automatic disaster audit is claimed.

Qualification residue: the concurrent aggregate baseline passed 7,542/7,543;
`latex_test_tex_script_digest` returned status 124 at its 60-second deadline.
Its focused default-deadline retry passed in 58.7 seconds. Concurrent LaTeX
sources are outside this sync change; no timeout/golden/source was adjusted.
A final JIT document peer exchange also exited by SIGKILL (`82-sync`, no native
diagnostic) during that aggregate run. Kernel diagnostics did not identify a
Lambda memory kill; resource contention is only a hypothesis. The isolated
process retry passed all 18 checks without code/deadline changes. Its late-stage
observed process-tree peak was about 5.2 GiB; that does not establish the original
kill cause. Evidence and exact identities remain in
`temp/sync-retention/final/`; preserve the failed run as qualification residue,
not a fixed runtime bug or a claim of production workload capacity.

Existing [LR01-24](#lr01-24) and sibling LI-015 remain open and separate.
No owner source or native test timeout was changed to pass these checks.

## Durable document integration findings — 2026-11-12

<a id="sync-document-coverage"></a>

**Status:** FIXED in the new script host; no new core-runtime defect is claimed.
During integration, using the protocol's causal-clock status as whole-document
durability would report an empty Text document as covered before its root was
published. A newly saved version can also change shared metadata without changing
the live edit clock. The protocol status is a causal-frontier predicate; the host
must supply publication evidence (D11.1.2, S12.1.1v3).

`document_state.coverage` therefore requires a receipt for the current immutable
document root in addition to the causal frontier. Local persistence records the
root before reply; peer ACK and verified cloud publication retain their separate
root receipts. An empty-document regression in `sync_document_repository.ls`
requires local coverage and rejects premature peer/cloud coverage.
Agent-level status also combines workspace and document receipts;
`sync_document_coverage.ls` supplies workspace-only evidence, requires the overall
flag to remain false, then admits the document root and verifies that a later live
edit invalidates it. Configured cloud status checks its selected provider. Existing
same-epoch CRDT semantics are unchanged. The
[document implementation record](<impl/Lambda_Impl_Sync_Documents (done).md>) holds final
verification, source identities and remaining scope. The first sandboxed sync
sweep's listener failures were environmental (`cannot bind the listener`);
the loopback-enabled sweep passed without changing fixtures or the runtime.

## System-function results alias their arguments — 2026-11-11

<a id="lr12-47"></a>

**LR12-47 — FIXED:** system functions and `++` that place an argument's items
into their result now capture them (S9.3.1), so a write through the argument or
through the result no longer reaches the other, on either tier. See
[cause, fix and qualification](<Lambda_Issue_Ledger (fixed).md#lr12-47>).

## Place borrows and the watercolor timeout — 2026-11-11

<a id="lr12-45"></a>

**LR12-45 — RESOLVED:** a member place passed to a Lambda `pn` beside container
arguments is now borrowed in place on both tiers unless a plain argument may
alias it, and MVP no longer captures a binding passed to a forwarding wrapper
or left untyped by a native `var` call. See
[cause, fix and qualification](<Lambda_Issue_Ledger (fixed).md#lr12-45>).

<a id="lr12-46"></a>

**LR12-46 — RESOLVED:** the `proc_procart_watercolor_sim` timeout (recorded
here as LR12-45 by another session on the same day, renumbered because that ID
was already in use) came from promoted functions copying the whole cells array
on each store. See [cause, fix and qualification](<Lambda_Issue_Ledger (fixed).md#lr12-46>).

## Radiant integration layout-host selection — 2026-11-11

<a id="lr12-44"></a>

**LR12-44 — RESOLVED:** the user-approved Lambda layout/snapshot host selection
repair is applied. See [cause, patch and qualification](<Lambda_Issue_Ledger (fixed).md#lr12-44>).
Radiant RI-010 retains the ten independent layout comparisons.

## Numeric conversion and recursive type follow-up — 2026-11-03

The user adopted **S4.1.6 / S11.4.12v2**. `int()` now returns an int with
sign-preserving saturation; E240 stays fatal in strict and relaxed modes.
[LR04-9](<Lambda_Issue_Ledger (fixed).md#lr04-9>) and
[LR07-41](<Lambda_Issue_Ledger (fixed).md#lr07-41>) moved to the fixed ledger.
The inferred-string outcome from [LR07-18](#lr07-18) already follows
S7.7.4v2 and is covered by a new regression; its separate effect-policy
questions remain open. Focused qualification passed **102/102** tier/GC/
strictness runs; the final Lambda/input baseline passed **8,732/8,732**. [Implementation and qualification](<impl/Lambda_Impl_Ledger_Tier_Fixes (done).md>).

## Place borrows, row snapshots and async requalification — 2026-11-03

The user clarified **S9.1.3v3 / S9.2.2v2 / D5.4.6**. Rooted place homes now
publish rebinds, COW detaches and scalar updates on both tiers; row/view values
snapshot. [LR12-10](<Lambda_Issue_Ledger (fixed).md#lr12-10>) and
[LR12-14](<Lambda_Issue_Ledger (fixed).md#lr12-14>) moved to the fixed ledger.
[LR01-17](<Lambda_Issue_Ledger (fixed).md#lr01-17>) was already superseded by
runtime-async: its concurrency/async sweep passed 210/210 tier/GC runs.
The final Lambda/input baseline passed **8,718/8,718**.
[Implementation and qualification](<impl/Lambda_Impl_Place_Borrow_Views (done).md>).

## Undefined-name follow-up — 2026-11-03

The user adopted **S16.10.4**: an unresolved value name rejects compilation,
including in relaxed mode. The resolver now reports E202/E203 after binding
and call dispatch; [LR02-35](<Lambda_Issue_Ledger (fixed).md#lr02-35>) is fixed.
The baseline passed 8,704/8,712 in the sandbox; the eight listener cases passed
with loopback access. [Implementation and qualification](<impl/Lambda_Impl_Unresolved_Names (done).md>).

## Handler values and invalid writes follow-up — 2026-11-03

Fixed the direct-write residue of **LR12-27** under **S7.1.3v3 / S7.7.6v2**:
unannotated stores now preserve the existing containment/reporting boundary on
both tiers. The original out-of-range write reports E302 and exits one;
recoverable calls retain their error payload and container snapshots.
**LR10-13 / LR10-18** already conform to **S7.6.1v9 / S7.6.7v5** and moved to
the fixed ledger after rechecking. The caller policy for a discarded plain
procedure call remains an LR12-27 ruling question. LR09-31 still needs its
separate ruling.

Qualification: **56/56** focused tier/promotion/GC runs; baseline
**8,700/8,708**, with all eight sandbox-denied listener cases passing their
socket-enabled rerun. See
[implementation and qualification](<impl/Lambda_Impl_Ledger_Handler_Write_Followup (done).md>).

## Type-operator precedence follow-up — 2026-11-03

The user adopted **S10.1.1v5**: ordinary expressions now use the type-pattern precedence `to` > `&` > `!` > `|`. The production C parser has distinct binding powers for these operators. **LR02-37 remains open pending focused qualification; no tests were run in this change.**

## Names, containment and matrix-row follow-up — 2026-10-31

Implemented the long-name portion of **LR07-11**, correctness residues 1–4 of
**LR12-24**, and strict-T0 row stores for **LR12-31**, under existing
**S1.6 / S7.7.2–S7.7.6 / D7.1.3 / D8.3.2** rulings. LR07-11's scope
and loop caps and LR12-24's proof/join optimization residues remain open.
LR12-31 moved to the [fixed ledger](<Lambda_Issue_Ledger (fixed).md#lr12-31>).
Implementation and qualification: [containment follow-up](<impl/Lambda_Impl_Ledger_Containment_Followup (done).md>).
No pending ruling was decided.

## Names, presence and run-configuration follow-up — 2026-10-29

Implemented the remaining **S16.9.5** nominal/attribute presence cases,
**LR01-5** profile capacity and **LR12-6** owner-scoped dry-run configuration
(**D6.4.1–D6.4.4**). Their records are in the
[fixed ledger](<Lambda_Issue_Ledger (fixed).md>).
**LR03-40 / S8.2.2v6** has its four core discrepancies repaired, but remains
PARTIAL because Mark namespace serialization needs a ruling. No new policy was
chosen. Implementation, compatibility changes and qualification:
[names, presence and run-configuration follow-up](<impl/Lambda_Impl_Ledger_Names_Presence_Followup (done).md>).

## Parser, admission and formatting follow-up — 2026-10-29

Fixed **LR02-21**, **LR02-22**, **LR02-25**, **LR09-32**, **LR10-11** and
**LR10-20** under existing **S16.10.1v3 / S11.1.6v3 / S7.6.2v2 /
S7.11.1v2–S7.11.2v2** rulings and **PTH33**. All six moved to the
[fixed ledger](<Lambda_Issue_Ledger (fixed).md>). The **S16.9.5** typed-map
and pattern presence distinction was repaired in that pass; the nominal and
required-attribute follow-up is now [closed](<Lambda_Issue_Ledger (fixed).md#s16-9-5-gap>). No pending ruling was decided.

Implementation and final qualification:
[parser, admission and formatting follow-up](<impl/Lambda_Impl_Ledger_Parser_Admission_Followup (done).md>).
Qualification: Lambda baseline **8441/8441**, Test262 **40261/40261**,
release tier/GC checks **90/90**, ASan **14/14**, and S16 harnesses
**442/442 C**, **428/428 reference**.

## Error and activation follow-up — 2026-10-29

Fixed **LR02-32**, **LR02-33**, **LR08-13**, **LR10-14**, **LR10-15** and
**LR12-43** under existing **S7.4.4 / S8.2.4v3 / S11.4.1v3 / S12.3.4 /
S12.3.6 / D6.1.1 / D6.3.3 / D6.3.6** rulings. Five older records
already pass and now have permanent regression coverage: **LR05-16**,
**LR07-20**, **LR07-39**, **LR07-48** and **LR10-17**. All eleven moved to
the [fixed ledger](<Lambda_Issue_Ledger (fixed).md>), preserving their original
reports and distinguishing new fixes from verified existing repairs.
Pending-ruling items remain open.

Implementation and qualification:
[error and activation follow-up](<impl/Lambda_Impl_Ledger_Error_Activation_Followup (done).md>).
Qualification: Lambda baseline **8420/8420**, Test262 **40261/40261**,
release tier/GC checks **90/90**, native activation/runtime checks **34/34**,
and ASan live runtime/mesh assertions **22/22** and **17/17**. The unrestricted
auxiliary native runner's Jube/context failures remain explicitly recorded
in that implementation record.

## Ownership follow-up — 2026-10-29

Fixed **LR12-32**, **LR12-34** and **LR13-17** under existing **S9.1.2 /
S9.1.3 / S9.3.1 / S11.4.1v3 / S16.9.8 / D5.2.5** rulings. **LR12-41** was
already fixed and is now verified with a permanent regression. All four
records moved to the [fixed ledger](<Lambda_Issue_Ledger (fixed).md>).
No pending-ruling policy was changed.

Implementation and final qualification:
[ownership follow-up](<impl/Lambda_Impl_Ledger_Ownership_Followup (done).md>).
Qualification: Lambda baseline **8403/8403**, Test262 baseline
**40261/40261**, focused debug/release tier-and-GC checks **60/60**,
MIR emission **279/279** and size ratchet **20/20**. No emission budget
or expectation changes were needed.

## Ruled implementation pass — 2026-10-28

Twenty-two reproduced records were resolved under existing rulings and moved
to the [fixed ledger](<Lambda_Issue_Ledger (fixed).md>). The selected nineteen
items are complete except LR07-40's unknown-name residue: its duplicate-slot
half now reports E206 and the record remains PARTIAL. Four related fixes cover
named error acknowledgement, symbol documentation, input-note severity and
literal diagnostic names. No pending-ruling policy was changed.

Implementation details, fixtures, emission review and qualification:
[Lambda_Impl_Ledger_Ruled_Fixes](<impl/Lambda_Impl_Ledger_Ruled_Fixes (done).md>).
The final Lambda baseline is **8397/8398**, with one HTTP browser fixture
failure; the full HTTP suite passes **152/152** in isolation. Focused tier/GC
checks pass **78/78**, C S16 **428/428**, and Tree-sitter S16 **414/414**.
Test262 baseline was not run in this pass.

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
| `Lambda_Issues_Outstanding.md` | **Reviewed in full and RETIRED 2026-08-25** → archived as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 was verified subsumed by §1–§13; §2/§4 were pointer indexes into docs that still own them; its OI design gaps and hygiene themes moved to §15. One item was genuinely missing from this ledger — LR12-8, now resolved and moved to [LR12-R8](<Lambda_Issue_Ledger (fixed).md#lr12-r8>). |
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
ruling: LR02-18 (**S12.3.3v2**, **D3.1.4**), LR05-3 (**S6.2.2v3**), LR07-12,
LR08-8, LR10-1, and LR10-4. Focused JIT/T0 fixtures and the error regression
suite pass; the resolution records are in the fixed issue ledger.

### Closure verification pass — 2026-09-13

Four high-impact records were re-verified against the current tree and moved to
the fixed issue ledger: LR01-12 (`g_template_registry` is now owned by
`EvalContext`), LR07-7/LR08-3 (precise JIT-root classification and its collector
consumer now have fail-closed coverage), and LR12-9 (S9.1.2/S9.1.3/S9.3.1 COW
capture is unconditional; the escape hatch is retired). The root-witness
level-2 corpus sweep reports zero violations, and the COW fixtures pass under
forced GC and poison mode on JIT. LR13-8 (validator success-path work) remains active.
[LR12-6](<Lambda_Issue_Ledger (fixed).md#lr12-6>) closed on 2026-10-29.

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

| Area ID | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR01 | Compilation pipeline, CLI & REPL | 6 | 2 | 0 | 8 |
| LR02 | Parsing & AST construction | 2 | 4 | 0 | 6 |
| LR03 | Value & type model | 3 | 1 | 0 | 4 |
| LR04 | Numbers, decimal & datetime | 5 | 1 | 0 | 6 |
| LR05 | Strings, symbols & vectors | 2 | 1 | 0 | 3 |
| LR06 | C transpiler (legacy C2MIR) | 0 | 0 | 0 | 0 |
| LR07 | MIR Direct transpiler & JIT | 9 | 0 | 0 | 9 |
| LR08 | Memory management & GC | 6 | 0 | 0 | 6 |
| LR09 | Runtime builtins | 3 | 0 | 0 | 3 |
| LR10 | Error handling | 1 | 0 | 0 | 1 |
| LR11 | Mark data API | 6 | 0 | 0 | 6 |
| LR12 | Procedural runtime | 5 | 0 | 0 | 5 |
| LR13 | Schema validator | 6 | 1 | 0 | 7 |
| TS / Issues8 / Lint / Issues0 | Sibling vibe ledgers | 5 | 1 | 0 | 6 |
| **Live total** | | **61** | **11** | **0** | **72** |

The active ledger now contains 72 live records, with the 64 previously counted
resolved records moved to the archive. Duplicate/split records and
verification-only findings remain represented there for provenance.
Two original entries each split into a resolved half and a surviving residue —
LR03 #4 (sentinels → LR03-R4 + LR03-5) and LR05 #3 (two string orderings →
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
`LAMBDA_C2MIR`; the `--c2mir` CLI flag is not parsed). All nine LR06 issues are
therefore archived as obsolete, and every cross-doc "diverges from C2MIR"
framing (LR07-3) now reads as a plain MIR Direct gap rather than a backend
divergence.
This is consistent with CLAUDE.md rule 14.

### Semantics gap survey — 2026-09-21

A seven-area survey of runtime behaviour against `doc/Lambda_Formal_Semantics.md`
(probes under `temp/spec_survey/`) surfaced eight defects that return a **wrong
value with no error**. Each was re-reproduced on both tiers before filing:
[LR03-11](<Lambda_Issue_Ledger (fixed).md#lr03-11>), [LR04-9](<Lambda_Issue_Ledger (fixed).md#lr04-9>), [LR05-9](<Lambda_Issue_Ledger (fixed).md#lr05-9>), [LR07-17](<Lambda_Issue_Ledger (fixed).md#lr07-17>),
[LR07-18](#lr07-18), [LR10-7](<Lambda_Issue_Ledger (fixed).md#lr10-7>), [LR10-8](<Lambda_Issue_Ledger (fixed).md#lr10-8>), [LR12-27](#lr12-27).
The survey's spec gaps (behaviour no `S#` ruling covers) are not filed here —
they need rulings, not fixes. *2026-09-22:* the list/array kind gaps were ruled
(S2.5.6–S2.5.8, S10.6.1, S11.1.6v2 and companions, semantics 29.0.0) and filed as
[LR03-12](<Lambda_Issue_Ledger (fixed).md#lr03-12>), [LR05-10](<Lambda_Issue_Ledger (fixed).md#lr05-10>), [LR05-11](<Lambda_Issue_Ledger (fixed).md#lr05-11>),
[LR05-12](<Lambda_Issue_Ledger (fixed).md#lr05-12>), [LR12-28](<Lambda_Issue_Ledger (fixed).md#lr12-28>). P1 of the list fixes then found
[LR02-18](<Lambda_Issue_Ledger (fixed).md#lr02-18>) (reference grammar has no list literal) and fixed
[LR02-19](<Lambda_Issue_Ledger (fixed).md#lr02-19>) (let-group item loss, another wrong value with no error).
The effect-colour work of the same day (D7.1.1, ES48) fixed
[LR12-30](<Lambda_Issue_Ledger (fixed).md#lr12-30>) (an `fn` could call a built-in procedure). Moving the
effect examples into a `pn` found [LR10-9](<Lambda_Issue_Ledger (fixed).md#lr10-9>): E228 is checked only in
top-level expression statements. Triaging the baseline gate against a clean control
then found and fixed [LR01-16](<Lambda_Issue_Ledger (fixed).md#lr01-16>), an `auto`-tier satellite key-linking
defect that made 17 baseline scripts and the MathLive gate timing-dependent, and
filed [LR01-17](<Lambda_Issue_Ledger (fixed).md#lr01-17>). P2 of the list fixes (sequence operations keep their
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

The 2026-09-24 grammar work noted five disagreements between the two front ends without filing them. Each was re-run at `293b7a175` on `lambda.exe` and on the reference grammar, regenerated with the pinned tree-sitter CLI 0.25.10 (S16 harnesses: 343/343 C, 330/330 Tree-sitter). Under D8.1.1, each is judged against the rulings.
- **Four reproduce** and are filed as [LR02-24](<Lambda_Issue_Ledger (fixed).md#lr02-24>)–[LR02-27](#lr02-27). LR02-24 and LR02-26 were fixed on 2026-09-25, with LR02-20.
- **The fifth no longer reproduces.** The grammar had lexed keywords as type names (`let x: if = 1`, `fn f() pn { 1 }`). Since 538dca7b0 reserved them, both front ends reject every probe.
- **Checking LR02-24 found [LR03-14](<Lambda_Issue_Ledger (fixed).md#lr03-14>), and fixing it found [LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>).** Both had one cause and were fixed later on 2026-09-25 (see "Range type fix" below).

Setup note: the checkout first needed `npm install`. `node_modules` still held CLI 0.24.7, although `package.json` pins 0.25.10. The untracked ABI 14 `src/parser.c` then failed to compile against the committed ABI 15 `parser.h`. `test/ts_s16_conformance.sh` classifies a case by grepping for `ERROR|MISSING|Unexpected`, so it reads that compile failure as an accept.

### Wrong-value fix pass — 2026-09-25

The "wrong value, no error" group was triaged against the rulings; each defect a ruling covers was fixed on both tiers, and the rest wait on a decision.
- **Fixed and archived:** [LR03-11](<Lambda_Issue_Ledger (fixed).md#lr03-11>) (sized and literal admission), [LR07-16](<Lambda_Issue_Ledger (fixed).md#lr07-16>) (named arguments on a dynamic call, S12.3.2), [LR07-17](<Lambda_Issue_Ledger (fixed).md#lr07-17>) (imported literals, and a failed module init, D9.3.2), [LR10-7](<Lambda_Issue_Ledger (fixed).md#lr10-7>) (error members).
- **Fixed in part:** [LR04-9](<Lambda_Issue_Ledger (fixed).md#lr04-9>) (in-band `int()`), [LR07-18](#lr07-18) (error-returning rows, `format`), [LR12-27](#lr12-27) (push). Each keeps its residue.
- **Waiting on a ruling:** [LR03-13](#lr03-13), [LR09-31](#lr09-31), [LR10-8](<Lambda_Issue_Ledger (fixed).md#lr10-8>), plus the residues above.
- **Found on the way, all reproduced:** [LR03-15](<Lambda_Issue_Ledger (fixed).md#lr03-15>), [LR03-16](<Lambda_Issue_Ledger (fixed).md#lr03-16>), [LR07-19](<Lambda_Issue_Ledger (fixed).md#lr07-19>)–[LR07-22](<Lambda_Issue_Ledger (fixed).md#lr07-22>), [LR10-10](<Lambda_Issue_Ledger (fixed).md#lr10-10>), [LR12-31](<Lambda_Issue_Ledger (fixed).md#lr12-31>)–[LR12-34](<Lambda_Issue_Ledger (fixed).md#lr12-34>). [LR07-21](<Lambda_Issue_Ledger (fixed).md#lr07-21>), the JIT's `for` over bools, has since been fixed and archived.

Three goldens had pinned wrong values and were corrected (`conc/cancel_*`), and several fixtures that are wrong only on the JIT had been hidden because goldens run on `auto`, which starts in T0. The new fixtures are pinned on every tier.

### Error containment pass — 2026-09-27

The six "error containment is only partly built" items were worked on both tiers.
- **Fixed and archived:** [LR10-9](<Lambda_Issue_Ledger (fixed).md#lr10-9>) (E228 in every position), [LR10-10](<Lambda_Issue_Ledger (fixed).md#lr10-10>) (the error members, `error(msg, source)`, `last_error` rooting) and [LR12-25](<Lambda_Issue_Ledger (fixed).md#lr12-25>) (`print` of an error, `string(err)` escaping containment).
- **Implemented, waiting on a ruling:** [LR03-13](#lr03-13) (a call through a declared function-type contract checks its result).
- **Ruled and fixed:** [LR10-8](<Lambda_Issue_Ledger (fixed).md#lr10-8>): `raise v` of a non-error is `raise error(v)` (S7.4.6, TE-21, user ruling of 2026-09-27).
- **TE-17 I3 landed, residue open:** [LR12-24](#lr12-24), with the D7.1.3 `may_defect` fixed point it was blocked on.
- **Found on the way, all reproduced:** [LR02-31](<Lambda_Issue_Ledger (fixed).md#lr02-31>), [LR10-14](<Lambda_Issue_Ledger (fixed).md#lr10-14>)–[LR10-16](<Lambda_Issue_Ledger (fixed).md#lr10-16>) and [LR12-36](<Lambda_Issue_Ledger (fixed).md#lr12-36>), which was fixed and archived later the same day. A `type` or function value in an `any` map slot read back as an error on both tiers (`typeditem_to_item` had no arm for either); fixed in passing.

Verified: `make test-lambda-baseline` 5974/5974 once the four emission-size probes are re-baselined (`test/mir/mir_budgets.json`, reasons recorded there). New fixtures, pinned on interp, jit and auto: `proc/defect_value_flow`, `proc/rejected_error_value_flow`, `contract_return_check`, `error_members`, `raise_non_error`, `proc/print_error_value`. `normalize_error`'s golden had pinned the hidden early return S7.7.1 forbids and now reads the value flow.

### JIT golden sweep — 2026-09-25

Goldens run on `auto`, which starts in T0, so a wrong value that only the JIT produces can pass the baseline. Running all 955 goldens through the harness with `LAMBDA_EXEC_BACKEND=jit` found 18 failures (`LAMBDA_EXEC_BACKEND=interp`: none). They came from five defects, all fixed and archived; each has a fixture pinned in `kTune27TierParity`, and the 955 goldens now pass on both tiers.
- [LR07-23](<Lambda_Issue_Ledger (fixed).md#lr07-23>): a method's boxed wrapper re-boxed its Item result (12 goldens, six of them segfaults).
- [LR07-24](<Lambda_Issue_Ledger (fixed).md#lr07-24>): a widened bool array's read folded the new value to `false`.
- [LR07-25](<Lambda_Issue_Ledger (fixed).md#lr07-25>): a repeated literal key read its first entry.
- [LR07-26](<Lambda_Issue_Ledger (fixed).md#lr07-26>): a string-pattern `case` compared with `==`.
- [LR07-27](<Lambda_Issue_Ledger (fixed).md#lr07-27>): a direct store wrote a raw int64 over an `i64?` field.

The first, per-file sweep also flagged 109 fixtures that are not JIT defects:
- **80 negative fixtures.** No harness compares their `.txt` files, which have drifted. Comparing the two tiers' output instead, 11 of the 159 negatives differ: [LR07-29](#lr07-29) and [LR10-11](<Lambda_Issue_Ledger (fixed).md#lr10-11>).
- **13 sweep artifacts.** Twelve math and LaTeX fixtures timed out under the sweep's parallel load, and `conc/conc_worker_mod.ls` was run without `run`.
- **5 fixtures on the harness's MIR skip list** (`MIR_SKIP_TESTS`), which the baseline never runs. `object`, `object_inherit`, `object_update` and `map_object_robustness` now pass on all three tiers. `object_direct_access` failed on all three because its field `open` is now a keyword. The first four, together with `object_default`, `object_pattern`, `object_constraint` and `typed_param_direct_access`, which already passed on every tier, were removed from the skip list the same day. `object_direct_access` followed once its field was renamed to `is_open`. The last three entries followed too. `beng_pidigits` and `beng_revcomp` already matched their goldens. `beng_fasta` passed its random seed through a plain parameter, which S9.1.3 made a snapshot, so its THREE section restarted the stream; with a `var` parameter it matches. The emptied skip list was then removed.
- **11 fixtures outside the baseline directories** (`ext/`, `sem/`, `wip/`, `jube/`, `proc-ext/`) fail identically on every tier.

Probing the fixes found [LR07-28](<Lambda_Issue_Ledger (fixed).md#lr07-28>), an untyped array that keeps its inferred element lane after a store widens it, and [LR03-17](#lr03-17), a repeated literal key whose write and read reach different entries.

### Range type fix — 2026-09-25

[LR03-18](<Lambda_Issue_Ledger (fixed).md#lr03-18>) (`is` against a union holding a range) and [LR03-14](<Lambda_Issue_Ledger (fixed).md#lr03-14>) (range-typed parameters) had one cause: a range type wore `LMD_TYPE_RANGE`, the tag of a range value, where D4.1.1 puts it under the shared `LMD_TYPE_TYPE` tag. The same cause made a range-typed map field segfault at a call boundary. All three are fixed on every tier; the two records are archived, and LR03-18's describes the segfault. Checking the fix found three older defects, each reproduced on the binary from before it, and left one residue:
- [LR03-19](<Lambda_Issue_Ledger (fixed).md#lr03-19>): in a nominal object type, a union- or range-typed field that is not last reads back a wrong value.
- [LR03-20](<Lambda_Issue_Ledger (fixed).md#lr03-20>): object construction does not check its field contracts. Fixed the same day.
- [LR07-30](<Lambda_Issue_Ledger (fixed).md#lr07-30>): a range-typed `var` changes its member's representation on the JIT.
- [LR03-21](<Lambda_Issue_Ledger (fixed).md#lr03-21>), the residue: `<:` does not split a range across union arms.

[LR03-16](<Lambda_Issue_Ledger (fixed).md#lr03-16>) gained two symptoms: an integer literal alias is not a type value, so `3 is Three` is `false`.

### Constrained type and `~key` fix pass — 2026-09-25

A review of the `that` proviso (S10.1.5v3) and of `T that cond` (S11.4.6) found four defects. Each was reproduced on both tiers and fixed on both, with fixtures pinned in `kTune27TierParity`:
- [LR03-22](<Lambda_Issue_Ledger (fixed).md#lr03-22>): a constrained type's base was tested by its TypeId, or not at all on the generic `fn_is` path.
- [LR03-23](<Lambda_Issue_Ledger (fixed).md#lr03-23>): a match arm naming a constrained type admitted its base alone, and an alias chain lost its inner predicates.
- [LR13-11](<Lambda_Issue_Ledger (fixed).md#lr13-11>): the validator refused every element of a constrained element type, so `[1, 2] is Pos[]` was `false`.
- [LR07-31](<Lambda_Issue_Ledger (fixed).md#lr07-31>): `~key` in a single-subject body read a stale register on the JIT, and T0 segfaulted on it in a handler's value arm. The JIT's value arm also hid a nested pipe's `~`.

Found on the way: [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>) (T0 answers a predicate outside its allow-list with `false`, so `auto` flips a hot function's answer on promotion) and [LR03-25](<Lambda_Issue_Ledger (fixed).md#lr03-25>) (T0 reads an imported predicate's constants from the importing module), both fixed 2026-09-26 (next section); [LR03-26](<Lambda_Issue_Ledger (fixed).md#lr03-26>) (an inline pattern island never compiles as a constrained base), fixed 2026-09-26 by the type-value fix pass; and, still open, [LR10-12](#lr10-12) (a proviso answers null for an error operand, unruled).

The proviso itself matches S10.1.5v3 on both tiers. By S11.4.6 the generic path is still base-only: a first-class type value, a constrained type nested in a union, container or field, a declaration boundary, and an object type's field and object-level constraints (SO9). `doc/Lambda_Type.md` shows field and object constraints failing `is` (`<User name: ""> is User; // false`); both answer `true`.

### Predicate evaluation pass — 2026-09-26

The user ruled [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>) the way S11.4.11 now reads (TE-20): a `that` predicate is an `fn` body over the scope it is written in, evaluated in full on every tier. Fixed with it, each on both tiers, with fixtures pinned in `kTune27TierParity`:
- [LR03-24](<Lambda_Issue_Ledger (fixed).md#lr03-24>): T0 answered `false` for a predicate outside its allow-list. The colour walk never entered a constraint body, so a `pn` call there compiled and the JIT ran it inside `is`.
- [LR03-25](<Lambda_Issue_Ledger (fixed).md#lr03-25>): T0 read an imported predicate's constants from the importer.

Found on the way and fixed with them: a closure that names a local constrained type did not capture what the predicate reads, so the JIT logged "undefined variable" and failed the test. T0, once it evaluated such predicates, also needed the names a predicate binds itself kept out of the declaring frame. Left open, and fixed later the same day by the hazard pass below: [LR03-27](<Lambda_Issue_Ledger (fixed).md#lr03-27>) (the JIT read an imported predicate's names in the importer) and [LR03-28](<Lambda_Issue_Ledger (fixed).md#lr03-28>) (a predicate that names its own type crashed compilation).

### Subscript probe pass — 2026-09-25/26

Probing slices, index arrays, multi-key subscripts and `last` while the query result shape was re-ruled (S8.2.4v2, then S8.2.4v3) found six defects, none covered by a golden — the goldens subscript with literal keys only. Five were fixed on 2026-09-26 with the S8.2.4v3 implementation (both tiers; `make test-lambda-baseline` see the spec's Appendix A row):
- [LR07-32](<Lambda_Issue_Ledger (fixed).md#lr07-32>): an `any`-typed key took the typed-array int fast path on the JIT.
- [LR07-33](<Lambda_Issue_Ledger (fixed).md#lr07-33>): the checker typed `a[r]` as the element and `m[i, j]` as the row, so a map field stored `0` or `null`.
- [LR07-34](<Lambda_Issue_Ledger (fixed).md#lr07-34>): `fn_int64_index` was emitted but not registered; with it, the interpreter's N-D key decoding and the JIT's unchecked N-D target.
- [LR07-35](<Lambda_Issue_Ledger (fixed).md#lr07-35>): a partial N-D subscript was `null`; it is now the leading-axis view [`Lambda_Typed_Array.md`](Lambda_Typed_Array.md) §5 records.
- [LR07-36](<Lambda_Issue_Ledger (fixed).md#lr07-36>): `last` resolved against an outer container on the JIT, a computed container was evaluated twice, and `a[last] = v` needed a preceding read.

[LR07-37](<Lambda_Issue_Ledger (fixed).md#lr07-37>), an inline `T | null` subscript key that was a set union rather than a type, was ruled (S10.1.1v2) and fixed the same day.

### Empty type pass — 2026-09-26

Implementing `none`, the empty type (S11.1.7), fixed two older defects on the way (spec Appendix A, S11.1.7 row): `==` on type values compared a payload tag, so `number == integer` was `true`, and `is` tested a bare numeric literal type by its tag. It found two more:
- [LR03-29](<Lambda_Issue_Ledger (fixed).md#lr03-29>), fixed later the same day by the type-value fix pass: type equality compared a compound type's payload tag only, so `(1 | 2) == (3 | 4)` was `true`.
- [LR03-30](<Lambda_Issue_Ledger (fixed).md#lr03-30>), fixed the same day: a one-literal alias `type T = 1` was not a type value, and a symbol alias admitted nothing. Its fix also restored `type(int) == type`, which the S11.1.7 identity comparison had made `false`, and found [LR03-31](<Lambda_Issue_Ledger (fixed).md#lr03-31>), also fixed that day: a bool literal type carried no value, so it admitted both bools.

The audit of the day's rulings (S8.2.4v3, S10.1.1v2, S11.1.7) that followed fixed two more defects and completed the rulings' residue: [LR07-38](<Lambda_Issue_Ledger (fixed).md#lr07-38>) (the JIT skipped a one-value numeric literal contract) and [LR13-12](<Lambda_Issue_Ledger (fixed).md#lr13-12>) (the validator refused bare datetime, binary and decimal arms and misread sized types). Container, bool, datetime and binary operands now read as literal types, `unique`/`intersect` take any number of operands, `!none` reduces, and a type query walks the virtual carriers (spec Appendix A rows S10.1.1v2, S8.2.4v3, S11.1.7).

The user then clarified S11.1.7 (spec 47.2.0): literal operands include containers, and its `int & string` sentence is implementation status, not a ruling. Container literals now decide (`[1] & [2]` is `none`). The pass fixed [LR03-32](<Lambda_Issue_Ledger (fixed).md#lr03-32>) (the reduction asked only a literal's own value, so `1 ! int` was `none` though `1.0 is 1`) and [LR13-13](<Lambda_Issue_Ledger (fixed).md#lr13-13>) (`1.0 is (1 | 2)` was `false`). It found two validator defects, still open: [LR13-14](<Lambda_Issue_Ledger (fixed).md#lr13-14>) (`[]` admits every array) and [LR13-15](<Lambda_Issue_Ledger (fixed).md#lr13-15>) (`{a: null} is {a: null}` is `false`).

### Procedure arrow pass — 2026-09-26

The user ruled the unnamed form of [LR02-21](<Lambda_Issue_Ledger (fixed).md#lr02-21>) as S16.6.7v2 (spec 48.0.0): `fn` never begins an anonymous function, and the procedure arrow `pn (x) => { … }` is the anonymous procedure ([Design_Syntax §7.30](Lambda_Design_Syntax.md)). Both front ends implement it; the S16 harnesses stand at C 372/372 and Tree-sitter 358/358, and `test/lambda/proc/pn_arrow.ls` is pinned in `kTune27TierParity`. That remaining value-name residue was fixed 2026-10-29 under S16.10.1v3; LR02-21 is archived. Probing the arrow found [LR10-13](<Lambda_Issue_Ledger (fixed).md#lr10-13>): a value-producing handler over a `pn` call yields `null`, where S7.6.7v3 makes it a compile error. A named `pn` does the same.

### Type-value fix pass — 2026-09-26

Seven records were closed on both tiers and archived, each with a fixture pinned in `kTune27TierParity`:
- [LR03-19](<Lambda_Issue_Ledger (fixed).md#lr03-19>): an object type's fields stride by storage size, so a union- or range-typed field no longer overlaps the next.
- [LR03-29](<Lambda_Issue_Ledger (fixed).md#lr03-29>): type equality compares normalized forms (S5.5.2), and the hash agrees with it (S5.6.2).
- [LR03-21](<Lambda_Issue_Ledger (fixed).md#lr03-21>): a union's arms may split a range under `<:`, and the S11.1.7 reduction splits one the same way.
- [LR03-26](<Lambda_Issue_Ledger (fixed).md#lr03-26>): an inline pattern island compiles where its type resolves, so it admits as a constrained base, a union arm or a field.
- [LR07-19](<Lambda_Issue_Ledger (fixed).md#lr07-19>): a method's named arguments bind by name, and a skipped optional parameter takes its default, as on a direct call.
- [LR03-15](<Lambda_Issue_Ledger (fixed).md#lr03-15>) and [LR03-16](<Lambda_Issue_Ledger (fixed).md#lr03-16>) no longer reproduced: LR13-12 and LR03-30, fixed earlier the same day, had their causes. They are now pinned by a fixture.

Found on the way and fixed with them, each covered by those fixtures:
- The JIT tested a `case \(d+):` arm with `==`, so an inline pattern arm never matched.
- `x is P` raised an error for a non-text value against a pattern, where the domain check makes it `false` (S11.1.2v2).
- `[1, 2] is type((3, 4))` crashed. `type(x)` wrapped a bare prefix that readers took for an array, map or function struct, and `fn_is` knew `list` and `array` only by their static wrappers.
- A direct call that named typed parameters out of order was rejected with E207.
- A named call that skipped a required parameter split the tiers; it is now E206.
- `name(map)`, `name(element)` and `name(object)` crashed, and so did `{a: int} <: map` and `<p> <: element`. Both read a shape past the end of the bare generic kinds. `object <: map` is now `false`, since `object` holds nominal values of every kind (S2.1.1v4), and `object`, `date` and `time` print and name as themselves rather than as `map` and `datetime` (`type_alias_name`).

Found and filed: [LR03-33](#lr03-33) (container patterns and function types still compare by kind), [LR03-34](<Lambda_Issue_Ledger (fixed).md#lr03-34>) (a pattern annotation is rejected statically), [LR03-35](#lr03-35) (an object literal drops an undeclared field), [LR07-39](<Lambda_Issue_Ledger (fixed).md#lr07-39>) (a default naming an earlier parameter fails on the JIT's direct call), [LR07-40](#lr07-40) (an unknown or repeated argument name is dropped silently) and [LR07-41](<Lambda_Issue_Ledger (fixed).md#lr07-41>) (the JIT crashes on a self-referencing union's value).

Verification: `make test-lambda-baseline` 5962/5962 (the forced-GC sweep included), the `kTune27TierParity` fixtures (three tiers), the named-argument negatives, every golden with `LAMBDA_EXEC_BACKEND=interp` (1005/1005) and `LAMBDA_EXEC_BACKEND=jit` (1004/1005). The one JIT miss, `latex/test_latex_m7`, timed out under the sweep's load and matches its golden alone. The validator gtest suites pass 154/154. `make test-radiant-baseline` had seven failures under the full parallel run: four page-load timeouts, the page suite's zengarden, one view UI fixture and the cascade-memory check. Each passes when rerun on its own.

### Hazard and recursion pass — 2026-09-26

A crash, two stale caps and three latent hazards were closed together, each with a regression that fails on the tree before the fix:
- [LR03-28](<Lambda_Issue_Ledger (fixed).md#lr03-28>) and [LR03-27](<Lambda_Issue_Ledger (fixed).md#lr03-27>): a `that` body is now an `fn` of its own, as TE-20 named, which both tiers call where the type is named. A body that names its own type recurses as any `fn` does, and an imported type's body runs in its declaring module on the JIT too (S11.4.11 status row).
- [LR08-6](<Lambda_Issue_Ledger (fixed).md#lr08-6>) and [LR11-4](<Lambda_Issue_Ledger (fixed).md#lr11-4>): the 64-field caps had gone with the shape pool and SCU10, but a shape draft that could not grow still truncated an edit silently, and the printer printed any map over 10,000 fields as `{[invalid map_type length]}`.
- [LR11-2](<Lambda_Issue_Ledger (fixed).md#lr11-2>): the render map's retransform skipped a dirty entry that an insert displaced behind its iterator.
- [LR11-3](<Lambda_Issue_Ledger (fixed).md#lr11-3>): MarkEditor chose which buffers to free or regrow from `ui_mode_` and from "not my arena, so malloc's"; it now asks the owner, and its child growth moves the owned scalar tail it had left behind.
- §15.1's masked memory-safety bug: the event-loop SIGSEGV band-aid was already gone; timer-callback stress fixtures now run in the forced-GC lane.

Verified on the tree merged with master: `make test-lambda-baseline` 5967/5967 (`Test262Prelim.RunnerContracts` needs the Test262 data, which a nested worktree reaches only when linked); every golden 1008/1008 with the tier pinned to `jit` and to `interp`. Before the merge, `make test-radiant-baseline` was green but for fixtures a nested worktree cannot reach through the tracked relative link `test/jquery-ui` (the four jQuery UI clicks, 31/31 assertions with the data linked) and two runs killed under load (the page suite and `RadiantViewTest.WindowScrollOnlyEmitsForPositionChanges`), each of which passes when rerun. Found on the way, not a defect of these: a `that` body cannot name a type declared after it, since only a type's own name is bound before its body and S10.1.7v2 reads an unbound bare name as `~.Name`, so two types cannot name each other.

### Site snippet pass — 2026-09-30

Running every Lambda snippet on the website (`site/`) against the current build found five defects: [LR01-18](<Lambda_Issue_Ledger (fixed).md#lr01-18>), [LR02-32](<Lambda_Issue_Ledger (fixed).md#lr02-32>), [LR02-33](<Lambda_Issue_Ledger (fixed).md#lr02-33>), [LR11-9](<Lambda_Issue_Ledger (fixed).md#lr11-9>) and [LR13-16](<Lambda_Issue_Ledger (fixed).md#lr13-16>). Two further findings are not defects. A constrained type used as a map field is enforced by its base only, by `is` and by `lambda validate` alike (`{age: 300} is {age: Age}` is `true`); S11.4.6 rules that interim ("enforce the base only, for now") and its status row records it. And `<body d *content(b)>` multiplies `d` by the array, because juxtaposed content continues through a binary `*`; `;` is the separator that makes the spread an item (`<body d; *content(b)>` works on both tiers), and the `error` child the product leaves is a value error flowing as data (S7.4.1).

### Std test triage — 2026-10-07

`test_lambda_gtest`, `test_lambda_extended_gtest` and `test_lambda_std_gtest` used to drop every `.ls` that had no golden, so 57 `test/std` scripts (added 2026-03, written against syntax that had since changed) and a few `test/lambda` scripts never ran. Discovery now fails a script without its golden unless it is named `_*`, `mod_*` or `schema_*` (`test/test_script_discovery.hpp`). Triage fixed and goldened 41 scripts, moved 12 compile-error scripts to `test/lambda/negative/` under `test_lambda_errors_gtest`, and parked nine as `_*.ls` (spec-correct output kept as `_*.expected.pending` where one exists): [LR02-32](<Lambda_Issue_Ledger (fixed).md#lr02-32>) (`_query_depth`), [LR02-34](<Lambda_Issue_Ledger (fixed).md#lr02-34>) (`_closures`), [LR02-35](<Lambda_Issue_Ledger (fixed).md#lr02-35>) (`_undefined_function`, `_undefined_variable`; reactivated as negative fixtures on 2026-11-03), [LR03-40](#lr03-40) (`_namespace_decl`, reactivated as `namespace_decl` on 2026-10-29), [LR03-41](#lr03-41) (`_type_declarations`, `_object_limits`), [LR10-19](<Lambda_Issue_Ledger (fixed).md#lr10-19>) (`_raise_in_pure_fn`), and the S11.4.6 base-only interim (`_object_constraint_fail`). [LR02-36](#lr02-36) was found along the way.

---


## 1. Compilation pipeline, CLI & REPL (LR01)

<a id="lr01-2"></a>**LR01-2 · `serve` is a stub · OPEN**
The subcommand exists but does nothing: `// TODO: Phase 5 — instantiate Server,
configure, and run` (`lambda/main.cpp:3818`).

<a id="lr01-6"></a>**LR01-6 · Fixed and non-reentrant static buffers · OPEN**
Module BSS name `char buf[256]` (`runner.cpp:565`); REPL synthetic path `char
script_path[64]` (`main.cpp:903`); the JS CLI thread stack is a 256 MB
`JS_CLI_STACK_SIZE` allocated per run (`main.cpp:264`, applied `:316`); and
non-reentrant `static char mir_error_msg[256]` (`main.cpp:1560`).

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


---


<a id="lr01-21"></a>**LR01-21 · Disabling MIR reuse can recurse on a retained AST hit · OPEN (found 2026-11-03)**

Reproduced on the error-handling branch over `f7ce32b36`: running the complete
`test_io_terminal_gtest.exe` with `LAMBDA_DISABLE_MIR_CACHE=1` exhausts the
native stack in `load_script`; normal caching passes all ten cases. An earlier
interpreter import leaves an AST artifact. A later forced-JIT import skips
AST lookup (`lambda/runtime/runner.cpp:2224`) but selects an AST build claim
because MIR reuse is disabled (`:2266`). The READY claim retries the same
lookup (`:2291`) indefinitely. These selection conditions are unchanged from
the pre-rework `0452aaabc` source. The claim and hit paths must agree on the
artifact kind without changing the requested execution tier (D8.3.2,
D8.5.1). This is separate from error-union admission. Bounded LLDB trace and
source comparison: `temp/error-handling/m8-terminal-no-cache-stack.log` and
`m8-cache-retry-root-review.json`; qualification record:
[type/error implementation](<impl/Lambda_Impl_Type_Error_Handling (done).md>).

<a id="lr01-22"></a>**LR01-22 · Experimental Lambda MIR MVP needs the new error rules · OPEN (2026-11-03)**

The production frontend enforces S7.7.2v2/S7.7.4v2 retained binding outcomes
and S11.4.6v4 normal admission through error unions. Experimental MVP fixtures
still use clean returns after fallible admissions/assignments and unchecked
loop conversions. Six incompatible fixtures were found against `88d30f849`;
the latest MVP work at `1b0927e30` brings the current count to eleven.
`admitted_call` explicitly rejects propagation, and unary admission excludes
`OPERATOR_PROPAGATE` (`lambda/runtime/mvp/mvp_plan.cpp`). Adapt fixtures and
prototype together; do not restore declaration exits or erase error outcomes.
Normal admission and post-binding recovery also need prototype qualification.
**USER, 2026-11-03: leave MVP adaptation to the ongoing main-checkout work.**
This merge preserves that implementation without claiming MVP qualification.
Evidence: `temp/error-handling-merge-20261103T054805Z/latest-mvp-frontend.json`
and `final-source-audit.json`.
[Production qualification](<impl/Lambda_Impl_Type_Error_Handling (done).md#production-integration-and-user-acceptance-2026-11-03>).

<a id="lr01-23"></a>**LR01-23 · AUTO fallback cache test expects a MIR import image · OPEN (2026-11-10)**

`LambdaScriptCache.AutoFallbackCachesMirImportConeAndViews` computes the expected
42 but fails its `cache_mir_artifact` and MIR-hit assertions: the dependency has
an AST image and the second run adds no MIR cache hit. This reproduces both on
the helper-retirement candidate and with all six changed runtime implementation
objects restored from `fd07a893f`, under identical current headers, sibling
archives and test code. It predates these helper deletions. Whether the defect
is a stale tier-specific expectation or missing native-image reuse is not yet
established; preserve the assertions pending that investigation (D8.3.1,
D8.3.5, D8.5.1). It does not block the dead-helper audit.

Reproduce from the Lambda checkout with
`DYLD_LIBRARY_PATH="$PWD/modules/node-core" ./temp/mvp_helper_retirement/test-runtime.exe --gtest_filter=LambdaScriptCache.AutoFallbackCachesMirImportConeAndViews`.
The private-path loader override is only needed for this temporary executable.
Evidence: `temp/mvp_helper_retirement/cache-current.log`,
`control/cache-test.log`, `control/summary.json`; the causal control retains the
pre-cleanup helpers, and neither test oracle nor cache policy was changed.


<a id="lr01-24"></a>**LR01-24 · Forced-GC batch timeout can crash during cleanup · OPEN (2026-11-11)**

On `21fdc7cf6` plus the document-protocol changes and logical-join fix,
the interpreter's `sync_laws.ls` reaches the standard batch harness's 60-second
limit under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The process
prints `BATCH_END 124`, then the shell reports segmentation fault 11 before the
next fixture. Normal interpreter/AUTO/JIT runs complete successfully. No CRDT
assertion fails before the stress timeout.

Reproduction: run `test/test_lambda_gtest.exe` with
`LAMBDA_EXEC_BACKEND=interp`, both GC variables above, and
`--gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/sync_*'`.
Evidence: `temp/sync-protocol/interp-forced-gc.log`, especially the timeout and
subsequent teardown crash. `lambda/main.cpp:4920` uses `sigsetjmp`/alarm around
script evaluation and continues into `runtime_reset_heap` at `:4984`. A signal
jump bypassing precise-root/owner cleanup is a candidate cause, not confirmed
(D6.3.3, D6.4.3). Do not restore conservative stack scanning or mask the crash
in the harness. This needs a separate cancellation/cleanup investigation.

The CRDT qualification reruns the unchanged stress fixtures with the existing
`test-batch --timeout=300` option and checks process exit, complete markers and
goldens. That distinguishes the stress budget from protocol correctness; it
does not fix or close this timeout-path issue. Progress:
[CRDT protocol implementation](<impl/Lambda_Impl_Sync_Protocol (done).md>).

2026-11-12 document integration follow-up: the unchanged normal baseline passes
7,507/7,507, but every-allocation GC makes `sync_document_admission.ls` exceed
the ordinary 60-second batch budget. The following four fixtures then fail with
`nested open transactions are not supported`. A smaller normal-GC reproduction
opens SQLite, loops until a one-second batch alarm, then runs a new SQLite-open
fixture: statuses are **124, 1**; the control fixture alone returns **0**.
This confirms that the interrupted SQLite scope remains observable after this
abandon path. It does not establish the cause of the earlier segmentation fault
or a complete safe cancellation fix. Native `test-batch` still jumps over normal
scope exits; `io_sqlite.cpp::current_scope` reads task attachments, and
`write_set.cpp::fn_open_begin` rejects the surviving scope.

Evidence: `temp/sync-documents/initial-60s-final-stress-auto.log`,
`_timeout_cpu_scope.ls`, `_scope_control.ls`, `timeout-sqlite.{stdout,stderr}`,
`scope-control.stdout`. No harness, ordinary deadline or native runtime code was
changed. The document qualification uses the existing 300-second stress budget
above, with unchanged fixture goldens, complete batch markers and process-exit
checks in `test/sync_documents/test_stress.py`; the ordinary 60-second suite
remains a separate gate. LR01-24 stays **OPEN**. S12.4.4v4 and D6.4.3 apply.

The admission workflow also exceeds the existing 300-second forced-GC budget;
`large-300s-admission.{stdout,stderr}` preserves its 124 marker and cleanup
diagnostics. The brief `admission-gc.sample.txt` shows collection/mark-stack work,
not evidence that the protocol failed to converge. The exact cost/retention cause
is unconfirmed. The full workflow stress profile is **unqualified**, available
as `test_stress.py --full`; focused persistence/export GC cases and the unchanged
full normal workflow gates are reported separately. No failure is reclassified
as a passing stress result.

## 2. Parsing & AST construction (LR02)

<a id="lr02-1"></a>**LR02-1 · Relational result type is representation-sensitive · PARTIAL**
`< <= > >=` still yields `TYPE_BOOL` or `set_type_any(tp, ANY_COMPARE)`
depending on operand openness (`build_ast.cpp:7270`–`7274`), and must stay in
lockstep with the transpiler's vectorized-comparison codegen ([LR07](#7-mir-direct-transpiler--jit-lr_07)).
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


<a id="lr02-27"></a>**LR02-27 · Function-type parameters without `: T`: C admits them untyped, the grammar rejects them (S11.1.5v2, S16.10.1v2) · OPEN, needs a ruling (found 2026-09-24)**
The two front ends read a function-type parameter differently.
- **C** (`parse_fn_type`, `parse_type_pattern.cpp:749`) takes any word as the parameter name, and `: T` is optional. So `type F = fn (x) int` declares an untyped parameter. `fn (int) int` declares one untyped parameter *named* `int`, although S16.10.1v2 bars that word as a name everywhere else (`fn g(int) { 1 }` is E201). The contract then checks nothing. With `let f: fn (int) int = (x) => x`, `f("s")` returns `"s"`, where `fn (x: int) int` reports E207 at the call.
- **The reference grammar** requires `name: T` in every slot (`fn_param`, `grammar.js:1332`), so it rejects `fn (int) int`, `pn (int) int` and `fn (x) int`.

The positional form is documented and used:
- `doc/Lambda_Type.md` describes `fn (int) int` as "Takes int, returns int", in its function-type table (:404–:412) and in `apply_fn` (:455).
- `doc/Doc_Convention.md` uses it in its `type`-marker example (:214, :232).
- The S11.1.5v2 fixture `test/lambda/proc/fn_pn_function_types.ls` spells it on five lines (`PureUnary`, `ProcUnary`, `apply_fn`, `apply_pn`, and the `is` checks).

S11.1.5v2 shows only named parameters, and Design_Syntax §7.29 leaves the form "Not decided here". Under D8.1.1, no ruling yet says which side is wrong.

**Needs a ruling:** may a signature spell a parameter by its type alone?
- **If it may,** C must read `int` as a type, and the grammar must accept the form.
- **If it may not,** C must reject it, and the docs and the fixture must migrate to `fn (x: int) int`.
- **Either way,** rule whether an untyped name (`fn (x) int`) is allowed.


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



---






<a id="lr02-36"></a>**LR02-36 · A quoted-path `import` without an alias is silently ignored · OPEN (found 2026-10-07, std test triage)**
`import './circular_import.ls'` at the top of a script neither imports nor errors: the quoted-module branch at `lambda/runtime/build_ast.cpp:13179` only acts when `node->alias` is set, so the script runs as if the line were absent (it printed `42`). S16.9.8 gives the relative form `import .circular_import`, which does detect the cycle (E217, now asserted by `NegativeScriptTest.SelfImportIsCircularE217`). The unaliased quoted form should be rejected or resolved, not dropped.

<a id="lr02-37"></a>**LR02-37 · The C expression parser gave `|`, `&`, `!` and `to` one binding power · OPEN (S10.1.1v5 implemented 2026-11-03; qualification pending)**
`infix_bp` previously mapped all four operators to `LAMBDA_BP_SET`, left-associative (`lambda/runtime/parser/lambda_parser.c`). That made `type T = "a" | "b" & "c"` admit `"a"`, while the same expression in `let x = ...` denoted `none`; `let r = 5 | 1 to 3` also failed at run time. **S10.1.1v5** adopts the reference grammar and type-pattern order, `to` > `&` > `!` > `|`, left-associative. The C parser now has separate binding powers, so both contexts group the first example as `"a" | ("b" & "c")`, and the range expression as `5 | (1 to 3)`. The previous reproducers have not yet been rerun.

## 3. Value & type model (LR03)

<a id="lr03-40"></a>**LR03-40 · Qualified-name integration · PARTIAL (core fixed 2026-10-29; Mark namespace encoding pending)**

**S8.2.2v6:** all four original discrepancies below are repaired. Source symbols
store local characters and resolved namespace identity; maps, source attributes,
parsed XML, Input copies, iteration and editor rebuilds preserve qualified and
global keys independently. XML formatting retains resolved identity and native
DOM/SVG consumers handle the new representation. `namespace_decl.ls` is active
again in the standard suite.

**Remaining:** Mark has no ruled namespace-URI encoding. Formatting a resolved
XML name as Mark and reparsing it loses namespace identity; the preserved
reproducer is `test/lambda/_namespace_mark_pending.ls`. Choosing new syntax or
reflection is outside this fix pass and needs a ruling (namespace design Q2).
The record stays PARTIAL for this format boundary. Implementation and focused/
aggregate qualification: [names, presence and run-configuration follow-up](<impl/Lambda_Impl_Ledger_Names_Presence_Followup (done).md>).

*Original report (2026-10-06; the four discrepancies below are now fixed):*

S8.2.2v4 makes a key's identity its resolved namespace plus its normalized characters, so `ns.a` and the global `a` are different keys and two prefixes that resolve to one namespace are one key. Four places disagree, and the user ruled on 2026-10-06 that each must follow the namespace design:
1. **The map face compares local characters only.** `fn_map_set` takes a `Symbol` key's `chars`/`len`, `map_get` goes through `key.get_chars()` into `_map_get` by bytes, and `shape_field_name_equals` and the hash lookups carry no namespace, so a qualified symbol written to a map lands on, or reads, the unqualified field of the same spelling. Element attribute reads (`target_equal(field->ns, symbol_lambda_namespace(key))`) and map/element equality (`map_find_matching_field`) already compare the namespace.
2. **An element literal desugars `ns.attr: v` into a nested map.** `<a xlink.href: "x", href: "y">` becomes `<a xlink: {href: "x"}, href: "y">` through `build_ns_attr_map_from_parts` (build_ast.cpp, the "legacy desugaring" at the element-attribute reduction). Ruled wrong: it was a workaround from before namespaces were defined; `xlink.href` is one qualified attribute key, keyed by the resolved namespace of `xlink` and the local name `href`.
3. **Parsed XML resolves nothing.** `parse("<a xmlns:xlink=… xlink:href=…/>", 'xml')` yields the attributes `xmlns:xlink` and `xlink:href` as flat names with the colon kept and `ShapeEntry::ns` NULL; no parser sets a namespace target. Ruled wrong for the same reason: a prefix declared by `xmlns:` must resolve to its namespace target and the attribute be keyed by (target, local name); the same holds for prefixed tags and `TypeElmt::ns`.
4. **A qualified symbol's characters include its prefix.** `namespace_symbol_type` (build_ast.cpp) spells the symbol `prefix.field` and attaches the resolved `Target*`; under S8.2.2v4 the normalized characters are the local name and the prefix is only how the namespace was written, so two prefixes bound to one namespace must compare equal.


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

<a id="lr03-13"></a>**LR03-13 · A function contract's return type is trusted, never checked · IMPLEMENTED 2026-09-27, waiting on a ruling (found 2026-09-24)**
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
*Implemented 2026-09-27, both tiers, placement unruled:* a call whose callee is a binding or parameter declared with a function-type contract checks its result against that contract's return where the call is made (`ast_call_contract_return`, `ast.hpp`; T0 `interp_check_contract_return`, the JIT before its unbox). The failure is the call's value (E201 "function return"), as S7.7.3's call-site contagion is. A direct callee's own declared return governs instead, and a contract with a binder or an open return is not checked. The JIT reads such a call as a boxed join (TE-17 I3). The alternative placement, at admission, is not built. Fixture `test/lambda/contract_return_check.ls`.

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

<a id="lr03-33"></a>**LR03-33 · Container patterns and function types compare by kind under `==` (S5.5.2, S2.1.1v4) · OPEN (found 2026-09-26, while fixing LR03-29)**
`lambda_type_repr_equal` compares a map, element, bracket-pattern or function type by kind alone. So `{a: int} == {b: string}`, `<p> == <div>`, `[int] == [string]` and `fn (x: int) int == fn (x: string) int` are all `true`. The cause is `type(x)`: S2.1.1v4 has it name the kind, but for an element it returns the element's own tagged shape, kept for `name(type(e))` (074321c54a), and for a named map alias its TypeMap. Nothing tells that shape from a declared pattern of the same kind, and structural comparison would make `type(<p>) == element` `false`, which about 70 library and fixture lines rely on (`type(c) == element`). The same split shows under `is`: `<div> is type(<p>)` is `false`, because the shape is tested as the pattern `<p>`, while `type(<p>) == element` is `true`. A fix has `type(x)` of a structural element or map return the kind, keeps the tag elsewhere for `name()`, and then compares patterns structurally. Function types also wait on S11.1.4v2's pending variance. Related, and unruled: `type()` names `datetime` for a date value, so `type(t'2025-01-01') == date` is `false`; S2.1.1v4 does not say whether `type()` names the `date`/`time` sub-kind, as it names `list` and `range`.

<a id="lr03-35"></a>**LR03-35 · An object literal drops a field its type does not declare (S2.1.4) · OPEN (found 2026-09-26, while fixing LR03-19)**
With `type P { a: int, b: string }`, `<P a: 1, b: "x", extra: 5>` prints `<P a: 1, b: "x">` and `.extra` is `null`, on both tiers and with no diagnostic. S2.1.4(3) makes an instance open: it may hold fields the type does not declare. A member addition does keep one (`p.z = 9`, fixture `proc/object_open_instance.ls`). No ruling says whether the literal keeps the field, as a shape transition, or rejects it; dropping it silently fits neither.


---


<a id="lr03-41"></a>**LR03-41 · A derived type that redeclares an inherited field fails with E209, reported at 1:1 · OPEN, needs a ruling (found 2026-10-07, std test triage)**
`type Animal { name: string, sound: string = "..." }` then `type Dog : Animal { sound: string = "woof" }` is rejected with `error[E209]: duplicate definition of 'sound' in the same scope`, located at line 1 column 1 rather than at the redeclaration. S2.1.3v2 (and OB7) say only that attributes "merge", so whether a derived type may override an inherited field's default needs a ruling; the location is wrong either way. Parked tests: `test/std/core/statements/_type_declarations.ls`, `test/std/boundary/_object_limits.ls` (their pending goldens assume the override is allowed).



## 4. Numbers, decimal & datetime (LR04)


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

## 5. Strings, symbols & vectors (LR05)


<a id="lr05-6"></a>**LR05-6 · Fixed-size buffer truncation in stencils · OPEN**
`STENCIL_MEDIAN_CAP 4096` (`lambda-vector.cpp:4045`) rejects larger median
kernels (`:4070`) and backs a 4096-element stack `medbuf` (`:4086`); `fn_otsu`
hard-codes 256 bins in a stack `int64_t h[256]` (`:4501`).

<a id="lr05-7"></a>**LR05-7 · utf8proc allocation crosses the allocator boundary · OPEN**
Normalizers return raw utf8proc-allocated buffers that callers must `raw_free`
(`utf_string.cpp:64`–`65`, `:90`); the `RAWALLOC_OK` annotations acknowledge
this sits outside the pool/GC discipline.


<a id="lr05-17"></a>**LR05-17 · String-pattern design questions with no formal open-issue entry · OPEN (logged 2026-10-23, string doc consolidation)**
These pattern questions are open in the vibe records but have no `SO#` entry in `doc/Lambda_Formal_Semantics.md` Appendix B (SO32 captures and SO47 the empty needle already have one):
- **SPO7, value half resolved 2026-11-03 (S10.1.1v5):** value expressions now use `to` > `&` > `!` > `|`; implementation qualification remains [LR02-37](#lr02-37). The island half is SP19.
- **SPO10:** what `.`, `...` and `"\s"` match across line terminators. Today `.` stops at a newline and `...` does not; the recommendation is a dotall `.` and the ECMAScript `\s` set.
- **SPO12, spelling half:** how to write a case-insensitive `is` or `match`. The folding rule is S17.7.1. SP16's `\tag( … )` family, which the recommendation leaned on, was superseded by SP24.
- **SPO1:** multi-letter and Unicode class names (`\p{Letter}`), deferred on 2026-08-07.
- **Rev 1 questions never ruled (2026-01-27):** whether recursive patterns are allowed, and how detailed a pattern-mismatch diagnostic should be.

Full text and recommendations: [Lambda_Design_String_Pattern.md §9](Lambda_Design_String_Pattern.md#9-open-issues-spo1spo14).

---


## 6. C transpiler — legacy C2MIR (LR06)

**All nine issues are archived in the [fixed issue ledger](<Lambda_Issue_Ledger (fixed).md#lr06-r1r9>).** The backend no longer exists in the tree.

## 7. MIR Direct transpiler & JIT (LR07)

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

<a id="lr07-11"></a>**LR07-11 · Fixed-size structural caps · PARTIAL (long names fixed 2026-10-31)**
**S1.6:** source names, closure captures, imports and generated MIR/BSS/prototype
identities retain their complete spelling. Names sharing more than 128 bytes
remain distinct on interp, auto and jit; `ledger_long_names.ls` and its imported
module cover the former truncation paths.

**Residue:** `var_scopes[64]` and `loop_stack[32]` remain bounded; this pass does
not lift their independent structural caps. Implementation and qualification:
[containment follow-up](<impl/Lambda_Impl_Ledger_Containment_Followup (done).md>).

<a id="lr07-13"></a>**LR07-13 · Interpreter TCO iteration ceiling · OPEN**

The interpreter retains `LAMBDA_INTERP_TCO_MAX_ITERATIONS`; a correctly
transformed tail loop consumes no native stack. Review the remaining ceiling
against the execution/interrupt policy before changing it (D8.3.1–D8.3.2).
The old Full MIR iteration guard disappeared with that emitter. Its unused
`is_tco_function_safe` proof and two private walkers are removed in the
2026-11-10 helper audit; neither T0 nor MVP called them. T0's live recursive-call
identity and tail-position analysis remain. This retirement does not lift the
interpreter ceiling. [Audit and evidence](impl/Lambda_Impl_MIR_Full_Retirement.md#runtime-helper-audit-2026-11-10).


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

*Fixed 2026-09-25:* the `max` case had been fixed by a20e59b4f. The rest were registry rows that return an ordinary error but declared `may_error=false`, so a call typed as its success shape and the JIT unboxed the error: `slice` (both arities), `take`, `drop`, `replace` (both), `url_resolve`, `symbol/2`, `chr`, `real`, `imag`, `ndim` and `sort/2` (a string became `"<error>"`, a symbol null, a float `nan`, an int `0`). They now declare it (S11.4.9, D7.1.3), and `sys_func_call_may_return_error` keeps provably clean calls clean: constant integer offsets on a text, materialized or range source; all-text `replace`; a complex `real`/`imag`; an error-free `chr` or `ndim` operand. `format` returned `String*`; it now returns an Item (D7.6.1), so an error operand stays an error instead of the text `"<error>"`, and a bad format is an error on both tiers. The string builders keep their in-place appends: a `string | error` part of an owned chain or a flattened leaf takes a cold error edge or the all-string guard (hyphen2's append profile is unchanged at 11,092 calls). Corpus: three typed benchmark bindings (`hyphen_typed.ls`, `json2.ls`) and `openapi_json` rescue with `or ""`, and `Lambda_Func.md`'s variadic `printf` example, which never worked (`format` has no printf mode), was replaced. Fixture `test/lambda/proc/sysfunc_text_error_lane.ls`; `slice_float_indices.ls` is now pinned on every tier (it was wrong on the JIT, hidden by `auto`).
*Requalified 2026-11-03:* **the inferred-string reassignment residue is fixed**
by S7.7.4v2's continued binding outcomes. `var line = "a"; line = line ++
slice(s, 1.5, 3)` retains the error and executes later statements on interp,
auto and jit, including forced poisoned GC and low promotion thresholds.
`proc/inferred_string_error_outcome.ls` pins explicit recovery, later appends,
snapshots and capture transport. No new runtime change is needed for this
reported case. [Qualification](<impl/Lambda_Impl_Ledger_Tier_Fixes (done).md>).

*Remaining effect-policy questions (not reproduced tier mismatches):*
- **Unproven `int` offsets.** `fn f(s: string, i: int) => slice(s, i, i + 1)` is now E208, because `int` admits `nan` and `inf` and a non-finite offset errors. Whether such an offset should clamp or read as absent (S7.10.2 admission) is unruled; a ruling would let an `int`-typed offset prove the call clean.
- **`format`.** A function returning `format(x, 'json')` is E208 too: formatting a value that holds a complex number fails.




<a id="lr07-29"></a>**LR07-29 · Negative fixtures report different diagnostics per tier · OPEN (found 2026-09-25)**
Of the 159 negative fixtures, 11 fail differently on the two tiers. All of them fail on both tiers except `stack_overflow.ls` ([LR10-11](<Lambda_Issue_Ledger (fixed).md#lr10-11>)).
- **Different boundary names for the same E201.** "argument 1 of _takes3_317" (T0) against "typed array call argument" (JIT) in `array_count_resized_binding`; "typed array element assignment" against "typed array representation fallback" in `nullable_array_reject_null` and `type_enforcement_array_write`; "argument 1 of" against "parameter 'value' of" in `type_enforce_static_float_to_int_parameter`; a declaration-level check (`validator at .cells`) against a field-level one in `typed_literal_required_array_null` and `typed_literal_required_dynamic_null`.
- **Calling a non-function.** T0 prints only "Error: Script execution failed"; the JIT prints `error[E212]: fn_call2: cannot call non-function value` (`test_call_nonfunc`, `test_deep_call_stack`, `sysfunc_shadow_not_callable`).
- **An imported module's parse error is printed twice by T0** (`import_parse_error_driver`).

Both tiers also leak internal names: `_takes3_317`, `fn_call2`, "representation fallback". The negative gtests check only an exit status and a substring, and no harness compares these fixtures' `.txt` files, which have drifted (`Error[E201]` against `error[E201]`).



<a id="lr07-40"></a>**LR07-40 · Unknown argument names are dropped; duplicate slots are diagnosed (S12.3.2) · PARTIAL (found 2026-09-26, while fixing LR07-19)**
Original reproducer: with `fn f(a, b = 2) => [a, b]`, both `f(1, zz: 5)` and `f(5, a: 1)` silently produced `[1, 2]`; argument normalization either skipped an unknown name or overwrote a filled slot. **Fixed 2026-10-28:** duplicate positional/named and named/named slots report E206 before normalization, including method calls. Positional arguments after named ones still fill the next empty slot, as documented. **Residue:** unknown argument names are still ignored; their policy remains outside this implementation pass. See [ruled ledger fixes](<impl/Lambda_Impl_Ledger_Ruled_Fixes (done).md>).

<a id="lr07-42"></a>**LR07-42 · An async procedure's state machine grows quadratically with its calls: ~300 ms to compile `beng/knucleotide` (D8.3.1) · OPEN (found 2026-10-03, by the AUTO benchmark sweep)**
`test/benchmark/beng/knucleotide.ls` (rewritten in `c839925ca`) runs a 2–3 ms workload, but AUTO takes ~318 ms end to end and eager JIT ~236 ms (quiet machine, release). The v50 binary shows the same cost on the new script, so this is a codegen cost, not a recent regression. `main` reads its input with `io.read(INPUT_PATH)^`. That makes it an async procedure (the concurrency planner logs "pn main suspends because it calls io_read"), and an async procedure must run as a resumable MIR state machine. T0 therefore compiles it synchronously at first entry, together with its 8 direct callees (`interp_make_closure` → `compile_ast_function_satellite`). Under `LAMBDA_SATELLITE_CLUSTER_SKIP`, the image costs 315 ms with every member and 23 ms with `main` alone. Most of the cost follows `print_count`, a one-line callee: skipping it leaves 82 ms. The size is in `main` itself: **54,254 MIR lines**, of 59,238 for the whole module.

Two causes multiply:

1. **A resume state at every call that can raise.** `mir_call_may_suspend` is correct here: every local callee has `may_await = 0`. But each call to a raising `pn` has an error exit, and in an async procedure every error exit unwinds the task scope through `transpile_task_scope_unwind_to`. `lambda_task_scope_unwind` may suspend, so each such exit allocates its own park/resume state. `main` gets 12 states: 2 for `io.read` and 10 for scope unwinds after `extract_three`, `count_small_kmers`, two `print_frequency_table` calls and five `print_count` calls. `main` starts no task, so under S13.3.1 (a handle is owned by the nearest lexical block) its scope can hold no handle to join at any of those exits.
2. **Each state saves every register created so far, live or not.** `new_reg` calls `async_track_reg`, which adds every register an async procedure creates to `mt->async_spills`, and the list never shrinks. Each state's `async_save_spills` / `async_restore_spills` covers the whole prefix, as one `lambda_async_frame_set_word`/`get_word` call per register, each with its own root-slot reloads around it. The words saved per state grow 14, 28, 41, 58, 71, 109, 124, 139, 154, 169, 187, and the MIR between consecutive states grows from ~240 to ~12,000 lines, so size is quadratic in the number of states. `main` has 1,104 `set_word` and 1,104 `get_word` calls.

Confirmation: replacing `io.read(INPUT_PATH)^` with the synchronous `input(INPUT_PATH, "text")^` leaves `main` non-async. The module drops from 59,238 to 5,502 MIR lines with no async states, and eager JIT wall falls from 891 to 121 ms (minimum of 3 at load ~59; read the ratio, not the absolute times). The effect is not specific to this benchmark: any `pn` that awaits once and then makes many calls pays it.

Possible fixes, independent and combinable:

- **(A) Save only what is live across each state.** Restrict `async_save_spills` / `async_restore_spills` to registers live at the suspension point, from a liveness pass over the async function or from the tracked set pruned at scope exits. Cost becomes linear in the number of states, with no semantic change. This is the larger codegen change.
- **(B) Emit no suspendable unwind where the scope can own no handle.** Under S13.3.1 a block owns only the handles started in it, so a procedure (or block) with no `start` in its body can skip the park/resume state at its error exits and unwind synchronously. This removes 10 of `main`'s 12 states here. It depends on the task-scope rules: confirm that nothing else, such as `io.read`'s own task, can be registered in the caller's scope after its await completes. It needs a ruling (D8.3.1 / S13.3).

Repro: `LAMBDA_EXEC_BACKEND=jit ./lambda.exe run test/benchmark/beng/knucleotide.ls` on a debug build, then `wc -l temp/mir_dump.txt`, and count `lambda_async_frame_set_state` in `_main`. Analysis record: [`impl/Lambda_Impl_Interp_Tune2.md`](impl/Lambda_Impl_Interp_Tune2.md) §12.6.



<a id="lr07-53"></a>**LR07-53 · Full MIR loses a nonnumeric recursive array replacement · OPEN**

Reproduced 2026-11-09 at `14f48f209` plus the MVP parity changes, using
the frozen release `temp/mvp_untyped_tuning_20261109/starting-release.exe`
(SHA256 `e45cec36fef54f4ebb1d2833fbde9d99dbc79f4c8b6be926630025f5cfb5ad70`).
`LAMBDA_EXEC_BACKEND=jit LAMBDA_MIR_COMPILER=production <exe> run
test/lambda/mvp/mir_mvp_untyped_snapshots.ls --no-log` prints `[0, [99]]`
for the recursive string replacement; **S9.1.2 / S12.2.1v3 / D4.3.1** require
`["changed", [99]]`. MVP returns the required result. Full fails identically
under normal, forced and seeded GC; the input array snapshot stays unchanged.
`mir_param_scalar_snapshot` / `mir_confined_scalar_snapshot` are relevant
source anchors; the exact Full codegen cause is not yet confirmed. This does
not block MVP tuning or its benchmark comparisons. The permanent golden is
unchanged to keep the Full failure visible. Evidence:
`temp/mvp_untyped_tuning_20261109/p6-focused-units.log`; implementation context:
[MVP tuning checkpoint](impl/Lambda_Impl_MIR_MVP_Language_Parity_and_Untyped.md#1091-untyped-release-tuning-2026-11-09).

Additional reproduction, 2026-11-09 at `84f18b39b`: an inferred local
`var values = fill(2, 0)` passed to `pn replace_owner(var values) {
values = ["changed"] }` remains `[0, 0]` in Full MIR; MVP publishes
`["changed"]` as required by **S9.1.3v2 / S12.2.1v3 / D4.3.1**. This case is
nonrecursive, so the defect is broader than the original recursive example;
whether the internal cause is identical remains unconfirmed. The permanent
fixture/golden is `test/lambda/mvp/mir_mvp_inferred_primitive_loops.ls`.
Frozen P26 release SHA256
`39edeb116459fadca1ed71b97461c96a86ef4714a8ea331bdb4227d8d0d19ef7`
reproduces the difference before this continuation's changes, and P29 Full
has the same result. Evidence: `temp/mvp_untyped_tuning_p27/` files
`mir_mvp_inferred_primitive_loops-p26-production.log` and the corresponding
MVP/P29 logs. The expected result remains unchanged. This does not block MVP
tuning or benchmark comparison.

<a id="lr07-54"></a>**LR07-54 · Full MIR accepts a projected string as a writable map · OPEN**

Reproduced 2026-11-09 at `84f18b39b`, on both frozen P26 Full (SHA256
`39edeb116459fadca1ed71b97461c96a86ef4714a8ea331bdb4227d8d0d19ef7`)
and current P81 Full. Run `LAMBDA_EXEC_BACKEND=jit
LAMBDA_MIR_COMPILER=production <exe> run
test/lambda/mvp/mir_mvp_inferred_record_paths.ls --no-log`. For
`{children: ["0123456789abcdefg"]}`, `owner.children[0].value = 3`
returns a normal result, so the fifth printed line is `false` instead of
the golden's `true`. Invalid writes must return Error (**S7.1.3v3 /
D4.3.1**). P81 Full fails under normal, forced and seeded GC; MVP passes
all three modes. A lost Item tag in the inferred projection path is a
suspected cause, not yet confirmed in Full. Evidence:
`temp/mvp_untyped_tuning_goal/p81-fixture-full-control.json` and
`p81-corrected-fixture-units-2.log`. This does not block MVP tuning. The
MVP fixture and its Error oracle are retained unchanged; Full correctness
is not claimed for this case.

Additional reproduction, 2026-11-10 on frozen P26 and P86 Full:
`test/lambda/mvp/mir_mvp_nested_primitive_stores.ls` preserves the rows on
negative/NaN/fractional keys and a non-container child, but reports normal
results instead of Errors on its last two lines. MVP passes the unchanged
golden under normal/forced/seeded GC. Evidence:
`temp/mvp_untyped_tuning_goal/p86-nested-full-control.log` and
`p86-focused-units-2.log`. Whether the rejected indexed-store outcome has
the same cause as the original projected-map defect is unconfirmed
(**S7.1.3v3 / S7.7.3**).

<a id="lr07-55"></a>**LR07-55 · Full MIR replaces the outer borrowed owner on a different-owner recursive edge · OPEN**

The same P26/P81 Full controls reproduce this with
`test/lambda/mvp/mir_mvp_borrowed_tail.ls`: `child_tail(tree, 2)` returns 17,
but the unchanged `tree.child.child.value` then reads null. Full prints
`[17, null]`; the required golden is `[17, 17]` (**D5.4.6 / S9.1.3v2 /
S12.2.1v3**). MVP retains the outer tree under normal, forced and seeded GC;
Full fails all three. The recursive call passes a local `child` var rather
than the enclosing var parameter. Incorrect borrow-home forwarding or
publication is suspected; an identical cause to LR07-53 is unconfirmed.
Evidence and frozen P26 SHA are as in LR07-54. This does not block MVP
tuning. The immutable golden remains the MVP acceptance oracle, and Full's
observed failure remains recorded separately.

<a id="lr07-56"></a>**LR07-56 · Full MIR plain argument observes a later argument's write · OPEN**

Reproduced 2026-11-10 on frozen P26 Full (SHA256
`39edeb116459fadca1ed71b97461c96a86ef4714a8ea331bdb4227d8d0d19ef7`)
and current P92 Full. Run `LAMBDA_EXEC_BACKEND=jit
LAMBDA_MIR_COMPILER=production <exe> run
test/lambda/mvp/mir_mvp_projection_call_snapshots.ls --no-log`. The last
call, `earlier(owner, change(owner))`, must keep the first plain argument's
40-valued snapshot while the second argument writes 60 to the caller's
owner (**S9.1.2 / S9.1.3v3**). Full prints `[60, 60]`; the unchanged golden
requires `[40, 60]`. The first four projection/whole-root/local-write/alias
cases pass both compilers. P92 MVP passes all five under normal, forced and
seeded GC. The Full call-site capture/evaluation-order interaction is a
suspected cause, not yet confirmed. Evidence:
`temp/mvp_untyped_tuning_goal/p92-projection-full-control.log` and
`p92-focused-units-2.log`. This does not block MVP tuning. The permanent
MVP oracle is retained; no Full correctness pass is claimed.

<a id="lr07-57"></a>**LR07-57 · MVP MIR-shape assertions disagree with current emission · OPEN (2026-11-10)**

`test_implicit_tail_and_split_observations` expects `lambda_map_set_checked` in
`updated_box`; `test_guarded_recursive_arrays` expects generic `fn_index` and
checked-array-store calls in the guarded `place_queen` fallback. The selected
calls do not appear in current MIR. Both assertions fail identically on the
saved pre-cleanup executable (SHA256
`21884b40fcb5392a1747838d91e27628064afb1599a9c1a998f3d95308ff903b`) and the
helper-retirement release. The expected helpers remain implemented/registered;
these are not unresolved deleted imports. Whether each oracle should follow
newer lowering or exposes an optimization defect remains unconfirmed
(D4.3.1, D7.3.1–D7.3.3). Do not weaken the assertions without tracing the
relevant guarded path and checking its semantic fallback.

Reproduce with `LAMBDA_TEST_EXE=<candidate> python3 -m unittest test_lambda_mir_mvp.LambdaMirMvpTests.test_implicit_tail_and_split_observations test_lambda_mir_mvp.LambdaMirMvpTests.test_guarded_recursive_arrays`
from `test/benchmark`. Evidence: `temp/mvp_helper_retirement/tco-final-units.log`
and `tco-oracle-control-2.log`; the saved pre-cleanup executable is
`previous-root.exe` in that directory. This does not block dead-helper removal.

The optimization-coverage audit at `cf0caf5c7` runs all 56 Python methods that
inspect MVP MIR against the installed debug CLI (identity below, LR07-60).
**47 pass and nine fail.** Besides the two original methods, failures are
`test_immutable_indirect_keys`, `test_propagation_outcomes`,
`test_batch_record_and_decimal_paths`, `test_fallible_inline_and_bool_count`,
`test_nullable_numeric_cast_emission`, `test_modular_array_update` and
`test_single_use_string_constructor`. These fail structural assertions; where
semantic/GC checks precede the assertion, they complete successfully. Later
assertions in the failed method are not qualified. Whether each mismatch is
a stale oracle or a missed optimization remains unconfirmed. Source lines,
assertions and raw MIR are preserved in
`temp/mvp_optimization_coverage/python-emission.log`; the complete review is
`coverage-report.md` there. No assertion is changed or skipped.


<a id="lr07-58"></a>**LR07-58 · MVP rejects the u32 regression fixture during representation conversion · OPEN (2026-11-10)**

`test/lambda/mvp/mir_mvp_u32.ls:10:23` fails with E501:
`unsupported representation conversion in mvp_6_main: any (0) from 1 to 8`.
The failure reproduces on the helper-retirement release and the saved
pre-cleanup executable (SHA256
`21884b40fcb5392a1747838d91e27628064afb1599a9c1a998f3d95308ff903b`), under
normal, forced/poisoned and seeded GC, for both default and explicit MVP.
This is a compiler admission/lowering failure, not a missing runtime import;
its cause is not established. Retain the fixture and golden (D4.3.1, D8.2.5).

Reproduce with `LAMBDA_EXEC_BACKEND=jit ./lambda.exe run test/lambda/mvp/mir_mvp_u32.ls`.
Evidence: `temp/mvp_helper_retirement/affected-goldens.log` and
`affected-goldens-control.log`. This predates and does not block dead-helper
retirement. The encompassing `test_benchmark_expansion_semantics` reports eight
subcase failures from this single fixture.


<a id="lr07-59"></a>**LR07-59 · Three execution-profile tests retain Full MIR emission expectations · FIXED (2026-11-10)**

`LambdaOptProfile.ExecutedCallsAreCountedOnlyWhenEnabled`,
`ExecutedCallsCanBeAttributedToCaller`, and `CowBindingDoesNotReloadLiveOwner`
fail on both the helper-retirement release (SHA256
`0e17c8c569764b0619088296bdb93fc5bfadc2f6cfde2f433b4fa55ea536ee75`)
and the COW-site-attribution candidate. Semantic output, diagnostic TSV emission
and overflow checks pass. The failures require retired `_accept_u32_`,
`_shaped__raw0_`/`_shaped__raw1_`, `_main_`/`_capture_dynamic_` names, a
`fn_len_l` call, or two `cow_bind_var` calls. Current MVP uses `mvp_*` entries,
inlines the scalar/capture bodies and uses checked length helpers for these
fixtures. The ordinary non-profiled native capture also bypasses a COW helper.

The structural/counter oracles need an MVP audit that preserves their intent:
opt-in diagnostics, executed-call attribution, and no spurious GC roots or
safepoints (D6.3.1–D6.3.4, D7.3.1). Do not restore Full variants or add runtime
calls solely to satisfy old counter shapes. Assertions remain unchanged.

Reproduce with `LAMBDA_JS_OPT_EXE=<candidate> ./temp/mvp_regions_profile/test-opt.exe --gtest_filter='LambdaOptProfile.ExecutedCalls*:LambdaOptProfile.CowBindingDoesNotReloadLiveOwner'`.
Evidence: `temp/mvp_regions_profile/profile-units-control.log`,
`profile-units-candidate.log`, and the current TSVs under
`temp/lambda_opt_contract/`. The separate COW census regression was caused by
missing MVP site-wrapper selection and is fixed; it passes on the candidate.
This issue does not block the allocator assessment or COW-site fix.


Migration update (2026-11-10): the two executed-call tests now identify MVP
entries, inlining and the canonical checked length helper. The COW test is now
`CowBindingMarksInlineBeforeObservation`: it requires the native share-mark,
observed COW mutation and unchanged values, rather than Full's helper count and
retired physical caller. All three pass on the rebuilt release identified in
the [migration checkpoint](impl/Lambda_Impl_MVP_Optimization_Tests.md).
The separate diagnostic parity unit preserves roots, memory operations and
safepoints, including COW site attribution (D6.3.2–D6.3.3). No Full helper or
profiling event is restored.


<a id="lr07-60"></a>**LR07-60 · MIR optimization gates are not fully migrated to MVP · PARTIAL (2026-11-10)**

At clean `cf0caf5c7`, private test controllers built from current source run
against the installed debug `lambda.exe`, SHA256
`c6f6766f9e28aacec482d5762b90a4b22be885c21f2ee1eddf37b053a385a3aa`.
The engine is not rebuilt for this read-only audit. Lambda emission fixtures
pass **2/279**, Lambda size ratchets **0/12**, optimization contracts **31/55**
(mixed T0/JS/JIT population), and selected MVP Python emission methods **47/56**.
Authority: D8.6.1 (optimization regression checks), D6.3 (precise-root safety).

Of 277 emission failures, 270 first fail because the sidecar selects a retired
`_<name>_#` function, while the artifact contains `mvp_<id>_<name>`; the matcher
in `test/test_mir_check_helpers.hpp:984` substitutes digits only. Five fail
instruction assertions and two fail execution/compilation (LR07-61/LR07-63).
Twenty-three sidecars positively require retired helpers. All 12 Lambda size
probes retain old selectors or budgets: seven selector failures, four budget
failures and the LR07-61 crash. These are not useful MVP regression gates until
their native/fallback intent and costs are reviewed, not merely renamed.

There is also a false-green optimization assertion:
`LambdaOptProfile.MultiSourceParameterAvoidsWrongShapeGuard` passes at
`test/test_lambda_opt_gtest.cpp:903`, although its TSV contains no
`field_attempt` rows. `exec_profile_sum_prefix` returns zero when the retired
`_print_node_`/`_expression_precedence_` prefixes do not exist, satisfying both
ceilings. Require an active instrumentation/body control before crediting zero.
The planned-effect rejection test at line 250 sets
`LAMBDA_TEST_GC_PLAN_CLAIM`, but no MVP/runtime source reads that knob; it exits
0 rather than exercising rejection. MVP still verifies emitted effects at
`mvp_mir.cpp:8725`; the test no longer injects a contradictory plan.

The current 142-method `test/benchmark/test_lambda_mir_mvp.py` is not invoked by
the build configuration, Makefile, `.github` workflows or `utils`. Its benchmark
golden method executes typed `*2.ls` variants only; the manifest verifies both
variants' files exist, not that every untyped kernel is executed. Existing
erased/untyped kernels give selective coverage. Register a bounded MVP unit
selection and review typed/untyped mirror and cost-budget coverage. Do not mask
the failed checks or weaken their semantic controls (S9.1.2–S9.1.3).

Reproduction, exact selectors, raw JSON/logs, passed-family map and controller
hashes: `temp/mvp_optimization_coverage/coverage-report.md` and
`final-summary.json`. No baseline, complete GC sweep or timing campaign ran.
LR07-57/LR07-59 retain the earlier assertion findings; naming failures do not
explain all counter failures or the implementation defects below.


Migration update (2026-11-10): explicit MVP selectors, exact callee checks
independent of ABI prototype suffixes, current helper paths, profiler controls
and new region fixtures are implemented. The missing-counter false pass now
requires real emitted bodies and executed member calls. The planned-effect
claim reaches MVP's verifier and rejects, with successful empty/unmatched-name
controls (D6.3.2). The 57-method Python selection is registered in the normal
Lambda gate and correctly reports failures.

On private release SHA256
`15b99367361d4a2f7482c7ded6f2966cf4958721348afc4a5f70c5f55efd1ac1`,
new fixture/matcher/ABI/parity checks pass 10/10, all 17 Lambda size/frame probes
pass (plus config), optimization contracts pass 46/56, and selected Python
methods pass 54/57. The full emission controller passes 97/294: 87/284 fixture
cases, including all five new fixtures, and 10 other checks. The older 279
fixtures now pass 82 instead of two; 197 contracts still need further MVP path
review or missing-optimization work. These failures are retained, not skipped.
The seven JS budget probe objects remain unchanged; seven selected JS matcher
regressions pass. MVP cost budgets are a reviewed compiler baseline migration,
not a claim that Full emission size was preserved (D8.6.1).

The remaining ten optimization contracts cover the immutable consumer crash,
numeric borrowed-store work, recursive union admission/cache work, string tail
copying/hyphen appends, checked-tail frame reuse, read-only length roots,
nullable packed construction and redundant stable-length loads. Three Python
checks retain missed propagation boxing, conditional modular and string-fusion
contracts. See the checkpoint above and `temp/mvp_emission_test_update/` for
exact reports, reviewed budget deltas, source/binary identities and the remaining
fixture inventory. No baseline or benchmark timing campaign was run.

Follow-up qualification (2026-11-10, base `c2f1d2426` plus the recorded working
diff): 148 of those 197 emission failures are resolved. The final private
release controller passes **530/579**, comprising **235/284** MIR contracts,
**284/284** semantic goldens and 11 other checks. The goldens are now mandatory;
12 formerly absent files are supplied. All 284 also pass under each forced and
seeded poison-GC mode. Named-register sequence matching retries ambiguous
prefixes without leaking bindings or relaxing adjacency. Eight shared JS
capture controls pass. Lambda size/frame ratchets remain 18/18 with unchanged
budgets; optimization contracts remain 46/56 with the same ten failures.
No new whole emission fixture fails. Remaining 49 MIR contracts stay red.

The nullable-scalar conversion, wide-result publication, recursive-array effect
classification and stale nullable/nested/borrowed record proofs are fixed by
root cause. Unannotated record mutation succeeds, snapshots remain unchanged,
and later typed crossings reject invalid data (D3.4.5, S11.4.10). The new record
controls fail on the preceding host and pass on the fixed host. Current release
SHA256 is `cb1fac1e77f2b7ac74163f3be0c0b1734446c9289148deca629a738af11ee8ef`.
See [the follow-up implementation/qualification](impl/Lambda_Impl_MVP_Optimization_Tests.md#remaining-fixture-follow-up-2026-11-10)
and `temp/mvp_fixture_followup/record-identities.json`. The shared executable and
sibling/vendor source remain unchanged; no baseline or timing campaign ran.

Remaining-49 qualification (2026-11-10, base `dced21367`): all 49 retained
emission failures are resolved. Private release SHA256
`6bd984ed27c89b1971094c0878fabfdf0c0074857c932f8a22d0208ff16e860c`
passes **582/582** controller checks: **285/285** emission contracts,
**285/285** semantic goldens and 12 other checks. Both forced/poisoned and
seeded/poisoned GC modes pass all 285 goldens. Lambda size/frame checks pass
18/18 after three reviewed zero-slack budget updates (D8.6.1); shared JS
capture controls pass 8/8. Native lanes, bounds/record proofs, nullable packed
construction, scalarization, private builders and safe self-tail reuse are
implemented rather than accepting generic paths as native witnesses.

Optimization contracts improve to **51/56**, with no new failures. Retained
failures are LR07-62, one checked borrowed numeric store, 213 recursive union
admissions (ceiling below 40), 198 open-union cache hits (required above 200)
and 19,275 hyphen appends (LR07-64). Their assertions remain intact. The current
release implementation and receipts are in
[the remaining-49 checkpoint](impl/Lambda_Impl_MVP_Optimization_Tests.md#remaining-49-contracts-2026-11-10)
and `temp/mvp_remaining49/complete-*.json`. Python aggregate checks were not
rerun; this is unit-only qualification, with no baseline or timing campaign.
The shared executable and sibling/vendor source remain unchanged.

Final-five GTest qualification (2026-11-10, base `b3463551a` plus qualified
diff): optimization contracts now pass **56/56** and the complete emission
controller passes **588/588** (288 emission contracts, 288 goldens, 12 other
checks). Both poison-GC modes pass all 288 goldens and all five repaired
optimization units. The original 582 cases remain present and pass. Lambda
emission/frame ratchets pass 18/18 after one reviewed zero-slack budget update
(D8.6.1). LR07-62 and LR07-64 are resolved on private release SHA256
`104fdc33ddc9cdefd42b39bb52b37b9c118903b417ed001913fd5e921eba9e83`.
The remaining cache unit used an impossible Full-era hit lower bound; it now
pins exact valid-crossing accounting, a 201-admission cap and the existing
four-miss/no-map-cache/malformed-child controls. Other optimization ceilings
remain unchanged. See [implementation/qualification](impl/Lambda_Impl_MVP_Optimization_Tests.md#final-five-optimization-units-2026-11-10)
and `temp/mvp_opt_remaining5/qualified-*.json`. Keep LR07-60 partial for the
unrerun Python aggregate and other unqualified configurations; the GTest
emission/optimization gates are green. No baseline or timing campaign ran.

<a id="lr07-61"></a>**LR07-61 · Native MVP wide-scalar tail-forward fixture traps · PARTIAL (2026-11-10)**

On the exact installed debug CLI identified in LR07-60,
`LAMBDA_EXEC_BACKEND=jit LAMBDA_MIR_COMPILER=mvp LAMBDA_DISABLE_MIR_CACHE=1
./lambda.exe test/mir/lambda/scalar_home_tail_forward.ls --no-log` terminates
with signal 5 on **3/3 isolated runs**. The same script under `interp` succeeds
3/3. It recursively adds `1i64` and forwards the scalar companion through a tail
edge. It must return normally while respecting result/home lifetime
(D6.2.1, D7.4.2). Both the emission and ratchet probes are blocked. No cause,
owning code defect or regression-introducing commit is established. No vendor
change or workaround is made. Evidence:
`temp/mvp_optimization_coverage/isolated-exit-rechecks.json` and the emission/
ratchet JSON/logs in the same directory. The engine was not rebuilt in this audit.


Qualification correction (2026-11-10): the earlier release check credited
exit zero and a passing size probe, but its value output was malformed.
`runtime_publish_result` retained a transient pending wide Item without resolving
its companion. It now resolves that pair before publication (D6.2.1). The
fixture's new golden requires `5`; native JIT, MIR interpreter and both poison-GC
modes pass on the release identified above. The size/frame ratchet still passes.
The unchanged installed debug executable is not evidence about this fix;
a rebuilt debug host is not qualified here. Keep that residue explicit.
See [implementation/receipts](impl/Lambda_Impl_MVP_Optimization_Tests.md#remaining-fixture-follow-up-2026-11-10)
and `temp/mvp_fixture_followup/record-backend-parity.json`.

<a id="lr07-62"></a>LR07-62 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-62>).

<a id="lr07-63"></a>LR07-63 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-63>).

<a id="lr07-64"></a>LR07-64 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-64>).

<a id="lr07-65"></a>**LR07-65 · T0 snapshots a plain argument after a later argument mutates its owner · OPEN (2026-11-10)**

The new `test/mir/lambda/mvp_call_ownership.ls` calls
`observe(values, change(values))` with `values = [3]`; `change` writes `9`.
S9.1.3v3 requires the plain argument's snapshot before a later borrow mutation.
The correct final line is `[3, 9]`. The rebuilt release JIT matches that golden
under normal, forced/poisoned and seeded GC; its interpreter prints `[9, 9]`.
The installed debug interpreter has the same mismatch. Both tiers exit zero,
so exit status alone would hide it. The other four new fixture goldens match
the release interpreter. Cause and regression provenance are unconfirmed.

Reproduce: `LAMBDA_EXEC_BACKEND=interp <host> run test/mir/lambda/mvp_call_ownership.ls --no-log`.
Evidence: `temp/mvp_emission_test_update/new-t0-controls.json`,
`new-fixture-probes.json` and `final-python.json`. The release identity is in
the LR07-60 migration update. Keep the expected result and report the T0 defect;
it does not block MVP JIT emission tests (S9.1.2–S9.1.3v3).

<a id="lr07-66"></a>LR07-66 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-66>).

<a id="lr07-67"></a>LR07-67 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-67>).

<a id="lr07-68"></a>LR07-68 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr07-68>).

<a id="lr07-69"></a>**LR07-69 · Builtin reference values have no boxed dynamic-call adapter · OPEN (2026-11-11; existing runtime capability gap)**

`let f = crypto.open; f(key, ciphertext, context)` reports E229
`fn_call_into: function does not publish a Core boxed dynamic-call entry`.
Direct `crypto.open(...)` calls work. This is the existing general builtin
reference boundary, not an AES-GCM defect: `to_sys_fn_named` in
`lambda/runtime/lambda-eval.cpp` deliberately marks heterogeneous native
function pointers as `FN_ENTRY_ABI_FOREIGN` and identity/name-only;
`lambda_dynamic_call` rejects that ABI before invocation. The interpreter and
MVP both construct those same reference values. D7.2.1/D7.3 describe the boxed
dynamic-call target; a general builtin adapter remains missing.

Evidence: `temp/sync-local/native-first-class.ls` and
`temp/sync-local/native-first-class-jit.log` from the standalone native probe. The two dynamic-reference assertions are excluded from the direct
API fixture's qualification scope, and the failure is retained here rather than
adding a per-crypto ABI exception. The sync implementation calls native APIs
directly and does not depend on this missing general adapter. No runtime fix,
golden reinterpretation or formal-spec change is claimed for LR07-69.

<a id="lr07-70"></a>**LR07-70 · Existing JIT/T0 differences block interpreter census refresh · OPEN (2026-11-11)**

The checked-subscript repair passes all 2,216 interpreter goldens, but
`make interp-sweep ARGS='--jobs 3 --timeout 30'` reports 1,543 matches and six
differences in its 1,549-row corpus. It refuses list regeneration. Five
differences reproduce unchanged on the saved original host present at starting
checkout `8b4fbd521`
(SHA256 `ee2abf8ce1b8d11639ca210a7618b75d89255546d65200666f7a35ecc40b01fe`):

| Fixture under `test/lambda/` | Interpreter | Eager JIT |
|---|---|---|
| `js_array_props_tail_bridge.ls` | golden, final boolean `true` | final boolean `false` |
| `last_defect_receiver.ls` | golden | compile exit 1 |
| `ledger_optional_fields.ls` | golden | signal 11 |
| `proc/interp_var_writeback.ls` | golden | compile exit 1 |
| `proc/proc_fill_bool_inferred_read.ls` | golden | compile exit 1 |

The sixth row, `conc/proc_handler_fault_after_wait.ls`, differs in the bounded
sweep but matches in the subsequent isolated before/after probe; its cause is
unconfirmed. Both probe tiers print `error308` and exit zero in isolation.

Reproduce each stable row with `LAMBDA_EXEC_BACKEND=jit ./lambda.exe <path>`;
use `run <path>` for `proc/` rows. Repeat with `interp`. Evidence:
`temp/sweep_full.tsv`, `temp/interp-e501/sweep.log`, and
`temp/interp-e501/sweep-divergences.json`. Source causes and introducing commits
remain unconfirmed. These differences violate tier parity (S1.6, D8.3.1),
block census regeneration, and do not block the interpreter E501 repair.
The failing oracles and all three generated coverage lists are preserved.

## 8. Memory management & GC (LR08)


<a id="lr08-2"></a>**LR08-2 · Execution-side-stack capacity is reserved up front · OPEN**
Root and raw-number regions have fixed virtual limits. Checked prologues fail
deterministically instead of corrupting adjacent memory, but workloads that
genuinely exceed those reservations cannot grow them dynamically.

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


## 9. Runtime builtins (LR09)

<a id="lr09-1"></a>**LR09-1 · Commented-out replace-in-file procedures (key collision) · OPEN**
`pn_replace_file3` / `pn_replace_file4` are still commented out in the registry
table (`lambda/runtime/sys_func_registry.c:1432`, `:1434`, re-resolved 2026-10-23) because their
`("replace", 3)` key collides with the existing `SYSFUNC_REPLACE` row; the
composite `(name, arg_count)` map in `build_ast.cpp` cannot disambiguate them.
Enabling them requires `first_param_type`-based disambiguation (one is
`LMD_TYPE_PATH`) in `get_sys_func_info`, which does not exist.
*Design status (noted 2026-10-23):* the file form `pn replace(path, …)` was proposed on 2026-02-28 and never ruled. GRP32 (2026-10-05) names `input()` → `replace()` → `output()` as the way to change file text, so whether the dedicated form is still wanted needs a user ruling before the registry work. Record: [Lambda_Design_String_Pattern.md §7.4](Lambda_Design_String_Pattern.md#74-file-replacepath---proposed-not-built).

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
- **Why filed here:** the formatters sit outside LR09's scope (`lambda/format/`, which `lambda convert` also uses), but the wrong value is observed through the `format()` builtin.
- **Fix:** needs a decision — remove the caps, or make an oversized string an error.


<a id="lr09-34"></a>**LR09-34 · File-based `find(path, pattern, options?)` is designed but not built; it waits on a proposal that does not exist yet · OPEN (logged 2026-10-23, string doc consolidation)**
GRP27 (USER, 2026-10-04) keeps file-based `find` an `fn` running on `lib/grep`, with the uniform `{value, index}` shape, `file` for a directory or wildcard, and opt-in `line` / `byte_offset` fields. Its determinism is to come from session caching, which a separate proposal is to design together with `input()`, `exists()` and `path#`; that proposal has not been written, so phase G5B of [Lib_Grep.md](lib/Lib_Grep.md) is blocked. Today `find` takes a string or symbol only (`SYSFUNC_FIND`, `SYSFUNC_FIND3` in `lambda/runtime/sys_func_registry.c`), and `io.grep` (a `pn`) is the only file search. Record: [Lambda_Design_String_Pattern.md §7.3](Lambda_Design_String_Pattern.md#73-file-based-findpath---designed-not-built).

## 10. Error handling (LR10)


<a id="lr10-12"></a>**LR10-12 · A proviso answers null for an error operand when its predicate touches `~` (S10.1.5v3, S7.9) · OPEN (found 2026-09-25, waiting on a ruling)**
`x that p` binds `~` to `x` whatever it is. With `let e = error("boom")`, `e that true` is the error, but `e that ~ > 3` is `null`: the comparison propagates the error (S7.9.3), an error is falsy, so the proviso fails. S10.1.5v3 rules a failed proviso absence and says nothing of an error operand. S7.9 asks whether a result can be mistaken for a successful computation, and a null proviso can. Both tiers agree.



## 11. Mark data API (LR11)

<a id="lr11-1"></a>**LR11-1 · Reader traversal is stubbed · OPEN**
`MarkReader::ElementIterator`'s destructor leaks `state_`
(`lambda/core/mark_reader.cpp:50`, `// TODO: Free traversal state`), and
`next()` only linear-scans *direct* children (`:57`, `// TODO: Implement proper
tree traversal for nested elements`). There is no real descendant or CSS-like
matching, so any caller expecting deep selection gets silently wrong results.


<a id="lr11-8"></a>**LR11-8 · `push` / `splice` are not in the Mark editor · OPEN (note)**
They are runtime builtins in `lambda/runtime/collection_runtime.cpp` (registered
as `SYSPROC_PUSH` / `SYSPROC_SPLICE`) and belong to
[§12](#12-procedural-runtime-lr_12), not the editor surface.


---


## 12. Procedural runtime (LR12)

<a id="lr12-47"></a>LR12-47 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr12-47>).

<a id="lr12-46"></a>LR12-46 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr12-46>).

<a id="lr12-45"></a>LR12-45 is [resolved and archived](<Lambda_Issue_Ledger (fixed).md#lr12-45>).

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



<a id="lr12-24"></a>**LR12-24 · TE-15 defect containment — declaration and reassignment boundaries FIXED 2026-09-18; TE-17 I3 and the remaining origination classes FIXED 2026-09-27; residue OPEN**
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
tiers — not a containment failure. Recorded as [LR12-25](<Lambda_Issue_Ledger (fixed).md#lr12-25>), together
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
call-graph fixed point (D7.1.3) as what removes it, and says it "must be built
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

*2026-09-27 — TE-17 I3 and the D7.1.3 fixed point landed; the other origination classes are fixed.*
- **The fixed point.** `mir_solve_may_defect` (`transpile-mir.cpp`) runs once per compile, before any variant is registered. It first records each function's own origination sites, each judged by the emitter's own gate: declaration and reassignment boundaries, element and field stores, CRUD, `open`, `start`, handler statements, deferred object fields, computed-key maps, call admissions and array boundaries, the system rows that can fail, and the return boundary. It then takes the least fixed point over the call edges. An import reads build_ast's conservative fact (`TypeFunc::defect_known`/`may_defect`, `ast_note_function_defect`); anything else fails closed.
- **Lane gating (I3).** A call whose value may be an error it does not declare is a boxed join to every planner: the carrier oracle, the lane witness, the lane proof and boundary redundancy. That covers a defect-capable callee, a call through a function-type contract ([LR03-13](#lr03-13)), and a system function that rejects (S11.4.3) or passes through (S7.9.3) an operand that may be one. So are a member or element of such a value and arithmetic over it. `type()` is exempt, since it observes an error (S7.9.1).
- **Keeping the cost down, within the rulings.** `==`/`!=` of an `int | error` join against an in-band int compares Item words (S5.1.2). Integer arithmetic over joins tests for the first error and runs on the int lanes (S7.9.3), and `int()` of a join is the join itself. A destination whose contract the join's success already satisfies (the callee's own return proved it) tests only the error arm: declarations, reassignments, returns, arguments and the typed-array witness. A record literal built in its contract counts no return boundary in the solve, and a record binding's fields stay on their typed path (its record call has already returned any error).
- **Literal slots.** build_ast lays literal slots out before either tier runs, so it boxes a slot fed by a value that may be a defect (`build_map_shape_entry`, `resolve_array`), using its own conservative fact, which over-approximates the solve. `{v: f(x)}` had stored the error's pointer bits and `[f(x), 2]` a 0.
- **Also fixed on the way.** The JIT returned an error from a function through `let x: any | error = raising()`: the let and assign sites now skip only when the contract excludes error. T0 bound a value-carried error into a declared binding that excludes it (S7.7.2), and a type or function value in an `any` map slot read back as an error.

Fixture `test/lambda/proc/defect_value_flow.ls`, pinned on every tier; the base build fails it (T0 prints pointer bits and `0` for errors, and the JIT crashes). `make test-lambda-baseline` 5974/5974 with the emission budgets re-baselined for four probes (tune4 +14, cube3d +82, deltablue +217, prettier_ast +53 module instructions; reasons in `test/mir/mir_budgets.json`). Runtime: release builds, paired A/B against the base, 15 pairs over 27 typed rows (`run_paired_benchmarks.py`, artifacts `temp/errc/ab*.json`). 20 rows are within ±2% and three are faster (prettier_ast −3.8%). Slower: deltablue +2.4%, fast_diff +4.8%, hashmap +8.4% (between −4.5% and +15.6% across four runs), and queens +12% on a 70 µs run. Before the join lowerings, storage and queens had been +22% and cube3d +8%.

**Correctness follow-up (2026-10-31), S7.7.2–S7.7.6 / D7.1.3.**
Residues 1–4 are fixed: deferred declaration and reassignment failures end their
declaring block (including iteration-local and comma-separated bindings), fresh
parameter admission failures become the call's value, checked assignment/store
failures report the skipped tail and retained/partial state, and inferred record
bindings preserve an error without abandoning the caller's later effects. Inline
activations retain the same local regions and reports. Call-site admission facts
remain separate from the callee-body effect fixed point, preserving recursive
native proofs. Fixtures: `proc/ledger_containment.ls`,
`ledger_parameter_result_site.ls`, and the native runtime diagnostic test.

**Residue (open):**
5. build_ast's fact over-approximates the solve. It can box literal slots and
   joins needlessly. An unannotated defect-capable record call currently uses
   the boxed ABI: the success-only scalar-record destination cannot represent
   its error arm. Improving the proof or adding that carrier remains tuning.
6. Join arithmetic into a declared accumulator still boxes the native result,
   tests/unboxes/adds it and re-tests at assignment. Keeping native and error
   lanes through pure arithmetic remains an independent optimization.

The complete S7.7 static diagnostic tiers and the unparsed `for x: T in e`
surface are not claimed complete by this bounded runtime-correctness pass.
Implementation, compatibility changes, emission review and qualification:
[containment follow-up](<impl/Lambda_Impl_Ledger_Containment_Followup (done).md>).

<a id="lr12-7"></a>**LR12-7 · The procedural surface is thin and ad hoc · OPEN**
IO procedures are a hand-curated set in one file with bespoke validation per
procedure; there is no general effect/capability system, so adding a
network-write or process-spawn procedure means another bespoke `pn_*` plus a
registry row.

---


<a id="lr12-27"></a>**LR12-27 · `push` and direct-write failures repaired; plain-call error policy pending · PARTIAL (found 2026-09-21; push fixed 2026-09-25; direct writes fixed 2026-11-03)**

**Original report (retained):**
In a `pn`, `var a = [1, 2, 3]; push(a, 4)` leaves `a` as `[1, 2, 3]` on both
tiers: the literal is an uncertified `ArrayNum` and `push` returns a soft error
that nothing surfaces (`collection_runtime.cpp:240`, "an uncertified ArrayNum
has no append contract"). `int[]`, string, and mixed arrays append correctly.
Likewise `var b = [1]; b[5] = 2` only logs "index 5 out of bounds"
(`lambda-eval.cpp:8200`) and the procedure continues with exit 0. Both
contradict **S7.1.3v2** (writes are checked and raise through the `T^`
channel).

*Fixed 2026-09-25 (push):* S9.1.1 makes `push(b, v)` mean `b' = b ++ [v]`, and index writes already keep an open packed array's lane or widen it in place. `array_num_push_open` (`lambda-eval.cpp`) does the same for push: it appends in the lane when every item fits and otherwise converts to a generic Array, then spreads a list (D3.3.1); a view or an N-D array refuses, as splice does. `pn_push_cow` no longer checks an open binding against a past admission's certificate (SI3v2): after `pn g(x: int[])` had admitted `u`, `push(u, "s")` had failed. `proc/jit_tail_if_push_error` now fails its push on a matrix, and `proc-ext/list_var_mutation`, green at last, moved to the baseline. Fixture `test/lambda/proc/push_open_packed.ls`.
*Fixed 2026-11-03 (direct writes, S7.1.3v3 / S7.7.6v2):* the setter already returned a rich E302, but both tiers could discard it in a procedure without a declared error return. T0 now establishes the store boundary for unannotated roots as well. MIR's raw positional fallback arms route the setter's error through that boundary before statement-result discard. The original `pn main() { var b = [1]; b[5] = 2; print("unreachable") }` reports E302, exits one and never prints the marker on interp/jit/auto. Native-result transport no longer blocks this fix (D7.1.3). `proc/ledger_index_write_error` checks recovery, native int/float results, unchanged snapshots, skipped tails and inner-block containment; the original main is a negative regression. See [implementation and qualification](<impl/Lambda_Impl_Ledger_Handler_Write_Followup (done).md>).

*Remaining (needs a ruling):* whether a write error from a plain `pn` must escape a **discarded statement-position call in its caller**, rather than being available as the call's result. This separate caller-boundary policy is not decided by repairing the originating store.


## 13. Schema validator (LR13)


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
required by **D4.2.3**; subsequent crossings of that unchanged accepted value
still revalidate it. **S11.4.1v3** permits reuse, and **D4.2.5** defines the
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

**2026-10-15 recheck (PDF P2):** `make build-test` succeeds, but
`test_validator_gtest.exe` and `test_validator_input_gtest.exe` now abort at
startup with `dyld: symbol not found in flat namespace '_LIT_BOOL'`.
The validation DSO imports `LIT_BOOL` and other private compiler globals;
the host contains them as local symbols after the **D9.4.6** export allowlist
is applied. Both the import in `build_ast.cpp` and that export policy are
present in HEAD `5fd9e797e`, before the P2 changes. P2's schema helper imports
also require a valid internal link boundary. This run obtains no verdicts
from either standalone suite. The earlier statement that all validator
executables load is historical; the current DSO/host closure remains open.
The ordinary statically linked Lambda host exercises schema validation in
the P2 native and interpreter/MIR regression tests.

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

**Consequence for the specs:** the D3.1.1 content-arity claim can only be read from the code, not run. `validate_against_element_type` does enforce `content_length`, and an element-kinded nominal type reaches that arm by tag, so the old "not implemented" note is wrong — but "conformant" cannot be asserted until this is fixed. Both conformance rows now say exactly that.






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
> `LR*` Known-Issues section. **IDs are kept as their owning doc assigns them**
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


<a id="i8-genafterlet"></a>**Issues8 · A comprehension generator may not follow a `let` clause · OPEN (design question, not a defect)**
*Update 2026-10-20:* the clause tail is now ruled and enforced — S14.1.2v2
puts `let`/`where` before `group by` and `order by`/`limit`/`offset` after
it, each at most once (E239). That is the phase-ordered tail both options
below keep; only the generator/`let` prefix question remains open
([Expr_For_Clauses §4.4](Lambda_Expr_For_Clauses.md#44-the-clause-pipeline)).

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
> `LR01–13`) was verified fully subsumed by §1–§13 above — every MAJOR item it
> bolded resolves to a ledger entry, and the ledger carries more per section than
> the rollup listed. Its §2 (JO1–JO13) and §4 (JS01–16) were pointer indexes
> into docs that still own them: `vibe/Lambda_Design_Stack_Frame_JS.md` (18 JO
> references) and the JS known-issue sections, now in
> `vibe/jube/JS_Issue_Ledger.md`. What had **no other home** were the OI design gaps and the cross-cutting
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
  Node-style error behavior. See JS14 in `vibe/jube/JS_Issue_Ledger.md`.
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

- **Silent fixed caps with inconsistent failure modes** — closure captures 16, generator states 63, promise reactions 8, TypeMap hash 32, union types 32, module vars 2048/1024, regex groups 256, and more. One grow-or-error doctrine retires the class. Ledger instances: [LR01-5](<Lambda_Issue_Ledger (fixed).md#lr01-5>), [LR13-5](#lr13-5); [LR11-4](<Lambda_Issue_Ledger (fixed).md#lr11-4>), the Mark data API's four, was closed on 2026-09-26 with every one of its caps failing visibly.
- **Layout-coupled raw offsets** — resolved for module binding by removing the
  unreachable `init_module_import` walk ([LR01-8](<Lambda_Issue_Ledger (fixed).md#lr01-8>)); GC
  trace/compaction is resolved by [LR08-5](<Lambda_Issue_Ledger (fixed).md#lr08-5>).
- **Masked memory-safety bugs** — none remain. The event-loop SIGSEGV band-aid was removed on 2026-07-31 (ER-S6 in `impl/Lambda_Impl_Type_Enforce (done).md`): a memory fault in a timer callback now terminates the process, as S7.11.4 requires, and a callback re-enters only its owning context (`timer_runtime_enter`, an owner check since 2026-07-29), which closes JO12's re-entry audit. Verified 2026-09-26: `test/js/timer_callback_gc_stress.js` and its document twin `dom_timer_callback_gc_stress.js` drive allocating callbacks, nested scheduling, clears and promise jobs through the drain, and run in the forced-GC lane (`make test-gc-rooting-core`) with freed memory poisoned. The `sys://` map-walk segfault workaround was replaced by the shape-aware traversal in [LR01-R3](<Lambda_Issue_Ledger (fixed).md#lr01-r3>).
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

### 15.3 Radiant SVG support residue

<a id="rad14-svg-support"></a>**RAD14 SVG support · PARTIAL · 2026-10-03.**
The [SVG support matrix](../doc/HTML_CSS_SVG_Support.md#15-svg) is mapped to
reproducers and implementation phases in the
[SVG implementation plan](impl/Lambda_Impl_SVG_Support.md#3-coverage-map).
The P7 stroke/marker oracle review and P10 F1–F4 coordinate/resource audit pass,
including resource font units, mutation recovery and output survival after
scratch/program teardown. macOS SVG/PDF exports and the complete Radiant gate
pass. P12's clock dependency/default/fragment/detach audits and initial-time export
contract are implemented. The plan's §7.16 closes access-key/wallclock timing,
local/external/nested use event state, qualified broadcasts, clone timing events
and animation DOM queries/TimeEvents. N1 is implemented; the broad SMIL row stays
partial for motion/discard, general SVG DOM expansion and unavailable renderer
features. Linux/Windows P13 runtime smoke remains open. Broader SVG2 vector effects
(`non-scaling-size`, `non-rotation`, `fixed-position`, combinations and explicit
viewport/screen selectors), filter component transfer/convolution and the larger
SVG document/cache architecture remain explicit limits rather than full support
claims. These are SVG behavior gaps; no Lambda ruling changed. Retained resources
follow **D5.2.2–D5.2.5/D5.5.1** (pin, gen-check, copy-as-value).
The plan separates implemented cases, historical checkpoints and open gates.

2026-10-29: numeric Lambda attributes now reach the DOM text boundary and SVG
presentation fallback through the shared number formatter and document-owned
attribute store (**D5.5.1/D5.5.2**). Numeric opacity/stroke/focus and exact
string parity pass the focused native replay on all three tiers with forced
poisoning GC. Required Radiant qualification and the exact old-host CSS failure
comparison are recorded separately; no clean aggregate claim is made.
[Repair record](impl/Lambda_Impl_Eden_Engine_Repairs.md#numeric-dom-and-svg-presentation-attributes).

---


### 15.4 Reactive presentation gaps

<a id="ui-2"></a>**UI-2 · Uncontrolled native select loses its selection after a parent presentation update · OPEN (found 2026-10-10)**

On `047a15f6f` with the working UI packages, `test/ui/bold/controls.json`
records **20/21** assertions passing on `interp`, `auto` and `jit`. Choose
Twenty with Down then Enter: the package emits integer `20`, the parent
updates its request display, and the native control returns to authored
default `10`. Replacing the package and logical tags with dtna in
`temp/ui_bold/_dtna_controls.ls` reproduces the same failure independently of
bold's presentation. The failing assertion remains in the committed fixture.

`rebuild_lambda_doc_incremental` replaces DOM wrappers and migrates ViewState
through `view_state_preserve_subtree_identity`. The migration does not copy the
option's native selectedness/dirty bits; the fresh option wrappers retain
authored `selected` defaults. `dom_option_is_selected` and native form-data
queries then read those defaults instead of the chosen value. Preserve native
control state in the general presentation lifecycle, respecting changed authored
defaults; do not add a second selection owner inside a theme (**D10.2.1;
S12.1.3**).

<a id="ui-3"></a>**UI-3 · Native overflow clips retain earlier flex dimensions · OPEN (found 2026-10-10)**

`./lambda.exe render test/ui/bold_gallery.ls -vw 1100 -vh 1900 -o
temp/ui_bold/gallery_render.png` reproduces a clipped progress track in the
right-hand card. The layout tree gives the track a 24px border-box height;
the raster trace pushes a rounded inner clip only 8px high. Card clips also
retain a smaller height than the final flex box. `layout_block_overflow_clip`
reads `ScrollProp::clip`, populated during block layout, while later flex
sizing changes the box dimensions. The resize/clip publication paths need a
focused engine regression and a shared lifecycle fix. The native viewer also
shows a mispositioned block-button label after its initial relayout; that
observation still needs isolation from the clipping defect. No compensating
padding, fixed heights or theme-specific layout branches are added. The
package exercises ordinary CSS through the existing engine (**D9.3.4;
S12.1.3**).

# Appendix B — Cross-cutting clusters

Several ledger entries are one defect wearing different masks. Fix them
together, not individually.

| Cluster | Entries | Root |
|---|---|---|
| **TCO safety proof residue** | LR07-13 | The former root-classification faces LR07-7/LR08-3 are resolved and archived. The surviving TCO face is the unused `is_tco_function_safe` proof, now tracked independently under LR07-13. |
| **Representation ↔ semantics coupling** | LR07-14 | Remaining container and result-domain cases. Lambda expression lowering carries `MirValue`; see resolved [LR07-1](<Lambda_Issue_Ledger (fixed).md#lr07-1>). |
| **Silent-truncation caps** | LR01-6, LR03-2, LR05-6, LR07-11, LR08-10, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency was stated most plainly by LR11-4, closed on 2026-09-26 with LR08-6 (archived). |
| **Surface syntax (S16) residue** | i8-genafterlet, SO36, O3, §7.17, LR02-27 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). The historical LR02-24–LR02-27 (2026-09-25) reports describe four type-sublanguage splits between the front ends: a range after `is`, a chained count, a line-start `?`, and signature parameters without `: T`. That historical harness snapshot was C 343/343 and reference 330/330 at `293b7a175`. LR02-24 and LR02-26 are archived; LR02-25 closed 2026-10-29 with mirrored tight-count cases (C 442/442, reference 428/428). LR02-27 remains open. |

---

# Appendix C — Maintaining this ledger

1. **This file is the working list; `LR*` sections stay as design record.**
   When an `LR*` "Known Issues" section changes, mirror the change here with
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

## Lambda Script expl3 subset needs a TeX expansion boundary — 2026-11-11

**Status:** OPEN — feature integration boundary. **Dependent task blocked:** yes,
the requested expl3 subset in `lambda.doc.latex`. Authority: D9.1.3v2,
D10.2.2, D8.3.1 and D11.2.1.

`lmd/package/doc/latex/latex.ls` calls `parse` before its adapters run. Input's
`run_tex_engine` performs TeX expansion during that parse, and its current
`EngineOptions` and `InputParseOptions` carry adapter names and raw commands,
not a way to call Lambda Script during expansion. Consequently a script module
used by the existing post-expansion adapters cannot implement expandable expl3
operations, scoped TeX assignments or macro definitions at the point TeX needs
them. Reproduce by inspecting those entry points and the expl3 fixture at
`test/lambda/latex/fixtures/local_packages/lmdexpl.sty`.

The requested implementation needs Lambda-owned package/runtime work and an
agreed input/host boundary; the shape of that boundary is not yet decided.
Input must not import the runtime, values crossing the boundary require precise
rooting, and resource authority must be preserved. Input-side design and the
candidate first subset are recorded in `../lambda-input/vibe/input/Input_Latex.md`.

### Superseding request: full TeX and LaTeX script port — 2026-11-11

The user now requests porting the TeX engine to Lambda Script and moving the
entire LaTeX handling path to script, superseding the partial expl3 bridge
request above. **Dependent task blocked:** yes, pending Lambda-owned package,
runtime, formal-design and test work and a coordinated input migration.

Current evidence: `lambda.doc.latex.latex.parse_source` calls the built-in
`parse`; input's `parse_latex_direct` runs `tex::Engine` and then the C++
`DirectLatexParser`, while `parse_tex_expansion` exposes the engine directly.
Inspect `lmd/package/doc/latex/latex.ls`, input's `lambda/input/input-latex-c.cpp`
and `lambda/input/input.cpp`. A package-only engine leaves those public and
native entry points unchanged. D10.2.2 currently places every LaTeX parse at
the input boundary, so the requested migration requires coordinated revision
of that ruling. Preserve S1.8 (TeX remains data), D9.1.3v2 (no input runtime
imports), D11.2.1 authority and existing conformance and document fixtures.

Input-side scope and candidate compatibility gates are documented in
`../lambda-input/vibe/input/Input_Latex.md`. No script engine has been shipped.

2026-11-11 follow-up: the user authorized coordinated edits to the Lambda and
input checkouts. `lmd/package/doc/latex/tex_engine/reader.ls` starts the script
port with incremental token reading, and `test/lambda/latex/test_tex_script_reader`
has a golden result passing in interpreter and JIT modes. The production parse
path is still the C++ implementation. Expansion, primitives, AST construction,
public-entry migration, formal ruling updates and full parity remain open;
the earlier authorization blocker is removed.


## Member access on a `file` binding — 2026-11-11

**Status:** OPEN — semantics and cause unconfirmed. **Observed:** macOS debug
`lambda.exe`, interpreter and JIT. **TeX-port blocker:** no; the script uses a
different local name.

A script containing `let file = {x: 1}; file.x` prints `file.x`, while
`let keyword = {x: 1}; keyword.x` prints `1`; evaluating `file` itself prints
the map. In the TeX prototype this caused a later map spread to fail when it
used `file.state`. Reproduce with these two expressions through `lambda.exe`
under each `LAMBDA_EXEC_BACKEND`. Determine whether `file.x` is intentionally
a different syntactic form and whether accepting the binding requires a
diagnostic or a member-access resolution change.

## Workspace-sync observation of member access on a `file` binding — 2026-11-11

**Status:** OPEN — semantics and cause unconfirmed. This is independent evidence
for the `file`-binding finding, not a separate confirmed runtime defect.

Workspace-sync evidence: `temp/sync-workspace/_lex_probe.ls`
compares `{id:42}` bound to `file` with records bound to `json` and
`member_record`. The archived debug AUTO output in
`temp/sync-workspace/lex.log` is
`[file.id, 43, 44, {entry:file.id}, {entry:43}, {entry:44}]`.
This observation predates the workspace task's native rebuild; it is not a
recheck of the final qualified executable. No common parser/resolver cause or
intended syntax has been established, and this evidence does not close the issue.


## Peer-sync qualification: TeX/expl3 package-echo golden mismatch — 2026-11-11

**Status:** FIXED for the script-handled package echo. The earlier aggregate
Lambda baseline was **7,475/7,476**, with the sole failure in
`test/lambda/latex/test_tex_script_expl3.ls`. Its `script expl3
package` golden expects `A` but receives `usepackage{expl3}A`; the duplicate
case expects `X ` but receives `usepackage{expl3}usepackage{expl3}X `. The
isolated unchanged test reproduces the same mismatch. These cases inspect the
script expansion's direct output-token values; this report does not decide
whether that projection, its golden or the expansion needs correction.
Evidence in the Lambda checkout: `temp/sync-peer/baseline.log`,
`expl3-recheck.log` and `expl3-recheck.json`.

The peer task changed neither the TeX modules nor their goldens; the mismatch
is outside its sync-module paths and does not block the focused peer acceptance
gates. This report does not assess the separately recorded package-echo selection
fix. S1.8 and D10.2.2 remain the applicable input boundary.


## JIT rejects dynamic stream map in TeX package loader — 2026-11-11

**Status:** OPEN — cause unconfirmed. **Observed:** macOS debug `lambda.exe`,
interpreter succeeds but JIT compilation reports E501, `unsupported
representation conversion in mvp_*_start_package: any (0) from 1 to 8`.
**TeX-port blocker:** no; the loader explicitly validates its dynamic stream
value with `is map` before calling `stream.configure`. LR07-58 reports a
similar conversion diagnostic, but a common cause is not established.

To reproduce the failing case, copy `lmd/package/doc/latex/tex_engine/` to
a temporary directory, and in `expansion.ls` replace the guarded
`let configured = if (source_input is map) stream.configure(source_input,`
call and its `else null` with the unguarded
`let configured = stream.configure(source_input, ...)^;`, keeping its two
remaining arguments unchanged. Import that copied expansion module and run
`expansion.expand(stream.from_reader(reader.reader("x")), 100)^` with
`LAMBDA_EXEC_BACKEND=jit`; compilation rejects the module before execution.
The same module and input execute under `LAMBDA_EXEC_BACKEND=interp`. The
guard validates the runtime boundary and remains in the shipped prototype;
the unguarded case must remain visible until the compiler defect or the
reason for rejecting it is established.


## Computed element names needed by the script LaTeX digester — 2026-11-11

**Status:** FIXED — the user selected `<[tag] ...>` and S16.8.10 now rules
computed element tags. Both parser conformance suites and interpreter/JIT
regression tests pass, including forced precise garbage collection. The script
LaTeX digester can now construct arbitrary named nodes. **Dependent task
blocked:** no for element construction; full LaTeX parity remains unfinished.

The native LaTeX digester builds an element whose tag is the input command or
environment name (for example `\\custom` produces `<custom ...>`). The script
prototype can construct only its explicitly named literal tags and reports an
unknown command as unsupported. Reproduce the missing general construction by
running `let tag = "custom"; <[tag]>`: the current parser rejects it with E100,
`expected an element tag` before the fix. S16.8.9v2 covers computed *attribute keys*, not tags;
`vibe/Lambda_Pkg_Readability.md` RD-O1 records computed tags as needing a user
ruling at the time. The user's requested full script TeX/LaTeX port needs a general
construction operation that preserves element value, attribute, content and
precise-rooting semantics. A computed-tag literal and a runtime constructor are
possible designs; S16.8.10 adopts the computed-tag literal.


## Decimal `trunc` narrowed through binary floating point — 2026-11-11

**Status:** FIXED. **TeX-port blocker:** no after the fix; this defect blocked
accurate 16-significant-digit expl3 floating-point evaluation.

On interpreter and JIT, `let x = decimal("9999999999999999.9")^;
[string(trunc(x)), type(trunc(x))]` returned `["1e+16", float]` instead of
`["9999999999999999", decimal]`. Similarly, truncating decimal
`-0.999999999999999999` returned `-1` instead of `-0`. The scalar `trunc`
implementation converted its argument through binary double, losing decimal
precision and sometimes rounding across an integer boundary.

The fix uses the shared numeric rounding implementation and `decimal_trunc`,
preserving decimal values and supporting vectors through the shared numeric
vector path. `test/lambda/trunc_decimal_precision.ls` covers precision, signs,
null, vectors and dynamic types; it passes on both backends with forced precise
garbage collection and freed-memory poisoning. The existing `numeric_sys_func`,
`vector_sys_func` and `typed_array_vectorized_math` goldens also pass on both
backends. S7.1.1v3 covers null propagation and S7.10.5v3 covers sequence input
and array output.


## Expanded parenthesized command argument source mapping — 2026-11-11

**Status:** OPEN — the source-provenance gap is confirmed; a complete mapping
strategy remains to be designed. **Full script TeX-port blocker:** yes, for AST
parity on expanded parenthesized arguments. The production native parser is
unaffected; the experimental digester reports these cases as unsupported.

Expand `\def\arg{(1,2)}\begin{document}\mystery\arg\end{document}` with
`profile.expand_with_kernel` and `echo_definitions: true`, then compare
`digest.digest(expanded).ast` to `parse(source, {type: "latex"})`. The native
AST puts `(1,2)` in a `parenthesized` argument of `mystery`, mapped to the
invocation; the expansion tokens point at the macro definition and have
`direct: false`. A second reproduction is `\def\arg{1,2}\begin{document}
\mystery(\arg)\end{document}`: only the contents are expanded, but native still
recognizes a coordinate argument and records its expanded raw value. Using only
original source slices misclassifies both examples as ordinary text and cannot
produce the native argument descriptor. The script digester must retain enough
invocation provenance and expanded text to reproduce native raw values and
source offsets without violating S1.8 or precise source ownership. The staged
port explicitly returns `latex-digest-unsupported` for these cases until then;
its regression tests retain these reproductions.


## Script spacing reconstruction lost separators and comment-adjacent whitespace — 2026-11-11

**Status:** FIXED for the tested spacing and control-space forms in the
experimental script digester. **Full TeX-port blocker:** no for these cases;
full source-provenance and LaTeX parity remain open.

With `\def\spaceout{\quad}\begin{document}A\spaceout B\end{document}`
and `echo_definitions: true`, native parsing yields `"A"`, `quad`, `" B"`;
the script digester previously yielded `"A"`, `quad`, `"B"`. The expanded
`quad` token is not source-adjacent to the following letter: native TeX
reconstruction inserts a separator to keep a control word and following ASCII
letter distinct. This is not simply copied invocation whitespace: two spaces
at the macro call still produce one separator, while following punctuation
needs none. The digester now follows that reconstruction rule.

The same investigation found that direct spacing commands and control spaces
followed by a comment could lose whitespace around the comment. The shared
source-gap scanner now retains that whitespace while matching native comment
line-ending handling, including CRLF. The full script digester goldens pass
interpreter and JIT, with native-AST comparisons for macro expansion, following
letters, punctuation and groups, and LF/CRLF comments. The separate expanded
parenthesized-argument source-mapping issue remains open.

## Script TeX control-space primitive was unsupported — 2026-11-11

**Status:** FIXED in the experimental script engine. **Full TeX-port blocker:**
no for the tested control-space forms; full TeX and LaTeX parity remains open.

Expanding `A\ B` with the script profile previously stopped with
`tex-unsupported-primitive` for control space, although native TeX keeps the
primitive in the expanded stream and native LaTeX parses it as a literal space.
The primitive was recognized in the registry but lacked an execution branch.
The script expansion step now preserves the control token, including `\let`
aliases; the digester consumes a literal control space and recovers direct
following whitespace from source. New TeX expansion token tests and
`test/lambda/latex/test_tex_script_control_space.ls` cover direct, repeated,
macro-expanded, and formatted cases. The staged expansion and digester
goldens pass interpreter and JIT; the control-space AST cases match native in
both backends.


## Macro-generated inline formatting uses definition offset — 2026-11-11

**Status:** FIXED for the tested inline-formatting cases. The stream records
fresh main-file reads and synthetic formatting tokens carry the current read
offset; source reconstruction for generic commands remains open separately.
The production native parser is unaffected.

Expand `\def\u{\textbf{Hi}}\begin{document}\u\end{document}` using
`profile.expand_with_kernel` with `echo_definitions: true`, then compare
`digest.digest(expanded).ast` against `parse(source, {type: "latex"})`. The
script digester previously returned a formatting element whose source offset
points at the command in the macro definition, whereas native reconstruction
maps the expanded command to its invocation. The affected synthetic token has
`direct: false` and retains its definition offset. The stream now records the
last fresh main-file read without resetting it when tokens are pushed back or
foreign files are read; the digester uses that position for synthetic inline
formatting. `test_tex_script_provenance` compares nested and repeated macros,
arguments, lookahead, case changes and non-ASCII text against native ASTs;
`test_tex_script_provenance_package` compares offsets from a local package.
The installed tests and existing TeX script suites pass interpreter and JIT,
including focused forced-GC checks. Expanded parenthesized and foreign generic
command source mappings remain open.


## Script TeX underline and overline primitives were unsupported — 2026-11-11

**Status:** FIXED for direct and tested macro-generated text underline, and
tested math underline/overline forms. **Full TeX-port blocker:** no for the
covered forms; generic source reconstruction remains open.

The experimental profile stopped with `tex-unsupported-primitive` for
`\underline{Hello}` and `$\overline{x}$`, although native TeX retains those
primitives for the LaTeX digester. They were recognized in the primitive
registry but lacked execution branches. The script engine now preserves the
primitive tokens and aliases; the digester builds native-compatible text
underline and math accent nodes. `test/lambda/latex/test_tex_script_underline`
covers nesting, empty and spaced groups, math expressions, primitive aliases,
and macro-generated formatting cases. Expansion and digester goldens pass
interpreter and JIT, and supported ASTs
match native parsing on both backends.


## Empty TeX macro argument inserts a null token — 2026-11-11

**Status:** FIXED for tested empty required and optional macro arguments.
**Full script TeX-port blocker:** no for those forms. Native TeX is unaffected.

Before the fix, expanding `\def\a#1{[#1]}\a{}` in the script engine failed with
E201, `map literal spread requires a map-like value or element`. The macro
substitution comprehension places a conditional around `*args[t.value]`; an
empty argument becomes a null item, which `stored_tokens` then tries to spread
as a map. A standalone reproduction is `let a = [[]]; [for (t in [0]) if
(true) *a[t] else t]`, which produces `[null]` on the observed interpreter.
The fix spreads the selected token array outside the conditional so that the
empty argument contributes zero tokens. The expression's behavior is consistent
with collapsed list values under S2.5.5v2; no runtime defect is asserted. Both
TeX and expl3 macro substitutions use this shared helper. The installed
`test_tex_script_provenance` covers empty, repeated and mixed arguments in both
backends; existing TeX and expl3 goldens also pass.

## TeX case shifting retains direct source flags — 2026-11-11

**Status:** FIXED for the tested case-shift source mappings. **Full script
TeX-port blocker:** no for those forms. Native TeX is unaffected.

For `\begin{document}\uppercase{\textbf{Hi}}\end{document}`, the native
AST reports the `textbf` source offset at the last main-file token read by
the case operation, while the script previously used the command's original
position. Native `case_shift` backs up a stored copy of the scanned token list,
and `copy_stored` clears direct-source flags; the script shifted the list and
pushed it without clearing them. The fix stores the resulting tokens before
pushing them, preserving TeX token values and category codes while marking their
source as synthetic. The stream also tracks the main-file read position used
for synthetic formatting tokens.
`test_tex_script_provenance` compares upper and lower case, nested formatting
and zero mappings to native in both backends.


## Script LaTeX profile cannot load lmdnotes counter declaration — 2026-11-11

**Status:** FIXED for counter expansion in the local-package fixture. The
fixture's synthesized class declaration still blocks script AST digestion in
the separate source-mapping issue below; the native production path remains
available.

Run `profile.expand_with_kernel` with the source from
`test/lambda/latex/fixtures/local_packages/document.tex`, authorized local
snapshots of `lmdreport.cls` and `lmdnotes.sty`, and an empty bundled snapshot.
The script result previously stopped at `newcounter` in `lmdnotes.sty`; see
`../lambda-input/temp/expl3-script/provenance-existing-fixture.out`. The script
engine now implements register-backed declarations, global set/add/step,
child resets, `value` and the tested counter formats. The installed
`test_tex_script_counters` passes interpreter and JIT and compares with native
and pdfLaTeX, keeping input LI-014's two intentional native divergences visible.
`test_tex_script_counters_fixture` confirms expansion reaches both exercise
numbers; AST digestion still reports the class source-mapping issue.
`refstepcounter` currently implements numeric step/reset only; reference-label
state remains unfinished.


## Foreign generic command offsets use package positions — 2026-11-11

**Status:** OPEN — foreign-source argument mapping is unimplemented.
**Full script TeX-port blocker:** yes, when loaded package files emit generic
commands. Native production parsing is unaffected.

A local package containing `\mystery{Package}` and loaded by
`\usepackage{foreign-output}\begin{document}Hi\end{document}` produces
a native `<mystery>` whose `source_offset` and required-argument boundaries
map to main-file offset 27. The staged script expansion/digestion instead
returned the package-file command and group offsets 54, 62 and 71. Reproduce
with `../lambda-input/temp/expl3-script/provenance-foreign.ls` and its captured
local files under `../lambda-input/temp/expl3-provenance/`; evidence is in
`provenance-foreign-generic.out` there. The command tokens are direct relative
to their package file but their emitted bytes are synthetic relative to the
main document. The staged digester now reports foreign generic commands as
unsupported until reconstructed output spans and main-file offset mapping are
available. This is related to the expanded parenthesized-argument issue.


## Script TeX writes bypass terminal line buffering — 2026-11-11

**Status:** FIXED for terminal writes and `\showtokens` in the script engine.
Output-file streams remain separately OPEN below.

Previously `\immediate\write16{}` recorded an empty message, while native
TeX flushes no line. With `\newlinechar=64\immediate\write16{A@B}`, native
records `A` and `B` separately while the script recorded `A@B`. The cause was
that `write_message` directly appended `token_text` instead of honoring
`newlinechar` and native flush rules. The script now buffers terminal output,
only splitting newly printed text using the current newline character; writes
and `\showtokens` flush around their output. The installed
`test_tex_script_message_pending` compares empty writes, line breaks, changes to
`newlinechar`, and interleaved message/write/show operations against native in
interpreter and JIT. The earlier reproduction is in
`../lambda-input/temp/expl3-script/message-old-interp.out`.


## Script TeX message primitive is unsupported — 2026-11-11

**Status:** FIXED for terminal output and string diagnostic codes; structured
native diagnostic metadata remains separately OPEN. **Full script TeX-port
blocker:** this primitive no longer blocks the tested message cases.

Previously `\message{hello}` returned `tex-unsupported-primitive`, while native
`parse("\\message{hello}", {type:"tex"})` appended `hello` to its messages.
The registered `\errmessage` primitive also had no execution branch. Both now
expand their arguments and use the shared terminal buffer, including native
spacing and flush behavior; `\errmessage` adds `tex-errmessage` to script
diagnostic codes. Installed native comparisons cover multiple messages, empty
messages, macro and counter expansion, newline changes, Unicode, and error
lines; they pass in interpreter and JIT. Forced-GC tests pass both tiers.
Structured diagnostics and output-stream state remain separately OPEN.


## Echoed TeX source spans lose token sequence boundaries — 2026-11-11

**Status:** FIXED for sequence-boundary retention. The general reconstruction
and source map remain OPEN; native production parsing is unaffected.

`\def\x{A}% comment\nB` emits an echoed `source-span` followed by `B`.
The original reader tokens have consecutive sequence numbers, but the span
only retains byte offsets and drops its first and last sequence. Native output
reconstruction uses those sequence boundaries to preserve intervening source
bytes; without them, the script cannot correctly tell whether the comment and
newline belong in the reconstructed output. `stream.end_trace` computed the
boundaries but did not return them. It now returns the first and last sequence,
and echoed definition spans retain them. The installed stream and trace-span
tests cover adjacent comments, lookahead, and macro output in interpreter and
JIT; forced-GC tests also pass. Evidence is in
`../lambda-input/temp/expl3-script/trace-span-interp.out`.


## Script TeX output does not retain emission origins for all tokens — 2026-11-11

**Status:** OPEN — the general emitted-output provenance contract is incomplete.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed
source offsets. Native production parsing is unaffected.

For `\def\m{\quad}\begin{document}A\m B\end{document}`, native
reconstruction inserts a space between the expanded control word and `B`. Its
separator is mapped to the main-file read position recorded at emission. The
script's direct `B` output token has no emission-position field; synthetic
class declaration tokens also have no recorded main-file origin. Existing
`read_offset` covers selected synthetic tokens processed by `step`, not all
emission paths. A temporary reconstruction prototype therefore reports
`tex-reconstruct-missing-origin` at this separator rather than guessing.
Evidence: `../lambda-input/temp/expl3-script/reconstruct-tests-interp.out` and
`counter-fixture-prefix.out` in the same directory. Native `emit_token` and
`emit_text` record `last_main_offset` per output item; the script needs the
same contract, including output emitted after lookahead or by package helpers.


## Foreign RequirePackage declaration is echoed instead of synthesized — 2026-11-11

**Status:** FIXED for selecting the echoed or synthesized declaration.
Reconstructed source gaps and full offset mapping remain separately OPEN.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, the captured
`lmdnotes.sty` contains `\RequirePackage{xcolor}`. Native input emits the
synthesized declaration `\usepackage{xcolor}`, while the script emits the
foreign direct `RequirePackage` token and its package-file arguments. Previously the
script's `latex_usepackage` emitted original tokens whenever the plan had no
local loads, without checking whether a main-file source echo was valid. Native
`do_usepackage` uses `echo_finish`, which rejects foreign source reads, and
otherwise emits a synthesized declaration. The script now traces and validates
the echo, synthesizing the declaration when tracing fails. Installed native
comparisons cover direct and macro-generated commands and names, options,
duplicates, empty names, and the local-package fixture; they pass interpreter
and JIT, as do focused forced-GC checks. The original reproduction is in
`../lambda-input/temp/expl3-script/reconstruct-fixture-interp.out`.

## Echoed package declarations lose their consumed source gap — 2026-11-11

**Status:** OPEN — the script emits the declaration's explicit tokens but
omits whitespace consumed by its optional-version lookahead. **Full script
TeX-port blocker:** yes, for native reconstructed text and offset parity.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, native output
preserves the newline after the direct main-file `\usepackage{amsmath}` before
a subsequently synthesized declaration. The script emits the command and its
argument tokens, while `optional_argument` consumes the following newline;
the resulting output items no longer contain that source gap. Native
`echo_finish` emits one source span extending through the consumed bytes.
A temporary reconstruction with matching empty adapter configuration shows the
missing newline in `../lambda-input/temp/expl3-script/reconstruct-fixture-aligned.out`.
The same prototype also lacks the raw trailing source after `\end{document}`;
that trailing-source behavior needs to be carried into reconstruction.


## Script scantokens pseudo-source is unavailable to reconstruction — 2026-11-11

**Status:** FIXED for pseudo-source retention. General reconstructed output
and source mapping remain OPEN. Native production parsing is unaffected.

For `\begin{document}\scantokens{\textbf{Hi}}\end{document}`, native
expanded text contains `\textbf {Hi}` while a temporary script reconstruction
emits `\textbf{Hi}`. Native converts the scanned token list to a pseudo-file,
registers it in the engine's source list, and preserves the skipped space between
adjacent direct pseudo-file tokens. The script previously created its reader with file id -1 and did not retain the
source, preventing reconstruction from reading the gap. It now registers a
unique source for each `\scantokens` reader. Installed tests cover multiple and
nested calls and Unicode byte offsets; both backends and forced-GC checks pass.
The experimental reconstruction now matches native for this case and all 29
provenance text cases in interpreter and JIT. Evidence:
`../lambda-input/temp/expl3-script/reconstruct-scan-probe.out` and
`reconstruct-provenance-fixed-interp.out`.


## Map spread loses lookup for an embedded-NUL key — 2026-11-11

**Status:** FIXED in Lambda runtime source and verified with an isolated debug
executable. The existing shared executable was not overwritten.

Previously `k = chr(0)`, `m = map([k, 12])`, `n = {*:m}` gave `m[k] == 12`
but `n[k] == null`; `for (k, v in m)` also yielded null for that value. VMap
iteration and spread reified a length-bearing key as a symbol and then called
`vmap_get_by_str`, which derives the length with `strlen`. The shared
`vmap_get_by_symbol` now preserves embedded-NUL keys through length-aware
lookup while retaining the existing synthetic numeric-key path for ordinary
names (S8.3.1, S2.3.1). Installed `test/lambda/vmap_embedded_nul` covers leading and internal
NUL, iteration, direct and nested spreads, ordinary keys and numeric-key
projections. It passes interpreter and JIT with forced GC and freed-memory
poisoning; seven existing VMap/map suites pass both backends. The isolated
reconstruction prototype also now matches native on the NUL diagnostic cases.
Evidence: `../lambda-input/temp/expl3-script/nul-map-fix/`.


## CLI result output truncates at embedded NUL — 2026-11-11

**Status:** OPEN — result display truncates. **Full script TeX-port blocker:**
not for evaluation itself; it prevents reliable CLI inspection and golden
output when a TeX result contains character code zero.

Run a script containing `"before"; "a" ++ chr(0) ++ "b"; "after"` in either
interpreter or JIT. The process exits successfully but stdout contains only
`"beforea`, with the remaining result missing. A separate numeric inspection
confirms the string has length 3, character codes 0 and 98 at its last two
positions, and compares equal to itself. The CLI result path in
`lambda/main.cpp` prints the length-bearing result buffer using `printf("%s", ...)`,
which truncates at a NUL; whether the item formatter should escape that NUL in
the textual representation also needs verification. Evidence:
`../lambda-input/temp/expl3-script/nul-print-interp.out`, `nul-print-jit.out`,
and `nul-print-check.out`. This is independent of the TeX engine.


## Script TeX inputlineno quantity is unsupported — 2026-11-11

**Status:** FIXED for the tested physical, pseudo-file, and local-package lines.
Structured diagnostic line metadata remains separately OPEN. Native production
parsing is unaffected.

Previously `Line 1\n\the\inputlineno/\number\inputlineno` stopped with
`tex-unsupported-primitive`, while native emitted `2/2`. The reader now tracks
physical lines per reader, and both `\the` and numeric scans read the active
reader's line number. Installed native comparisons cover CRLF, blank/comment
lines, signed scans, macro calls, multiline expansion, nested `\scantokens`,
and a local package returning to the main file. They pass interpreter and JIT,
as do affected regression and forced-GC checks. Original evidence:
`../lambda-input/temp/expl3-script/inputlineno-probe.out`.


## Script TeX character primitive is unsupported — 2026-11-11

**Status:** PARTIALLY FIXED — scalar values and invalid-range diagnostics are
implemented; surrogate code points remain explicitly unsupported. **Full script
TeX-port blocker:** yes, for surrogate values. Native parsing is unaffected.

Previously `\char65\char36` returned `tex-unsupported-primitive` for `char`,
while native reconstructed `A\$`. The script now scans and emits character
tokens with native categories, keeps the main-read position, and reports
`tex-bad-char` for numbers outside the native range. Installed tests cover
ASCII, Unicode scalars, zero, invalid ranges, registers, aliases, numeric forms,
macros, and special characters in interpreter and JIT; forced-GC checks pass.
Surrogate code points are explicitly rejected with `char surrogate`: native
retains such tokens while Lambda strings cannot encode them. The regression
test keeps this limit visible. Evidence:
`../lambda-input/temp/expl3-script/char-complete-interp.out`.


## Script TeX stops after an extra closing brace — 2026-11-11

**Status:** OPEN — script error recovery differs from native. **Full script
TeX-port blocker:** yes, for malformed documents where native continues after a
diagnostic. Native production parsing is unaffected.

Expand `A}B`: the script stops with `tex-extra-brace` after emitting `A`.
Native reports the same diagnostic and continues, producing `A}B ` (including
the reader's final space). The script treats this diagnostic as a terminal
`issue`; native `handle_right_brace` emits the extra token after reporting it.
Evidence: `../lambda-input/temp/expl3-script/brace-recovery-probe.out`.
The same difference occurs for an extra `\endgroup` and mismatched group
types. A staged fix matches native output and diagnostic codes on seven cases,
but the existing `test_tex_script_expansion.txt` explicitly expects a terminal
`tex-extra-endgroup` for `{\endgroup}`. The supplied checkout instructions
require preserving reference results; the staged change is not installed and
that failing comparison remains visible in
`../lambda-input/temp/expl3-script/group-recovery-existing-test.out`. Structured
diagnostic metadata remains a separate OPEN issue.


## Script digester rejects active-character definition targets — 2026-11-11

**Status:** OPEN — echoed definitions of active characters lack a digester
branch. **Full script TeX-port blocker:** yes, for documents defining active
characters when definition echo is enabled. Native parsing is unaffected.

For `\def~{XYZ}\begin{document}A~B\end{document}`, expansion succeeds, but
script digestion returns `latex-digest-unsupported` for `definition target`.
Native LaTeX retains the definition and expands the document text to `AXYZB`.
The staged active-token text fix does not handle definition targets. Evidence:
`../lambda-input/temp/expl3-script/active-digest-interp.out`. The existing
`test/lambda/latex/test_tex_script_active_digest.txt` explicitly expects the
unsupported result; preserving existing references blocks promoting a change
that alters that result without an owner ruling.


## Script LaTeX digester rejects emitted active characters — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary emitted active text is supported;
active characters that reconstruct to LaTeX syntax remain explicitly unsupported.
**Full script TeX-port blocker:** yes for those syntax characters. Native parsing
is unaffected.

Previously `\begin{document}A~B\end{document}` produced
`latex-digest-unsupported` for an active token while native produced paragraph
text `A~B`. The script now accepts emitted active text and accounts for active
letters after spacing commands. Installed native-AST tests cover tildes,
macros, custom active punctuation and letters, and spacing in interpreter and
JIT; forced-GC checks pass. Active `%`, `$`, and other syntax characters are
explicitly rejected because treating them as plain text produces an incorrect
AST; `active-syntax-probe.out` in `../lambda-input/temp/expl3-script/` shows
the native error. Active definition targets remain separately OPEN.


## Script math digester lacks common style commands — 2026-11-11

**Status:** FIXED for the tested style-command arguments and `\in` symbol.
Broader math support and general reconstruction remain OPEN. Native parsing is
unaffected.

Previously the local-package fixture reached `latex-digest-unsupported` for
`math command mathbb`. The script now emits native-compatible `style_command`
nodes for 12 common style commands and handles `\in`. Sixteen installed
native-AST comparisons cover braced, unbraced, nested, and scripted arguments
in interpreter and JIT; forced-GC checks pass. An isolated reconstruction and
reparse of the fixture now matches native AST content and mapped offsets in
both tiers, while root diagnostics/package metadata remain separately OPEN.
Evidence: `../lambda-input/temp/expl3-script/reparse-fixture-remapped.out`.


## Script math digester rejects repeated scripts — 2026-11-11

**Status:** OPEN — repeated scripts are not represented by the script math
digester. **Full script TeX-port blocker:** yes for native AST parity on these
expressions. Native parsing is unaffected.

For `\begin{document}$x^2^3$\end{document}`, the script reports
`latex-digest-unsupported` for `repeated math script`; native produces a
superscript group containing `23`. The existing `test_tex_script_digest`
golden records this case as pending. Evidence:
`../lambda-input/temp/expl3-script/reparse-digest-v2-interp.out`. This remains
a visible unsupported case; no reference result has been altered.


## Script digester rejects bracket text after unknown environment start — 2026-11-11

**Status:** OPEN — the generic environment guard rejects a leading bracket.
**Full script TeX-port blocker:** yes for documents using this syntax. Native
parsing is unaffected.

For `\begin{document}\begin{strange}[x]Hello\end{strange}\end{document}`,
the script reports `latex-digest-unsupported` for `environment options`;
native places `[x]Hello` in the `strange` environment's body. The existing
`test_tex_script_digest` golden records this case as pending. Evidence:
`../lambda-input/temp/expl3-script/reparse-digest-v2-interp.out`. This remains
a visible unsupported case; no reference result has been altered.


## Script digester rejects leading bracket text in document — 2026-11-11

**Status:** FIXED for the document environment; generic environment brackets
remain separately OPEN. Native parsing is unaffected.

Previously `\begin{document}[A]\end{document}` returned
`latex-digest-unsupported` for `environment options`, while native treated
`[A]` as body text. The script now applies the option guard only to other
environments. Installed native comparisons cover direct, repeated, nested,
followed-by-group and macro-generated brackets in interpreter and JIT. The
full existing digest golden passes both tiers, and the reconstructed-path corpus
now matches 101 native ASTs with two explicitly unsupported cases. Original
evidence: `../lambda-input/temp/expl3-script/document-bracket-probe.out`.


## Experimental reconstructed-text digestion drops NUL characters — 2026-11-11

**Status:** FIXED in the isolated reconstruction prototype using the VMap
embedded-NUL key fix; general reconstruction remains uninstalled and OPEN.
**Full script TeX-port blocker:** yes, before that path can produce native ASTs
for documents containing character code zero. Production parsing is unaffected.

The prototype reconstructs `\begin{document}\char-1\end{document}` and
retokenizes the resulting text with the TeX reader. Native LaTeX digestion
retains the emitted NUL character, while the TeX reader's default category
for NUL ignores it; both `tex-bad-char` metadata and a successful prototype
AST are then present, but AST equality is false. The installed direct-token
script digester matches native on this example. Evidence:
`../lambda-input/temp/expl3-script/reparse-diagnostics-interp.out`. The
prototype must use LaTeX digester lexical semantics or explicitly reject cases
it cannot preserve; it must not be connected to production in its current form.


The tokenizer's explicit NUL category now survives the corrected map spread,
and the prototype matches native ASTs for invalid `\char` and mixed supported
diagnostics in interpreter and JIT with the isolated fixed executable. Evidence:
`../lambda-input/temp/expl3-script/nul-map-fix/reparse-diagnostics-interp.out`.


## Synthesized class declaration has no source span — 2026-11-11

**Status:** OPEN — expanded-output reconstruction is missing. **Full script
TeX-port blocker:** yes, for the existing local-class document fixture.
The native production parser is unaffected.

With the staged counter implementation, `profile.expand_with_kernel`
completes expansion of `test/lambda/latex/fixtures/local_packages/document.tex`
with authorized `lmdreport.cls` and `lmdnotes.sty` snapshots. Digestion stops
with `latex-digest-unsupported`, `declaration source mapping`. The class
loader emits a synthesized `\documentclass{article}` whose tokens have no
source file, span, or main-file read offset; `group_raw` therefore cannot
construct the native declaration's argument descriptor. Reproduce with
`../lambda-input/temp/expl3-script/counter-fixture-prefix.ls`; the observed
tokens and issue are in `counter-fixture-prefix.out`. The general output
reconstruction and source-segment mapping must cover generated declarations
as well as foreign generic commands and expanded parenthesized arguments.


## Generic argument end offsets ignore reconstructed segment boundaries — 2026-11-11

**Status:** FIXED for the tested direct-argument boundaries. The native
production path is unaffected; general expanded-output reconstruction remains
open.

In `\begin{document}\newcounter{p}\newcounter{c}[p]\stepcounter{p}\arabic{c}\end{document}`,
the direct `stepcounter` argument ends at source offset 62, but native maps
its reconstructed-output boundary to offset 71, the next synthesized token's
main-file read position. The script previously returned 62. An empty macro
between a direct command argument and the next direct token produces the same
class of mismatch: reconstruction maps the boundary to the next nonadjacent
source segment. Evidence and native AST comparisons are in
`../lambda-input/temp/expl3-script/counter-ast-detail.out` and
`counter-boundary-probe.out`. The digester maps a boundary to the next
synthetic token's read offset, or to the next nonadjacent direct token's
position, and keeps the original end for adjacent direct segments. The installed
`test_tex_script_counters` covers direct, empty-expansion, synthetic,
parenthesized and optional boundaries in both backends. More general
expanded-output source reconstruction remains open.


## Script digester omits native TeX diagnostic metadata — 2026-11-11

**Status:** OPEN — metadata representation and propagation are incomplete.
**Full script TeX-port blocker:** yes, for AST parity when TeX reports
diagnostics. Native production parsing is unaffected.

For `\begin{document}\newcounter{n}\setcounter{n}{1\relax A}\arabic{n}\end{document}`,
native LaTeX parsing attaches `tex_diagnostics` containing a
`latex-counter-value` diagnostic and a `tex_packages` attribute to the root
element. The staged script expansion records the diagnostic code in its
state, but `digest.digest` returns an AST without those attributes; full AST
equality fails even when the document children agree. Reproduce using
`../lambda-input/temp/expl3-script/counter-relax-detail.ls`; the output is
recorded in `counter-relax-detail.out`. The script engine currently keeps only
string diagnostic codes in several paths, so reproducing native structured
messages and source positions requires a general diagnostic representation
and mapping rather than adding empty attributes.

For clean documents that loaded local packages, the script digester now attaches
`tex_diagnostics: []` and the ordered local `tex_packages` list. The installed
`test_tex_script_package_metadata` compares a real package fixture and a document
without packages against native in interpreter and JIT, and focused forced-GC
checks pass. The experimental reconstructed path now achieves full native AST
equality on the real local-class fixture when diagnostics are empty. Structured
diagnostics and their file/line/offset metadata remain OPEN for other codes.

The script now also records complete native-compatible `tex-errmessage` entries
for messages up to 511 UTF-8 bytes without NUL, including main-file,
`\scantokens`, and local-package source locations. The digester attaches them
only when every diagnostic has a complete record. Installed tests compare
messages, offsets, filenames, lines, and ASTs in interpreter and JIT, with
forced-GC checks. Mixed diagnostic codes and oversized messages remain visibly
incomplete in `test_tex_script_errmessage_diagnostics`; see
`../lambda-input/temp/expl3-script/errmessage-structured-qualified-interp.out`.

Out-of-range `\char` now also records the native `tex-bad-char` message and
location. Installed comparisons cover negative and oversized values, multiple
errors, and mixed `tex-bad-char`/`tex-errmessage` ASTs; both backends and
forced-GC checks pass. Other diagnostic types remain OPEN.


## Script TeX output streams are unsupported — 2026-11-11

**Status:** FIXED for the existing native input engine's modeled stream behavior.
Both engines discard written file contents; actual filesystem output is not
implemented by this compatibility model.

Previously `\immediate\openout0=unused.txt \immediate\write0{secret}`
stopped with `tex-unsupported-primitive` for `openout`. The script now scans
filenames, tracks the 16 output stream slots, implements `openout` and
`closeout` (including their immediate and aliased forms), and suppresses
terminal output for writes to open streams. The installed
`test_tex_script_output_stream` compares 12 stream, grouping, number-wrapping,
filename, and terminal-buffer cases with native in interpreter and JIT. Focused
forced-GC tests pass in both backends. The original reproduction is in
`../lambda-input/temp/expl3-script/stream-output-probe.out`.

## Script class and counter echoes omit consumed source bytes — 2026-11-11

**Status:** OPEN — the installed script emits the explicit tokens of direct
main-file class and counter declarations, but does not retain native echo spans.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed ASTs
and source offsets. Native production parsing is unaffected. S1.8 and D10.2.2
cover the data and input boundary.

In an isolated reconstruction prototype, `\documentclass{article}` followed
by a newline, comment or spaces produces a different AST from native because
optional-version lookahead consumed the gap. `\newcounter{n}` followed by a
newline similarly loses bytes consumed by optional-parent lookahead. The native
`echo_finish` emits the traced source span, including these consumed bytes.
The prototype also explicitly reports missing origins for macro-generated
`\stepcounter`, `\setcounter`, and `\chardef` output. Evidence, including
interpreter/JIT parity: `../lambda-input/temp/expl3-script/reparse-origin-interp.out`
and `reparse-origin-jit.out` in the same directory. These findings extend the
separately recorded package-gap and emission-origin issues; the prototype is
not installed as a production path.

## Reconstructed LaTeX comments can start an empty paragraph — 2026-11-11

**Status:** OPEN — the isolated script reconstruction and reparse prototype
cannot yet represent every comment preserved in emitted TeX source. **Full
script TeX-port blocker:** yes, for native-compatible ASTs. Native production
parsing is unaffected. S1.8 and D10.2.2 apply to the data and input boundary.

For `\documentclass{article}%x` followed by a newline and
`\begin{document}A\end{document}`, the native AST contains an empty paragraph
between the class declaration and document. The experimental reparse feeds
reconstructed bytes to the TeX reader, which discards `%x` and its line ending
and therefore silently omitted that paragraph. The prototype now explicitly
reports `latex-digest-unsupported` / `preserved comment` when skipped reader
gaps contain `%`; this conservative guard can reject cases whose AST would
otherwise match. Native examples in document content and command arguments are
recorded in `../lambda-input/temp/expl3-script/native-comment-tests.out`; the
original mismatch and guarded outcomes are in `reparse-origin-v2-interp.out`
and `reparse-origin-tests-guard-v2-interp.out` in that directory. Interpreter
and JIT agree on the focused outcome. A LaTeX-source-aware comment representation
is needed before this guard can be removed.

## Script LaTeX declarations omit trailing optional groups — 2026-11-11

**Status:** FIXED for adjacent brace and bracket groups on declarations.
Native production parsing is unaffected. S1.8 and D10.2.2 apply to the input
boundary; the broader script TeX migration remains incomplete.

For `\documentclass[11pt]{article}[2020/01/01]` followed by a document, native
places the trailing optional group in `documentclass.argument_groups` and a
`brack_group` child; the isolated reconstruction and script digest instead
emit it as paragraph text. Reproduced in both execution backends by
`../lambda-input/temp/expl3-script/reparse-origin-more.ls`, with results in
`reparse-origin-more-interp.out` and `reparse-origin-more-jit.out` beside it.
The declaration parsing branch in `lmd/package/doc/latex/tex_engine/digest.ls`
reads only the initial optional and required groups; the trailing group needs
native-compatible general argument handling.

The script digester now extends declarations through adjacent required and
optional groups, retaining group descriptors and native argument packing;
whitespace and parentheses end the declaration's group sequence. New
`test/lambda/latex/test_tex_script_declaration_groups.ls/.txt` compares ten
cases with the native AST. The new test and existing digest and provenance
goldens pass interpreter and JIT; the declaration suite also passes against
the installed package in both tiers. The separate reconstructed-comment
limitation and general emission provenance remain OPEN.

## Script LaTeX environment recognition ignores command-to-brace spaces — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary whitespace between environment commands
and braces now follows native parsing; comment gaps and other malformed or
unmatched cases can still be explicitly unsupported. **Full script TeX-port
blocker:** yes, for remaining AST parity. Native production parsing is
unaffected. S1.8 and D10.2.2 govern the data and input boundary.

Reproduction `\begin {document}\begin {quote}A\end {quote}\end {document}`
produces nested `document` and `quote` nodes in the experimental reconstructed
script AST, but four paragraph nodes in native parsing. The script digester's
`blocks` branch recognizes a control token followed by a group without checking
the raw gap. Both backends reproduce the difference in
`../lambda-input/temp/expl3-script/reparse-environment-origin-interp.out` and
`reparse-environment-origin-jit.out`. The same audit found that macro-generated
`\end{quote}` and `\end{document}` are rejected by the prototype as missing
emission origins, consistent with the separately recorded general provenance
issue. Macro-generated starts and an unmatched math definition in the test
produce native parse errors, so those cases are not evidence of script parity.



The script digester now preserves whitespace before a spaced environment
argument as paragraph content, rather than treating the command as a boundary.
`test/lambda/latex/test_tex_script_environment_gaps.ls/.txt` covers spaces,
tabs, newlines, nesting, ordinary adjacent environments, and an unclosed
environment. The installed suite and existing digest, provenance and declaration
goldens pass interpreter and JIT. Unsupported comment gaps and unmatched
endings remain visible, and the separate general provenance issue remains OPEN.

## Cloud-sync isolated-host baseline qualification — 2026-11-12

**Status:** OPEN qualification residue; this report establishes no new shared
root cause. The cloud phase changes only Lambda Script, tests, deployment plumbing
and docs. Its isolated worktree starts at `dc186e3d52`; native execution uses the
unchanged copied main-checkout host. D6.3.3 / D11.1.2 remain applicable.

The broad script golden gate is **2,279/2,284**, with five failures:
`binary_js_bridge`, `image_io_formats`, `input_markdown_emoji`,
`vmap_embedded_nul` and `latex_test_tex_script_expl3`. Rechecking those same five
from the main checkout with the identical executable passes the first three;
the VMap embedded-NUL and TeX package-echo mismatches reproduce there. The first
three therefore depend on the isolated checkout's resources/runtime environment;
the exact resource differences remain unconfirmed. No affected engine, input
source, golden or test harness was modified to mask these results. The VMap and
TeX findings have their earlier reports; this is evidence for the existing host,
not a requalification of their later source fixes.

The selected sync scope separately passes **51/51** fixtures on interpreter,
AUTO and JIT, plus **9/9** focused every-allocation GC/poison cases per tier.
The additional combined cloud/peer process-exit stall was reported separately as
input **LI-016**. Its 2026-11-12 owner fix removes quadratic context-list unlinking;
the rebuilt host passes the full 17-case gate on interpreter, AUTO and JIT with
that case mandatory and the original deadline unchanged. Original failed receipts
remain available. Existing LI-015 still limits concurrent administrative writes.

Fresh native follow-up (2026-11-12): `make test-lambda-baseline` completes all forty
suites at **7,493/7,494**. Only the existing TeX/expl3 package golden mismatch
remains; the VMap case passes with the rebuilt host. This does not attribute the
earlier VMap source fix to the context-list change. Current identities and
qualification are in the [completed cloud implementation record](<impl/Lambda_Impl_Sync_Cloud (done).md>).

Evidence in the Lambda checkout:
`temp/sync-cloud/worktree/temp/sync-cloud/script-baseline.log`,
`baseline-failure-excerpts.log`, `main-recheck.log`, `sync-normal-*.log` and
`focused-stress-*.log`. Native host SHA and script input identities are preserved
in the cloud implementation record. Main-checkout merge state was left untouched.

## Script TeX digestion and reconstruction follow-up — 2026-11-12

The script inline digester now consumes the `\end` command itself while
retaining its following group and whitespace, matching the native direct
parser's `parse_command` handling. The installed
`test/lambda/latex/test_tex_script_definition_end.ls/.txt` compares seven
macro-definition examples, including nested and spaced forms, with native.
The new test passes installed in interpreter and JIT; existing digest,
provenance, declaration and environment goldens also pass both backends.

The separately recorded document-tail fix cannot yet be installed without changing the
existing `test_tex_script_expansion.txt` expectation: its `latex document hooks`
case asserts the old `TAIL` value. The checkout instructions require preserving
reference results; the native-matching change stays in the temporary prototype.

The isolated reconstruction prototype now represents preserved top-level
comments immediately before block commands, including empty paragraphs and
whitespace before a comment. Eight focused examples match native in interpreter
and JIT (`../lambda-input/temp/expl3-script/reparse-comment-tests-v2-interp.out`
and `reparse-comment-tests-v2-jit.out`). It still rejects comments in groups,
math, and argument gaps, and has not been installed. This narrows but does not
close the earlier reconstructed-comment issue.

## Script TeX passes unexpanded environment names through output — 2026-11-12

**Status:** FIXED for the tested computed environment names and emitted
closings. The full script TeX port remains incomplete; native production
parsing is unaffected.
S1.8 and D10.2.2 govern the data and input boundary.

For `\def\ename{quote}\begin{document}\begin{\ename}A\end{\ename}\end{document}`,
the script computes `quote` for its own environment state but emits the raw
`\ename` argument. Native expansion instead emits `\begin{quote}` and
`\end{quote}` when direct source echo cannot represent the expansion.
Macro-generated `\end` likewise requires synthetic output at its emission
position. Native emission uses source spans only for valid direct echoes.
The staged script records those spans and emits computed forms with the
current main-source offset. Evidence: six native text comparisons in
`../lambda-input/temp/expl3-script/envstage-env-output-interp.out` and its JIT
counterpart; existing expansion, profile and provenance goldens pass in both
backends. The separate main-file tail issue remains OPEN and its existing
golden is preserved.

The script now echoes an unchanged package declaration only when every named
package remains in the passthrough set. Script-handled `expl3` is consumed,
while mixed foreign, adapter and local packages retain their proper output.
`test/lambda/latex/test_tex_script_expl3_package_echo.ls/.txt` covers eight
cases. The previously failing `test_tex_script_expl3` golden and the new test
pass against the installed package in interpreter and JIT.

Environment echo spans and synthetic expanded names are now in the installed
script engine. Six native expanded-text comparisons are covered by
`test/lambda/latex/test_tex_script_environment_output.ls/.txt` and pass in both
backends. Existing expansion, profile, provenance, counters, package metadata
and related digest goldens pass in both backends. Small focused tests for both
changes pass with every-allocation GC and freed-memory poisoning in interpreter
and JIT (`../lambda-input/temp/expl3-script/env-package-gc-{interp,jit}.out`).
The independent trailing-source and full source-mapping issues remain OPEN.

## Script LaTeX digester rejects standalone math groups — 2026-11-12

**Status:** FIXED for supported standalone math groups. The full script TeX
port and other math constructs remain incomplete. Native production parsing is
unaffected. S1.8 and D10.2.2 apply to the document and input boundary.

For `\begin{document}${a+b}$\end{document}`, native creates a `group` inside
the `inline_math` AST, while the script digester returns
`latex-digest-unsupported` / `math atom`. Empty, nested, scripted and display
math groups fail similarly. `math_atoms` handles groups as arguments to
commands and scripts but has no branch for a group used as an ordinary atom.
Eight reproductions and native ASTs are in
`../lambda-input/temp/expl3-script/math-group-before-interp.out` and the JIT
counterpart. The independently tracked repeated-script issue is unaffected.

The script `math_atoms` now uses the existing group parser and applies any
following scripts to the resulting group. The installed
`test/lambda/latex/test_tex_script_math_groups.ls/.txt` compares eight empty,
nested, scripted, adjacent, radical and display groups with native; it passes
interpreter and JIT. Existing digest, underline, active-text and definition
goldens pass both tiers. The reconstruction prototype also matches four native
ASTs for comments inside those groups; it remains uninstalled.

The reconstructed-comment prototype now retains supported comment whitespace,
handles comments in tested math formulas, and explicitly rejects unqualified
argument/definition gaps. After these changes its prior 103-case corpus matches
100 native ASTs, with repeated scripts, unknown environment options and a
multiple-comment argument gap explicitly unsupported; see
`../lambda-input/temp/expl3-script/reparse-digest-tests-mathcomments-interp.out`
and its JIT counterpart. Further malformed-input, provenance and performance
qualification is required before production use.

## Script TeX rejects native pass-through and penalty primitives — 2026-11-12

**Status:** FIXED for the listed pass-through, penalty and unskip primitives.
Other TeX operations and downstream math digestion remain incomplete. Native
production is unaffected. S1.8 and D10.2.2 govern the document and input
boundary.

`\begin{document}\noindent!\end{document}` terminates script expansion with
`tex-unsupported-primitive` for `noindent`, while native emits the command
and continues. The same happens for `indent`, fill/filneg variants, alignment
markers, discretionary hyphen and italic correction; `penalty`, `unpenalty` and
`unkern` are also unsupported. Native `do_typesetting` passes the first group
as tokens, scans and discards a penalty's integer, and ignores the last two.
Eight reproductions in both backends are recorded in
`../lambda-input/temp/expl3-script/typesetting-primitives-before-interp.out`
and its JIT counterpart. This is about preserving native expansion behavior;
layout remains Radiant's responsibility under D10.2.2.

The same primitive audit found `\mathord`, `\displaystyle`, `\limits`,
`\left`, `\over`, and their native `P_MATH_EMIT` siblings are also rejected
by the installed script engine. Native expansion emits these commands as tokens;
it does not consume their math structure at this stage. The reproductions and
native expanded text are in `../lambda-input/temp/expl3-script/typeset-all-before-interp.out`
and the JIT counterpart. A staged implementation follows the shared native
pass-through branch, with downstream math AST support tracked separately.

The script now emits the same pass-through token families as native, scans and
discards penalties, and implements `\unpenalty`, `\unkern` and `\unskip`
with native output-token behavior. The new `test_tex_script_typesetting_tokens`
`.ls/.txt` compares 18 expansion cases with native, including
math commands, aliases, numeric expressions and whitespace removal. The new
suite passes installed in interpreter and JIT; existing expansion, profile,
expl3 and environment-output goldens pass both tiers. This does not claim
downstream LaTeX AST support for every emitted math command.

## Script math digester rejects style declarations and operatorname — 2026-11-12

**Status:** FIXED for the four style declarations and supported `operatorname`
forms. Other math syntax and source-provenance cases remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After native-compatible math token emission, `\begin{document}$\displaystyle x+1$\end{document}`
reaches script digestion but returns `latex-digest-unsupported` /
`math command displaystyle`. Native emits a `style_command` node without an
argument. The other three math style declarations and `\operatorname` are
also unsupported, including the starred operator form. Six comparisons and
native ASTs are in `../lambda-input/temp/expl3-script/math-declarations-before-interp.out`
and its JIT counterpart. This is a digester issue independent of the newly
ported TeX token pass-through.

## Script math digester rejects ordinary punctuation atoms — 2026-11-12

**Status:** PARTIALLY FIXED for parentheses, brackets, comma, colon and vertical
bars. Other punctuation and multi-character relations remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After staging support for `\operatorname`, the formula
`\begin{document}$\operatorname{foo}(x)$\end{document}` still returns
`latex-digest-unsupported` / `math atom` when the script digester encounters
`(`. Native creates `punctuation` nodes for the parentheses. The script's
`math_atoms` handles letters, numbers, a few operators and relations, but no
ordinary punctuation branch. Both backends reproduce this in
`../lambda-input/temp/expl3-script/math-declarations-after-interp.out` and
its JIT counterpart. Other punctuation and multi-character relation semantics
need individual validation rather than assuming all characters are alike.

## Script math digester rejects atom classes and mathchoice — 2026-11-12

**Status:** FIXED for supported atom-class bodies and four-branch mathchoice.
Malformed arguments and operator modifiers remain incomplete. Native production
is unaffected; S1.8 and D10.2.2 apply.

After the TeX token pass-through port, `\begin{document}$\mathord{x}$\end{document}`
still returns `latex-digest-unsupported` / `math command mathord`. Native
produces a `math_atom` with a `mord` class and grouped body. `\mathop` and
`\mathchoice` are similarly unsupported; native retains the operator body and
all four choice branches. Five examples and native ASTs are recorded in
`../lambda-input/temp/expl3-script/math-atom-commands-before-interp.out` and
the JIT counterpart. Additional malformed and modifier cases need separate
qualification.

The script math digester now emits native-style declaration nodes and
`operatorname` nodes, including direct adjacent starred forms; uncertain
star provenance remains explicitly unsupported. Parentheses, brackets, comma,
colon and vertical bars are parsed as punctuation atoms and can take scripts.
The installed `test_tex_script_math_declarations.ls/.txt` compares 13 cases
with native and passes interpreter and JIT. Existing digest, math-group,
math-style and underline goldens also pass both backends. Other punctuation,
relations and the independently recorded atom classes remain open.

The script math parser now retains the native atom class and body for
`\mathord`, `\mathbin`, `\mathrel`, `\mathopen`, `\mathclose`,
`\mathpunct`, and `\mathinner`, represents `\mathop`, and preserves all four
`\mathchoice` branches. A shared argument scanner also serves fractions.
The installed `test_tex_script_math_atom_classes.ls/.txt` compares nine native
ASTs and passes interpreter and JIT. Existing digest, math-group, declaration,
style and underline goldens pass both tiers. A focused native AST comparison
passes with forced GC and freed-memory poisoning in both backends; evidence is
`../lambda-input/temp/expl3-script/math-atoms-gc-{interp,jit}.out`.

## Script math digester rejects left/right delimiter groups — 2026-11-12

**Status:** PARTIALLY FIXED for supported scalable delimiter groups, including
nested and middle delimiters. Invalid middle delimiters and other malformed
forms remain explicitly unsupported. Native production parsing is unaffected;
S1.8 and D10.2.2 apply.

`\begin{document}$\left(x+1\right)$\end{document}` now passes script TeX
expansion but digestion returns `latex-digest-unsupported` / `math command left`.
Native builds a `delimiter_group` with its left/right spellings and parsed
children. Seven native AST comparisons, including nested, named, invisible,
scripted, middle and unclosed forms, are recorded in
`../lambda-input/temp/expl3-script/math-delimiters-before-interp.out` and its
JIT counterpart. Malformed and raw delimiter behavior requires qualification
before enabling a broad implementation.

The script now matches nested `\left`/`\right` pairs, parses supported
`\middle` delimiters, retains delimiter spellings and children, applies
following scripts, and covers tested native recovery and braced-right forms.
The installed `test_tex_script_math_delimiters.ls/.txt` compares 20 native ASTs
and asserts explicit rejection for an invalid middle delimiter. It passes both
interpreter and JIT; existing digest and math goldens pass both tiers. A focused
nested delimiter AST comparison also passes every-allocation GC and poisoning
in both backends (`../lambda-input/temp/expl3-script/math-delimiters-gc-*.out`).

## Script TeX rejects native mark and special handling — 2026-11-12

**Status:** FIXED for the tested native mark and special expansion behavior.
Malformed-input recovery and the broader script TeX port remain incomplete.
Native production is unaffected; S1.8 and D10.2.2 apply.

`\begin{document}A\special{color push}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `special`, while native expands and discards
the special's token list and continues with `AB`. `\mark`, indexed `\marks`,
and all five ordinary and indexed mark queries are likewise unsupported.
Native discards expanded mark contents, returns empty mark queries, and scans
an integer for indexed forms; it does not perform page building. Nine examples,
including nested expansion and aliases, are in
`../lambda-input/temp/expl3-script/mark-special-before-interp.out` and its JIT
counterpart. This concerns native expansion compatibility, not page layout.

The script now scans expanded mark/special text without executing its
unexpandable tokens, consumes the index for `\marks` and indexed queries,
and returns the native empty mark result. The installed
`test_tex_script_marks_special.ls/.txt` compares 12 native expanded outputs and
checks that discarded messages are not executed. It passes interpreter and JIT;
existing expansion, profile, expl3, token, environment and digest goldens pass
both tiers. A focused every-allocation-GC/poison check also passes both backends
(`../lambda-input/temp/expl3-script/marks-special-gc-{interp,jit}.out`).

## Script TeX rejects explicit glue and kern commands — 2026-11-12

**Status:** FIXED for the tested forms. Full script-port qualification remains
open; S1.8 and D10.2.2 apply.

`\begin{document}A\hskip1pt!B\end{document}` stops script expansion with
`tex-unsupported-primitive` / `hskip`. Native scans the glue and emits
`\hspace{1.0pt}` in this case. `\vskip`, `\kern`, `\mkern` and `\mskip`
are also unsupported; native uses math-sensitive command names, formatted
scanned values, and direct source echo when its trace is valid. Nine native
examples, including flexible glue, aliases, registers and expressions, are in
`../lambda-input/temp/expl3-script/spacing-command-before-interp.out`.
Successful implementation must preserve direct-echo spans and synthetic output
origins rather than replacing every case with a fixed spelling.


The script now scans and emits these glue and kern commands, retains captured
direct tokens and source spans when native echo is valid, and synthesizes
math-sensitive output otherwise. The installed
`test_tex_script_spacing_commands.ls/.txt` compares 23 native cases, including
comments, UTF-8 offsets, aliases, expressions, and a dimension longer than 64
tokens. It passes interpreter and JIT; focused forced-GC and freed-memory-poison
checks pass both. Existing expansion, stream, trace, profile, provenance, digest
and package tests pass with the staged candidate in both backends. Malformed
input recovery and release-profile performance qualification remain open.

## Script TeX dimension scanner lacks font-relative and pixel units — 2026-11-12

**Status:** FIXED for the tested units and supported font parameters. Full
script-port qualification remains open; S1.8 and D10.2.2 apply.

The script dimension scanner rejects `em`, `ex` and `px` as
`tex-illegal-unit`. For example, `\pdfpxdimen=2pt \dimen0=1.5px
\the\dimen0` yields `3.0pt` from the native TeX parser but script expansion
stops. A selected font's metrics likewise determine `em` and `ex`. The
reproduction and native comparisons are in
`../lambda-input/temp/expl3-script/font-unit-before-interp.out`. Native also
tries these units before physical units; partially matched keyword tokens are
restored as non-direct tokens, which can invalidate a source-echo trace. The
script currently omits those probes and preserves the direct flags on matched
prefixes. This additionally affects the spacing-command work recorded above.

The script scanner now reads the selected font's em/ex parameters and the
`pdfpxdimen` value, and restores partially matched keyword prefixes with the
native non-direct provenance. The installed `test_tex_script_font_units.ls/.txt`
compares ten native cases, including assigned and negative font metrics, pixel
values, case variants and math units. It passes interpreter and JIT, including
focused forced-GC and freed-memory-poison runs. The existing expansion golden also passes with the candidate in both backends.

## Script TeX keyword scanner rejects character category codes accepted by native — 2026-11-12

**Status:** FIXED for tested keyword characters. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

``\catcode`p=7 \dimen0=1pt \the\dimen0`` yields `1.0pt` from the native parser without a diagnostic, while the
script scanner returns `tex-illegal-unit`. The executable reproduction and output
are `../lambda-input/temp/expl3-script/unit-catcode-check.ls` and
`unit-catcode-check.out`. Native `scan_keyword` matches character values without
requiring letter/other categories; script `scan_word` currently requires those
categories. This also affects other keyword-using primitives, so changes need
regression checks beyond dimension scanning.


The script keyword scanner now matches character values without restricting
their category, in line with native `scan_keyword`. The installed
`test_tex_script_keyword_catcodes.ls/.txt` compares six native dimension, glue
and arithmetic cases and passes interpreter and JIT. Focused forced-GC and
freed-memory-poison tests, spacing comparisons, and existing expansion, stream,
trace, profile, digest, provenance and expl3 goldens pass in both backends.

## Script TeX rejects discretionary replacement text — 2026-11-12

**Status:** FIXED for tested well-formed groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\discretionary{-}{z}{bc}D\end{document}` stops script
expansion at `tex-unsupported-primitive` / `discretionary`. Native expansion
consumes the first two unexpanded token groups and processes the third group,
producing `\begin{document}AbcD\end{document}`. A macro-containing case and
native output are in `../lambda-input/temp/expl3-script/discretionary-before.ls`
and `discretionary-before-interp.out`. The script implementation needs to
preserve token-list expansion and source provenance, and report unsupported
recovery where malformed input is not yet handled.


The script now scans three unexpanded token groups, discards the first two, and
executes stored replacement tokens with non-direct provenance. The installed
`test_tex_script_discretionary.ls/.txt` compares 13 native cases including
macros, nested groups, aliases, Unicode, assignments and messages. It passes
interpreter and JIT, as do focused forced-GC/poison and existing expansion,
stream, trace, profile, digest, provenance, expl3 and spacing checks. Missing
opening braces still return an explicit issue rather than native recovery.

## Script TeX rejects numeric accent primitive — 2026-11-12

**Status:** PARTIALLY FIXED for tested expansion and digest forms. Full script
TeX-port qualification remains blocked on unsupported accent arguments and
source reconstruction. Native production parsing is unaffected; S1.8 and
D10.2.2 apply.

`\begin{document}\accent19 e\end{document}` stops script expansion with
`tex-unsupported-primitive` / `accent`, while native emits `\'{e}`. The native
engine maps selected accent numbers to LaTeX commands, scans an optional
character or character definition, and emits the character unaccented for an
unknown accent number. Reproduction and examples are in
`../lambda-input/temp/expl3-script/accent-before.ls` and
`accent-before-interp.out`. Successful support needs native-compatible
character-number scanning, lookahead, and synthetic-output provenance.


The script now scans numeric accent and character arguments and emits the native
command mapping with synthetic origins. Installed `test_tex_script_accent.ls/
.txt` compares 26 native outputs and diagnostic records in interpreter and JIT;
focused forced-GC/poison checks and existing character, diagnostic, expansion,
profile, digest, provenance, stream and expl3 goldens pass both. The script digester now builds supported text accent nodes, including the
first example, and the installed `test_tex_script_accent_digest.ls/.txt`
compares 28 native ASTs and renders `é à č` to HTML. It passes interpreter and
JIT, as do focused forced-GC/poison and math, underline, digest, provenance and
package regressions. Comment-adjacent arguments, control-sequence arguments and
certain emitted syntax characters return explicit unsupported results; native
behavior for comment gaps is documented in
`../lambda-input/temp/expl3-script/accent-digest-probe-v3-interp.out`. General
emitted-source reconstruction and full accent coverage remain open.


A staged control-argument candidate matches native for `\'\i!`, `\'\%`,
and nested accents (`../lambda-input/temp/expl3-script/accent-args-probe-interp.out`).
The installed `test_tex_script_accent_digest` golden explicitly asserts that
the first two forms are unsupported, so the checkout rule preserving existing
assertions and reference results prevents installing that behavior without an
explicit user ruling. The candidate remains in
`../lambda-input/temp/expl3-script/accent-control-candidate-home`; nested accents
are handled independently.



Nested accents are now supported by a shared accent-node parser. The installed
`test_tex_script_nested_accents.ls/.txt` compares six native ASTs and checks the
remaining comment-gap rejection; it passes interpreter and JIT, as do the
existing accent golden, math, digest and package regressions and focused
forced-GC/poison checks. The separate control-argument limitation remains.

## Script TeX rejects insert and vertical adjustment groups — 2026-11-12

**Status:** PARTIALLY FIXED for tested groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\insert0{hidden}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `insert`; `\vadjust` similarly stops. Native
runs the group with ordinary scoping and then discards its output, while global
assignments still take effect. Three native comparisons are in
`../lambda-input/temp/expl3-script/insert-before.ls` and
`insert-before-interp.out`. A correct port must execute the body, manage group
scope and aftergroup actions, and remove only the produced output; simply
skipping group tokens would lose side effects.


The script now executes these groups with normal scoping, preserves global and
aftergroup effects, and discards their emitted output at group close. Installed
`test_tex_script_insert.ls/.txt` compares 24 native outputs including nested
groups, macros, math, messages, and an early document end. It passes interpreter
and JIT, as do focused forced-GC/poison and existing expansion, discretionary,
spacing, digest, provenance, math and package regressions. Cross-boundary
`\unskip` returns an explicit unsupported issue, and malformed-opening-brace
recovery is incomplete (`../lambda-input/temp/expl3-script/insert-rejection-probe.out`).

## Script TeX rejects rule primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested rule forms. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hrule width1pt height2pt depth3pt!B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `hrule`; native emits
`\rule{1.0pt}{5.0pt}`. A macro-generated `\vrule` emits the native default
`\rule{0.4pt}{1em}`. Native scans optional dimension keywords in any order,
uses source echo when its trace is valid, and otherwise synthesizes a rule.
Reproductions are in `../lambda-input/temp/expl3-script/rule-before.ls` and
`rule-before-interp.out`. The script port needs correct scanning, echo spans,
and synthetic source origins.


The script now scans and emits rules with native source echo or synthesized
output. Installed `test_tex_script_rules.ls/.txt` compares 19 native expanded
outputs, and `test_tex_script_rule_digest.ls/.txt` compares 17 native ASTs and
a rendered HTML rule. Both pass interpreter and JIT, as do focused forced-GC/
poison checks and existing insert, spacing, digest, math and package regressions.
An echoed rule containing a comment can lose whitespace in direct digestion;
the script explicitly reports `rule comment source gap` instead. The mismatch
and rejection are reproduced in
`../lambda-input/temp/expl3-script/rule-digest-stage-check-interp.out` and
`../lambda-input/temp/expl3-script/rule-comment-gap-probe.out`. General
comment reconstruction and malformed dimension recovery remain open.

## Script TeX rejects leader primitives — 2026-11-12

**Status:** PARTIALLY FIXED for rule and inline-box leaders. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\leaders\hrule\hskip2cm!B\end{document}` stops script
expansion with `tex-unsupported-primitive` / `leaders`. Native consumes the
rule specification and then processes the following glue, emitting
`\hskip2cm`. A second case using `\cleaders\vrule` is in
`../lambda-input/temp/expl3-script/leaders-before.ls` and
`leaders-before-interp.out`. Native also permits box leaders, executing and
discarding their box contents before processing the following glue. The script
port needs both rule and box behavior, with scoped execution and source fidelity.


The script now consumes rule leaders and executes/discards `hbox`, `vbox` and
`vtop` leader bodies, including their hooks, scoping and global effects. The
installed `test_tex_script_leaders.ls/.txt` compares 24 native outputs and
passes interpreter and JIT; focused forced-GC/poison and existing rule, insert,
digest, math and package regressions pass both. Leader boxes obtained from
registers, copies or splits remain explicitly unsupported, as does malformed
box recovery. Box-leader side effects were reproduced in
`../lambda-input/temp/expl3-script/leader-box-before-interp.out`.

## Script TeX rejects inline box primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested inline boxes. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hbox{Hi}B\end{document}` stops script expansion at
`tex-unsupported-primitive` / `hbox`; native emits `\mbox{Hi}`. A vertical box
emits a grouped body. Reproductions are in
`../lambda-input/temp/expl3-script/inline-box-before.ls` and
`inline-box-before-interp.out`. Correct support must scan box specifications,
execute scoped bodies and box hooks, and preserve synthetic-output origins.
Box registers, copying, splitting and movement require additional work.


A staged inline-box expansion matches native output in the initial cases. Its
direct vertical-box digester currently loses a space after a skipped comment:
`\vbox{a%note\n b}` yields `ab` where native yields `a b` (reproduction in
`../lambda-input/temp/expl3-script/inline-box-digest-probe.out`). Synthetic
horizontal boxes also lack command source mapping. Both need explicit handling
or rejection before installing the expansion.


A category-code probe found an additional horizontal-box argument mismatch:
when `$` has category 12, native records escaped `\$` in the argument's raw
source but a first candidate recorded `$`. The staged digester now rejects
this source shape until token serialization is available. Before and after
evidence is `../lambda-input/temp/expl3-script/inline-box-catcode-probe.out`
and `inline-box-catcode-probe-v2.out`.


The script now expands inline boxes with scoped bodies and hooks, and digests
supported simple horizontal and vertical boxes. Installed
`test_tex_script_inline_box.ls/.txt` compares 17 native expanded outputs;
`test_tex_script_inline_box_digest.ls/.txt` compares 13 native ASTs and renders
HTML. Both pass interpreter and JIT, as do focused forced-GC/poison and existing
leader, rule, insert, math and package regressions. Comment-sensitive vertical
boxes, macro-expanded horizontal bodies and special category-code characters
return explicit unsupported results pending source reconstruction
(`../lambda-input/temp/expl3-script/inline-box-unsupported-probe.out`,
`inline-box-catcode-probe-v2.out`).

## Script TeX rejects box registers and retrieval — 2026-11-12

**Status:** OPEN. **Full script TeX-port blocker:** yes, for `\setbox`,
`\box`, `\copy` and related operations. Native production parsing is unaffected;
S1.8 and D10.2.2 apply.

`\setbox0=\hbox{Hi}\begin{document}A\copy0!\box0!\copy0 B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `setbox`. Native stores
the box body, copies it once, consumes it once, and leaves the final copy empty.
A related `\ifvoid` example and output are in
`../lambda-input/temp/expl3-script/box-register-before.ls` and
`box-register-before-interp.out`. The port needs group-scoped box assignment,
copy versus consume semantics, box conditionals, hooks and source provenance;
splitting, unboxing and movement also remain to be qualified.


An initial register candidate matched the tested output and scope cases, but
replaying `\setbox0=\hbox{a%note\n b}` into the script digester produced
`ab` where the native AST contains `a b`. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe.out`. Box replay
needs to preserve or explicitly reject unqualified source gaps before the
register implementation can be installed.


Further staged AST comparisons found a definition argument ending at the wrong
source offset when a box assignment separates it from the next emitted input,
and a synthesized rule replay whose final argument end should map to the next
emitted source segment. The output text matches native; ASTs differ. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe-v3.out`. General
definition reconstruction is already incomplete; the candidate must reject
unqualified definition/replay combinations and fix or reject the rule mapping.


The definition-offset mismatch also occurs with an empty assigned box and no
replay: `../lambda-input/temp/expl3-script/box-assignment-definition-probe.out`
shows native mapping the definition argument end to the next emitted document
segment, while the candidate used the original closing brace. The staged
digester must account for assignments, not just replay markers, when it reports
this unqualified reconstruction.


A staged register candidate now matches 26 native expanded-output examples and
eight native AST examples in interpreter and JIT, with a focused forced-GC/
poison check. Existing `test_tex_script_expansion.ls:134` and
`test_tex_script_profile.ls:13` explicitly use `\setbox0=\hbox{}` as an
unsupported primitive and failed preload. Their unchanged `.txt` references
therefore fail in both backends when the candidate correctly accepts it. The
checkout rule requires preserving assertions and reference results, so this
blocks installation without an explicit user ruling; no workaround or golden
change was made. The candidate and failing outputs remain at
`../lambda-input/temp/expl3-script/box-register-stage-home` and
`box-register-regression-{expansion,profile}-{interp,jit}.out`. All other
focused regressions passed. Comment and definition source gaps are conservatively
rejected by the candidate; broader source reconstruction remains open.

## Script TeX rejects box dimension access — 2026-11-12

**Status:** OPEN. **Full script TeX-port blocker:** yes, for `\wd`, `\ht` and
`\dp`. Native production parsing is unaffected; S1.8 and D10.2.2 apply.

With the staged box-register implementation,
`\setbox0=\hbox{Hi}\setbox1=\copy0\wd1=2pt` stops at
`tex-unsupported-primitive` / `wd`. Native reports both `\wd0` and `\wd1` as
`2.0pt`, because the copied registers refer to the same box object. Evidence is
`../lambda-input/temp/expl3-script/box-dimensions-before.ls` and
`box-dimensions-before-interp.out`. Correct support needs native-compatible
shared-box mutation, scanning, scoped references, and dimension diagnostics.


## Script TeX rejects paragraph pattern and shape assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed expansion. AST source mapping,
some quantity-read ASTs and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

The script profile stops with `tex-unsupported-primitive` for `\patterns`,
`\hyphenation`, `\parshape`, and `\interlinepenalties`. For example,
`\begin{document}A\patterns{a1b}B\end{document}` expands natively to
`\begin{document}AB\end{document}`. Reproduction and native outputs are in
`../lambda-input/temp/expl3-script/paragraph-assign-before.ls` and
`paragraph-assign-before.out`. The native engine consumes the pattern text
without expansion and scans shape dimensions or penalty integers without storing
layout state; parity requires preserving scanning and assignment side effects.

An experimental implementation matches 25 native expansion cases, but its
initial AST comparisons expose three source-mapping failures: a prior echoed
definition gets the wrong end offset, a nested formatting argument loses a
`\hyphenation` node, and math source includes a consumed penalty assignment.
See `../lambda-input/temp/expl3-script/paragraph-digest-probe-interp.out` for
actual and native trees. These AST mismatches block claiming complete support;
the digester must reject affected inputs until source reconstruction is fixed.
Malformed numeric and brace recovery remains separate from the well-formed
assignment subset.

The script implementation scans pattern and hyphenation text without executing
it and consumes paragraph-shape dimensions and the four penalty lists, including
prefixes and `\afterassignment`. The installed
`test_tex_script_paragraph_assignments.ls/.txt` compares 25 text and diagnostic
cases with native behavior and checks that pattern contents do not execute. Both
backends pass, as do 23 focused regressions (the fixture-dependent test was
rerun after correcting its isolated working directory) and sampled forced GC
with freed-memory poisoning. To avoid emitting incorrect ASTs, digestion now
explicitly rejects documents that execute these assignments until the mapping
gaps above are repaired. `\the\parshape` and `\the\clubpenalties1` were unsupported while native
returns zero; the original reproduction is
`../lambda-input/temp/expl3-script/paragraph-query-probe.ls` and `.out`.

The Script engine now reads those quantities in `\the`, numeric expressions,
conditionals and dimension factors, scanning the required penalty index. The
new `test_tex_script_paragraph_quantities.ls/.txt` compares 20 expansion and
diagnostic cases, and three supported ASTs, against native in interpreter
and JIT. Both backends also pass 24 focused regressions and sampled forced GC
with freed-memory poisoning. Math with a generated quantity and combinations
with echoed definitions report unsupported where source mapping remains
unqualified; assignment-containing documents retain the broader guard above.


## Script LaTeX definition offsets omit following consumed assignments — 2026-11-12

**Status:** OPEN. Blocks AST parity for affected documents; S1.8 and D10.2.2
apply.

With `\def\x{Hi}\count0=0\begin{document}A\x B\end{document}`, Script
digestion reports the definition body's argument end as byte 10, while native
reports byte 19, the start of the next emitted segment. `\dimen0=1pt` similarly
reports 10 versus 21. The Script result is marked successful despite the AST
mismatch. Reproduction and both trees are in
`../lambda-input/temp/expl3-script/definition-assignment-mapping-probe.ls` and
`.out`. A candidate for paragraph quantity reads also exposes this existing
problem; the issue appears to be incomplete reconstruction of the native source
map across consumed assignments. Exact general cause remains unconfirmed.


## Script TeX rejects font hyphen and skew characters — 2026-11-12

**Status:** PARTIALLY FIXED for direct font identifiers. Family identifiers,
AST source mapping and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

`\the\hyphenchar\nullfont` and `\the\skewchar\nullfont` stop Script
expansion at `tex-unsupported-primitive`; native emits `45` and `-1`. Font
property assignments such as `\hyphenchar\f=63` likewise stop, whereas
native updates the selected font and reads back `63`. Reproduction is
`../lambda-input/temp/expl3-script/font-character-before.ls` and `.out`.
The implementation needs native font identity, defaults, property mutation,
scoping and quantity scanning rather than treating the properties as text.

A staged candidate matches 20 native expansion and diagnostic examples,
including shared font identity, defaults and group behavior. Its initial AST
probe produces incorrect successful trees for an assignment inside a formatting
argument and for an assignment following an echoed definition; see
`../lambda-input/temp/expl3-script/font-character-digest-probe.out`. The latter
also occurs for ordinary register assignments (the separate definition-offset
issue above). The candidate must report unsupported for unqualified AST source
mappings; full source reconstruction remains open.


Script now tracks these properties on shared font identities, preserves native
defaults for new fonts and nullfont, and supports assignment and reads through
`\the`, numeric expressions, conditions and dimension factors. The new
`test_tex_script_font_characters.ls/.txt` compares 20 expansion/diagnostic
cases and two supported ASTs to native in interpreter and JIT. Both backends
pass 26 focused regressions and sampled forced GC with poisoning. Digestion
explicitly rejects property assignments or unqualified definition combinations,
and math source mapping remains unsupported. Font-family identifiers are
tracked separately below.

## Script TeX rejects math font family assignments and identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for family assignments and font-identifier scans.
Direct font-token `\the`, AST source mapping and malformed-input recovery
remain open; S1.8 and D10.2.2 apply.

`\textfont0=\f` and `\scriptfont2=\f` stop Script expansion at
`tex-unsupported-primitive`; native accepts them. With the staged font-property
work, `\the\hyphenchar\textfont0` reports `tex-missing-font`, while native
reads the default null font and emits `45`. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-family-before.ls` and `.out`. Correct
support needs family-slot assignment, index scanning and font identifiers in
numeric and `\the` contexts, including font identity and source mapping.

A staged family-slot candidate matches 16 native expansion and diagnostic
examples, including index wrapping, aliases, shared font identity and global
effects. Removing its provisional AST guard reproduces incorrect successful
trees when a family assignment occurs inside formatting or after a definition;
see `../lambda-input/temp/expl3-script/font-family-digest-probe.out`. These
source-mapping cases must remain explicitly unsupported until reconstruction
is qualified. Direct `\the\textfont` font-token production is not yet ported.


Script now scans and assigns all three math font families, including wrapped
indices, aliases, shared font identity and native group behavior. It resolves
family identifiers for font properties and font dimensions. The new
`test_tex_script_font_families.ls/.txt` compares 16 expansion/diagnostic cases
and two supported ASTs with native in interpreter and JIT; 27 focused
regressions and sampled forced GC with poisoning pass both. Family assignments
explicitly reject AST digestion while source mapping remains unqualified.
Direct font-token production through `\the` remains the separate issue below.

## Script TeX cannot expand font identifiers through the — 2026-11-12

**Status:** PARTIALLY FIXED for expansion and experimental text reconstruction.
Font-token AST source mapping and production integration remain open; S1.8
and D10.2.2 apply.

`\the\font`, `\the\nullfont` and `\the\f` stop Script expansion as
unsupported, while native produces the font's original control-sequence token.
For example, after `\font\f=cmr10 at 13pt`, `\edef\x{\the\f}` stores
`\f`, not the literal font name. Redefining that control sequence affects
subsequent use of the returned token. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-the-before.ls`, `font-the-before.out`,
and `font-family-the-probe.out`. Correct support needs original font identifier
identity and `\the` token-list expansion semantics, including `\edef`.

The staged candidate in `../lambda-input/temp/expl3-script/font-the-stage-home`
returns the original font token for direct use, but `\edef` cases expose two
additional mismatches: the echoed definition is lost when tracing sees the
generated font token, and in-document `\meaning` emits a raw backslash where
native serializes a text-backslash command. Sixteen comparisons are in
`test_tex_script_font_tokens.ls`, with actual/native output in
`font-token-mismatch-probe.out`. That earlier candidate remained uninstalled while the general source-trace and
text serialization causes were investigated.


The Script engine now preserves original font control-sequence identity for
`\the` of named, current, null and math-family fonts, including `\edef` and
later redefinitions. Generated tokens are distinguished in source tracing.
`test_tex_script_font_tokens.ls/.txt` compares 17 native text/diagnostic cases
and verifies safe AST handling in interpreter and JIT. The experimental
`reconstruct.ls` is used for those text comparisons; the existing digester
returns unsupported for font-token ASTs until mapping is qualified.

## Script LaTeX meaning output does not serialize backslash characters — 2026-11-12

**Status:** PARTIALLY FIXED in the experimental reconstructor. Production
integration and complete source-map qualification remain open; S1.8 and
D10.2.2 apply.

For `\def\x{\y}\begin{document}\meaning\x\end{document}`, Script
expansion emits a raw backslash before `y`, while native serializes it as
`\textbackslash{}y`. The Script result is marked successful. A macro meaning
with only ordinary characters matches. Reproduce with
`../lambda-input/temp/expl3-script/meaning-serialization-probe.ls` and `.out`.
The evidence points to generated character serialization in the LaTeX profile;
the exact general cause and all affected character categories are unconfirmed.


The experimental Script `reconstruct.ls` canonicalizes generated characters
and matches native output for the reproduction above. It is a separate API,
not yet the production LaTeX path; direct expanded tokens remain tokens.
The new `test_tex_script_reconstruction.ls/.txt` checks 91 native text cases
in both backends, including this one, comments, boxes, rules, accents and
assignments. Offset-map and full-corpus qualification remain open.

## Script TeX reconstruction loses numeric accent text segments — 2026-11-12

**Status:** FIXED for tested numeric accent reconstruction in the experimental
Script reconstructor. Full reconstruction qualification remains open; S1.8
and D10.2.2 apply.

Native `\accent19 \char123` and `\accent19 \char92` emit a single raw
text segment, including the character. The staged Script reconstructor treats
the characters as individual synthetic tokens and escapes them, producing
`\'{\{}` and `\'{\textbackslash{}}` rather than the native output.
Reproduction, all 51 comparisons and actual/native text are in
`../lambda-input/temp/expl3-script/reconstruct-conformance-b.ls` and
`reconstruct-conformance-b-interp.out`. The native `do_accent` calls
`emit_text`; the script must retain that segment boundary and origin.


The Script expansion output now retains native accent text-segment boundaries
and the experimental reconstructor emits them as one segment. Both examples
are included in the 91-case native comparison test.

## Script LaTeX font-token expansion loses nested and math source mapping — 2026-11-12

**Status:** OPEN in the staged font-token implementation. Blocks AST parity
for documents using font-valued `\the`; S1.8 and D10.2.2 apply.

The staged engine correctly reconstructs expansion text for direct
`\the\nullfont`, but its AST silently differs from native when that command
appears in math or a formatting argument. See
`../lambda-input/temp/expl3-script/font-token-digest-probe.ls` and `.out` for
actual and native trees. Source mapping for font-token expansion must report
unsupported until the native digestion behavior is ported or reconstructed.


## Script TeX executes assignments inside constructor arguments — 2026-11-12

**Status:** OPEN. Blocks script-port output, state and AST parity for affected
documents; S1.8 and D10.2.2 apply. Production native parsing is unaffected.

For `\count0=0\begin{document}\textbf{\global\count0=7 X}\the\count0\end{document}`,
the Script engine executes the assignment, removes it from the output and
emits `7`; native leaves the assignment in the constructor argument and emits
`0` afterwards. Optional constructor arguments have the same defect. The
`\hyphenation` nested formatting discrepancy and several font-related AST
mismatches arise from this difference. Actual/native text and controls are in
`../lambda-input/temp/expl3-script/constructor-arg-mode-before.ls` and `.out`.
Native `arg_mode` in `input-tex-main.cpp` expands macros in constructor
arguments while passing unexpandable primitives and assignments through, with
special handling for nested constructors, environments and raw arguments.
Correct support must preserve those semantics and source provenance.

A staged constructor-argument implementation in
`../lambda-input/temp/expl3-script/constructor-arg-stage-home` matches the four
reproductions above, including state and optional arguments. Its unchanged
regressions for `test_tex_script_paragraph_assignments`,
`test_tex_script_font_characters`, and `test_tex_script_font_families` now
fail because those existing references explicitly expect unsupported ASTs for
these nested cases. The corrected candidate returns native-matching ASTs for
all three; the paragraph AST was compared directly in interpreter and JIT in
`../lambda-input/temp/expl3-script/constructor-paragraph-ast.ls` and `.out`. Failing comparisons are
`constructor-arg-regression-{paragraph_assignments,font_characters,font_families}-interp.out`;
the paragraph failure also reproduces in JIT. Checkout rules require preserving
test assertions and reference results, so this blocks installation without a
user ruling. The candidate also still needs complete environment/raw-command
handling and qualification; no golden was changed.

The same state error also occurs in arguments of unknown passthrough commands:
`\unknown{B\global\count0=7 C}` executes the assignment in Script, while
native preserves it and leaves the register unchanged. Evidence is
`../lambda-input/temp/expl3-script/unknown-arg-before.ls/.out`. Native main
control applies argument mode to both undefined controls and constructors. The
existing staged candidate matches this example in the interpreter; see
`../lambda-input/temp/expl3-script/unknown-arg-staged.out`. It still needs
rebasing, JIT qualification, and environment and raw-argument handling.

The rebased prototype at `../lambda-input/temp/expl3-script/constructor-current-stage-home`
also handles tested passthrough environment options and unknown commands in
inline math; see `constructor-current-probe2.out`. It retains passthrough
nested environment names and explicitly rejects unqualified raw commands and
custom environments inside an argument. The latter is blocked by input issue
[LI-017](../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-017), which causes
native to emit its internal environment-end marker. Test evidence is
`constructor-env-probe.ls`, `.out`, and `constructor-env-probe2.out` in the
same temporary directory. Interpreter and JIT now agree on 17 of 20 combined
LaTeX probes; the other three explicitly reject raw commands or LI-017's custom
environment. Four plain-TeX cases also match in both backends; see
`constructor-pure-probe2.out` and `constructor-pure-probe2-jit.out`. The
candidate remains uninstalled, and the reference result conflict described
above remains unresolved.

The rebased stage initially lost main-file origin on a synthetic opening brace
read as a constructor argument from a `\read` stream, causing
`tex-reconstruct-missing-origin` in `script_read_scope`; evidence is
`constructor-read-detail2.out` in the same temporary directory. A shared
emission-origin helper now annotates such delimiters, and the existing nine-case
`test_tex_script_read` matches its preserved reference in both backends
(`constructor-read-fixed2-interp.out` and `constructor-read-fixed2-jit.out`).

A further rebase at `../lambda-input/temp/expl3-script/constructor-verb-stage-home`
uses the installed direct-source `\verb` reader inside constructor arguments.
Nine native comparisons cover braces, comments, optional arguments, Unicode,
starred and aliased forms in interpreter and JIT; evidence is
`constructor-verb-probe.ls` and `constructor-verb-probe-{interp,jit}.out`.
Nine focused existing references, including `\read` and `\verb`, pass in both
backends; the three paragraph/font references still fail only because they
expect unsupported results. Logs and preserved actual outputs are under
`../lambda-input/temp/expl3-script/constructor-verb-regressions-{interp,jit}.log`
and `constructor-verb-regression-{interp,jit}/`. The candidate still
explicitly rejects custom environments affected by LI-017 and remains
uninstalled because of the preserved-reference conflict.

The latest rebase at `../lambda-input/temp/expl3-script/constructor-random-stage-home`
retains the installed identity and deterministic-random features. Nine further
native comparisons cover random expansion and seed assignments, `\jobname`,
`\fontname`, `\the` and inline `\verb` inside arguments; all match in both
backends. Evidence is `constructor-random-probe.ls` and
`constructor-random-probe-random-{interp,jit}.out` in the same directory.
The prior command and inline-verb probes also continue to match.

The current rebase at `../lambda-input/temp/expl3-script/constructor-shipout-stage-home`
also matches six native cases in both backends where movement or shipout appears
inside arguments, including nested assignments and verbatim content. Evidence is
`constructor-shipout-probe.ls` and `constructor-shipout-probe-{interp,jit}.out`.
The staged fix remains subject to the preserved-reference and LI-017 limits.


## Script TeX rejects letterspacefont alias assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments outside constructor
arguments. AST source mapping and malformed-input recovery remain open; S1.8
and D10.2.2 apply.

`\font\f=cmr10 at 13pt\letterspacefont\g\f 100` stops the Script
profile at `tex-unsupported-primitive`, while native defines `\g` as the
same font identity as `\f`. Native reads its sixth font dimension as
`13.0pt` and `\ifx\f\g` succeeds; nullfont aliases also work. Reproduction
and native outputs are in `../lambda-input/temp/expl3-script/letterspacefont-before.ls`
and `.out`. Native scans and discards the spacing integer, preserving font
identity; Script support needs that scanning, scope and source handling.


Script now scans the font and spacing expression and assigns a scoped alias to
the existing font identity. The new `test_tex_script_letterspacefont.ls/.txt`
compares 18 native text and diagnostic cases and checks safe AST outcomes in
both backends, including global definitions and `\afterassignment`. Thirteen
focused regressions and sampled forced GC with poisoning pass both. AST
digestion explicitly reports unsupported for assignments, because unguarded
nested and definition cases differ from native; see
`../lambda-input/temp/expl3-script/letterspacefont-digest-probe.out`. The
separate constructor-argument defect affects assignments inside arguments.

## Script TeX rejects interaction and page-state primitives — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments and queries outside
constructor arguments. AST source mapping and malformed-input recovery remain
open; S1.8 and D10.2.2 apply.

`\the\interactionmode`, `\interactionmode=3`, `\batchmode`,
`\the\pagegoal`, and `\pagegoal=1pt` stop Script expansion at
`tex-unsupported-primitive`. Native currently reports interaction mode `1` and
page dimensions `0.0pt` even after assignment, because its input engine does
not model interaction state or a page builder; it still scans assignment
values. Evidence is `../lambda-input/temp/expl3-script/page-state-before.ls`
and `.out`. Script parity must scan values and preserve assignment side effects
and diagnostics rather than omit the commands or their inputs.


Script now models the native engine's fixed interaction/page results and scans
assignment values, including prefixes, aliases and `\afterassignment`. The
new `test_tex_script_page_state.ls/.txt` compares 23 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. Unguarded nested
and definition assignments produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/page-state-unguarded-probe.out`. The
digester reports unsupported for those unqualified assignment mappings, and
the separate constructor-argument defect affects nested assignments.

## Script expl3 rejects token-list append and prepend APIs — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed `Nn` and `Nx` variants outside
constructor arguments. AST source mapping and invalid-target recovery remain
open; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_put_right:Nn`, `\tl_put_left:Nn` and
`\tl_put_right:Nx` stop at `tex-unsupported-primitive`; native produces
`AB`, `BA`, and an expanded token-list result respectively. Reproduction
and native text are in `../lambda-input/temp/expl3-script/expl3-tl-put-before.ls`
and `.out`. Support needs token-level concatenation, local/global scope,
`n` versus `x` expansion and source/diagnostic behavior.


A staged implementation matches 20 native text and diagnostic cases in both
backends. Unguarded digestion still returns incorrect successful ASTs for
nested constructor arguments and assignments following definitions; see
`../lambda-input/temp/expl3-script/expl3-tl-put-digest-probe.out`. The staged
digester must report unsupported for these unqualified source mappings.

Script now supports local/global append and prepend, preserving token bodies
and `n` versus `x` expansion. `test_tex_script_expl3_tl_put.ls/.txt` compares
20 native text/diagnostic cases and checks safe AST handling in interpreter and
JIT. Thirteen focused regressions and sampled forced GC with poisoning pass
both. The digester explicitly rejects token-list mutations until source mapping
is qualified; constructor arguments remain a separate issue.

## Script expl3 omits standard scratch token lists — 2026-11-12

**Status:** FIXED for the four standard local/global scratch token lists.
Other expl3 initialization remains incomplete; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_use:N\l_tmpa_tl` and
`\tl_use:N\l_tmpb_tl` fail with `expl3-undefined-variable`; native expl3
provides these as initially empty token lists. Reproduction and native text
are in `../lambda-input/temp/expl3-script/expl3-scratch-before.ls` and `.out`.
Correct initialization must preserve standard scope and duplicate-declaration
behavior rather than treating every missing token list as empty.


Script expl3 initialization now provides `\l_tmpa_tl`, `\l_tmpb_tl`,
`\g_tmpa_tl` and `\g_tmpb_tl` as empty token lists, without treating other
undefined names as empty. The new token-list test covers all four and scoped
mutation; existing expl3 tests continue to pass in both backends.

## Script expl3 rejects token-list emptiness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for tested `N` and `n` branch variants. AST
source mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_empty:NTF` and `\tl_if_empty:nTF` stop Script expansion at
`tex-unsupported-primitive`, while native selects the true branch for an empty
list and the false branch for a nonempty one. Reproduction and native output
are in `../lambda-input/temp/expl3-script/expl3-tl-empty-before.ls` and `.out`.
The implementation must scan both branches, expand only the selected branch,
and preserve token-list and grouping semantics.

The staged conditional initially chose correct branches but echoed an `\edef`
that native suppresses after expanding expl3's internal macro tokens. Both `N`
and `n` cases reproduce this in
`../lambda-input/temp/expl3-script/expl3-tl-empty-trace-probe.out`. Source
tracing must model that internal expansion origin; the uncorrected staged
result is not qualified.

A second staged probe found that checking only the token-list body is
insufficient for `N` conditionals: native compares the complete TeX meaning
to the empty token-list meaning. Empty long/protected macros, macros with
parameter prefixes, `\relax` and undefined controls select false. Evidence
is `../lambda-input/temp/expl3-script/expl3-tl-empty-meaning-probe.out` and
`expl3-tl-empty-meaning-native.out`. The candidate must compare meanings.

An unguarded staged AST comparison also returns an incorrect successful
definition-offset tree after a token-list conditional; see
`../lambda-input/temp/expl3-script/expl3-tl-empty-digest-probe.out`. The
conditional must remain guarded in the current digester until source mapping
is qualified.


Script now provides `NTF`/`NT`/`NF` and `nTF`/`nT`/`nF` variants, comparing
complete TeX meanings for `N` and unexpanded tokens for `n`. Internal
macro-expansion provenance is retained for definition tracing. The new
`test_tex_script_expl3_tl_empty.ls/.txt` compares 31 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. AST digestion
explicitly reports unsupported until conditional source mapping is qualified.

## Script expl3 rejects token-list blankness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `n` and `V` branch variants. AST source
mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_blank:nTF` stops Script expansion at `tex-unsupported-primitive`.
Native selects true for a token list consisting only of spaces, false for a
letter or for an unexpanded control sequence even if that macro expands to
empty. Evidence is `../lambda-input/temp/expl3-script/expl3-tl-blank-before.ls`
and `.out`. Correct support requires token-category-aware whitespace tests and
normal conditional branch and source-trace behavior.

The staged `nTF`/`nT`/`nF` implementation matches 17 native text and
diagnostic examples. Without the existing conditional source-map guard, an
conditional following an echoed definition yields an incorrect successful AST;
see `../lambda-input/temp/expl3-script/expl3-tl-blank-digest-probe.out`. The
staged digester retains the explicit guard until mapping is qualified.

The common `VTF` variable variant is also unsupported in the staged `n`
implementation. Native checks the variable's stored tokens without expanding
control sequences inside it; evidence is
`../lambda-input/temp/expl3-script/expl3-tl-blank-v-before.ls` and `.out`.

Script now implements the `n` variants with category-aware space checks,
unexpanded token semantics and macro-origin tracing. The new
`test_tex_script_expl3_tl_blank.ls/.txt` compares 17 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fifteen focused
regressions and sampled forced GC with poisoning pass both. The digester
continues to reject unqualified conditional source mappings.


The Script engine now also implements `VTF`/`VT`/`VF`, checking the stored
tokens without expanding embedded controls. `test_tex_script_expl3_tl_blank_v.ls/.txt`
compares 19 native text/diagnostic cases and checks safe AST outcomes in both
backends. Sixteen focused regressions and sampled forced GC with poisoning pass
both; unsupported variable types and conditional source mapping remain bounded.

## Script TeX rejects input-stream EOF conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for opened and closed captured-resource streams.
Stream AST source mapping remains open; S1.8 and D10.2.2 apply.

`\ifeof0 Y\else N\fi` stops Script expansion at
`tex-unsupported-primitive`, while native selects `Y` for an initially closed
stream. Nested and `\unless` examples show the same gap; evidence is
`../lambda-input/temp/expl3-script/ifeof-before.ls` and `.out`. Native scans a
stream number, diagnoses out-of-range values and tests the stream's actual
open/EOF state; a partial port must not assume open streams are closed.

The staged closed-stream candidate matches 15 native text/diagnostic cases,
but unguarded AST comparisons still differ for math and a preceding echoed
definition. Evidence is `../lambda-input/temp/expl3-script/ifeof-digest-probe.out`.
The current digester must explicitly reject those unqualified conditional source
mappings until the native mapping is reproduced.


Script now tracks input-stream state and implements `\ifeof`, including
stream-number diagnostics, nested conditions and `\unless`. Captured-resource
opening, closing, `\readline` and `\read` now update this state.
`test_tex_script_ifeof.ls/.txt` compares 15 native text and diagnostic cases and checks safe AST outcomes in both backends. Seventeen
focused regressions and sampled forced GC with poisoning pass both. The
digester explicitly rejects EOF conditional source mapping for now.

## Script TeX rejects input stream opening and closing — 2026-11-12

**Status:** PARTIALLY FIXED for captured-resource opening and closing. Stream
reading is supported for tested valid UTF-8 resources; AST source mapping
remains open. S1.8 and D10.2.2 apply.

`\openin0=stream_fixture` stops the Script engine at
`tex-unsupported-primitive`. Native opening a fixture relative to the input
file changes the following `\ifeof0` result to false; a missing file leaves
the stream closed. The isolated reproduction uses
`../lambda-input/temp/expl3-script/openin-main.tex`, `stream_fixture.tex`,
`openin-file-native.ls/.out`, and `openin-before.ls/.out`. Native source
`do_open_stream` scans the stream number and file name, tries the exact name
and `.tex`, and keeps state until closed or read past EOF. Script must use the
authorized captured-resource provider rather than assuming file availability;
the initial candidate did not yet port `\read` and `\readline`.

A staged captured-resource implementation matches a file-backed native fixture
covering existing and missing files, `.tex` fallback, empty files, wrapped
stream numbers, grouping, closing and scanner lookahead. Without a digestion
guard, opening after a definition and closing inside a formatting argument
produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/openin-digest-probe.out`. The separate
constructor-argument defect contributes to the nested case.


Script now opens and closes streams through the existing captured-resource
provider, trying exact and `.tex` names, tracking empty files and wrapped stream
numbers, and preserving native scan order. The new file-backed
`test_tex_script_openin.ls/.txt` compares native text/diagnostics across
opening, missing files, grouping, empty files and closing. Both backends pass
18 focused regressions and sampled forced GC with poisoning. The digester
reports unsupported for unqualified stream command mappings; `\read` remains
separate work.

## Script TeX rejects readline from input streams — 2026-11-12

**Status:** PARTIALLY FIXED for valid UTF-8 captured resources. Malformed
inputs, unsupported endline characters and AST mapping remain open; S1.8 and
D10.2.2 apply.

With the staged `\openin` implementation, `\readline0 to\first` stops
Script expansion at `tex-unsupported-primitive`. Native reads and trims a
line, stores other-category character tokens in a control sequence, and closes
the stream only after a subsequent read passes EOF. The fixture with two lines
and an EOF check is `../lambda-input/temp/expl3-script/openin-test-cwd/test/`
`lambda/latex/fixtures/local_packages/script_readline.tex` and
`script_stream_data.dat`; reproduction and native output are
`../lambda-input/temp/expl3-script/readline-before.ls` and `.out`. Support
must preserve input bytes, endline handling, stream position, scope,
`\afterassignment` and diagnostics. Ordinary `\read` remains separate.


Script now reads physical lines, trims trailing spaces, emits other-category
character tokens, tracks stream position and EOF, honors endline settings,
scopes assignments and preserves `\afterassignment`. Six file-backed native
fixtures compare text and diagnostics in interpreter and JIT, covering CRLF,
UTF-8, special characters, category behavior, scope and terminal reads.
Nineteen focused regressions and sampled forced GC with poisoning pass both.
Invalid UTF-8, surrogate endline characters, malformed headers and stream AST
mapping remain explicitly unsupported; tokenized `\read` is tracked below.

## Script TeX rejects tokenized input-stream reads — 2026-11-12

**Status:** PARTIALLY FIXED for tested valid UTF-8 captured resources. Invalid
UTF-8, surrogate endline characters, malformed headers and stream AST source
mapping remain unsupported; S1.8 and D10.2.2 apply.

With staged stream opening and `\readline`, `\read0 to\first` stops Script
expansion at `tex-unsupported-primitive`, while native reads a line, tokenizes
it using current category codes and stores it in a control sequence.
Reproduction is `../lambda-input/temp/expl3-script/read-before.ls/.out` with
`openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read.tex`
and `script_stream_data.dat`. Native continues across lines while braces remain
unbalanced, so a correct port must retain tokenization, balance, EOF, scope,
`\afterassignment`, source provenance and diagnostics.

Script now tokenizes lines with the current category codes and continues while
braces remain unbalanced. It preserves stream state, scope, `\afterassignment`,
reused synthetic source provenance and the tested native diagnostics. Nine
file-backed fixtures compare native text and diagnostics in interpreter and JIT,
including multiline and mixed `\read`/`\readline` input. Twenty focused
regressions and sampled forced GC with poisoning pass both backends. The
digester continues to reject unqualified stream AST mappings.


## Script TeX reconstruction errors on stale partial UTF-8 spans — 2026-11-12

**Status:** FIXED for tested stale partial UTF-8 spans. Full reconstruction
mapping remains experimental; S1.8 and D10.2.2 apply.

The staged `\read` implementation reproduces native reuse of one synthetic
read buffer. In `script_read_mixed.tex`, an earlier token span later points into
a changed UTF-8 line and slices through a multibyte character. The installed
Script `reconstruct.ls` propagates `E318` from UTF-8 decoding instead of
falling back to the token's canonical serialization as native does. The
file-backed fixture and failure are
`../lambda-input/temp/expl3-script/openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read_mixed.tex`
and `../lambda-input/temp/expl3-script/read-final4-interp.log`. The Script
reconstructor must treat an undecodable candidate span as non-faithful; a
required undecodable source gap still needs an explicit issue.

The reconstructor now falls back to canonical token serialization when a
candidate source span cannot be decoded; an undecodable required gap remains an
explicit `tex-reconstruct-missing-origin` issue. Two focused cases and all 91
existing native text comparisons pass in interpreter and JIT, including
sampled forced GC with poisoning for the focused cases.


## Script TeX rejects expl3 token-list equality conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `NN` and `nn` variants. Other variants and
conditional AST source mapping remain open; S1.8 and D10.2.2 apply.

`\tl_if_eq:NNTF\l_tmpa_tl\l_tmpb_tl{T}{F}` and
`\tl_if_eq:nnTF{A}{A}{T}{F}` stop Script expansion with
`tex-unsupported-primitive`, while native selects the true branch. Native
also distinguishes a braced group from its unbraced contents. The comparisons
and diagnostic evidence are `../lambda-input/temp/expl3-script/tl-eq-before.ls`
and `.out`. The implementation must preserve TeX token identity, category
codes, full meanings for N arguments, conditional branch selection and source
mapping; AST qualification remains separate.

Script now implements `\tl_if_eq:NNTF/NNT/NNF` and `nnTF/nnT/nnF`.
The N variants compare complete TeX meanings and the n variants compare token
identity without expansion. Thirty-seven native text and diagnostic cases and
three safe AST checks cover branch selection, scope, category codes and `\edef`
in interpreter and JIT, including forced GC with poisoning. Conditional AST
mapping remains explicitly guarded.


## Script TeX rejects expl3 token-list stringification — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `V` inputs.
Unsupported variable types and stringification AST mapping remain open; S1.8
and D10.2.2 apply.

In Script expl3 mode both variants stop with `tex-unsupported-primitive`, while
native produces string tokens. Native output also serializes control sequences,
groups and spaces before emitting TeX-safe text. Evidence:
`../lambda-input/temp/expl3-script/tl-to-str-before.ls/.out`. A port needs
native token/category and serialization behavior, including variable lookup and
source mapping, not only ordinary text examples.

The Script port serializes tokens into other-category characters and spaces,
with variable lookup and the tested `\edef` source tracing. Forty-one native
text and diagnostic comparisons and three guarded AST checks pass interpreter
and JIT, as do 19 focused regressions and 13 sampled forced-GC/poison cases.
Tests include category-sensitive
characters, control sequences, scoping, escape characters, empty values and
nested definitions. The temporary trace exemption is cleared when a generated
token is stored in a definition, so its later macro expansion retains its
correct foreign origin.


## Script TeX rejects expl3 token-list counting — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `N` inputs. Other
variable types and AST source mapping remain open; S1.8 and D10.2.2 apply.

Both variants stop Script expansion with `tex-unsupported-primitive`. Native
counts `ABC` as three items, ignores a top-level space, and counts a braced
group as one item. Reproduction and native results are
`../lambda-input/temp/expl3-script/tl-count-before.ls/.out`. A correct port
must preserve unexpanded TeX token and group semantics, variable lookup, and
source tracing; AST mapping needs independent qualification.

The Script port counts unexpanded items, skipping top-level space tokens and
treating balanced groups as one item. Thirty-seven native text/diagnostic
comparisons and three guarded AST checks pass in interpreter and JIT, including
nested groups, scope, macro meanings, `\edef` and a 256-token input. Thirteen
sampled GC/poison cases and ten focused regressions pass both backends.


## Script TeX rejects raw inline verbatim commands — 2026-11-12

**Status:** PARTIALLY FIXED for direct-source, terminated `\verb` with an
ASCII delimiter. Unterminated input, non-ASCII delimiters, macro-sourced
commands, `\lstinline` and other raw forms, and AST mapping remain open;
S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\verb|a_% b|`,
`\verb*+x y+` and `\lstinline|a_% b|`; native retains the raw bytes and
continues after the closing delimiter. Evidence is
`../lambda-input/temp/expl3-script/verb-before.ls/.out`. A correct port must
read from the active physical source without interpreting TeX category codes,
respect direct-source eligibility, delimiters, reader position, source spans,
EOF diagnostics and constructor argument mode. Raw environments and command
variants need separate qualification.

The staged direct-source implementation found a reconstruction defect for raw
spans emitted from an included file: converting an echoed span to a temporary
source span dropped its main-file emission origin, so reconstruction returned
`tex-reconstruct-missing-origin`. The failing comparison is
`../lambda-input/temp/expl3-script/verb-final2-interp.out`. The current staged
reconstructor now retains the source token's emission origin. All 91 existing
native reconstruction comparisons pass in interpreter and JIT. Raw spans also
remain distinct from echoed definition spans.

The Script reader now captures the tested `\verb` spans from direct main and
included sources without tokenizing their contents, and advances the active
physical reader. Sixteen inline native text/diagnostic cases and one file-backed
case pass in interpreter and JIT, along with eight focused regressions and
sampled forced-GC/poison checks.
Tests cover Unicode content, special TeX characters, aliases, line endings,
source-span classification, and explicitly unsupported boundaries. The
digester rejects raw inline source mapping until it is qualified.

Native LaTeX AST comparison for valid verbatim containing `%` or an apparent
`\end{document}` is additionally blocked by input issue
[LI-018](../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-018): native TeX
preserves those spans, but its LaTeX environment scan interprets their contents.


## Script TeX rejects expandable engine identity primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested identity expansion. Font-name AST
source mapping and downstream malformed-input recovery remain open; S1.8 and
D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\jobname`,
`\fontname\nullfont` and `\pdftexrevision`, while native expands them to
`texput`, `nullfont` and `27` respectively for string input. Reproduction and
native diagnostics are `../lambda-input/temp/expl3-script/fontname-before.ls`
and `.out`. A port must preserve source-dependent job names, font identity and
size formatting, expanded-token behavior and diagnostics. File-backed cases
and AST source mapping need separate qualification.

Script now expands `\jobname` from the explicit `base_path` option (or
`texput` when absent), `\fontname` from the selected font identity and size,
and `\pdftexrevision` to the native revision. Thirty-three string-input and
four file-backed native text/diagnostic comparisons pass in interpreter and JIT,
including included-file job names. Font-name AST source mapping remains guarded;
other tested AST outcomes match native. Sixteen focused regressions and sampled
forced-GC/poison checks pass both backends.


## Script TeX stops on missing font identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for font-identifier scanning and tested
`\fontname` recovery. Downstream numeric and dimension recovery remains open;
S1.8 and D10.2.2 apply.

`\fontdimen2\bad` and `\hyphenchar\bad=3` stop Script expansion at
`tex-missing-font`, while native reports the diagnostic, pushes the offending
token back, uses `nullfont`, and continues with further diagnostics and
output. Native `scan_font_ident` in `lambda/input/input-tex-scan.cpp` implements
that recovery. `../lambda-input/temp/expl3-script/font-recovery-before.ls/.out`
contains the native and Script evidence, including a `\fontname` lookahead case
in the staged identity implementation. Correct support must preserve input
position, diagnostic metadata, downstream scans and group behavior.

The shared font scanner now records `tex-missing-font`, restores the offending
token and returns `nullfont`. Native comparisons cover undefined and character
identifiers, EOF, definition lookahead and included-file transitions in both
backends. Later numeric and dimension scans can still stop on their own errors;
those diagnostic recovery paths remain open.


## Script TeX retains a stale source line after main input EOF — 2026-11-12

**Status:** FIXED for tested main-input EOF diagnostics and active included
readers. Broader malformed-input recovery remains open; S1.8 and D10.2.2 apply.

With staged `\fontname` and missing-font recovery, the source `\fontname`
produces the correct `tex-missing-font` text and offset, but Script attaches
`line: 1` while native has no line attribute. Evidence is
`../lambda-input/temp/expl3-script/fontname-eof-detail.ls/.out` and
`fontname-eof-state.ls/.out`. `stream.next` retains the previous root reader
when `reader.next` returns null, and `record_diagnostic` consequently mistakes
that stale reader for an active source. Correct recovery must distinguish an
exhausted main reader from a still-active included reader and preserve native
metadata without suppressing the diagnostic.

The stream now records root EOF separately while retaining its source data;
active included readers remain visible. Diagnostic recording omits the stale
line at exhausted root EOF and preserves a line when a scan resumes in an
included or parent file. The identity tests cover these cases and raw-reader
eligibility in interpreter and JIT, including sampled forced GC with poisoning.


## Script TeX rejects deterministic pdfTeX random primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested deterministic random and seed behavior.
Malformed numeric recovery and AST source mapping remain open; S1.8, S12.1.1v2
and D10.2.2 apply.

The Script engine reports `tex-unsupported-primitive`, while native returns 27
for the default-seeded `\pdfuniformdeviate 100`, 95 after seed 1, zero for
`\pdfnormaldeviate`, and 12345 for the initial `\pdfrandomseed`. Evidence:
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native uses a
32-bit deterministic linear congruential sequence, an explicitly settable
seed, and a fixed zero normal deviate. Correct support must preserve state,
scanning, grouping, arithmetic, expansion and diagnostic behavior.

Script now keeps native's 32-bit state, using exact-range intermediate products,
including seed assignment, zero and negative bounds, grouped and nested scans,
and the native fixed-zero normal deviate. Sixty-one string-input comparisons,
one included-file comparison, five safe AST checks, 18 focused regressions and
sampled forced-GC/poison tests pass in interpreter and JIT. The digester
explicitly rejects unqualified random and timer source mappings.

## Script TeX rejects pdfTeX timer primitives — 2026-11-12

**Status:** FIXED for the native input engine's zero-valued timer model.
AST source mapping remains guarded; S1.8 and D10.2.2 apply.

`\pdfresettimer\the\pdfelapsedtime` stops Script at
`tex-unsupported-primitive`, while native emits zero. Reproduction is
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native's timer model
must be verified before porting its observable behavior.

The native input engine returns zero for elapsed time and ignores reset; Script
now reproduces that deterministic model. Numeric and `\the` uses are included
in the native comparison suite.


## Script TeX loses definition echo for the-expansion — 2026-11-12

**Status:** FIXED for tested numeric, dimension, glue and token-register
`\the` values in echoed definitions. Broader reconstruction remains open;
S1.8 and D10.2.2 apply.

For `\edef\x{\the\count0}\x`, Script reconstructs `0` while native
reconstructs the original definition followed by `0`. The same discrepancy
occurs for `\tolerance`, `\inputlineno`, `\eTeXversion` and `\dimen0`;
`\number\count0` correctly suppresses that echo. Evidence is
`../lambda-input/temp/expl3-script/the-edef-before.ls/.out`. Numeric and
dimension tokens produced by `\the` lack the internal-origin trace exemption
already used by font-token `\the`; a fix must cover relevant quantities and
clear the exemption when tokens enter a stored definition.

The same defect affects `\the\toks0`: characters, control sequences and
nested groups from a token register suppress an echo that native retains. See
`../lambda-input/temp/expl3-script/the-toks-detail.ls/.out`. The staged fix
also marks these directly inserted tokens as internal to the current trace;
`the-toks-detail2.out` matches native for four cases in the interpreter.

Numeric, dimension and glue text and token-register contents now retain their
internal trace origin only while scanning the current definition. Tests cover
nested definitions, `\number`, macros in register indices, and global, long and
protected prefixes in both backends. They are part of the same native
comparison and focused-regression runs above.


## Script TeX rejects box movement primitives — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes and AST source mapping remain open; S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for these commands.
Native turns `\raise1pt\hbox{B}` into `\raisebox{1.0pt}{B}`, negates the
dimension for `\lower`, and emits the moved inline box's contents for
`\moveleft` and `\moveright`. Evidence is
`../lambda-input/temp/expl3-script/move-before.ls/.out`. A correct port must
scan the dimension and box, preserve grouping, hooks and source origins, and
handle registered boxes without assuming they are empty; AST mapping requires
separate qualification.

Script now scans movement dimensions and executes inline box bodies with the
native wrapper or discarded shift, preserving group scope, hooks and source
origins. Thirty-five string-input and one included-file native comparisons,
three safe AST checks, 16 focused regressions and 16 sampled forced-GC/poison
cases pass in interpreter and JIT. Registered-box movement remains explicitly
unsupported; the digester guards unqualified movement source mapping.


## Script TeX rejects shipout of inline boxes — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes, cross-boundary `\unskip` and AST source mapping remain open;
S1.8 and D10.2.2 apply.

`\shipout\hbox{B}` stops Script with `tex-unsupported-primitive`, while the
native input engine discards the box's emitted contents and continues; global
assignments within the box still take effect. Evidence is
`../lambda-input/temp/expl3-script/shipout-before.ls/.out`. A correct port
must preserve box grouping, hooks, assignment effects, diagnostics, and source
mapping; registered boxes require the separate box-register implementation.

Script now executes inline boxes and discards their emitted material, retaining
global assignments, grouping, hooks, terminal messages and diagnostics. Twenty-one
string-input native comparisons, four message/diagnostic comparisons, one
included-file comparison, three safe AST checks, 16 focused regressions and
sampled forced-GC/poison checks pass in interpreter and JIT. Cross-boundary
`\unskip` remains explicitly unsupported as tracked below and in LI-019.


## Script TeX rejects cross-boundary unskip in discarded material — 2026-11-12

**Status:** OPEN. Blocks output parity when `\unskip` inside discarded insert,
vadjust, leader or shipout material removes preceding output; S1.8 and D10.2.2
apply.

For `\begin{document}A \insert0{\unskip}B\end{document}`, Script explicitly
reports `tex-unsupported-primitive` / `insert cross-boundary unskip`, while
native emits `AB`. The same occurs for `\vadjust` and discarded leader boxes;
the inline `\shipout` slice has the same limit. Evidence is
`../lambda-input/temp/expl3-script/discard-unskip-before.ls/.out` and
`shipout-edge-probe.ls/.out`. The discard scope stores an output-length
boundary and cannot currently represent removal of pre-existing output without
corrupting its later truncation. A fix needs to preserve native mutation and
nested discard behavior rather than dropping the error.

A staged boundary-adjustment candidate matches 16 of 18 native text cases but
differs on two nested/echo cases; it also exposes unqualified AST mappings.
The native differences are caused by input issue
[LI-019](../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-019): native leaves
stale discard boundaries after the removal and can leak discarded output.
Evidence is `../lambda-input/temp/expl3-script/discard-unskip-detail.ls/.out`,
`discard-unskip-detail.log` and `discard-unskip-ast-probe.out`. The candidate
is uninstalled; the existing explicit unsupported result remains in place.


## Script TeX rejects vcenter boxes — 2026-11-12

**Status:** PARTIALLY FIXED for tested `\vcenter` expansion and grouping.
Math/document AST source mapping and broader group semantics remain open; S1.8
and D10.2.2 apply.

Script expansion stops with `tex-unsupported-primitive`, while native emits a
`\vcenter{...}` wrapper and processes its body; an optional `to` dimension is
scanned rather than emitted. Evidence is
`../lambda-input/temp/expl3-script/vcenter-before.ls/.out`. A correct port must
preserve box specification scanning, grouping, body effects, hooks and source
mapping. Math and document AST digestion require separate qualification.

An isolated digester experiment can recognize the native math `box_transform`
shape, but six probes remain explicitly unsupported because the synthetic
wrapper lacks qualified math or command source mapping. Evidence is
`../lambda-input/temp/expl3-script/vcenter-ast-probe.ls/.out`. The engine slice
keeps an explicit AST guard until reconstruction is qualified.

Script now scans optional size specifications and emits the native wrapper
while preserving tested scope and body effects without injecting `\everyvbox`.
Twenty-six string-input and one included-file native comparisons, three safe AST
checks, 17 focused regressions and sampled forced-GC/poison tests pass both
backends. The digester explicitly guards unqualified `\vcenter` source mapping.


## Script TeX stops on native-diagnosed numeric math commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script now scans tested numeric operands for
`\mathchar`, `\delimiter`, `\radical` and `\mathaccent`, reports the native
diagnostic and continues. S1.8 and D10.2.2 apply. Numeric-math AST source mapping
and malformed operand recovery remain unsupported; the native input engine also
does not render these numeric math characters.

Previously Script reported `tex-unsupported-primitive` and stopped. Native scans the numeric
operand, reports `tex-unsupported-mathchar` with source metadata and continues
with subsequent output. Reproduction and diagnostic records are
`../lambda-input/temp/expl3-script/math-number-before.ls/.out`. A compatible
port must preserve operand expansion and scanning, diagnostics, source
locations and continued processing; native does not render these numeric math
characters, and broader math support remains separate. The port matches 32 string
cases and one included-file case, with three guarded AST checks, 17 focused
regressions and sampled forced-GC checks in both execution backends. See
`test/lambda/latex/test_tex_script_numeric_math.ls`.


## Script TeX rejects native inspection commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script handles tested `\show`, `\showbox`,
`\showlists`, `\showgroups`, and `\showifs` cases; S1.8 and D10.2.2 apply.
Inspection AST source mapping, registered boxes, and malformed operand recovery
remain unsupported. The separate `\showthe` issue records its tested support.

Previously Script reported `tex-unsupported-primitive`. Native reports a token's meaning
for `\show`, prints void-box information for `\showbox`, and treats the three
list/group/conditional inspection commands as no-ops in this input model.
Reproduction, native messages, text and diagnostics are
`../lambda-input/temp/expl3-script/show-before.ls/.out`. A correct port needs
token identity, register scanning, terminal buffering and box-state behavior;
`\showthe` remains a separate inspection operation. Fifty native string
comparisons, one included-file comparison, three guarded AST checks, 21 focused
regressions and sampled forced-GC tests pass both execution backends; see
`test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports dimension and glue register meanings — 2026-11-12

**Status:** FIXED for tested dimension and glue register aliases in `\meaning`
and `\show`; S1.8 and D10.2.2 apply.

For `\dimendef\d=3 \meaning\d`, Script previously output `undefined`, while the
native engine prints the meaning `\dimen3`. Similarly `\skipdef` and
`\muskipdef` aliases incorrectly reported `undefined` instead of `\skip5`
and `\muskip7`. Reproduction and output are
`../lambda-input/temp/expl3-script/meaning-probe.ls/.out`. The shared Script
`print_meaning` previously omitted dimension and glue register kinds; native
`print_meaning` handles all register kinds. The shared implementation now
handles them, covered by `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports LaTeX constructor meanings — 2026-11-12

**Status:** FIXED for tested LaTeX constructor meanings in `\meaning` and
`\show`; S1.8 and D10.2.2 apply.

For `\show\section`, Script previously printed `> \section=\section.` while native
prints `> \section=\protected macro:->\section .`. The same missing
protected-macro representation affects constructor symbols such as `\%`.
Evidence: `../lambda-input/temp/expl3-script/show-extra-probe.ls/.out`. The
Script `print_meaning` constructor branch previously omitted the native protected
macro wrapper and control-sequence spacing; it now reproduces both. Tests are
in `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX rejects showthe — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\showthe` quantities and token lists are
supported; inspection AST source mapping and malformed operand recovery remain
unsupported. S1.8 and D10.2.2 apply.

Previously Script stopped with `tex-unsupported-primitive`. Native prints the
scanned internal quantity or token-list value and continues: `\count0=12
\showthe\count0` prints `> 12.`, while `\toks0={abc} \showthe\toks0`
prints `> abc.`. Evidence: `../lambda-input/temp/expl3-script/showthe-before.ls/.out`.
The shared Script scanner now captures the result without expanding token-list
contents, including empty lists and nested `\the` expressions. Forty-one string
cases, one included file, two guarded AST checks, 29 existing regressions and
sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_showthe.ls`.


## Script LaTeX digester misinterprets generated meaning text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\meaning` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\meaning\section\end{document}`, Script expansion
matches native text, but digestion returns successful paragraph text containing
literal backslashes. Native produces `textbackslash` nodes with source offsets.
The same mismatch occurs for dimension and glue register aliases and constructor
symbols. Four native comparisons are recorded in
`../lambda-input/temp/expl3-script/meaning-ast-probe.ls/.out`. The Script
digester consumes generated character tokens directly and does not reparse
native escaped output; the required source mapping remains unimplemented. The
guard is reset after kernel preload and preserves ordinary text digestion. See
`test/lambda/latex/test_tex_script_generated_text_digest.ls`.


## Script LaTeX digester misinterprets generated string text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\string` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\string\foo\end{document}`, Script returns a successful
AST with a literal `\foo` paragraph string, while native creates a
`textbackslash` node and text `foo`. `\string\%` has the same mismatch; a
plain `\string A` matches native. Evidence:
`../lambda-input/temp/expl3-script/string-ast-probe.ls/.out`. Generated
character tokens bypass the native text escaping and source-offset mapping.
The guard and ordinary text cases are covered by
`test/lambda/latex/test_tex_script_generated_text_digest.ls`; 12 focused
regressions and sampled forced-GC checks pass in both backends.


## Script expl3 integer variables and comparisons are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Script now supports tested `\int_abs:n`,
`\int_min:nn`, `\int_max:nn`, `\int_compare:nNnTF/T/F` with direct
relations or zero-argument macros expanding to one relation, and chained
`\int_compare:nTF/T/F` expressions. Integer variables, predicate variants,
more complex `nNn` relations, delimiter-consuming operands, overflow recovery
and integer AST source mapping remain open; S1.8 and D10.2.2 apply.

With `script_expl3: true`, Script previously stopped with `tex-unsupported-primitive` for
`\int_new:N`, `\int_set:Nn`, `\int_zero:N`, `\int_incr:N`,
`\int_compare:nNnTF`, `\int_compare:nTF`, `\int_abs:n`, and
`\int_max:nn`. Native produces the expected values in seven comparisons,
including local scratch variables. Evidence:
`../lambda-input/temp/expl3-script/int-api-before.ls/.out`. A port needs
register identity and scoping, expression parsing and expansion behavior,
conditional branch semantics, and guarded AST source mapping. The installed
numeric helpers share the integer-expression scanner with `\int_eval:n` and
match 51 string cases, one included file, two guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both backends. See
`test/lambda/latex/test_tex_script_expl3_int_math.ls`. The direct-relation
comparison forms match 38 string cases, one included file, two guarded AST
checks, 13 focused regressions and sampled forced-GC checks in both backends;
see `test/lambda/latex/test_tex_script_expl3_int_compare.ls`. Zero-argument
relation macros that expand to a single `<`, `=` or `>` token are now supported:
34 string cases, one included file, three bounded unsupported cases, three safe
AST checks, 19 focused regressions and sampled forced-GC checks pass both
backends; see `test/lambda/latex/test_tex_script_expl3_int_relation.ls`.
Some conditional and parameter-taking relation expansions remain unsupported; the
original mismatch and examples are in
`../lambda-input/temp/expl3-script/int-compare-relation-before.ls/.out` and
`int-relation-debug.ls/.out`.
The single-expression forms support `=`, `==`, `!=`, `<`, `>`, `<=`, `>=` and
short-circuited chains; 44 string cases, one included file, three guarded AST
checks, 18 focused regressions and sampled forced-GC checks pass both backends.
See `test/lambda/latex/test_tex_script_expl3_int_compare_expr.ls`. Operand
macros that consume internal comparison delimiters still report an issue where
native continues; evidence: `../lambda-input/temp/expl3-script/int-marker-malformed-probe.ls/.out`.


## Script expl3 preload lacks native count register allocations — 2026-11-12

**Status:** OPEN. Blocks observable count allocation and register-identity parity
for the Script expl3 profile; S1.8 and D10.2.2 apply.

With `script_expl3: true`, `\ExplSyntaxOn\number\count10\ExplSyntaxOff`
succeeds but prints `24`; native prints `124`. Native `\meaning\l_tmpa_int`
is `\count26`, while Script reports `undefined`, and the next native
`\int_new:N` allocation is `\count125`. The bundled expl3 source defines
`\int_new:N` through `\newcount` and allocates numerous internal and scratch
registers. Evidence: `../lambda-input/temp/expl3-script/int-allocation-before.ls/.out`
and `lmd/package/doc/latex/tex/expl3-code.tex` near `\int_new:N`. A port must
preserve the shared allocator, identity and scope; setting observed counts or
register numbers to constants would hide the missing initialization.


## Script expl3 preload omits standard integer constants — 2026-11-12

**Status:** PARTIALLY FIXED. The non-allocating `\c_zero_int` and
`\c_one_int` now retain their native TeX meanings; register-backed constants
such as `\c_max_int` and the broader allocation state remain open. S1.8 and
D10.2.2 apply.

Native `\meaning\c_zero_int` is `\char"0` and `\meaning\c_one_int` is
`\mathchar"1`; Script previously reported both as undefined. Native `\int_eval:n` can
also read `\c_max_int`, while Script reports `tex-missing-number`. Evidence:
`../lambda-input/temp/expl3-script/int-constants-before.ls/.out`; the bundled
expl3 source defines these values and their TeX meanings in
`lmd/package/doc/latex/tex/expl3-code.tex`. Some integer constants allocate
registers, so their initialization is coupled to the separately recorded
count-allocation mismatch. `\c_two_int` and `\c_minus_one_int` were also
tested and are undefined in the native profile; do not invent them. The port
matches 32 string cases, one included file and five safe AST cases; see
`test/lambda/latex/test_tex_script_expl3_int_constants.ls`.


## Script LaTeX digestion misparses emitted expl3 math character names — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\mathchardef` control names that the
native LaTeX scanner would split are explicitly guarded; native AST source
mapping remains open. S1.8 and D10.2.2 apply.

With the Script expl3 profile, directly emitting `\c_one_int` inside a document
returns a successful generic `c_one_int` node, while native reconstruction
reparses its emitted command and produces an accent node followed by text. A
separate `\mathchardef\foo_bar` reproduction yields a generic `foo_bar` node
in Script and a `foo` node plus `_bar` text natively. Ordinary `\foo` and
`\alpha` math-character names match. Evidence:
`../lambda-input/temp/expl3-script/int-one-ast-probe.ls/.out`,
`mathchardef-expl-native-only.ls/.out`, and
`mathchardef-expl-script-only.ls/.out`. The direct-token digester does not
reparse those emitted names with the native LaTeX scanner's catcodes. The
guard preserves successful native-equal ASTs for ordinary control names;
coverage is in `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script TeX profile exceeds budget time on expl3 math-character input — 2026-11-12

**Status:** PARTIALLY FIXED. The tested exact self-recursive package bootstrap
now returns an explicit unsupported issue promptly after the package is attempted.
Other recursive macro shapes remain subject to the expansion budget; S1.8 and
D10.2.2 apply.

With `script_expl3` omitted, expanding the source in
`../lambda-input/temp/expl3-script/mathchardef-expl-disabled-small-budget.ls`
did not finish within a 10-second process timeout even with expansion budget
5000; the subprocess was terminated by the test. The same source with
`script_expl3: true` finishes in under a second in this local run, and native
LaTeX parsing finishes. See `mathchardef-expl-disabled-small-budget.out`,
`mathchardef-expl-script-only.ls/.out`, and
`mathchardef-expl-native-only.ls/.out` in that directory. At initial isolation it was unconfirmed
whether the slowdown was a non-progressing loop, excessive work outside the
expansion counter, or another cause. Further isolation shows `\ExplSyntaxOn`
alone reproduces the issue when expl3 resources and adapters are unavailable.
Budgets 500, 1000 and 2000 terminate with `tex-budget`; 5000 exceeded five
seconds. At budget 1000 the engine had accumulated 8000 output items and one
unknown `expl3` package. The kernel stub recursively requires `expl3` and
re-enters `\ExplSyntaxOn`; an unknown passthrough package does not replace the
stub. Evidence: `explsyntax-budget-results.out`, `explsyntax-cycle-detail.ls/.out`
and `mathchardef-timeout-isolates.out` in the same directory. The repeated
bootstrap is confirmed; quadratic cost from growing output is a plausible but
unconfirmed contributor. A general check now detects zero-argument macros whose
body requires an already attempted package and then re-enters the same meaning.
It preserves direct unknown-package passthrough, redefined `\RequirePackage`,
and packages that replace the bootstrap. Eleven behavioral cases, 18 focused
regressions and forced-GC checks pass both execution backends; see
`test/lambda/latex/test_tex_script_bootstrap_cycle.ls`.


## Script TeX reconstruction cannot emit an expl3 zero character — 2026-11-12

**Status:** FIXED for tested direct and macro-sourced `\c_zero_int` output;
numeric use is also supported. S1.8 and D10.2.2 apply.

With the staged non-allocating expl3 integer constants, expanding
`\ExplSyntaxOn\c_zero_int\ExplSyntaxOff` succeeded, but reconstruction
previously returned `tex-reconstruct-missing-origin` and no bytes. Native TeX output has
one NUL byte. Numeric byte counts and equality, which avoid the separately
recorded CLI NUL truncation, are in
`../lambda-input/temp/expl3-script/int-zero-nul-probe.ls/.out`. The shared `\chardef` emission branch omitted the reconstruction origin; see
`int-zero-origin.ls/.out` and the separate chardef provenance issue. The fixed
port reconstructs the same one byte as native; numeric evidence is in
`int-zero-nul-after.out`, and coverage is in
`test/lambda/latex/test_tex_script_chardef_origin.ls`. The direct-token LaTeX
AST also matched native in `int-constants-ast-probe.ls/.out`.


## Script TeX chardef emission loses source origin — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\chardef` characters now carry a
main-source origin for tested reconstruction cases; constructor-argument parity
and broader source mapping remain separate. S1.8 and D10.2.2 apply.

For `\chardef\c=65 \c`, Script expansion succeeded but reconstruction
previously returned `tex-reconstruct-missing-origin`; native emits `A`. The same occurred for
escaped punctuation, Unicode and macro-sourced uses. Evidence:
`../lambda-input/temp/expl3-script/chardef-origin-before.ls/.out`. The shared
`step` branch creates a character token with the control token's start/end
but omits file, read-offset and emission-origin fields. A staged
`\c_zero_int` token shows these fields are null in
`int-zero-origin.ls/.out`; the missing reconstruction origin is confirmed.
Constructor-argument behavior is separately blocked by the existing
constructor expansion issue and must not be inferred from the last probe. The
port matches 25 string cases, one included file, three safe AST checks, two
ordinary math-character AST cases, 24 focused regressions and sampled forced-GC
checks in both backends; see `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script expl3 integer zero and parity conditionals are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\int_if_zero:nTF/T/F`,
`\int_if_odd:nTF/T/F` and `\int_if_even:nTF/T/F` are supported. Predicate
variants, conditional evaluation order for nested diagnostics, malformed or
overflow recovery and integer AST source mapping remain open; S1.8 and D10.2.2
apply.

Previously Script stopped with `tex-unsupported-primitive` for these operations; native
selects the expected branches for zero, negative odd and positive even
expressions. Five native comparisons are in
`../lambda-input/temp/expl3-script/int-tests-before.ls/.out`. A port must
preserve expansion, signed parity, branch laziness, group behavior and guarded
AST source mapping; predicate variants remain separate. The port matches 44
string cases, one included file, three guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both execution backends; see
`test/lambda/latex/test_tex_script_expl3_int_tests.ls`. Branch evaluation order
for nested diagnostic-producing expressions remains an open issue recorded in
`Script expl3 integer conditionals read branches before evaluation`.


## Script expl3 integer division and modulo are unsupported — 2026-11-12

**Status:** OPEN. Blocks `\int_div_truncate:nn`, `\int_div_round:nn` and
`\int_mod:nn` in the Script expl3 profile; S1.8 and D10.2.2 apply.

Script stops with `tex-unsupported-primitive`; native distinguishes truncation
toward zero from nearest rounding and preserves the dividend's sign in the
remainder for the tested cases. Six native comparisons are in
`../lambda-input/temp/expl3-script/int-div-before.ls/.out`. Division-by-zero,
overflow recovery, expansion order and AST source mapping must be qualified.
The staged implementation explicitly stops with `tex-arith-overflow` for a
zero divisor, while native emits `0` and continues with a diagnostic; evidence:
`../lambda-input/temp/expl3-script/int-div-zero-probe.ls/.out`.


## Script expl3 integer conditionals read branches before evaluation — 2026-11-12

**Status:** OPEN. Blocks native diagnostic source metadata and potentially
branch-tokenization parity for nested integer operations; S1.8 and D10.2.2
apply. The tested ordering cause is confirmed for Script; native's exact
internal sequencing is inferred from source offsets and remains to be fully
qualified.

In the staged division port, `\int_compare:nTF{\int_mod:nn{1}{0}=0}{Y}{N}`
emits the correct `YX` text and arithmetic-overflow diagnostic but reports
offset 55, while native reports offset 49. The analogous `\int_if_zero:nTF`
case reports 59 instead of 53. The Script comparison and zero-test functions
read both branch arguments before evaluating the expression, advancing the
main-source cursor; native offsets indicate evaluation before those branch
tokens are consumed. Evidence:
`../lambda-input/temp/expl3-script/int-div-zero-offset-mismatch.ls/.out`. The
port must preserve branch laziness, source trace, grouping and diagnostics.
Conversely, the Script binary integer helpers evaluate their first operand
before collecting the second. Nested division diagnostics report offset 48
instead of native 51 for `\int_max:nn`, and 51 instead of 54 for
`\int_div_truncate:nn`; `nNn` comparison also reads branches too early.
Evidence: `../lambda-input/temp/expl3-script/int-order-binary-before.ls/.out`.
The first staged ordering revision regressed existing `nNn` comparisons when
an operand ended in `\relax`: its synthetic expression terminator was read as
a branch. Both backends produced two failed text comparisons in
`int-order-regression-{interp,jit}/expl3_int_relation.out` under that directory.
The installed implementation is unaffected; a corrected revision is being
qualified before installation.


## Concurrent integer-port preflight processes terminated with status 137 — 2026-11-12

**Status:** OPEN, cause unconfirmed. This limits highly concurrent qualification;
it does not currently block smaller test runs.

Ten concurrent interpreter/JIT preflight processes against the staged integer
ordering change exited with status 137 before producing result output. Sampled
stderr contained no fatal runtime diagnostic. A subsequent isolated interpreter
run of the unchanged integer-helper golden passed in 31.33 seconds. Evidence:
`../lambda-input/temp/expl3-script/int-order-concurrent-termination.out` and
`int-order-isolated-math.out/.log` in that directory. Resource pressure, external
termination and a runtime defect have not been distinguished; no cause is claimed.


## Script expl3 integer helpers mishandle explicit relax — 2026-11-12

**Status:** OPEN. Blocks native parity for some explicit expression terminators
and blocks qualification of the staged integer-division port; S1.8 and
D10.2.2 apply.

With `script_expl3: true`, `\int_abs:n{-1\relax}` emits `1`, while native emits
`1 `; `\int_min:nn{1\relax}{2}` stops with `tex-missing-number`, while native
recovers with a diagnostic and emits `0 `. The installed profile also stops on a
nested `\int_max:nn{\int_abs:n{-1\relax}\relax}{2}` where native continues with
two diagnostics. Evidence: `../lambda-input/temp/expl3-script/int-order-relax-detail.ls`
and `int-order-relax-live.out` in that directory. The staged ordering change
separates synthetic terminators and fixes the existing `nNn` branch regression,
but still reports a successful, different result for some helper arguments; for
example staged `\int_div_round:nn{7}{2\relax}` emits `4` without a diagnostic,
while native emits `4)` with `tex-missing-paren`. Evidence: `int-order-relax-detail.out`
and `int-div-fixed-interp.out` / `int-div-fixed-jit.out` in that directory. The candidate has not
been installed. Correct behavior requires modeling each native macro's expression
and terminator flow, not discarding the terminator or changing existing goldens.


## Script expl3 sequence queries and reversal are unsupported — 2026-11-12

**Status:** OPEN. Common sequence operations remain unavailable in the Script
expl3 profile; S1.8 and D10.2.2 apply.

After declaring a sequence, `\seq_if_empty:NTF`, `\seq_item:Nn`,
`\seq_reverse:N` and `\seq_if_in:NnTF` stop with `tex-unsupported-primitive`.
Native selects the expected empty/nonempty branches, returns `b` for index 2
and `c` for index -1, reverses `a,b,c` to `c,b,a`, and finds `b` in the
sequence. Reproduction and results:
`../lambda-input/temp/expl3-script/seq-api-before.ls/.out`. A port must retain
TeX meaning comparison, expandable/protected behavior, group scope, token
identity and source mapping.
