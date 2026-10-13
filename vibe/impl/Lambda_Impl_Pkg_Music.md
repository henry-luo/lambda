# Lambda Music — Implementation

**Started:** 2026-10-13. **Status:** Phase 1 bounded profile implemented.
Phase 2 is not implemented.

Design: [Lambda_Design_Pkg_Music.md](../Lambda_Design_Pkg_Music.md).
The user selected native Lambda and requested implementation. D7.2.4 governs
the package, S12.1.1v2 separates score transformations from playback effects,
and D7.1.2v2/D7.5.3 govern resource reads and Radiant calls.

## Delivered

- `lmd/package/music.ls`: explicit public `load`, `parse`, `from_xml`, `layout`,
  `svg`, and `render` facade (D7.2.4/D7.2.6, S7.4.2).
- `music/musicxml.ls`, `common.ls`, `time.ls`: score-partwise normalization,
  exact rational time, inherited/staff-scoped contexts, voices/chords,
  pickups, backup/forward, bounded validation, diagnostic policy. MXL uses
  the existing archive snapshot and declared rootfile; it never extracts
  or selects an arbitrary XML member (S12.4.1v2/S14.3.1v2).
- `music/fonts.ls`, `glyphs.ls`, `fonts/`: unmodified pinned Bravura/OFL
  assets, metadata, glyph outlines through existing `radiant.math_metrics`,
  shaped labels through `radiant.measure_text`. CMU Serif is reused from
  the existing LaTeX distribution (D7.1.2v2/D7.5.3).
- `music/notation.ls`, `header.ls`, `layout.ls`: accidentals, chord seconds,
  context glyphs, shared exact-onset columns, rhythmic spacing, source breaks,
  part filtering, staff geometry, scroll/pages, overflow diagnostics.
- `music/notes.ls`, `spans.ls`, `decorations.ls`, `draw.ls`, `engrave.ls`:
  anchored stems, beams/hooks, ledger lines, grace slashes, dots, articulation,
  lyrics, dynamics/words/metronome text, repeat barlines, endings, and
  tie/slur/tuplet/hairpin connections including system continuations.
- `music/svg.ls`: ordinary SVG with event IDs, accessible labels and page
  bounds. Event bounds use page-local CSS pixels; drawing geometry uses
  staff spaces. `doc/font.ls` owns shared font embedding, extracted from
  `math/font.ls` without a second implementation.
- Original piano and flute/viola fixtures, deterministic MXL archives,
  registered golden tests, and `test/music/_view.ls`.
- User API and support manifest: [Lambda_Music.md](../../doc/Lambda_Music.md).

The broad design remains the target. This release has no complete collision
solver, automatic beaming, lyric extender lines, percussion/tab/microtonal
profile, or cross-staff/cross-measure beams. Unsupported decorations warn;
unsupported structural features fail. Tuplets are single level. Source
coordinates/default typography are not reproduced. Spanners are resolved
during layout; structural event IDs are stable under reflow, with source IDs
retained separately as metadata. MIDI APIs and audible transport are Phase 2.

## Shared runtime fixes

`input.cpp` exposes the existing strict XML reader through `flavor:'strict'`.
`XmlParseOptions.allow_inert_doctype` permits a single external identifier
before the document element. Internal subsets, entity declarations,
malformed/public identifiers, nested/trailing/duplicate DOCTYPEs fail. The
ordinary XML reader and existing strict clients retain their previous policy.
No external fetch or vendor-parser change was introduced.

Forced GC found a pre-existing lifetime defect in `fn_parse2`: converting the
symbol `flavor` could collect the converted `type` string, so
`{type:'xml',flavor:'strict'}` dispatched to format `strict`. Slot-backed roots
now retain the source/options and converted strings through dispatch, per
D5.3.3. `parse_option_roots.ls` and the music layout regression are registered
in the existing forced-GC corpus; conservative native-stack scanning remains
unused.

## Validation record

- `make build` and `make build-test`: passed.
- Six focused goldens: passed on both automatic and JIT execution (five music
  scripts plus parser-root regression).
- Registered forced-GC cases: 2/2 passed, covering native JIT, randomized
  collections and MIR-interpreted execution; complete GC corpus: 303/303.
- Input baseline: 2,112/2,112 passed.
- SVG serialization, piano PNG, second-page SVG/PNG, and piano PDF smoke
  checks passed. The PDF was also rasterized with Poppler and inspected.
- Initial broad Lambda baseline: 6,827/6,828 including the input baseline.
  `edit_view_only` fails the editor's Markdown math round-trip check. The
  same failure reproduces with the original, pre-extraction math font helper
  in an isolated Lambda home. Its expected output was not changed.
- Final isolated Lambda baseline: 4,718/4,720. The editor failure persists;
  `latex_test_latex_phase3_corpus` also hit the 60-second timeout while the
  broad PDF suite ran concurrently (it passed in the initial baseline).
  The isolated serial rerun also timed out at 60 seconds. Its timeout was
  not raised, and this remains an unresolved broad-gate failure; the initial
  non-isolated run's pass does not establish a clean final baseline.
- Broad input runner: 3,859/3,883 as reported by the runner. Failures include
  the Mermaid LOC budget, validator executables aborting with unresolved
  `_LIT_BOOL`, CSS CRUD/parsing/round-trip checks, and Markdown source-span
  checks. The PDF suite reported background/gradient/shadow pixel mismatches,
  then timed out in `CanvasWebGlExportTest.PixelsAlphaAndEffectsReachEveryOutput`
  after 180 seconds without output. These results are not reported as a
  passing gate; fixtures, thresholds, and goldens were not relaxed.

Concurrent builds replaced the shared `lambda.exe` during an initial input
run, producing exit-127 failures. Verification was repeated with a separately
linked debug executable under `temp/music/verify/`, an unmodified runner
copy, and independent scratch/output directories. The Lambda baseline uses
`temp/music/verify-lambda/` with the same immutable executable. Source and
fixtures remain the working checkout. This isolates executable replacement;
it is not a different test policy.

The first verbose forced-GC layout probe was killed under resource pressure;
the quiet probe and the registered GC harness subsequently passed. Test
artifacts are under `temp/music/`; no performance claims use these debug runs.
