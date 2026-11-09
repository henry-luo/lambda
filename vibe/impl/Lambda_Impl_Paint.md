# Lambda Paint — Implementation

**Design:** [Lambda Paint Package](../Lambda_Pkg_Paint.md) (PT#) and [Radiant Paint Engine](../../../radiant/vibe/Radiant_Design_Paint_Engine.md) (PE#). The user asked to proceed to implementation on 2026-11-09. This record tracks delivery, decisions taken while implementing, and evidence.

## Decisions taken during implementation

- **Bridge in the `radiant` host module (2026-11-09).** System functions are enumerated in lambda-input's `lambda.h`, which Lambda work must not edit. The bridge is therefore a set of `radiant.paint_*` functions in `lambda/module/radiant/radiant_paint_adapter.cpp`, registered in the `radiant` module table. This settles PT-O3 (PT15).
- **The runtime never calls the engine.** The runtime boundary DSO must not import Radiant symbols (`utils/check_module_boundary.py`). `lambda.image`'s brush therefore migrates at the package level (PT13): `lambda.paint` produces coverage through `radiant.paint_*`, and `image_edit.cpp` composites a supplied coverage.
- **Dab falloff profiles.** The dab table gains a `profile` column (32): `0` is libmypaint's two-segment falloff over squared distance; `1` is the linear cone with a flat core that `lambda.image`'s brush uses. That makes 33 columns in `PAINT_DAB_V1`.
- **`var`-parameter procedures move to M1.** M0 needs only the pure forms (PT7). The general `var` system-procedure form arrives with the interactive session.
- **Dab layout grows to 34 columns before publication (M1).** Column 33 is `blend` (`PaintBlend`). No layout had been published, so `PAINT_DAB_LAYOUT_VERSION` stays 1.
- **Canvas rectangle write is its own DOM operation (M1d).** DOM operations have a fixed arity, so `dom.set_canvas_pixels_rect(canvas, pixels, rect)` sits beside `dom.set_canvas_pixels` instead of adding an options argument, which would have broken existing two-argument callers. The rectangle uses the engine's `[x0, y0, x1, y1]` form, so a `paint_present` rect and its pixels pass straight through.
- **Rectangle repaint uses presentation damage (M1d).** Radiant already repaints regions for paint-only writes (presentation damage). The canvas notes its placement while it is recorded, and a rectangle write marks the mapped region as presentation damage. A canvas drawn under a transform, a filter or an offscreen capture falls back to a full repaint.

## Milestone plan

| Step | Scope | Status |
|---|---|---|
| M0a | Radiant `radiant/paint/`: views, bounds query, procedural dabs (both profiles, AA, ellipse), build-up (normal, erase, lock-alpha) on f32 RGBA tiles, wash coverage on u8/f32 tiles, wash composite, import, present to sRGB u8; `test_paint_gtest` | **done**: 11/11 tests pass through Radiant's own `make` (`build/test/test_paint_gtest`) |
| M0b | Lambda bridge `radiant.paint_{dabs,wash,import,present,capabilities}` in `lambda/module/radiant/radiant_paint_adapter.cpp`; `radiant/paint` in `build_lambda_config.json` | **done** |
| M0c | `lambda.paint` package core: `layer.ls`, `brush.ls`, `stroke.ls`, facade; golden `test/lambda/paint/paint_core.ls` (21 checks) | **done**: passes on interp, JIT and AUTO, and under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` |
| M0d | `lambda.image` stroke coverage from `lambda.paint`; `edit_dab`, `edit_stroke` and `StrokeProgress` deleted; `image_edit.cpp` reads coverage tiles | **done**: `image_stroke_step`, `image_paint`, `proc/image_tile_storage` and `editor/photos_{stroke,paint,retouch,actions}` pass on three tiers (the image ones also under GC stress); Photos replays `brush_preview`, `brush_fit_scale`, `painting` and `retouch_actions` pass, as do the other Photos fixtures |
| M1a | Engine: smudge buckets, resolved colours, bitmap tips, paper grain, blend modes, colorize, posterize, colour shifts, `paint_sample` | **done**: `test_paint_gtest` 17/17 |
| M1b | Bridge: `paint_dabs` options `tips`, `paper`, `smudge`, `smudge_buckets`, `resolved`; `radiant.paint_sample` | **done** |
| M1c | Package: libmypaint dynamics (`dynamics.ls`), `mypaint` brush kind, `.myb` import (`myb.ls`), shared row builder (`table.ls`); golden `paint_mypaint.ls` (17 checks) | **done**: paint and image goldens pass on three tiers and under GC stress |
| M1d | `dom.set_canvas_pixels_rect` and region repaint of canvas writes (PE7) | **done**: `test/ui/paint/canvas_rect.json` passes, also under GC stress |
| M1e | macOS pen pressure, tilt, twist and pointer type; coalesced samples | pending |
| M1f | `lambda demo paint`: history, brush palette, colour picker, zoom and pan | pending |
| M1g | `.painting` ZIP save and load (stored pixels, PT12) | pending |
| M1h | General `var`-parameter system procedures with in-place writes (PT7) | pending |
| M2–M6 | As in [PT §6](../Lambda_Pkg_Paint.md#6-milestones-and-acceptance) | pending |

## Checkpoint

- **2026-11-09.** Design rulings recorded (in-place writes, `radiant/radiant/paint/`, `lambda-cli` without paint, CPU reference, ZIP container). Survey done: the build includes Radiant sources through `build_lambda_config.json` `source_dirs` and Radiant's `build_targets.json`; `alloc_ndim_arraynum` builds typed arrays; the existing brush serves stroke, stroke_step and retouch in `image_edit.cpp`; its goldens check relations rather than exact pixels. **Next:** write the M0a engine and its unit tests.
- **2026-11-09, later.** M0a done; M0b and M0c written and type-checked.
  - **Blocker:** `lambda.exe` does not build at HEAD `1c2da4bfd`, for a reason unrelated to paint. Commit `4abce6d5e` ("lambda mvp") added an `output_destination` parameter to the forward declarations of `expression`, `statements` and `direct_call` in `lambda/runtime/mvp/mvp_mir.cpp`, but not to their definitions or callers, so the calls are ambiguous.
  - **Language gotchas found while writing the package:** `state`, `continue`, `last` and `number` are reserved words. `if (c) [..]` parses as a subscript of `(c)`, so wrap array or map literals that follow an `if` condition in parentheses. An `if` expression followed by a spread inside an array literal does not parse. Import-time validation is stricter than a standalone `--emit-ast-dump`, so check modules through an importer.
  - **Next:** once `lambda.exe` builds, run the golden on interp, AUTO and JIT, then M0d.
- **2026-11-09, M0 complete (uncommitted).**
  - **Build unblocked.** The user approved reverting the three `output_destination` forward-declaration changes in `mvp_mir.cpp`. The same commit's stray `output_expression` declaration (never defined, rejected by `-Werror`) was also removed. Nothing else in that file changed.
  - **Image migration.** `image.ls` now builds coverage with `lambda.paint`'s linear-profile wash dabs on u8 tiles. The native `stroke`, `stroke_step` and `retouch` operations take `coverage`, `rect` and (for steps) `base` in place of points and brush options. The public continuation keeps `coverage` as a gray Raster, `distance` is now the arc length, and `next` and `layer` are new.
  - **Docs.** `doc/Lambda_Packages.md` §30, a `doc/Lambda_Image.md` note and the `test/README.md` entry.
  - **Baselines at M0.**
    - `make test-lambda-baseline`: 6,940 of 6,941 pass. The one failure is `mvp__mir_mvp_error_boundary_module`, a helper module under `test/lambda/mvp/` that expects `null` and prints nothing. It belongs to the half-committed MVP output-demand change (`4abce6d5e`) and not to paint.
    - `make -C ../radiant test-radiant-baseline`: 827 tests, 0 failures, including the 11 `PaintEngine` cases. The skips are pre-existing (RI-001 fixtures, live MIDI device).
    - All 28 Photos UI replays pass.
  - **Next:** M1 (interactive demo, pen input, `var`-parameter system procedures, libmypaint dynamics, stored-pixel painting files).
- **2026-11-09, M1 in progress (stopped at the usage limit; uncommitted, partly unverified).**
  - **M1a engine: done.** Smudge buckets with libmypaint pickup, resolved colours, bitmap tips, paper grain, blend modes (multiply, screen, overlay, darken, lighten, add), colorize, posterize, colour shifts and `paint_sample`. The dab layout is now 34 columns (column 33 is `blend`), and `paint_dabs` takes a `PaintDabParams`. `test_paint_gtest` passes 17/17 in the standalone build; Radiant's own `make` has not been rerun.
  - **M1b bridge: written and compiled, not run.** `paint_dabs` gains the `tips`, `paper`, `smudge`, `smudge_buckets` and `resolved` options, and `radiant.paint_sample(layer, x, y, radius)` is new. `lambda.exe` has not been rebuilt since.
  - **M1c package: started.** `lmd/package/paint/table.ls` (shared row builder) exists but nothing uses it yet. `stroke.ls` emits 34 columns, and `paint_core.ls` expects 34.
  - **Next:** rebuild with `make build`, rerun `temp/paint_m0/run_goldens.sh` over the paint and image goldens, then write the libmypaint dynamics in `paint/dynamics.ls` (settings and defaults from libmypaint's `brushsettings.json`, already reviewed) and `myb.ls`, and move `stroke.ls` onto `table.row`. M1d–M1h remain.
- **2026-11-09, M1a–M1d complete (uncommitted).**
  - **JIT GC defect found and fixed.** Under `LAMBDA_GC_FORCE_EVERY=1`, production-JIT string and symbol equality did not root its left operand while the right one allocated, so `string(b) == string('normal')` compared freed memory and matched unequal texts. This showed up as "unknown dab blend mode". The fix in `lambda/runtime/transpile-mir.cpp` roots the left text (**D6.3.3**). `test/lambda/string_eq_gc_roots.ls` is the regression; it fails without the fix, passes with it, and runs in the MIR GC stress suite (`kExtraLambdaScripts`).
  - **M1c verified.** `paint_core`, `paint_mypaint`, `image_paint`, `image_stroke_step` and `image_edit` pass on interp, JIT and AUTO, and under forced GC with poisoning on interp and JIT.
  - **M1d.** Radiant's `canvas_2d.cpp` gains `radiant_canvas_write_pixels`, placement notes and region damage. The DOM schema gains `write_pixels`, and the Radiant adapter gains `fn_radiant_set_canvas_pixels_rect`. `radiant_paint_rect_read` is shared from the paint adapter. The replay shows 5 region-only frames identical to full renders. `RadiantViewTest` Canvas and Photo cases pass 17/17.
  - **Environment.** `test_ui_automation_gtest` currently stops in preflight: `test/ui/js_block_in_inline_005.json` names `test/layout/data/baseline/block-in-inline-005.htm`, which the `lambda-test` checkout no longer has there. That is a test-data mismatch unrelated to paint.
  - **Next:** M1e pen input (Radiant NSEvent tablet fields, Lambda DOM pointer fields and coalesced samples), then the demo (M1f), painting files (M1g) and `var` procedures (M1h).
