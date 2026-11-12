# XLSX Import for Sheet

## Current checkpoint

- **Goal:** implement `lambda.doc.xlsx` workbook import and convert it to the
  Sheet demo's editable model; open `.xlsx` in the demo (**D9.3.4**).
- **Worktree:** Lambda-opus, `master` at `38545592f`, 2026-11-12; unrelated
  pre-existing edits are present and must be preserved.
- **Architecture:** reuse `lambda.doc.opc` and `lambda.doc.xml` for ZIP,
  relationship resolution and expanded XML names. XLSX import is a bounded
  semantic conversion, not a lossless workbook editor. Save imported workbooks
  as `.sheet.json`, never overwrite their XLSX source with JSON.
- **Milestone:** multiple sheets, shared/inline strings, numeric and boolean
  cells, formulas, basic styles and dimensions, useful diagnostics for omitted
  features, and UI opening. Respect the demo's existing workbook limits.
- **State:** package import and Sheet conversion are implemented; XLSX Open
  defaults subsequent saves to `.sheet.json`. A cached formula result is used
  when Sheet cannot evaluate an imported formula, and any edit invalidates all
  imported caches. Built-in date styles 14–17 use the workbook's 1900/1904
  epoch and display ISO dates.
- **Verification:** existing debug `lambda.exe`, automatic execution tier, and
  the existing GTest runner passed the package and Sheet focused scripts (6/6).
  Headless UI replays passed XLSX Open (10/10), existing file (12/12), editor
  (23/23), ribbon (7/7), selection (36/36), and resize (40/40) assertions.
  The package also read an XLSX produced by `lambda.chart.export`. Fixtures
  are synthetic; independent real-world workbook coverage remains outstanding.
- **Blocked checks:** automatic approval review rejected the full Lambda
  baseline build because it writes artifacts/logs in sibling repositories;
  the Test262 baseline likewise requires a sibling build and was not run.
- **Next:** request approval for the blocked baseline build. Do not modify
  sibling project sources.
