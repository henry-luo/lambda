# Existing-ruling ledger follow-up

## Checkpoint

- Request: implement the no-further-ruling shortlist from the 2026-11-12 ledger review.
- Starting HEAD: `85cc95ad4`; clean working tree. Work is limited to Lambda-owned source and tests.
- Status: complete for the selected existing-ruling repairs and reconciliation. The optional complete native concurrency gate remains explicitly unqualified at sibling issue LI-017.
- Evidence and frozen starting executable: `temp/ledger-ruled-fixes/`.
- No commit, publication, sibling-source change or new semantic ruling is requested.

## Scope and acceptance

| Records | Existing authority | Acceptance |
|---|---|---|
| LR07-65, LR07-66 | S1.6, S9.1.2, S9.1.3v4, D8.3.1 | unchanged goldens on interp/AUTO/JIT, forced/poisoned GC; resolve stable census differences without hiding failures |
| LR01-21, LR01-23 | D8.3.1, D8.3.5, D8.5.1 | cache-disabled loads terminate; reuse expectations follow the actual tier contract |
| LR07-29, LR03-41 location only | S7.4.4 | consistent rich diagnostics and correct source spans; inherited-default policy remains open |
| LR13-4, LR13-7 | S10.1.1v5, D4.2.3, S7.4.4 | no silently dropped validator arms/path components; actual-value presence is explicit; debugging uses logging |
| LR02-2, LR02-3 | D2.3.4, D4.2.4, D4.3.1 | precise inference facts reach consumers without changing forward ABI or accepted program results |
| LR13-8 | S11.4.10, D4.2.5–D4.2.6 | stable accepted values avoid repeated validation; mutation and malformed/layout controls still reject |
| LR02-37, LR03-17, LR07-57/58/60/61 | S10.1.1v5, S2.3.1v2, S16.8.9v2, D6.2.1, D8.6 | qualify existing repairs and reconcile stale ledger claims; preserve unqualified configurations |

Root-cause investigation precedes each edit. A source marker alone does not prove a live defect.
Pending behavior policies and sibling-owned implementation stay outside this pass.

## Verification budget

Use focused fixtures/native units during implementation. At the completed milestone run
`make test-lambda-baseline`, `make test262-baseline`, the validator gate, and the
interpreter census if its stable differences are repaired. Run emission, optimization
and GC gates for compiler/lifetime changes. Measure performance only with a release
host if needed to qualify the optimization residues. Record focused versus aggregate
results and any reproduced control failures separately.

## Progress

- Prior read-only review reproduced LR07-65 and all five stable LR07-66 differences.
- LR02-37, LR07-58 and LR07-61 focused probes pass on the starting host.
- Five formerly failing Python emission methods pass on the starting host.

### Runtime milestone

- LR07-65 fixed: T0 captures each plain argument before evaluating later actuals; foreign JS arguments retain their existing membrane behavior.
- All five stable LR07-66 rows now pass: boxed inline errors keep their ordinary result transport; optional structural record reads retain absence and avoid unproven offsets; dense regions reject collecting literal/constraint match tests; error-free unary `not` uses truth classification; foreign JS calls refresh aliased array witnesses.
- Existing three-tier parity suite passes (3,000 ms); six original fixtures were added to its mandatory inventory.
- LR01-21 fixed by giving eager MIR loads their own artifact kind and bypassing artifact claims when MIR reuse is disabled. New native AST/JIT/AST regression passes.
- LR01-23 original cache assertions still fail (AST dependency retained, no MIR hit); inspect against current AUTO/prebuild policy before editing.
- Validator native tests initially failed to link: the generated static validation host omitted Lambda's adapter archive. Corrected the shared generator dependency closure and added a generator assertion. No sibling source was changed.
- Native pre-fix tests reproduce the 32-arm union truncation and 100-component path truncation. Implemented dynamic iterative union traversal and one shared dynamic path formatter; post-fix qualification pending.
- LR13-7's claimed null/zero actual-value loss does not reproduce under current Item tagging: the null diagnostic regression already passes. Added zero coverage, and will qualify logging separately rather than changing presence representation without a live bug.
- LR03-41 source cause is a synthetic field scope node with no source span; copy the field declaration span, preserving the existing override policy.
- A stalled LLDB attempt was stopped; optional-field root cause was established by isolated value probes instead.

