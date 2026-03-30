# Radiant Rendering Automated Test Proposal

## Overview

This proposal introduces **pixel-level visual regression testing** for Radiant's rendering pipeline. The existing layout test suite (`test/layout/`) validates structural correctness (element positions, sizes, computed CSS), but does not verify the final rendered output — colors, gradients, shadows, borders, text appearance, opacity, transforms, and images.

The render test suite renders HTML pages through both the **browser (Chrome via Puppeteer)** and **Radiant (`lambda.exe render`)**, then compares the two PNG outputs pixel-by-pixel. Any difference exceeding a configurable threshold triggers a test failure.

---

## Directory Structure

```
test/render/
├── page/                      # HTML test pages (100×100px each)
│   ├── bg_color_01.html
│   ├── bg_gradient_linear_01.html
│   ├── border_solid_01.html
│   ├── box_shadow_01.html
│   ├── opacity_01.html
│   ├── transform_rotate_01.html
│   └── ...
├── reference/                 # Browser-rendered reference PNGs (100×100px)
│   ├── bg_color_01.png
│   ├── bg_gradient_linear_01.png
│   └── ...
├── output/                    # Radiant-rendered PNGs (gitignored, generated at test time)
│   ├── bg_color_01.png
│   └── ...
├── diff/                      # Visual diff PNGs (gitignored, generated on failure)
│   ├── bg_color_01.png
│   └── ...
├── capture_render_references.js   # Puppeteer script → captures reference PNGs
├── test_radiant_render.js         # Test runner → renders via Radiant, compares, reports
└── package.json                   # Local deps (pixelmatch, pngjs)
```

**Committed to git:** `page/*.html`, `reference/*.png`, scripts, `package.json`
**Gitignored:** `output/`, `diff/`

---

## HTML Test Page Format

Each test page is a self-contained 100×100 CSS-pixel HTML file. The small size keeps reference PNGs tiny (~1–4 KB) and comparisons fast.

```html
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  * { margin: 0; padding: 0; }
  html, body { width: 100px; height: 100px; overflow: hidden; }
  /* test-specific CSS here */
</style>
</head>
<body>
  <!-- test content here -->
</body>
</html>
```

**Conventions:**

| Rule | Rationale |
|------|-----------|
| Viewport fixed at 100×100px | Small reference images, fast comparison |
| No external resources (fonts, images) | Self-contained, no network dependencies |
| System fonts preferred unless testing font rendering | Avoids platform font variation |
| White or solid background | Clear pixel diffing |
| One CSS feature per test | Isolates failures to a specific rendering capability |
| Reset `* { margin: 0; padding: 0; }` | Eliminates browser default style variation |

---

## Reference Capture (Puppeteer)

### Script: `capture_render_references.js`

Uses Puppeteer to open each HTML file in headless Chrome at the exact viewport size and screenshot to PNG.

```js
// Pseudocode outline
const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

async function captureReferences(options) {
    const pageDir = path.join(__dirname, 'page');
    const refDir  = path.join(__dirname, 'reference');

    const browser = await puppeteer.launch({
        headless: true,
        args: ['--no-sandbox', '--disable-gpu', '--font-render-hinting=none']
    });

    const files = (await fs.promises.readdir(pageDir))
        .filter(f => f.endsWith('.html'));

    for (const file of files) {
        const page = await browser.newPage();
        await page.setViewport({ width: 100, height: 100, deviceScaleFactor: 1 });
        await page.goto(`file://${path.join(pageDir, file)}`, { waitUntil: 'networkidle0' });
        const outPath = path.join(refDir, file.replace(/\.html$/, '.png'));
        await page.screenshot({ path: outPath, type: 'png', clip: { x: 0, y: 0, width: 100, height: 100 } });
        await page.close();
        console.log(`  ✅ ${file} → ${path.basename(outPath)}`);
    }

    await browser.close();
}
```

**Key settings:**
- `deviceScaleFactor: 1` — matches Radiant's default `--pixel-ratio 1.0`
- `--font-render-hinting=none` — reduces platform-specific anti-aliasing variation
- `clip: { x: 0, y: 0, width: 100, height: 100 }` — exact 100×100 crop
- `waitUntil: 'networkidle0'` — ensures all rendering completes

**CLI usage:**
```bash
node test/render/capture_render_references.js                    # Capture all
node test/render/capture_render_references.js --test bg_color_01 # Capture one
node test/render/capture_render_references.js --force            # Re-capture all (overwrite)
```

---

## Radiant Rendering

The test runner invokes `lambda.exe render` for each test page:

```bash
./lambda.exe render test/render/page/bg_color_01.html \
    -o test/render/output/bg_color_01.png \
    -vw 100 -vh 100 --pixel-ratio 1.0
