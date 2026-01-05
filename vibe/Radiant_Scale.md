# Radiant Scale Refactoring Proposal

## Executive Summary

This proposal outlines a major refactoring of Radiant's coordinate system to:
1. **Decouple pixel_ratio from layout** — Layout operates in CSS logical pixels only
2. **Introduce unified scale system** — `DomDocument.scale = given_scale × pixel_ratio`
3. **Centralize scaling in rendering** — All output formats (screen/PNG/SVG/PDF) scale during render
4. **Add CLI `--scale` parameter** — User-controlled content scaling

---

## Implementation Status

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 0** | View tree printing in CSS logical pixels | ✅ Completed |
| **Phase 1** | Add DomDocument `given_scale` and `scale` fields | ✅ Completed |
| **Phase 2** | Remove pixel_ratio from style resolution | ✅ Completed |
| **Phase 3** | Update layout to use logical pixels | ✅ Completed |
| **Phase 4** | Update UiContext default font | ✅ Completed |
| **Phase 5** | Add scaling to rendering (PNG/JPEG/SVG/PDF) | ✅ Completed |
| **Phase 6** | Input event handling | ✅ No changes needed (already correct) |
| **Phase 7** | Add CLI --scale parameter | ✅ Completed |
| **Phase 8** | Event handling and iframe scale support | ✅ Completed |
| **Phase 9** | Viewport meta tag and body transform scale | ✅ Completed |
| **Testing** | All baseline tests | ✅ All 1777 tests pass |

### Test Results

```
Lambda Runtime Tests: 73/73 ✅
Radiant Baseline Tests: 1704/1704 ✅
```

---

## Phase 0: View Tree Printing (COMPLETED)

**Status:** ✅ Implemented

Updated view tree JSON output to use CSS logical pixels directly, removing pixel_ratio dependency.

### Files Modified:
- **radiant/layout.hpp** — Updated function declarations (removed pixel_ratio parameter)
- **radiant/view_pool.cpp** — Updated all JSON printing functions:
  - `print_view_tree()` — Removed pixel_ratio parameter
  - `print_view_tree_json()` — Removed pixel_ratio, added `"coordinate_system": "css_logical_pixels"`
  - `print_bounds_json()` — Outputs coordinates directly without division
  - `print_combined_text_json()` — Removed pixel_ratio parameter
  - `print_children_json()` — Removed pixel_ratio parameter
  - `print_block_json()` — Removed pixel_ratio parameter
  - `print_text_json()` — Removed pixel_ratio parameter
  - `print_br_json()` — Removed pixel_ratio parameter
  - `print_inline_json()` — Removed pixel_ratio parameter
- **radiant/layout.cpp** — Updated call site (line 1310)
- **radiant/cmd_layout.cpp** — Updated call site (line 3211)

### Result:
- All 1704 radiant baseline tests pass
- View tree JSON now outputs CSS logical pixel coordinates
- JSON includes `"coordinate_system": "css_logical_pixels"` for clarity

---

## Phase 8: Event Handling and IFrame Scale Support (COMPLETED)

**Status:** ✅ Implemented

Updated event handling and document loading to properly handle scale fields for nested documents (iframes) and main document loading.

### Files Modified:

**radiant/event.cpp:**
- Fixed iframe viewport calculation: `block->width/height` are now CSS pixels, no division by pixel_ratio needed
- Set `given_scale` and `scale` fields on newly loaded iframe documents
- Updated main document loading to use `viewport_width/height` (CSS pixels) instead of `window_width/height / pixel_ratio`

**radiant/cmd_layout.cpp:**
- Added `given_scale` and `scale` initialization in `load_lambda_html_doc()` for HTML documents
- Added scale field initialization in `load_pdf_doc()`, `load_svg_doc()`, and `load_image_doc()`
- PDF/SVG/Image documents set `scale = pixel_ratio` since their view trees are pre-scaled