### Validator, diagnostic and inference milestone

- Validator union traversal and full path reporting pass 3/3 focused native checks, including 65 arms and 160 path components. Canonical present-null and zero actuals already work; regression coverage retains them. Replaced the two magic type-name bounds with the shared `get_type_name` lookup.
- Validator CLI progress uses `log_debug`; the requested validation report remains visible with `--no-log`. Its focused native check passes.
- LR01-23 was a stale oracle: the original views/edits fixture is T0-supported under D8.3.1/D8.3.5. The revised test retains preceding eager-JIT verification and now requires AST ownership/hits on both supported AUTO runs. Together with the disabled-MIR-reuse regression, 2/2 cache checks pass.
- S7.4.4 diagnostics: source function names and rich non-callable errors are shared between tiers; speculative import prebuilds defer printing to the importing execution. Native three-tier diagnostics and the inherited-field source location pass 4/4 with the initial inference metadata check.
- Failed optimized typed-record construction reconstructs the complete source value from already evaluated operands on its cold path, preserving the declaration site and validator field path. The new effect-count control passes; neither producer reevaluation nor generic success construction is needed.
- Debug candidate SHA256 `2aad1b35a8d86770f1a444693891edc149c4c2635c3afb964a56530921e3715c`: 108/108 focused cases pass across interp/AUTO/JIT and normal/forced/seeded poison GC, including LR07-58, LR07-61 and the intermittent handler row.
- LR13-8: after one successful deep validation, immutable scalar-field domains can certify semantic admission without certifying physical member offsets (D4.2.3–D4.2.6). Private/mutable, nested-erased, nominal and value-dependent shapes retain checks. Representation misses are also cached. The new control records 39 semantic hits, 40 total union-cache hits and five deep validations; malformed scalar and literal-predicate mutations still reject, and COW snapshots survive.
- The first Lambda baseline passes emission 595/595, size/frame ratchets 25/25 and MIR GC 368/368, but is red overall (7,378/7,434). Mixed-arm precision exposes an existing refresh bug: a changed nested child adds an outer error to every enclosing `if`, even when a map/element retains it as data (S7.5.2v4). Repair by recomputing evaluated branch outcomes. One new fixture also printed an extra newline; corrected its own explicit print. The unrelated JS prebuild counter test fails in the parallel baseline and passes in isolation; retain evidence and recheck the final aggregate.
- First aggregate artifacts are `lambda-baseline-first-summary.json`, `opt-baseline-first.log` and `js-baseline-first.log` under the evidence directory. No failing oracle was removed or budget relaxed.

### Broader-control corrections

- Recomputing `if` outcomes resolves the false outer errors in recursive record construction and chart transforms. New mixed-result metadata coverage includes the original recursive union shape. The 57 optimization contracts now pass on the debug host; these are structural/counter checks, not a performance measurement.
- D4.3.1 permits stricter static rejection when inference becomes precise. Two formerly hidden source errors are corrected without changing their workloads or goldens: the hyphen code intermediate admits `int^` before its existing propagation; `procart.sample.jitter` propagates its checked conversion before binding a clean float. Both keep their runtime validation.
- EPS exposed a second T0 argument-order cause under S9.1.3v4. A later actual can reborrow and replace an earlier `var` home; the caller retained its earlier value word. Refresh every borrowed word from the authoritative home after actual evaluation. The new nested-borrow fixture reproduces `[[3], 1]` on the preceding interpreter against JIT's `[[7, 3], 2]`; the repair restores both it and EPS fonts.
- All 10 original LR07-29 diagnostic cases now agree exactly across interp/AUTO/JIT (`negative-final.json`). Existing array-write labels were already shared; several historical runtime reproducers now fail identically in the frontend. The new native dynamic-call control retains coverage below that frontend check.
- Validator registered gate: 44 passed and six skipped legacy-validator cases, 39/39 AST validator and 19/19 path/CLI checks. The legacy target also has 87 disabled historical cases; none was enabled, removed or credited. Additional native features 61/61 and input 14/14 pass. This is separate from the ordinary baseline's coverage (LR13-9).
- The first standalone concurrency run used a native host linked before the branch refresh repair. Its chart setup failed under those old types. Two death cases also hit macOS Objective-C's unsafe fork diagnostic; rerun the current native host using GTest's `threadsafe` death-test mode, without changing checks.
- The initial census was stopped after a rebuild invalidated its single-executable provenance. No coverage lists were refreshed from that run; rerun after the final host is frozen.

