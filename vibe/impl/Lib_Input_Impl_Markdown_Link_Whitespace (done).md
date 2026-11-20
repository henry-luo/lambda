# Markdown link-label whitespace

Completed 2026-11-14 for LI-050. Input owns this parser under D9.1.1v4 and
D9.1.3v2; shipped document processing follows D9.3.4. The user explicitly
authorized the owner-side repair. No formal ruling changed.

`parse_inline_spans` removed trailing spaces whenever an inline input ended.
Both the plain-string fast path and the mixed-content flush treated link-label
boundaries as paragraph endings. Direct and reference link parsers already
increment `ParserState::link_depth` while parsing labels.

The inline parser now snapshots that context and trims end spaces only outside
link labels. Both paths use the same decision. Paragraph trailing-space and
hard-break behavior remain covered by the existing CommonMark tests. The
editor round-trip guard and its golden retain their existing expectations.

The regression table covers nine plain, titled, formatted, all-space,
reference, collapsed-reference, shortcut-reference and Unicode labels. A
separate control verifies paragraph endings after plain text, formatting and
a link. The label test failed before the repair; the paragraph control passed.

Verification used Input base `fcd26ebef2ea509f3e2db46586dafd51631ee495` plus
`inline_spans.cpp` and `test_markup_modular_gtest.cpp` changes, and Lambda base
`054c73231` rebuilt against those debug archives with the AUTO backend:

- Modular markup: 77/77 tests pass.
- CommonMark corpus: 1,337/1,337 examples pass.
- Editor integration: 97/97 golden tests pass, including the reported failure.
- DOM range: 84/84 tests pass, including both replacement cases.
- `make test-lambda-baseline`: 7,509/7,509 tests pass.
- `make test-input-baseline`: 5,235 passed, three failed, one skipped (5,239 total).
  Its failures match existing LI-001 (graph LOC budget), LI-002 (external-parent
  admissibility) and LI-012 (`sample.tar.gz`); those sources and fixtures are
  unchanged by this repair.

Before/after logs and the reproduction probe are under the Lambda checkout's
`temp/failed-dom-editor-tests/`: `input-regression-before.log`,
`input-modular-after.log`, `editor-integration-after.log`,
`input-baseline-after.log` and `lambda-baseline-after.log`.