**radiant/window.cpp:**
- Updated `show_html_doc()` to set scale fields: `given_scale = 1.0f`, `scale = pixel_ratio`
- Updated `view_doc_in_window()` to set scale fields for HTML documents after loading

### Key Changes:

```cpp
// Before (event.cpp iframe loading):
int css_vw = (int)(iframe_width / evcon.ui_context->pixel_ratio);  // WRONG - double division

// After:
int css_vw = (int)block->width;  // block->width is already CSS pixels

// After loading iframe document:
new_doc->given_scale = 1.0f;
new_doc->scale = new_doc->given_scale * evcon.ui_context->pixel_ratio;
```

### Nested Document Scale Inheritance:

Iframe documents use default scale (`given_scale = 1.0`), combined with the display's `pixel_ratio` to compute final `scale`. This ensures consistent rendering across different display densities.

---

## Phase 9: Viewport Meta Tag and Body Transform Scale (COMPLETED)

**Status:** ✅ Implemented

Added support for extracting scale-related values from HTML viewport meta tags and CSS body transform properties. These values are stored in `DomDocument` for use by the rendering system.

### New DomDocument Fields

```cpp
struct DomDocument {
    // ... existing fields ...

    // Viewport meta tag values
    float viewport_initial_scale;  // initial-scale from viewport meta (default 1.0)
    float viewport_min_scale;      // minimum-scale from viewport meta
    float viewport_max_scale;      // maximum-scale from viewport meta
    int viewport_width;            // viewport width (0 = device-width)
    int viewport_height;           // viewport height (0 = device-height)

    // CSS body transform
    float body_transform_scale;    // transform: scale() from body CSS (default 1.0)
};
```

### Files Modified

**lambda/input/css/dom_element.hpp:**
- Added 6 new fields to `DomDocument` struct for viewport and transform scale values
- Updated constructor to initialize all new fields with defaults

**radiant/cmd_layout.cpp:**
- Added `parse_viewport_content()` — Parses viewport meta content attribute format (`width=device-width, initial-scale=1.0`)
- Added `extract_viewport_meta()` — Recursively finds `<meta name="viewport">` in HTML tree and extracts values
- Added `extract_transform_scale()` — Extracts scale value from CSS transform declaration (supports `scale()`, `scaleX()`, `scaleY()`)
- Added `extract_body_transform_scale()` — Finds body element and extracts its transform scale from computed CSS
- Integrated extraction into `load_lambda_html_doc()` pipeline

### Supported HTML/CSS Syntax

**Viewport Meta Tag:**
```html
<meta name="viewport" content="width=device-width, initial-scale=0.75, minimum-scale=0.5, maximum-scale=2.0">
```

Parsed values:
- `width` → `viewport_width` (0 for `device-width`)
- `height` → `viewport_height` (0 for `device-height`)
- `initial-scale` → `viewport_initial_scale`
- `minimum-scale` → `viewport_min_scale`
- `maximum-scale` → `viewport_max_scale`

**CSS Body Transform:**
```css
body {
    transform: scale(0.9);      /* Uniform scale */
    transform: scale(0.9, 0.8); /* X and Y scale (uses X) */
    transform: scaleX(0.9);     /* X scale only */
    transform: scaleY(0.9);     /* Y scale only */
}
```

### Integration Points

The extracted values are available in `DomDocument` after `load_lambda_html_doc()` completes:

```cpp
// Example usage in rendering
DomDocument* doc = load_lambda_html_doc(...);

// Access viewport meta values
if (doc->viewport_initial_scale != 1.0f) {
    // Apply initial scale from viewport meta
}

// Access body transform scale
if (doc->body_transform_scale != 1.0f) {
    // Apply body transform scale to rendering
}
```

### Log Output

When viewport or transform scale values are found, they are logged:

```
[viewport] Parsing viewport content: 'width=device-width, initial-scale=0.75, minimum-scale=0.5, maximum-scale=2.0'
[viewport] width=device-width
[viewport] initial-scale=0.75
[viewport] minimum-scale=0.50
[viewport] maximum-scale=2.00
[transform] Found scale(0.900)
[transform] Body transform scale=0.900
```

