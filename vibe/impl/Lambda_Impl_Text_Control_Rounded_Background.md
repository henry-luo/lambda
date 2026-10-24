# Native text-control rounded backgrounds

## Milestone and cause

Preserve the rounded borders of Word's citation source fields and other native
text inputs and textareas without an author background.

The CSS border already had the correct radius. `render_bound` painted that
border before `paint_default_text_control_box` filled the interior with a white
rectangle. That rectangle overwrote inward corner arcs and could extend outside
elliptical borders. An explicit CSS background avoided the fallback, explaining
why similarly styled boxes rendered correctly.

The shared fallback painter now scales and constrains the outer radius, then
uses `radiant_corner_inset_edges` with the actual border widths to derive its
inner curve. `render_path_create_rounded_rect` records the white fill through
the existing transform-aware painter. Rectangular controls retain the rectangle
path; author backgrounds and `appearance:none` retain their existing handling.
The scope is the default text-input and textarea fill, not other native widgets.

## Acceptance and verification

- Compare inputs and textareas against equivalent CSS boxes on a contrasting
  background. Preserve visible border pixels, leave the exterior untouched, and
  retain the default white interior at 1× and 2× display scale.
- Cover circular and percentage elliptical radii with unequal border widths.
- `RadiantFormControls.DefaultTextBackgroundPreservesRoundedBorders` failed on
  the original painter and passes after the repair. All four focused native
  form-control and textarea caret tests pass.
- Citation creation/editing and other source forms pass all 61 assertions.
  Before/after captures confirm complete rounded borders in the source dialog.
- Radiant dimension-cast lint and `git diff --check` pass.
- Required Radiant baseline: 6,148 passed, 141 existing partials, two skips and
  one pre-existing failure. All 210 text-query tests and 661 native UI cases
  pass. `RadiantViewTest.DemoCommandLaunchesCatalogAndPreservesViewerOptions`
  still expects the old splash selectors for unnamed demo launches; existing
  `lambda/main.cpp` changes open `test/demo/index.ls` instead. The same failure
  appears in the preceding textarea milestone's baseline. This repair does not
  change that command or its expectations.

Artifacts: `temp/word/rounded-fields/`.
