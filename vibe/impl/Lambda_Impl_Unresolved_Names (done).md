# Unresolved value names — implementation

## Completion — 2026-11-03

Qualified in `/Users/henryluo/Projects/lambda`, branch `master`, at base
`8acd88214` plus the uncommitted implementation and fixture changes.

- **Decision:** implement the user's S16.10.4 ruling: unresolved value
  reads and bare calls reject with E202/E203, including in `--static-warning`.
  S10.1.7v4 implicit fields, S7.1.1v3 data reads and existing declaration
  scopes remain in force.
- **Implementation:** queue unresolved AST identifiers until call dispatch,
  join rebinding and type completion finish; report only surviving unbound
  names. Distinguish builtin pipe calls, module prefixes, registered path
  schemes and deferred cross-language prebuilds. A failed import explains its
  missing alias without hiding independent unresolved names.
- **Migration:** restored the two parked undefined-name negatives, moved the
  post-group row-name case to a negative fixture, and repaired stale names and
  imports in tests and the LaTeX citation-range module. A new citation fixture
  checks that abbreviated endpoints roll over correctly.
- **Qualification:** `make build` passed. `make test-lambda-baseline` passed
  8704/8712; the eight sandbox-denied listener cases passed with loopback access
  (the complete serve suite 152/152 and four scripts 4/4). Five focused
  diagnostic tests, including batch status, and the REPL rollback test passed.
  The citation fixture added after the baseline passed on interp/auto/jit.
  Focused fixture parity passed 17/17 on JIT and 16/17 on strict interp:
  `editor/photos_tone` reaches an unsupported `AST_NODE_INDEX_EXPR` in its
  imported tone module (E501), also reached by the original fixture; auto and
  JIT pass it. Logs: `temp/lr02-35-baseline-final2.log`,
  `temp/lr02-35-serve-loopback.log`, `temp/lr02-35-socket-scripts.log`,
  `temp/lr02-35-focused-final.log` and `temp/lr02-35-tiers-*.log`.
- **Other build:** the broader `make build-test` failed to link the PDF input
  test because its `read_binary_file` duplicates the library symbol. The
  required Lambda baseline build succeeds.

## Implementation notes

The earlier resolver assigned unresolved identifiers `any`. The interpreter
could then return a bare error while the process succeeded, and the MIR tier
could report a different failure. The resolver already performs some binding
after visiting an identifier, so rejecting at the first failed lookup would
also reject valid joins and overloaded builtin calls. The final check uses the
resolved identity after those operations instead.
