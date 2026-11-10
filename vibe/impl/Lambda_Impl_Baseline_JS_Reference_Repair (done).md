# baseline-js browser-reference repair

**Completed:** 2026-11-10, macOS arm64. Lambda `32d495f9f`, corpus
`a64ed85dc` plus the 22 reference files listed below. The standard focused gate
improves from **619 passed / 11 failed** to **630 passed / 0 failed**, with no
partials or skips. Runtime sources, fixture sources and comparison thresholds
are unchanged. Project ownership follows **D9.1.1v4 / D9.1.3v2**; this repair
belongs to the sibling `lambda-test` corpus.

## Cause and repair

Eight original browser captures do not represent the fixtures' final layout:

- `collapsed-border-remove-cell`, `row-background-paint-remove-last-cell`,
  `col-change-span-bg-invalidation-001`, and
  `position-absolute-under-non-containing-stacking-context` contain geometry
  from before their asynchronous removal, column-span or scroll mutation.
  The existing extractor resolves local WPT resources and waits for the
  `reftest-wait` class to clear before capturing.
- `text-indent-dynamic-each-line-001` / `002` and
  `text-indent-dynamic-hanging-001` / `002` contain geometry consistent with
  rejected `each-line` / `hanging` declarations. Headless Chrome
  **154.0.8037.57** supports both values; fresh captures of the unchanged
  fixtures match the existing Lambda output. The historical browser cause is
  inferred from the old geometry; its version is not recorded there.

The eight full references and their compact partners were recaptured with the
existing extractor under `../lambda-test/layout/reference/wpt/`.

The remaining `text-align-match-parent-02` / `03` / `04` captures come from a
browser that rejects unprefixed `match-parent`. The current default Chrome
154 probe also rejects it while accepting `-webkit-match-parent`. Add three
full/compact `.standards` pairs through the existing reference-control
mechanism, retaining the original captures. Each control records the CSS Text
specification, reason, browser identity, equivalent prefixed declaration and
fixture SHA-256. The loader rejects a control when its source changes.
This follows the previously qualified Radiant
[RI-004 / RI-005 reference repair](<../../../radiant/vibe/Radiant_Issue_Ledger (fixed).md#ri-005>).
The relevant external definitions are
[CSS Text 3 indentation](https://drafts.csswg.org/css-text-3/#text-indent-property)
and [match-parent](https://drafts.csswg.org/css-text-3/#valdef-text-align-match-parent).
No Lambda semantics or design ruling changes.

## Verification and evidence

- Each of the **11 full / compact pairs** matches the originally captured,
  unchanged Lambda output at **100% elements and text**, with no differences.
- `make run-layout-baseline-suites LAYOUT_BASELINE_SUITES=baseline-js` exits 0:
  **630 passed, 0 partial, 0 failed, 0 skipped**.
- `node test/test_layout_reference_controls.js` passes provenance, source
  invalidation, precedence, legacy fallback and strict-geometry checks.
- `git -C ../lambda-test diff --check` passes. No native build is needed for
  this reference-only repair; unrelated engine suites were not rerun.
- Tested `lambda.exe` SHA-256:
  `c9d4dcf9893fa84cbb361d01f463aa3c9a1062dd695654945af7b732ed9f9ef1`.

Detailed evidence is in `temp/baseline-js-fix/`: `before.json`,
`failures.json`, `browser-support-false.json`, `capture.log`,
`reference-comparison.json`, `installed-references.json`, `after.log`, and
`reference-controls.log`. The install manifest records each original and
replacement hash; `original-references/` preserves the replaced files.