### Current qualified debug milestone

- Frozen final debug host SHA256 `3481c307340823771b58bde24a6e9dba8a3cf21d0af4b40982ec4cf6ec80e1bf`: 117/117 focused tier/GC cases pass (`focused-final-debug.json`). This includes the nested borrow regression, five stable census rows, the concurrent handler row, u32, wide-tail publication, precedence and semantic union caching.
- The complete 57-unit optimization contract gate passes (`opt-all-debug.log`); no timing result or cost-budget change is claimed. The native runtime metadata regression passes with mixed string/bool and string/null results, recursive union-return construction and a stable public boxed ABI.
- The original LR03-17 repeated explicit-key program is now required to fail with E242 under S2.3.1v2 / S16.8.9v2. Computed/spread collisions retain their existing last-writer-wins fixtures. Its pending-ruling claim is stale; this pass does not change duplicate-key policy.
- The shared-module concurrency setup also needed explicit acknowledgement of the now-visible chart result error. The chart and PDF stress sources retain their complete formatting/length workload and unchanged result checks, adding only postfix propagation.

### Aggregate Lambda qualification

- `make test-lambda-baseline` passes **7,435/7,435**, across 40 registered suites. Included gates: ordinary Lambda scripts/tier parity 2,315/2,315; structured errors 336/336; optimization 57/57; MIR emission 595/595; size/frame ratchets 25/25; MIR forced-GC 368/368. Registered Python emission selection also passes. `lambda-baseline-final.log` and `lambda-baseline-final-summary.json` preserve the receipts. No budget, skipped case or expected result was relaxed.
- The JS prebuild hit-count check that failed in the first parallel aggregate passes in the final aggregate; it also passed in isolation. No JS runtime or test assertion was changed for it.
- Terminal regression with `LAMBDA_DISABLE_MIR_CACHE=1` passes 10/10; the new AST/private-MIR/AST claim check passes in the focused cache group.

### Final argument-path review

- A pipe receiver is another first plain argument. The original ownership fixture rewritten as `values |> observe(change(values))` reproduces `[9, 9]` on the preceding T0 host, against JIT's required `[3, 9]`. Capture it from `AstBinaryNode::left`, the node that retains its source expression, before RHS/call actual evaluation. Reuse the common capture helper and preserve native/borrow handling. `proc/ledger_pipe_call_snapshot.ls` is a permanent mandatory three-tier control.
- Direct Premake's `lambda-exe` target writes `lambda-static.exe`, not the installed `lambda.exe`; the normal build performs installation. Focused verification uses the actual direct-build artifact; the normal baseline installs and checks it. No generated Lua was edited.
- The optional full concurrency run passes its preceding 97 checks and the 20-second shared-module execution test, then spins while destroying a scheduler in the second shared-module test. A one-second native sample locates `uv__run_prepare` through `lambda_uv_run_loop`/`lambda_scheduler_destroy`, with the main thread waiting in `pthread_join`. The shared libuv provider still has one global loop and prepare/check state, contrary to D6.4.4/RG8. Exact list corruption remains unconfirmed. Recorded **LI-017** in lambda-input's ledger and stopped this affected additional qualification under the project-boundary rule; no sibling source or workaround was changed. Evidence: `concurrency-complete.log`, `concurrency-hang.sample.txt`; native host SHA256 `c52e45d3446db09cf64903869b4c1a9efb4ec588c14649f822fd71c7c69c7096`.
- The preceding release host SHA256 `4a8de40a6a35d71285696b62c0be7447ba6bcf8967bdb51fb8c4b375f0b1cb74` passes Test262 **40,261/40,261**, with zero regressions, and optimization contracts **57/57**. This predates the final Lambda-only pipe capture; refresh the release/focused receipts after its complete baseline.

