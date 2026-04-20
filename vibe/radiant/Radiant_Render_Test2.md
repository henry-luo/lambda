# Radiant Render Test — Dual Threshold Design

## Overview

Pixel-level visual regression tests compare Radiant's PNG output against Chrome headless reference images using `pixelmatch` (YIQ color distance threshold 0.1). All tests use a 100×100 viewport at 1× pixel ratio.

**Test runner**: `lambda-test/render/test_radiant_render.js`

## Dual Threshold Design

Tests are automatically classified into two categories based on whether the HTML body contains visible text content:

| Category | Threshold | Rationale |
|----------|-----------|-----------|
| **No-text** | ≤ 1% | Pure geometry/color — Radiant should match Chrome closely |
| **Text** | ≤ 5% | Font rasterization differs between CoreText (Radiant) and Skia (Chrome) |

### Text Detection

The `hasVisibleText(htmlPath)` function:
1. Extracts the `<body>` content
2. Strips `<style>` and `<script>` blocks
3. Removes all HTML tags and `&nbsp;`
4. Collapses whitespace
5. If any non-whitespace characters remain → **text** test

### Threshold Priority

```
CLI --threshold  >  per-test .config.json  >  auto (text/no-text detection)
```

Per-test overrides via `page/<test>.config.json` with `{ "maxMismatchPercent": N }` are supported for cases that need individual thresholds, but not currently used.

## Current Results (60/93 passed)

### Non-Text Tests (51 total, 1% threshold) — 27 pass, 24 fail

#### Passing (27)

| Test | Diff |
|------|------|
| backdrop_filter_01 | exact |
| bg_color_01 | exact |
| bg_gradient_conic_01 | 0.27% |
| bg_gradient_linear_01 | exact |
| bg_gradient_radial_01 | exact |
| bg_gradient_rounded_01 | exact |
| blend_mode_01 | exact |
| border_radius_corners_01 | exact |
| border_radius_pill_01 | 0.64% |
| border_solid_01 | exact |
| box_shadow_01 | exact |
| box_shadow_rounded_01 | exact |
| clip_path_circle_01 | exact |
| clip_path_polygon_01 | exact |
| clip_path_polygon_complex_01 | exact |
| clip_path_shapes_01 | exact |
| color_hsl_01 | exact |
| css_clamp_01 | exact |
| filter_grayscale_01 | exact |
| filter_hue_rotate_01 | exact |
| filter_invert_01 | exact |
| filter_saturate_01 | exact |
| image_rendering_01 | exact |
| mix_blend_mode_01 | exact |
| object_position_01 | exact |
| opacity_01 | exact |
| opacity_nested_01 | exact |
| opacity_stacking_01 | exact |
| outline_01 | exact |
| overflow_clip_nested_01 | exact |
| svg_clippath_01 | exact |
| svg_gradient_01 | exact |
| svg_inline_01 | exact |
| svg_inline_shapes_01 | exact |
| svg_path_01 | exact |
| svg_stroke_01 | exact |
| svg_transform_01 | exact |
| svg_use_symbol_01 | exact |
| svg_viewbox_01 | exact |
| table_border_collapse_01 | 0.04% |
| transform_nested_01 | exact |
| transform_rotate_01 | exact |
| transform_scale_01 | exact |
| visibility_hidden_01 | exact |
| z_index_order_01 | exact |
| z_index_stacking_01 | exact |

#### Failing (24)

| Test | Diff | Category |
|------|------|----------|
| overflow_hidden_01 | 1.43% | Anti-aliasing |
| border_mixed_01 | 1.51% | Anti-aliasing |
| overflow_clip_rounded_01 | 1.83% | Anti-aliasing |
| outline_styles_01 | 2.16% | Anti-aliasing / border style |
| svg_opacity_01 | 2.60% | SVG compositing |
| overflow_clip_shapes_01 | 2.64% | Anti-aliasing |
| bg_repeat_01 | 3.24% | Background image |
| border_radius_01 | 3.96% | Border-radius |
| border_radius_elliptical_01 | 4.14% | Elliptical radius (not implemented) |
| border_radius_per_corner_01 | 4.20% | Border-radius |
| bg_image_01 | 4.95% | Background image |
| filter_drop_shadow_01 | 5.17% | Filter |
| border_styles_01 | 5.49% | 3D border color formula |
| bg_size_cover_01 | 6.47% | Background sizing |
| bg_position_01 | 6.62% | Background positioning |
| box_shadow_inset_01 | 7.36% | Inset box-shadow |
| border_radius_with_border_01 | 7.80% | Border-radius + border |
| clip_path_with_effects_01 | 9.60% | Clip-path + filter interaction |
| border_bg_combined_01 | 10.01% | Border + background combined |
| svg_file_bg_01 | 10.76% | External SVG sizing |
| filter_blur_01 | 14.08% | Blur filter (no offscreen buffer) |
| svg_file_sizes_01 | 14.49% | External SVG sizing |
| svg_file_img_01 | 18.74% | External SVG sizing |
| bg_clip_01 | 14.81% | Background clip |