---

## 1. Current Architecture Analysis

### 1.1 Where pixel_ratio Lives

```
UiContext
├── pixel_ratio          // Set during init: 1.0 (headless) or GLFW framebuffer/window ratio
├── window_width/height  // Physical pixels (framebuffer size)
├── viewport_width/height // CSS logical pixels (for vh/vw units)
└── default_font.font_size // Pre-scaled: 16 × pixel_ratio
```

### 1.2 Current pixel_ratio Usage (Problems)

| Location | Usage | Problem |
|----------|-------|---------|
| `resolve_css_style.cpp:988-1006` | Scale CSS length units (px, cm, mm, in, pt, pc) | Layout produces physical pixels, not CSS pixels |
| `resolve_css_style.cpp:1033-1049` | Scale viewport units (vw, vh, vmin, vmax) | Same problem |
| `resolve_htm_style.cpp` (40+ occurrences) | Scale HTML attribute values (margins, padding, borders, font sizes) | Hardcoded `* pixel_ratio` everywhere |
| `layout_block.cpp:1548-1549` | Scale intrinsic image dimensions | Image sizing tied to display ratio |
| `layout_table.cpp:3030` | Scale HTML width attribute | Table dimensions scaled at layout |
| `layout_positioned.cpp:406-407` | Scale positioned element intrinsic sizes | Positioned elements coupled to ratio |
| `ui_context.cpp:138-139` | Pre-scale default font sizes | Fonts initialized with physical pixels |
| `form_control.cpp` | Scale form control intrinsic sizes | Control sizes depend on ratio |
| `window.cpp:239-288` | Convert mouse coordinates | Input handling mixed with layout |

### 1.3 Current Data Flow

```
HTML/CSS Input
      │
      ▼
┌─────────────────────────────────────────┐
│         Style Resolution                 │
│  CSS values × pixel_ratio → physical px │ ← PROBLEM: Layout mixed with display
└────────────────┬────────────────────────┘
                 ▼
┌─────────────────────────────────────────┐
│              Layout                      │
│    All coordinates in physical pixels   │ ← PROBLEM: View tree scale-dependent
└────────────────┬────────────────────────┘
                 ▼
┌─────────────────────────────────────────┐
│             Rendering                    │
│  Direct output (no additional scaling)  │ ← PROBLEM: Can't render at different scales
└─────────────────────────────────────────┘
```

### 1.4 Why This Is Problematic

1. **Layout is non-portable** — View tree dimensions depend on display pixel_ratio
2. **Headless renders are 1x only** — CLI render always uses `pixel_ratio=1.0`
3. **No user-controlled scaling** — Can't render 2x image for Retina display from CLI
4. **PDF/SVG dimensions wrong** — Vector outputs have display-dependent coordinates
5. **Redundant scaling** — `* pixel_ratio` scattered across 80+ locations
6. **Testing inconsistency** — Layout tests depend on platform pixel_ratio

---

## 2. Proposed Architecture

### 2.1 New DomDocument Fields

```cpp
struct DomDocument {
    // ... existing fields ...

    // NEW: Scale system
    float given_scale;    // User-specified scale (default 1.0), from CLI --scale param
    float scale;          // Final render scale = given_scale × pixel_ratio

    // Note: pixel_ratio comes from UiContext, not stored in DomDocument
};
```

### 2.2 New Data Flow