```

This produces a 100×100 PNG rendered by Radiant's raster pipeline (ThorVG canvas → libpng).

---

## Image Comparison

### Tool: `pixelmatch`

[pixelmatch](https://github.com/mapbox/pixelmatch) is a lightweight, dependency-free pixel-level image comparison library. It is the standard for visual regression testing (used by Playwright, Puppeteer, Storybook, etc.).

**Why pixelmatch:**
- Perceptual color distance (YIQ ΔE) — tolerant of anti-aliasing artifacts
- Built-in anti-aliasing detection — ignores single-pixel AA differences
- Generates visual diff images highlighting mismatched pixels
- Pure JS, no native dependencies — works on macOS/Linux/Windows
- 40 bytes per pixel, handles 100×100 images in microseconds

### Comparison Algorithm

```js
const pixelmatch = require('pixelmatch');
const { PNG } = require('pngjs');

function compareImages(radiantPath, referencePath, diffPath) {
    const radiant   = PNG.sync.read(fs.readFileSync(radiantPath));
    const reference = PNG.sync.read(fs.readFileSync(referencePath));
    const { width, height } = reference;
    const diff = new PNG({ width, height });

    const mismatchedPixels = pixelmatch(
        reference.data, radiant.data, diff.data,
        width, height,
        { threshold: 0.1, includeAA: false }
    );

    // Write diff image on failure
    if (mismatchedPixels > 0) {
        fs.writeFileSync(diffPath, PNG.sync.write(diff));
    }

    const totalPixels = width * height;
    const mismatchPercent = (mismatchedPixels / totalPixels) * 100;
    return { mismatchedPixels, totalPixels, mismatchPercent };
}
```

### Threshold Configuration

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `threshold` | 0.1 | pixelmatch YIQ color distance tolerance (0 = exact, 1 = accept anything). 0.1 is the standard for visual regression testing — permits sub-pixel anti-aliasing differences |
| `includeAA` | false | Ignore anti-aliasing pixels (single-pixel boundary differences) |
| **Pass criteria** | ≤ 0.5% mismatched pixels | Allows up to 50 pixels on a 100×100 image. Catches real regressions while tolerating minor AA/rounding differences across platforms |

The pass threshold (`MAX_MISMATCH_PERCENT`) is configurable per-test via an optional JSON sidecar, e.g. `bg_color_01.config.json`:

```json
{ "maxMismatchPercent": 1.0 }
```

This allows relaxed thresholds for tests where known minor differences exist (e.g., font rendering).

---

## Test Runner: `test_radiant_render.js`

### Execution Flow

```
For each HTML file in test/render/page/:
  1. Check reference PNG exists in test/render/reference/
  2. Render via: ./lambda.exe render <page> -o <output> -vw 100 -vh 100
  3. Compare output PNG vs reference PNG using pixelmatch
  4. If mismatch% > threshold → FAIL (save diff image)
  5. If mismatch% ≤ threshold → PASS
```

### Output Format

```
🎨 Radiant Render Test Suite
==============================
  ✅ PASS  bg_color_01              (0 mismatched pixels, 0.00%)
  ✅ PASS  bg_gradient_linear_01    (12 mismatched pixels, 0.12%)
  ❌ FAIL  box_shadow_01            (342 mismatched pixels, 3.42%)
           → Diff saved: test/render/diff/box_shadow_01.png
  ⚠️ SKIP  transform_3d_01          (no reference image)

Results: 18/20 passed, 1 failed, 1 skipped
```

### Parallel Execution

Like the layout test runner, use worker processes (cores - 1) to run Radiant renders in parallel. Each worker gets a unique output path to prevent file conflicts.

---

## Makefile Integration

```makefile
# Capture browser reference PNGs (requires Chrome + Puppeteer)
capture-render:
    @cd test/render && npm install && node capture_render_references.js $(ARGS)

# Run render regression tests
test-render:
    @cd test/render && npm install && node test_radiant_render.js $(ARGS)

# Shortcut alias
render: test-render
```

**Usage:**
```bash
make capture-render                        # Capture all reference PNGs
make capture-render test=bg_color_01       # Capture single reference
make capture-render force=1                # Force re-capture all

