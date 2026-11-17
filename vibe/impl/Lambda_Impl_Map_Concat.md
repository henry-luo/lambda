# Map Concatenation

## Completion record (2026-11-17)

- Goal: implement the user's 2026-11-17 map `++` ruling, S10.6.1v2.
- Checkout: master, HEAD 1323612342784e3d8240a2d8342df89e271fecff plus this change.
- Scope: runtime concatenation, semantic ruling, syntax record, user reference
  and focused regression. Query and numerical convention changes remain discussion.
- Implementation: map/map concatenation reuses computed-map spread for ordered
  shallow merging. Inputs and the result have precise roots across allocation.
  Map/null concatenation retains null's identity and captures the shared value.
- Golden expectations were written from the ruling before the runtime change.
  The old host reproduced E201: unsupported operand types: map and map.
- Local debug runtime and host builds passed. The rebuilt host is installed
  as lambda.exe; its SHA256 is
  cadaf6c1f8fcf951e90e71f86dc6b97c2a9bb25433ad4f74f7e152ff6fe11eff.
- Focused verification: 16/16 runs passed (four fixtures, interpreter and JIT,
  ordinary GC and LAMBDA_GC_FORCE_EVERY=1). The standard golden harness passed
  all four selected tests. Formal-index and git whitespace checks passed.
- Both make test-lambda-baseline and make test262-baseline stopped during
  prerequisite builds: the sandbox denied writing the sibling lambda-input
  checkout's temp/build-debug.log or temp/build-release.log. Neither aggregate
  test suite ran. This is an unverified aggregate gate, not a compiler failure.
- Evidence: temp/query-conventions/{focused-results.json,harness-focused.log,
  build-runtime.log,build-host.log,lambda-baseline.log,test262-baseline.log}.
- Preserve the user's unrelated review-document move. The reader refactor
  remains staged separately under temp/reader-refactor.

## Decision record

See [Design Syntax §7.34](../Lambda_Design_Syntax.md#734-map-concatenation-is-ordered-shallow-merge-decided-2026-11-17).
The previous S10.6.1 wording rejected maps and required explicit map spread;
that restriction is superseded by S10.6.1v2.