```
HTML/CSS Input
      │
      ▼
┌─────────────────────────────────────────┐
│         Style Resolution                 │
│     CSS values → CSS logical pixels     │ ← FIXED: No pixel_ratio scaling
└────────────────┬────────────────────────┘
                 ▼
┌─────────────────────────────────────────┐
│              Layout                      │
│   All coordinates in CSS logical pixels │ ← FIXED: Portable view tree
└────────────────┬────────────────────────┘
                 ▼
┌─────────────────────────────────────────┐
│        Render Scaling                    │
│   scale = given_scale × pixel_ratio     │
│   Output coords = layout coords × scale │ ← NEW: Centralized scaling
└────────────────┬────────────────────────┘
                 ▼
┌─────────────────────────────────────────┐
│           Output Format                  │
│   Screen | PNG | SVG | PDF              │
└─────────────────────────────────────────┘
```

### 2.3 Scale Computation

```cpp
// In ui_context_init() or layout_html_doc():
void compute_render_scale(DomDocument* doc, UiContext* uicon) {
    // given_scale: user preference from CLI (default 1.0)
    // pixel_ratio: display scaling (1.0 headless, 2.0 Retina, etc.)
    doc->scale = doc->given_scale * uicon->pixel_ratio;
}

// Example scenarios:
// CLI render with --scale 2: given_scale=2.0, pixel_ratio=1.0 → scale=2.0
// Retina window view:        given_scale=1.0, pixel_ratio=2.0 → scale=2.0
// CLI render default:        given_scale=1.0, pixel_ratio=1.0 → scale=1.0
// Retina with --scale 0.5:   given_scale=0.5, pixel_ratio=2.0 → scale=1.0
```

---

## 3. Implementation Plan

### Phase 1: Add DomDocument Scale Fields

