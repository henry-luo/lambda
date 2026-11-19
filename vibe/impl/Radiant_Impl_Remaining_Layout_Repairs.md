# Remaining layout test repairs — 2026-11-11

Authority: **D9.1.1v4 / D9.1.3v2** assign these layout fixes to Radiant.
Native host ownership and **D5.5 / D6.3** remain unchanged. The user approved
16 macOS reference additions as a scoped exception to AGENTS.md rule 12.
No sibling implementation, shared fixture, scored baseline or tolerance is changed.

Final applied result: **5,631 passed / 293 partial / 0 failed / 12 skipped**,
all 5,936 cases present in the normal integration gate. All eight remaining
comparisons are clear; [RI-010](<../Radiant_Issue_Ledger (fixed).md#ri-010>) is resolved.

## Native repairs

- `layout_multicol.cpp` restores normal half-leading at sliced continuation
  edges even for `trim-both`. A multicol container's own start trim applies at
  continuation columns unless the inner box owns that edge. Fragmented flow
  height accounts for the start leading already removed in the first fragment.
- `layout_block.cpp` avoids trimming a formatted line again when its inner
  block already applied start trim. Float prescanning now stops at an empty
  list-item block, preserving the following float's vertical source order.
- `layout_text.cpp` applies the remaining closing-punctuation half-space when
  it allows the punctuation to fit at a line end. This changes the failing
  `国国国）国国国）` case from three fragments to two fitting 70px fragments.
- CJK hyphen-like punctuation U+301C/U+30A0 is breakable under normal/loose
  rules for a Chinese/Japanese writing system. Explicit script subtags override
  the language default. Inline seams and intrinsic sizing share the same owner
  information and restrictions. See [CSS Text 3 §5.2](https://drafts.csswg.org/css-text-3/#line-break-property)
  and [appendix F](https://drafts.csswg.org/css-text-3/#script-tagging).

The line-end adjustment follows the `allow-end` behavior implicit in
`text-spacing-trim:trim-start`, documented in
[CSS Text 4 §8.5](https://drafts.csswg.org/css-text-4/#text-spacing-trim-property).
Nested trim ownership and fragmentation follow
[CSS Inline 3 §6.2](https://drafts.csswg.org/css-inline-3/#text-box-trim).

Seven new tests in `test/test_layout_dynamic_gtest.cpp` cover continuation glyph
positions, nested first-line trim and following sibling position, root column
trim, closing punctuation, script subtags, inline/intrinsic boundaries and
list-item float ordering. The first four regressions fail before the trim
repair; the float regression separately fails before the prescan repair.
The complete native dynamic suite passes 58/58.

## Native repair outcome before reference corrections

The normal Lambda executable was refreshed through its documented split build.
The full shared integration entry point selects all 5,936 layout cases:

| Gate | Result |
|---|---|
| Before this follow-up | 5,621 passed / 293 partial / 10 failed / 12 skipped |
| After this follow-up | 5,623 passed / 293 partial / 8 failed / 12 skipped |
| `wpt-css-lists` | 52 passed / 8 partial / 0 failed |
| `wpt-css-text` | 897 passed / 82 partial / 1 failed |
| `wpt-css-inline` | 164 passed / 5 failed against stored captures |
| `wpt-css-writing-modes` | 1,812 passed / 166 partial / 2 failed |

`text-spacing-trim-end-001` and `list-marker-position-crash` now match both
stored and fresh captures at 100% element/text fidelity. All five reported
multicol cases match fresh captures at 100%, while their stored text-fragment
captures continue to fail. Their failing contracts remain visible.

The remaining CJK case has stored font differences and a fresh Chrome break
before U+301C under `ja-Hang`, contradicting the fixture's assertion. The native
Japanese box now matches fresh Chrome, and the explicit Hangul-script boundary
remains prohibited. The two orthogonal cases match their shared WPT reftest
target's nine aside heights exactly; fresh Chrome tests disagree under
containment. [RI-010](../Radiant_Issue_Ledger.md#ri-010) records the eight unresolved
comparisons and affected ownership/standards dependencies. No native behavior
is changed to imitate those reference differences.

An additional clone control has an unchanged pre-existing fresh-browser
mismatch, recorded as [RI-011](../Radiant_Issue_Ledger.md#ri-011). Pinned before and
after native trees are identical; its stored baseline remains passing.

## Initial qualification and evidence

`make -j8 INPUT_ROOT=/Users/henryluo/Projects/lambda-input run-radiant-baseline`
passes **901 native tests plus static corpus smoke**, **seven runner contracts**,
**3,851 static and 630 dynamic geometry comparisons**. One hardware MIDI test
and six static corpus fixtures retain existing skips. Layout lint checks
89 casts with zero unmarked; all five lint contracts pass.

The shared integration gate runs through `test/test_run.js --target=radiant
--category=baseline --radiant-baseline --lambda-owned
--include-test=test_radiant_layout_baseline --sequential`. It exits one for the
eight documented comparisons; no other layout category regresses.

Artifacts stay under `temp/radiant-layout-20261111/`: before/after native
regressions, `native-baseline.log`, `integration/`, fresh captures, detailed
`latest-comparisons.json`, and the source/artifact hashes in
[verification-summary.json](../../radiant/temp/radiant-layout-20261111/verification-summary.json).
Changes are left uncommitted.

## Reference audit follow-up

The eight remaining comparisons are now independently qualified in
`temp/radiant-reference-20261111/`. Five ordinary macOS recaptures correct the
malformed multicol source fragments. Three macOS standards captures use the
existing source-hashed `reference_control` mechanism. The CJK control changes
only the Hangul-script string's breaking mode to reproduce the fixture's
required boundary; the two orthogonal controls use the shared WPT reference's
explicit sizing formulas. The resulting nine aside boxes and line contents
exactly match a separate capture of that WPT target. None of the generated
reference geometry comes from Radiant output.

All eight full and compact captures agree and reach 100% element/text fidelity.
The three unmodified browser outputs fail against the standards controls, so
the new oracles continue to detect those defects. Earlier native output also
retains the repaired trim/Japanese-breaking failures under these references.
The existing capture contract passes horizontal, vertical, bidi, column,
Unicode and hidden-text cases.

The unchanged full integration inventory passes with a temporary overlay:
**5,631 passed / 293 partial / 0 failed / 12 skipped**. A reviewed 16-file
additive patch, consisting of eight full captures and eight compact equivalents,
passed `git apply --check`. The user explicitly approved the scoped exception
to AGENTS.md rule 12, and exactly those 16 additions are now applied. Every
file matches the reviewed hash; all pre-existing dirty corpus files are preserved.
See [the review](../../radiant/temp/radiant-reference-20261111/REVIEW.md) and
[patch](../../radiant/temp/radiant-reference-20261111/reference-fix.patch).

The same standards audit found missing Hanb/Bopo/Hira/Kana tags in the native
writing-system map required by CSS Text 3 appendix F. Those tags are repaired
with expanded before/after controls. A new native test protects scrollport
fallback across both containment arrangements and the viewport cap. The final
owner gate passes **902 native tests plus smoke**, **4,481 geometry comparisons**,
seven runner contracts and layout lint (89 casts, zero unmarked); all five lint
contracts pass. These native changes follow D9.1.1v4 / D9.1.3v2 independently
of the approved reference correction.

## Applied qualification

The normal integration entry point, using the real shared corpus and ordinary
reference resolution, exits zero with **5,631 passed / 293 partial / 0 failed /
12 skipped**. All 18 categories pass. The previously failing groups now report
169 inline passes, 898 text passes with 82 existing partials, and 1,814 writing-mode
passes with 166 existing partials. No temporary overlay participates in this run.

Source and executable hashes remain identical to the passing native owner gate.
The prior capture contracts, negative controls and full/compact parity checks
remain valid because the applied files exactly match the reviewed proposal.
The scope is macOS; other platform references remain intact. RI-011 is a separate
pre-existing defect and remains open. Nothing has been staged or committed.

Final reports and authorization are under
`temp/radiant-reference-20261111/applied-integration/` and `authorization.json`.
The applied file inventory and source/artifact identities are pinned in
[verification-summary.json](../../radiant/temp/radiant-reference-20261111/verification-summary.json).