make test-render                           # Run all render tests
make test-render test=bg_color_01          # Run single test
make test-render suite=gradient            # Run tests matching pattern
```

---

## Initial Test Suite

### Phase 1 — Core Visual Features (20 tests)

| Category | Test File | What it tests |
|----------|-----------|---------------|
| **Background** | `bg_color_01.html` | Solid background color |
| | `bg_gradient_linear_01.html` | Linear gradient (2 stops, 180deg) |
| | `bg_gradient_radial_01.html` | Radial gradient (circle, 2 stops) |
| | `bg_image_01.html` | Background image (inline data URI, no-repeat) |
| **Border** | `border_solid_01.html` | Solid borders (uniform width + color) |
| | `border_radius_01.html` | Border-radius (uniform) |
| | `border_styles_01.html` | Double, groove, ridge, inset, outset |
| **Shadow** | `box_shadow_01.html` | Box-shadow with blur |
| | `box_shadow_inset_01.html` | Inset box-shadow |
| | `text_shadow_01.html` | Text-shadow on text |
| **Text** | `text_color_01.html` | Text color + font-size |
| | `text_weight_01.html` | Bold text |
| | `text_align_01.html` | Text-align center/right |
| **Opacity** | `opacity_01.html` | Element opacity (0.5) |
| | `opacity_nested_01.html` | Nested opacity contexts |
| **Transform** | `transform_rotate_01.html` | `rotate(45deg)` on colored box |
| | `transform_scale_01.html` | `scale(0.5)` |
| **Outline** | `outline_01.html` | CSS outline (offset, style) |
| **Multi-column** | `multicol_rule_01.html` | Column-rule between columns |
| **Composite** | `composite_card_01.html` | Realistic card: bg, border, shadow, text, radius |

### Phase 2 — Extended Tests (added incrementally)

- Filter: `blur()`, `drop-shadow()`
- Background: `background-size`, `background-position`, `background-repeat`
- Inline SVG passthrough
- Visibility hidden (should produce blank output)
- HSL colors
- Nested transforms
- Table borders (collapsed, separated)

---

## Platform Considerations

### Font Rendering Differences

Font anti-aliasing varies across macOS (Core Text), Linux (FreeType), and Windows (DirectWrite). This is the primary source of cross-platform pixel differences.

**Mitigations:**
1. Use Puppeteer's `--font-render-hinting=none` to reduce browser-side variation
2. Store per-platform references when needed: `bg_color_01.darwin.png`, `bg_color_01.linux.png`
3. Use relaxed threshold (1–2%) for text-heavy tests
4. Prefer geometric/non-text tests for strict (0.1%) threshold

### Retina / HiDPI

All tests use `deviceScaleFactor: 1` and `--pixel-ratio 1.0` for consistency. HiDPI rendering can be tested separately with a dedicated suite if needed.

---

## Failure Investigation Workflow

When a test fails:

1. **Check the diff image** in `test/render/diff/<test>.png` — red pixels show mismatches
2. **Compare side-by-side**: open `reference/<test>.png` and `output/<test>.png`
3. **Determine cause**:
   - Anti-aliasing issue → relax threshold in `.config.json` sidecar
   - Real regression → fix the rendering bug
   - Browser rendering changed → re-capture reference with `make capture-render test=<name>`

---

## Integration with CI

```yaml
# GitHub Actions example
render-test:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - uses: actions/setup-node@v4
      with: { node-version: '20' }
    - run: make build
    - run: make test-render
    - uses: actions/upload-artifact@v4
      if: failure()
      with:
        name: render-diffs
        path: test/render/diff/
```

On failure, the diff images are uploaded as CI artifacts for visual inspection.

---

## Dependencies

| Dependency   | Purpose                               | Install                                   |
| ------------ | ------------------------------------- | ----------------------------------------- |
| `puppeteer`  | Headless Chrome for reference capture | Already in root `package.json` (v24.36.0) |
| `pixelmatch` | Pixel-level image comparison          | `npm install` in `test/render/`           |
| `pngjs`      | PNG encode/decode for Node.js         | `npm install` in `test/render/`           |

Local `test/render/package.json`:
```json
{
  "name": "radiant-render-tests",
  "private": true,
  "dependencies": {
    "pixelmatch": "^6.0.0",
    "pngjs": "^7.0.0"
  }
}
```

Puppeteer is referenced from the root `node_modules/` (already installed for layout tests).

---

## Summary

| Aspect | Decision |
|--------|----------|
| **Test size** | 100×100 CSS pixels, 1× device pixel ratio |
| **Reference** | Chrome via Puppeteer, committed as PNG |
| **Comparison** | pixelmatch (YIQ perceptual, AA-aware) |
| **Pass threshold** | ≤ 0.5% mismatched pixels (configurable per-test) |
| **Parallelism** | Worker processes (cores − 1) |
| **Platforms** | macOS primary, Linux CI, per-platform refs when needed |
| **Initial suite** | 20 tests covering core CSS visual features |
| **Makefile** | `make capture-render`, `make test-render` |
