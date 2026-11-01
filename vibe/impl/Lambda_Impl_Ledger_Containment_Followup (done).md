# Ledger name, containment and interpreter follow-up

**Status:** selected scope complete; qualification retains one pre-existing PDF golden failure.
**Date:** 2026-10-31.

This pass implements existing **S1.6 / S7.7.2–S7.7.6 / D6.1.3v2 /
D8.1.1v18** rulings. It introduces no new policy for pending-ruling items.

## Bounded scope and acceptance

| Record | Selected scope | Acceptance |
|---|---|---|
| LR07-11 | Full source-name identity through local/global bindings, captures, imports and generated MIR prototypes | Names sharing a prefix beyond the former buffers remain distinct on interp, auto and jit |
| LR12-24 | Declaration/assignment skip targets, parameter call-value errors, checked-store reports and inferred record-result errors | A nested block yields its failure; later outer statements run; a failed outer binding ends its declaring block; per-item declarations keep the batch alive; failed admission never enters the callee or exits the caller; checked stores report retained partial state |
| LR12-31 | Strict-T0 row replacement using the existing shared row-aware COW setter | Rank-2 and rank-3 row replacements preserve leading-axis structure, including widening to rows of a different length |

LR07-11's unrelated scope/loop capacity caps and LR12-24's independent proof
precision and join-arithmetic optimizations are outside this correctness pass.
Mutable row-view alias policy (LR12-14), place-borrow policy (LR12-10) and the
unruled plain-procedure statement-position write-error policy remain separate.

## Root causes and implementation

MIR copied identifiers into fixed scratch buffers and then used those truncated
copies as hashmap/BSS/prototype identities. Semantic keys now retain the complete
AST spelling. Generated names use one shared dynamically sized, interned builder.
The dependency-linking path also keeps the entire exported variable name.

Both tiers treated deferred rejection as an enclosing-function exit. Interpreter
skip signals now carry the rejected binding identity, and block completion consumes
only its own declaration's skip, including every binding in a comma-separated
declaration. MIR routes the cold failure branch to the same
declaring block's boxed join. Block membership uses AST declaration identity:
resolution scopes may be shared by nested ordinary content blocks. Task-scope
unwind stays inside the rejected declaration's region. Fused typed-index guards
retain that binding identity while lowering the initializer, and local/inline
rejections cannot be deferred to the enclosing function's terminal epilogue.
Inlining preserves nested block joins and checked-store reports.

An interpreter parameter rejection no longer signals its caller. A MIR parameter
check rejoins the call before any native unboxing. Unresolved callable analysis
fails closed under D6.1.3v2. Admission guards are call-site facts, separate from
the callee-body effect fixed point; recursive inferred native proofs remain
available. Inlining requires the full argument contract, including singleton
and range constraints, rather than only the matching physical scalar lane. The admission join wraps only
argument checks, not the existing post-call ABI checks. Sized bitwise helpers
retain a boxed result when the carrier oracle admits an error: a success-kind
classification alone cannot authorize unboxing (S7.9.3 / D2.4.1–D2.4.3).
`ledger_call_carriers.ls` covers successful imported bool/u64 calls, wrapping
u64 arithmetic, rejected imported admission and sized bitwise error flow. Inferred record bindings retain a boxed error-capable
result when their callee can fail; success-only scalar-record destinations cannot
represent that join.

Failed checked stores and assignments share a runtime report that names the
binding, the abandoned statement count and retained/partial state. The report
preserves the original error payload. Interpreter statement handling preserves an
already-selected declaration skip rather than overwriting it with a return.

T0's admission scan still rejected matrix row stores because its old generic
setter flattened packed storage. The runtime already has leading-axis row stores
and owned-row widening; removing the obsolete admission guard lets T0 use that
same setter. The obsolete shape-only rejection helper is removed with it.

## Compatibility and reviewed emission

Four negative fixtures previously depended on failed admission exiting the
caller: `fn_pn_colour_mismatch` now propagates explicitly, while
`array_count_resized_binding`, `literal_admission_int_param` and
`ledger_parameter_site` bind into `int` to exercise a rejecting declaration.
The original recovery case is now the positive `ledger_parameter_result_site`.
These are S7.7.2/S7.7.3 migrations, not accepted failures. The
`rejected_error_value_flow` and `typed_call_argument_layout` goldens now retain
the later caller effects after a rejected call; their new output is checked on
all three tiers with forced/poisoned GC.