### Final broad Python review

- The complete current Python aggregate runs **152 methods** on the preceding frozen release. All Lambda runtime/emission methods pass. Its only failures are two subcases of `test_reference_goldens`: the Havlak and CD C2MIR ports print PASS but omit the existing computed result line that their unchanged typed Lambda goldens require. Both C entrypoints already compute and verify those results. Add only output of `result` / `collisions`; no workload, iteration count, timing, golden or assertion changes. Rerun the complete reference-golden method.
- Final debug executable SHA256 `1124c55ef863f1c6be71bbd40807b7cf19bdf47109549091094482d04ffdf3cb` passes **126/126** focused tier/GC cases, now including direct and indirect pipe receivers (`focused-verified-debug.json`).
- To avoid the shared `lambda-static.exe` output being mistaken for a different build profile, the final release link uses a separate explicit Make `TARGET` under the evidence directory. The census pins that final executable path rather than following a shared output while other verification runs. This changes only executable selection, never sweep verdicts, fixtures or timeouts.

### Final release identity

- Separate optimized host SHA256 **`f148455009a720a723f06597bf235bb61d50b57302afee79894d4257f230cb7a`** (`verified-release.exe`). The preceding common-target debug/release mix was discarded before qualification. Final release checks use this explicit path; debug checks use `verified-debug.exe`.
- The complete C-reference golden method passes after the two computed-result output repairs (`mvp-reference-goldens.log`). The full aggregate's other 151 methods passed; only that failing method needed a rerun. This distinguishes the first aggregate result from final focused requalification and does not claim a second complete 152-method run.
- The final census uses the pinned debug host for the exact reported debug residue. The unmodified sweep/discovery/verdict logic still runs the full 1,555-row corpus, jobs=3, timeout=30. Only the executable argument is pinned; list regeneration remains the official `refresh_lists.py` operation.

### Final gate scheduling correction

- The final pipe-complete Lambda baseline runs all current Lambda/JS/emission/GC/optimization checks successfully, but its last Test262 preliminary subprocess is rejected by the shared Test262 run lock because the full pinned Test262 gate was launched concurrently. This is an orchestration error, not a script verdict. Preserve the aggregate's real nonzero exit and its summary; rerun only that rejected preliminary check once the complete Test262 gate releases the lock. Do not count the rejected invocation as a pass or alter the gate/lock.
- Final release focused tier/GC matrix: **126/126**. Final release optimization contracts: **57/57**. Host export allowlist: **284 entries, passed**. Generator self-test: passed.

### Completed release gates

- Final pinned release Test262 gate passes **40,261/40,261**, zero regressions (`test262-qualified.log`, jobs=4). The executable is `verified-release.exe` with the final pipe repair. All 75 existing C-reference ports pass their unchanged goldens after the two missing computed-result lines are emitted.
- Final normal Lambda aggregate: **7,435/7,436**, zero skipped; its only rejected check is the Test262 preliminary subprocess's run-lock collision. The same final aggregate passes ordinary Lambda/tier parity **2,316/2,316**, all 66 registered Python emission methods, all 595 MIR emission checks, 25 size/frame checks, 368 GC checks and 57 optimization checks. Retain this aggregate's nonzero exit separately from the lock-free preliminary retry. The preceding complete milestone aggregate was **7,435/7,435**.