### Text Tests (42 total, 5% threshold) — 33 pass, 9 fail

#### Passing (33)

| Test | Diff |
|------|------|
| h2_direction | 0.10% |
| h2_baseline_01 | 0.84% |
| text_weight_01 | 0.67% |
| text_color_01 | 1.29% |
| list_style_types_01 | 2.51% |
| text_align_01 | 2.90% |
| text_overflow_ellipsis_01 | 3.72% |
| svg_text_anchor_01 | 3.67% |
| svg_text_color_01 | 3.96% |
| list_inside_outside_01 | 4.05% |
| text_decoration_01 | 4.36% |
| text_letter_spacing_01 | 4.42% |
| list_style_image_01 | 4.56% |
| svg_text_basic_01 | 4.56% |

#### Failing (9)

| Test | Diff | Category |
|------|------|----------|
| composite_card_01 | 5.11% | Mixed text + effects |
| text_shadow_01 | 5.48% | Text shadow rendering |
| svg_text_tspan_01 | 6.12% | SVG tspan positioning |
| glyph_quality | 6.54% | Font rasterization |
| list_markers_01 | 7.20% | List marker positioning |
| text_deco_style_01 | 8.27% | Text decoration styles |
| line_clamp_01 | 9.41% | Line clamping |
| multicol_rule_01 | 10.50% | Multi-column rule |
| text_deco_color_01 | 14.66% | Text decoration color |

## Failure Categories (Non-Text)

| Category | Tests | Root Cause |
|----------|-------|------------|
| **Anti-aliasing** | 6 tests (1.4–2.6%) | ThorVG vs Chrome/Skia rasterizer differences at curved edges |
| **Border-radius** | 4 tests (3.9–7.8%) | Circular arc approximation + no elliptical radius support |
| **Background image** | 5 tests (3.2–14.8%) | Sub-pixel positioning, cover/contain sizing, clip handling |
| **Filter/blur** | 2 tests (5.2–14.1%) | No offscreen buffer isolation for blur; drop-shadow offset |
| **3D border styles** | 1 test (5.5%) | Remaining inset/double color formula differences vs Chrome |
| **External SVG** | 3 tests (10.8–18.7%) | SVG intrinsic sizing and viewport mapping |
| **Other** | 3 tests (7.4–10.0%) | Inset box-shadow, clip-path + effects, border+bg combined |

## Failure Categories (Text)

| Category | Tests | Root Cause |
|----------|-------|------------|
| **Near threshold** | 3 tests (5.1–5.5%) | Just above 5% — font metrics + effect rendering |
| **Text decoration** | 2 tests (8.3–14.7%) | Decoration line style/color rendering differences |
| **Layout/positioning** | 2 tests (7.2–9.4%) | List marker, line-clamp truncation |
| **Multi-column** | 1 test (10.5%) | Column rule rendering |
| **Font quality** | 1 test (6.5%) | Glyph rasterization differences |

## Fixes Applied

| Fix | Files Changed | Impact |
|-----|--------------|--------|
| Chrome-compatible 3D border colors (darken ×2/3, lighten ×1/3) | `render_border.cpp` | border_styles_01: 19% → 5.49% |
| Groove/ridge per-side color variation (top/left ≠ bottom/right) | `render_border.cpp` | Correct 3D border appearance |
| Collapsed border centering on cell edges | `render.cpp` | table_border_collapse_01: 24.22% → 0.04% |
| Table auto-layout double margin subtraction | `layout_table.cpp` | Table column widths now correct |