`typed_array_reuse` now expects a discarded failed call to leave the caller
running, so its handler flag is false. `tune27_literal_extent_dense` retains the
valid dense-loop probe and checks rejection in a separate function-body
`checked_delta` declaration. Its old negative call rejected inside the inner
`while` body before advancing that body's counter; S7.7.2's local skip made it
non-terminating. The execution golden and dense-loop emission assertions remain.
`typed_shrinking_indices` declares its checked scratch binding in the function
body and reassigns it in the loop: its deliberate negative-index failure must
end the function, rather than locally skip the counter update. Its reverse/COW
golden and shrinking-extent emission assertions are unchanged.

The two Tune24 record sidecars now require the boxed member/join fallbacks for
error-capable unannotated child bindings. Raw result destinations and allocation
exclusions remain checked. The mutual-component NO_GC sidecar permits either
member after contract-safe partial inlining, retaining its rootless guard and
collecting-call exclusions.

Before/after MIR was reviewed against a pinned pre-change debug host. No timing
claim is made; benchmark output agrees after removing its timing line. Exact
measured budget changes (only the measured profiles) are:

| Probe | Module instructions | Other reviewed growth |
|---|---|---|
| nested COW store | 169 → 177 | Two cold runtime reports |
| typed-array guard | 1455 → 1461 | guarded_store 134 → 138; roots 6 → 8; safepoints 3 → 4 |
| Cube3d | 16551 → 17056 | run_cube 9178 → 9651; 93 cold reports plus local admission/declaration joins |
| DeltaBlue | 10646 → 10882 | choose_method 1785 → 1875; cold reports and admission joins |
| Prettier AST | 20923 → 21495 | print_node 4899 → 4972; cold reports and admission/declaration joins |

The two record fixtures grow 824 → 927 and 1892 → 2086 instructions; these
fallbacks are a known optimization limit, retained under LR12-24's proof residue.
`emission-review.json` identifies both executable hashes and all per-function
changes; `*-emission.diff` retains the instruction review.

## Qualification

Artifacts live under `temp/ledger-fixes3-20261031/`; `before.json` identifies the
preserved pre-change executable. Required gates are focused interp/auto/jit
golden checks, forced/poisoned GC, relevant existing containment/COW fixtures,
MIR emission and size contracts, `make test-lambda-baseline`, and
`make test262-baseline`. Focused success does not substitute for those baselines.

Final macOS arm64 qualification, over base `b7590d83209aaaf559eb948c95881b3700241827`:

| Gate | Result |
|---|---|
| Focused interp/auto/jit, ordinary and forced/poisoned GC | 138/138 debug; 138/138 release |
| MIR emission | 279/279 |
| MIR size ratchet | 20/20 |
| Full MIR forced-GC sweep | 350/350 |
| Targeted Procart/document/tier-parity regression groups | 11/11 |
| Runtime admission/report groups on release CLI | 4/4 |
| `make test-lambda-baseline` | 8496/8497; nonzero exit for the existing PDF failure below |
| `make test262-baseline` | 40261/40261; zero regressions and no unstable/slow residue |
| Formal index and diff whitespace checks | pass |

The Lambda aggregate includes 2112/2112 input cases, 1966/1966 core runtime
cases and 152/152 server cases. After adding the five new fixtures to the
existing permanent three-tier table, only the GTest executable was rebuilt and
`LambdaTierParityTests.Tune27FixturesHonorTierSupport` rerun successfully; the
qualified engine was unchanged.

**Aggregate exception:** `PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`
compares object 4's raster transform width 91 against the golden's 90. The
preserved pre-change executable reproduces the identical mismatch on interp
and jit (`pdf-before-interp.log`, `pdf-before.log`). No PDF source or golden
was changed. This baseline remains red; focused results do not turn it green.

`qualification.json` records the base and debug/release executable hashes.
`focused-debug.json`, `focused-release.json`, `*-baseline-results.json`,
`lambda-baseline-final.log`, `test262-baseline-final.log`,
`aggregate-regressions.log`, `permanent-tier-parity.log` and `native-release.log`
retain the completed receipts. Earlier exploratory/interrupted runs are not
qualification results.
