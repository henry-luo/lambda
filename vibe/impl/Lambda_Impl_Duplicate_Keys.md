# Lambda duplicate literal keys

## Completion record (2026-11-04)

- Implemented the user-approved option 1 in `temp/map-duplicate-keys`, branch
  `codex/map-duplicate-keys`, based on `64be60daa1`. Main-checkout uncommitted
  work is untouched; this record qualifies the source changes on this branch.
- E242 rejects repeated explicit keys in maps and element/object attribute
  lists, including across spreads/computed entries and under `--static-warning`.
  Shared validation runs before namespace lowering and literal relinking.
- Computed keys and spread overlaps reuse `map_literal_put` / `fn_map_set`:
  one field per effective name, first insertion position and last value/type.
  No new representation, per-read deduplication or native-stack rooting.
- Updated S2.3.1v2, S16.8.9v2 (semantics 88.0.0) and D3.4.1v2 (design 61.0.0),
  their working decision records, user documentation and generated formal index.
- Name identity follows S8.2.2v6: decoded escapes, canonical surrogate pairs
  and resolved namespace aliases; Unicode composition/case folding is excluded.
  Parsed input retains its format-specific duplicate policy.
- Migrated the former static-duplicate lookup fixture and the PDF map-output
  fixture to computed overrides; both retain their existing goldens.
- Debug build passes. The native negative test checks all 14 invalid literals
  across interpreter/AUTO/JIT and strict/relaxed modes (18 process checks).
  Evidence: `temp/negative-gtest-final.log`, `temp/negative-checks.json`.
- Six literal/namespace/spread goldens pass in 36 direct checks (three tiers,
  normal and forced GC), and through GTest on all tiers under forced GC.
  The PDF fixture also passes on all three tiers under forced GC.
  Evidence: `temp/literal-checks.json`, `temp/literal-gtest-{interp,auto,jit}.log`.
- Required aggregate **PASS: 8,756/8,756** — 6,644 Lambda/runtime and 2,112 input
  tests. Command: `make test-lambda-baseline` with local loopback/media access;
  debug executable, default AUTO tier plus the harness's own parity/stress
  checks. Evidence: `temp/lambda-baseline-local-permissions.log`,
  `test_output/test_summary.json`, individual runner logs/results.
- Both new documentation examples satisfy their contracts. The full
  Lambda_Data document check retains nine failing blocks whose exact text is
  unchanged from HEAD; no marker violations. Evidence:
  `temp/doc-data-check.log`, `temp/doc-data-comparison.json`.
- Diff whitespace and formal-index freshness pass. The index generator retains
  the existing duplicate-DO25 warning; this task adds no duplicate formal ID.
- All required implementation and regression work is complete. No performance
  claims or release builds were needed. Branch is ready for review/merge.

## Qualification notes

The first aggregate stopped on the empty worktree YAML submodule. The local
corpus was linked for testing and then copied into the worktree without Git
metadata, so future runs can find the fixtures without modifying the main
checkout. The first completed sandboxed aggregate was 8,746/8,756: the PDF
fixture needed migration, eight loopback tests could not bind, and media
playback was blocked (also reproduced in the main checkout). The final run
above passes all of them with local test permissions.

An initial 2,547-file AST scan was inconclusive: failed AST dumps suppress
diagnostic text, so absence of E242 text is not evidence of acceptance. Its
report is marked superseded (`temp/duplicate-corpus-scan.json`); the aggregate
and direct golden checks are the qualifying evidence.

## Acceptance

1. Repeated written keys (also quoted, normalized and namespace-alias forms)
   report E242, including across spreads/computed entries and in relaxed mode.
2. Computed collisions and spread overlaps remain allowed. They preserve the
   first position, publish the last value/type, and expose one field to reads,
   length, iteration, formatting and equality.
3. Distinct names/namespaces, nested literals and computed constant expressions
   remain valid; compiler optimization does not affect acceptance.
4. Interpreter, automatic execution and MIR JIT agree; forced GC preserves
   values and aliases. Formal index and documentation examples remain valid.

## Verification budget

Use focused literal/namespace/spread checks while editing. At the completed
milestone run the Lambda baseline once; rerun individual failures to distinguish
regressions from environmental or pre-existing failures. No performance claim
or release build is required.