**Files to modify:**
- [dom_element.hpp](lambda/input/css/dom_element.hpp#L48-L68)

**Changes:**
```cpp
struct DomDocument {
    // ... existing fields ...

    float given_scale;   // User-specified scale (default 1.0)
    float scale;         // Final scale = given_scale × pixel_ratio

    // Constructor update
    DomDocument() : /* ... existing init ... */,
                    given_scale(1.0f), scale(1.0f) {}
};
```

### Phase 2: Remove pixel_ratio from Style Resolution

**Files to modify:**
- [resolve_css_style.cpp](radiant/resolve_css_style.cpp)
- [resolve_htm_style.cpp](radiant/resolve_htm_style.cpp)

**Key changes in `resolve_css_style.cpp` (~12 occurrences):**

```cpp
// BEFORE (line 988-1006):
case CSS_UNIT_PX:
    result = num * lycon->ui_context->pixel_ratio;
    break;
case CSS_UNIT_CM:
    result = num * (96 / 2.54) * lycon->ui_context->pixel_ratio;
    break;
// ... similar for other units

// AFTER:
case CSS_UNIT_PX:
    result = num;  // CSS logical pixels
    break;
case CSS_UNIT_CM:
    result = num * (96 / 2.54);  // CSS logical pixels (96 dpi)
    break;
```

**Key changes in `resolve_htm_style.cpp` (~40+ occurrences):**

```cpp
// BEFORE:
block->bound->margin.bottom = 8 * lycon->ui_context->pixel_ratio;
span->font->font_size = font_size * lycon->ui_context->pixel_ratio;

// AFTER:
block->bound->margin.bottom = 8;  // CSS logical pixels
span->font->font_size = font_size;  // CSS logical pixels
```

### Phase 3: Update Layout to Use Logical Pixels

**Files to modify:**
- [layout.cpp](radiant/layout.cpp)
- [layout_block.cpp](radiant/layout_block.cpp)
- [layout_table.cpp](radiant/layout_table.cpp)
- [layout_positioned.cpp](radiant/layout_positioned.cpp)
- [intrinsic_sizing.cpp](radiant/intrinsic_sizing.cpp)

**Example changes:**

```cpp
// layout_block.cpp:1548-1549
// BEFORE:
float w = img->width * lycon->ui_context->pixel_ratio;
float h = img->height * lycon->ui_context->pixel_ratio;

// AFTER:
float w = img->width;  // Intrinsic CSS pixels
float h = img->height; // Intrinsic CSS pixels
```

```cpp
// layout_table.cpp:3030
// BEFORE:
float attr_width = width * lycon->ui_context->pixel_ratio;

// AFTER:
float attr_width = width;  // CSS logical pixels
```

### Phase 4: Update UiContext Default Font

**Files to modify:**
- [ui_context.cpp](radiant/ui_context.cpp#L138-139)

```cpp
// BEFORE:
uicon->default_font = (FontProp){"Times New Roman", (float)(16 * uicon->pixel_ratio), ...};

// AFTER:
uicon->default_font = (FontProp){"Times New Roman", 16.0f, ...};  // CSS pixels
```

### Phase 5: Add Scaling to Rendering

**Files to modify:**
- [render.hpp](radiant/render.hpp) — Add RenderContext with scale
- [render_img.cpp](radiant/render_img.cpp) — PNG/JPEG scaling
- [render_svg.cpp](radiant/render_svg.cpp) — SVG scaling
- [render_pdf.cpp](radiant/render_pdf.cpp) — PDF scaling
- [window.cpp](radiant/window.cpp) — Screen rendering scaling

#### 5.1 Create RenderContext

```cpp
// render.hpp
struct RenderContext {
    float scale;           // Final render scale
    int output_width;      // Output canvas width in physical pixels
    int output_height;     // Output canvas height in physical pixels

    // Convert layout coordinates to render coordinates
    inline float to_render_x(float css_x) const { return css_x * scale; }
    inline float to_render_y(float css_y) const { return css_y * scale; }
    inline float to_render_dim(float css_dim) const { return css_dim * scale; }
};
```

#### 5.2 PNG/JPEG Rendering

```cpp
// render_img.cpp
int render_html_to_png(const char* html_file, const char* png_file,
                       int viewport_width, int viewport_height, float given_scale) {
    // ... load document ...

    // Compute final scale
    doc->given_scale = given_scale;
    doc->scale = doc->given_scale;  // headless: pixel_ratio = 1.0

    // Layout in CSS pixels
    layout_html_doc(&ui_context, doc, false);

    // Create render surface at scaled dimensions
    int render_width = (int)(content_width * doc->scale);
    int render_height = (int)(content_height * doc->scale);
    ui_context_create_surface(&ui_context, render_width, render_height);

    // Render with scaling
    RenderContext rctx = { doc->scale, render_width, render_height };
    render_view_tree_scaled(&rctx, doc->view_tree);

    save_surface_to_png(ui_context.surface, png_file);
}
```

#### 5.3 SVG Rendering

```cpp
// render_svg.cpp
int render_html_to_svg(const char* html_file, const char* svg_file,
                       int viewport_width, int viewport_height, float given_scale) {
    // ... load and layout document ...

    doc->given_scale = given_scale;
    doc->scale = doc->given_scale;  // SVG: always scale in coordinates

    // SVG header with scaled viewBox
    float svg_width = content_width * doc->scale;
    float svg_height = content_height * doc->scale;
    strbuf_printf(svg, "<svg xmlns=\"...\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n",
                  svg_width, svg_height, svg_width, svg_height);

    // Render with scale applied to all coordinates
    render_view_tree_svg(svg_ctx, doc->view_tree, doc->scale);
}
```

#### 5.4 PDF Rendering

```cpp
// render_pdf.cpp
int render_html_to_pdf(const char* html_file, const char* pdf_file,
                       int viewport_width, int viewport_height, float given_scale) {
    // ... load and layout document ...

    doc->given_scale = given_scale;
    doc->scale = doc->given_scale;

    // PDF page size in points (1pt = 1/72 inch)
    // Convert CSS pixels to points: css_px * (72/96) * scale
    float page_width = content_width * (72.0f / 96.0f) * doc->scale;
    float page_height = content_height * (72.0f / 96.0f) * doc->scale;

    HPDF_Page_SetWidth(page, page_width);
    HPDF_Page_SetHeight(page, page_height);

    // Render with coordinate transformation
    render_view_tree_pdf(pdf_ctx, doc->view_tree, doc->scale);
}
```

#### 5.5 Screen Rendering

```cpp
// window.cpp
void render_to_screen(UiContext* uicon) {
    DomDocument* doc = uicon->document;

    // Compute scale for display
    doc->scale = doc->given_scale * uicon->pixel_ratio;

    // Create surface at physical pixel dimensions
    int surface_width = (int)(doc->view_tree->root->width * doc->scale);
    int surface_height = (int)(doc->view_tree->root->height * doc->scale);
    ui_context_create_surface(uicon, surface_width, surface_height);

    // Render with scaling
    RenderContext rctx = { doc->scale, surface_width, surface_height };
    render_view_tree_scaled(&rctx, doc->view_tree);
}
```

### Phase 6: Update Input Event Handling

**Files to modify:**
- [window.cpp](radiant/window.cpp#L239-288)

```cpp
// BEFORE:
event.mouse_position.x = xpos * ui_context.pixel_ratio;
event.mouse_position.y = ypos * ui_context.pixel_ratio;

// AFTER:
// Mouse position in CSS pixels (match layout coordinates)
event.mouse_position.x = xpos;
event.mouse_position.y = ypos;
// Note: GLFW already gives us CSS pixels on modern systems
// For hit-testing, no conversion needed since layout is in CSS pixels
```

### Phase 7: Add CLI --scale Parameter

**Files to modify:**
- [main.cpp](lambda/main.cpp#L835-990)

**Changes to `render` command:**

```cpp
// Parse arguments
float given_scale = 1.0f;  // NEW: default scale

for (int i = 2; i < argc; i++) {
    // ... existing options ...

    if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) {
        if (i + 1 < argc) {
            given_scale = atof(argv[++i]);
            if (given_scale <= 0) {
                printf("Error: Invalid scale '%s'. Must be positive.\n", argv[i]);
                return 1;
            }
        } else {
            printf("Error: --scale option requires a value\n");
            return 1;
        }
    }
}

// Pass scale to render functions
exit_code = render_html_to_png(html_file, output_file, viewport_width, viewport_height, given_scale);
```

**Update help text:**

```cpp
printf("Options:\n");
printf("  -o <output>              Output file path (required)\n");
printf("  -s, --scale FACTOR       Scale factor for output (default: 1.0)\n");
printf("                           Use 2.0 for high-DPI/Retina output\n");
printf("  -vw, --viewport-width    Viewport width in CSS pixels\n");
printf("  -vh, --viewport-height   Viewport height in CSS pixels\n");
```

**Update render function signatures:**

```cpp
// In main.cpp declarations
int render_html_to_svg(const char* html_file, const char* svg_file,
                       int viewport_width, int viewport_height, float scale = 1.0f);
int render_html_to_pdf(const char* html_file, const char* pdf_file,
                       int viewport_width, int viewport_height, float scale = 1.0f);
int render_html_to_png(const char* html_file, const char* png_file,
                       int viewport_width, int viewport_height, float scale = 1.0f);
int render_html_to_jpeg(const char* html_file, const char* jpeg_file,
                        int quality, int viewport_width, int viewport_height, float scale = 1.0f);
```

---

## 4. File Change Summary

| File | Changes | Scope |
|------|---------|-------|
| `dom_element.hpp` | Add `given_scale`, `scale` to DomDocument | 5 lines |
| `resolve_css_style.cpp` | Remove `* pixel_ratio` from length resolution | ~15 changes |
| `resolve_htm_style.cpp` | Remove `* pixel_ratio` from HTML attribute resolution | ~40 changes |
| `ui_context.cpp` | Remove pixel_ratio from default font sizes | 2 changes |
| `layout_block.cpp` | Remove pixel_ratio from intrinsic sizing | 3 changes |
| `layout_table.cpp` | Remove pixel_ratio from table attribute handling | 1 change |
| `layout_positioned.cpp` | Remove pixel_ratio from positioned element sizing | 2 changes |
| `render.hpp` | Add RenderContext struct | 20 lines |
| `render_img.cpp` | Add scale parameter, render with scaling | 30 lines |
| `render_svg.cpp` | Add scale parameter, render with scaling | 30 lines |
| `render_pdf.cpp` | Add scale parameter, render with scaling | 30 lines |
| `window.cpp` | Update screen rendering with scale, fix input events | 20 changes |
| `main.cpp` | Add `--scale` CLI argument | 30 lines |

**Estimated total changes:** ~200 line modifications across 13 files

---

## 5. Testing Strategy

### 5.1 Layout Test Updates

Update `test/layout/` baseline tests to expect CSS pixel values (not physical):

```bash
# Run layout tests with pixel_ratio=1.0 (should match browser)
make layout suite=baseline

# Verify layout results are identical regardless of display pixel_ratio
```

### 5.2 Render Output Tests

Create new render tests:

```bash
# Test scale=1.0 (default)
./lambda.exe render test.html -o test_1x.png

# Test scale=2.0 (Retina equivalent)
./lambda.exe render test.html -o test_2x.png --scale 2

# Verify: test_2x.png dimensions = 2 × test_1x.png dimensions
# Verify: test_2x.png quality matches test_1x.png (no blur/artifacts)
```

### 5.3 Cross-Platform Verification

```bash
# macOS Retina (pixel_ratio=2.0):
./lambda.exe view test.html
# Verify: Window renders crisply at native resolution

# Linux (pixel_ratio=1.0):
./lambda.exe view test.html
# Verify: Window renders correctly

# CLI render produces identical output on both platforms
./lambda.exe render test.html -o test.png
diff test_mac.png test_linux.png  # Should be identical
```

---

## 6. Migration Notes

### 6.1 Breaking Changes

- **View tree coordinates change** — All `x`, `y`, `width`, `height` values in ViewTree will be CSS logical pixels instead of physical pixels
- **FontProp.font_size changes** — Font sizes will be CSS pixels, not pre-scaled
- **External code using ViewTree** — Any code reading view tree dimensions needs awareness that values are now CSS pixels

### 6.2 Backward Compatibility

For external code that expects physical pixel coordinates:

```cpp
// Helper function for migration
float to_physical_px(float css_px, UiContext* uicon) {
    return css_px * uicon->pixel_ratio;
}
```

### 6.3 Performance Impact

- **Slight improvement** — Remove ~80 runtime multiplications during style resolution
- **Memory unchanged** — No new per-element allocations
- **Rendering cost** — One additional multiplication per coordinate during render (negligible)

---

## 7. Future Enhancements

After this refactoring, the following become straightforward:

1. **Print-quality PDF** — `./lambda.exe render doc.html -o doc.pdf --scale 3` for 300dpi
2. **Thumbnail generation** — `./lambda.exe render page.html -o thumb.png --scale 0.25`
3. **Responsive testing** — Layout at different viewport sizes without pixel_ratio confusion
4. **Multi-resolution assets** — Generate 1x, 2x, 3x images from single layout
5. **PDF zoom** — User-controlled zoom in PDF output

---

## 8. Implementation Order

1. **Phase 1-2** — DomDocument fields + remove pixel_ratio from style resolution
2. **Phase 3-4** — Update layout and UiContext
3. **Phase 5** — Add rendering scale support
4. **Phase 6** — Fix input event handling
5. **Phase 7** — Add CLI parameter
6. **Testing** — Update baselines and add render tests

**Estimated effort:** 2-3 days for implementation + 1 day for testing

---

## References

- [CSS Values and Units Level 3](https://www.w3.org/TR/css-values-3/) — CSS pixel definition
- [CSS Typed OM Level 1](https://www.w3.org/TR/css-typed-om-1/) — Device pixel ratio
- [HTML Canvas devicePixelRatio](https://developer.mozilla.org/en-US/docs/Web/API/Window/devicePixelRatio) — Browser scaling model