- The Test262 preliminary retry passes **1/1** with an isolated output directory after the final full Test262 run releases its lock (`prelim-retry.log`). Thus every check in the final Lambda baseline is individually qualified; the final aggregate invocation itself remains recorded as 7,435/7,436 due to the run-lock rejection. No masked exit or rewritten aggregate receipt.
- Pending only: complete the pinned full census, use the official generator on its one TSV, archive the 20 prepared resolved/obsolete records if its six original divergences are clear, and validate links/diff. The migration script is `temp/ledger-ruled-fixes/archive_records.py`; it has not yet been applied.

- The debug census reaches the corpus's known long renderer rows; the committed inconclusive list already has 24 such timeout cases. Run the same unchanged full sweep against the final optimized host as well, preserving jobs=3 and the 30/90-second budgets. Keep the per-profile reports separate; regenerate from one complete successful TSV, never splice their rows or treat a timeout as a match.

## Final result and qualification limits

The final pinned **debug census passes 1,555/1,555** with no fallback, excluded,
mismatch or timeout row. `refresh_lists.py` regenerates all three coverage files
from that one TSV: subset=1,555, excluded=0, inconclusive=0, procedural=563.
An independent inventory check requires exact unique coverage of the TSV; the
refreshed controller's four new subset cases and two exclusion controls pass
**6/6**. Evidence: `interp-sweep-qualified.log`, `sweep-verified-debug.tsv`,
`interp-refresh.log`, `interp-generated-controls.log`. The redundant release
census is stopped after the complete debug result; it has no credited verdict.
Release qualification is the complete Test262 gate, 57 optimization contracts
and 126 focused tier/GC cases on the final pinned release host.

Twenty records move intact to the fixed archive, with stable active-ledger
anchors and current resolution notes: LR01-21/23, LR02-2/3/37, LR03-17,
LR07-29/53/54/55/56/57/58/60/61/65/66 and LR13-4/7/8. The four Full MIR records
are obsolete; several other reports were already repaired on the starting
checkout and receive qualification rather than unnecessary code changes.
LR03-41 remains partial for the inherited-default ruling, with its source
location fixed. LR13-9 remains partial for validator coverage, with static-host
link closure repaired. LI-017 blocks claiming the complete optional standalone
native concurrency gate; its owning provider was not modified.

The final normal Lambda invocation preserves its real **7,435/7,436** result:
the single Test262 preflight was rejected by a concurrently held run lock.
Its isolated lock-free retry passes **1/1**. Every final baseline check is thus
individually qualified, while the single-invocation aggregate is not rewritten
as green. The earlier milestone aggregate passed **7,435/7,435**. Final Test262
passes **40,261/40,261**. Debug and release focused matrices each pass **126/126**.
The complete 152-method Python run passes all runtime/emission methods and
fails only two output subcases of the reference-golden method; after repairing
the C ports' missing computed-result output, that method passes all **75** C
reference cases. No second complete Python aggregate is claimed.

The registered validator gate has **102 passed, six skipped**, with **87**
legacy disabled cases retained. Additional native validator features/input
checks pass **61/61** and **14/14**. No hidden validator-baseline claim, timing
campaign, benchmark-cell refresh, budget relaxation, commit or vendor change.
No task edits were made to the independently added `doc/MIME_Type_Support.md`.

### Concurrent checkout update

During qualification, another checkout operation advances HEAD from the starting
`85cc95ad4` to `15f6fd7ae` (a merge). Its 13 changed paths are disjoint from this
task's tracked edits: DOM/CSSOM, Radiant integration, online snapshot tests and
documentation, including the independently added MIME support document. These
changes are preserved. The binary hashes and gate receipts above identify the
frozen repair builds; they do not claim a fresh aggregate build of the later
merged tree. No merge, commit or Git reset was performed by this task.
