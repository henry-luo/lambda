# Lambda TikZ Phase IV — progress note (M5)

**Date:** 2026-10-07. **Status:** six §9.6 rows implemented in a worktree, uncommitted.
**Design:** [Lambda_Pkg_Latex3 §9.6](../Lambda_Pkg_Latex3.md#96-extended-pgfpgfplots-native); the admitted profile and its boundaries are recorded in [Lambda_Pkg_Tikz §12](../Lambda_Pkg_Tikz.md#12-phase-iv-profile-additions-latex3-96).

## What landed

| §9.6 row | Where |
|---|---|
| `positioning`, `calc` | `coords.ls` (new), `named.ls` (placement and routing), `tikz.ls` (`target_point`, calc path points); parser: options after a node name, `at_calc`/`calc`, `at_ref`/`at_anchor` |
| `arrows.meta`, `shapes.geometric` | `arrows.ls`, `shapes.ls` (new); `named.ls` and `tikz.ls` draw tips and outlines through them |
| Parameterized styles, `pic` | `input-tikz.cpp` (`define_handler`, `expand_style`, `substitute_parameters`, `pic`); `tikz.ls` (`handler_keys`, pic scopes); `tikz_bridge.ls` (preamble handlers) |
| `ybar`/`xbar`/stacked, error bars, `\closedcycle`, `fill between` | `plotkinds.ls` (new), `pgfplots.ls`; parser: error offsets, `\closedcycle`, `fill between[...]` |
| Tables, symbolic coordinates | `plotdata.ls` (new), `pgfplots.ls`; parser: `table[...] {...}` text and offset |
| `groupplots` | `input-tikz.cpp` (`group_plot`), `pgfplots.ls` (`render_group`) |

An earlier agent wrote most of this before it was stopped; its uncommitted files were recovered onto this worktree's base (identical for every touched file) and then reviewed, fixed and tested here.

## Fixes made during review

- Point lists built with `++` lost their rows, so area plots and fill-between regions rendered nothing. The cause was a runtime defect, since fixed as [LR09-33](<../Lambda_Issue_Ledger (fixed).md#lr09-33>). The error-bar segments join with `++` again.
- Arrow tip constants were not PGF's: `Stealth` used a line-width factor of 5 (PGF: 4.5); the legacy lower-case tips shared the meta geometry; `Triangle`, `To`, `Circle` and `Bar` had approximate values. All now follow `pgflibraryarrows.meta`/`pgfcorearrows` of PGF 3.1.10.
- Shape sizing: polygons and stars used the box half diagonal (PGF: `1.41421·max(half width, half height)`), diamond minimums were coupled, trapezium minimums did not scale the outline, and diagonal anchors of ellipses and diamonds were border rays (PGF: parameter angle 45° and side midpoints).
- Positioning: the diagonal 1/√2 factor applied only with `of`; `on grid` ignored key order; the last placement key was chosen by table order, not source order.
- `\tikzset` handlers in the LaTeX preamble other than `/.style` were rejected; they now reach the parser without shifting island line numbers.
- File tables gained a document-directory base from `options.base_uri` or `options.source_path`, and an exported axis plan (`pgfplots.resolve_axis`) for semantic tests.
- New modules were made free of `lambda_shadow_lint` warnings.
- The recovered named-node renderer had dropped the 1.2 px default stroke to 0.6 px, which failed `RenderOutputParity.TikzNamedWorkflowPaintsShapesAndBranchArrow`; the 1.2 px default is restored for named-node pictures.
- A picture-level `\tikzset` with a plain key (`>=Stealth`, `thick`) was silently ignored; it is now diagnosed.

## Verification

- `test/lambda/latex/test_tikz*.ls` and `test_latex_*.ls`: 40/40 before, 46/46 after (six new `test_tikz_phase4_*` tests).
- Positioning, calc and shape anchors: 49 values compared with pdfTeX (TeX Live 2025) + PGF 3.1.10 output, all within 0.002 cm (`test/latex/fixtures/tikz_phase4/positioning_reference.{tex,ref}`).
- Corpus check: every TikZ fixture and sample rendered through `latex.render_to_html` with the base and new trees; differences are only parser progress on already-unsupported pictures (`ac-drive-components`, one picture of `unit_tests_by_silviu`) and larger named-node specs.
- The six new tests give identical output under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
- `RenderOutputParity.Tikz*` (3/3, `test_pdf_render_visual_gtest`) and `RadiantViewTest.BatchLoaderRuntimeMatchesFreshRuntimes` pass.
- Named-node pictures with the new shapes and tips were rendered through `radiant.render_svg` once by hand; there is no automated Radiant test for the new shapes.

## Open items

- `render.ls` must pass `info.base_uri` to `packages/tikz.ls` `render_picture` (new trailing `base_uri` argument) for file tables in LaTeX documents.
- `registry.ls` `tikz_library_issues` still flags `calc` and `shapes.geometric`.
- Runtime defect found here, fixed 2026-10-07 as [LR09-33](<../Lambda_Issue_Ledger (fixed).md#lr09-33>). `++` of two packed numeric matrices joined their flat payloads, and spreading one into an array literal padded it with `null` items.
