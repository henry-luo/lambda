# Radiant UI Automation — Phase 7: Page Mutation Testing

> **Status:** Proposal  
> **Date:** 2026-04-16  
> **Prerequisite:** Phases 5a–6a complete (32 tests, 0 failures)

This document proposes enhancing the UI automation framework to test **page
mutations** — scenarios where the rendered page changes after initial load and
the engine must correctly reflow, repaint, or incrementally update. The three
mutation categories are: **window resize**, **scroll**, and **animation**.

A new `assert_snapshot` assertion compares Radiant's rendered output against
pre-captured browser reference PNGs using pixel-level comparison, providing
automated visual regression verification for every mutation step.

---

## Motivation

Current UI automation tests validate the **static** rendering pipeline:
load HTML → layout → render → assert. But real usage involves continuous
mutations — the user resizes the window, scrolls, or animations play. These
mutations exercise the **incremental reflow**, **selective repaint**, and
**dirty tracking** paths that are distinct from initial layout and have
historically been a source of bugs (blank pages after resize, stale content
after scroll, missing animation frames).

Testing mutations requires the ability to:
1. **Trigger** the mutation (resize, scroll, timer advance)
2. **Wait** for the engine to process it (reflow, repaint)
3. **Assert** the visual result against a browser reference image
4. **Report** pixel-level mismatch percentage as pass/fail

The existing event types (`resize`, `scroll`) trigger mutations but lack
the post-mutation reflow/render step in headless mode. Animation testing
requires a new `advance_time` event to control the animation scheduler
deterministically. And there is no in-engine snapshot comparison — the
`render` event writes a PNG but never compares it.

---

## Current State Analysis

### What Exists

| Event Type | Triggers Mutation | Triggers Reflow | Triggers Render | Headless |
|------------|------------------|-----------------|-----------------|----------|
| `resize`   | ✅ Updates viewport + creates surface | ✅ `reflow_html_doc()` | ❌ No render call | Partial |
| `scroll`   | ✅ Dispatches `RDT_EVENT_SCROLL` | ❌ | ❌ | ❌ No repaint |
| `wait`     | ❌ Just advances time | ❌ | ❌ | ✅ |

### What's Missing

1. **Resize** — The `SIM_EVENT_RESIZE` handler calls `reflow_html_doc()` but
   does NOT call `render_html_doc()`. In headless mode, the render loop
   (`window_refresh_callback`) never runs. Assertions after resize see stale
   pixel data, and `render` captures the pre-resize surface. The `is_dirty`
   flag is not set, so even if render were called it might take the selective
   path.

2. **Scroll** — The `SIM_EVENT_SCROLL` handler dispatches a scroll event via
   `sim_scroll()` which updates `scroll_y` in the state, but no repaint
   follows. In headless mode, scroll offsets are applied but the surface
   pixels are not updated. `assert_scroll` works (checks state), but
   `assert_rect` / `assert_element_at` / `render` reflect pre-scroll layout.

3. **Animation** — There is no event to advance the animation scheduler's
   clock. In headless mode, the main loop does call `event_sim_update()` with
   an advancing `current_time`, but `animation_scheduler_tick()` is only
   called in the GUI main loop — never in headless mode. There is no way to
   assert intermediate animation state (e.g., "at t=1s, opacity should be
   0.5").

---

## `assert_snapshot` — Visual Regression Assertion

### Overview

A new `assert_snapshot` event type that:
1. Renders the current Radiant surface to pixels (in-memory)
2. Loads a pre-captured browser reference PNG from disk
3. Compares pixel-by-pixel using YIQ color distance (same as pixelmatch)
4. Passes/fails based on mismatch percentage threshold

This runs **in-process** inside `event_sim.cpp`, requiring no external tooling
(no Node.js, no npm). It reuses the existing `image_load()` from `lib/image.h`
to read reference PNGs and `save_surface_to_png()` for diff output.

### JSON Format

```json
{"type": "assert_snapshot", "reference": "test/ui/snapshots/resize_narrow.png"}
{"type": "assert_snapshot", "reference": "test/ui/snapshots/scroll_bottom.png",
 "threshold": 2.0}
{"type": "assert_snapshot", "reference": "test/ui/snapshots/anim_t1000.png",
 "threshold": 5.0, "save_diff": "./temp/anim_t1000_diff.png"}
```

**Fields:**
- `reference` — path to browser reference PNG (relative to project root)
- `threshold` — max allowed mismatch percentage (default: `1.0`%)
- `save_diff` — optional path to write visual diff PNG on mismatch
- `save_actual` — optional path to save Radiant's rendered PNG for debugging

### Implementation

#### SimEvent Fields (event_sim.hpp)

```cpp
// assert_snapshot fields
char* snapshot_reference;   // path to reference PNG
float snapshot_threshold;   // max mismatch %, default 1.0
char* snapshot_diff_path;   // optional: save diff image on failure
char* snapshot_actual_path; // optional: save actual image for debugging
```

Add to enum: `SIM_EVENT_ASSERT_SNAPSHOT`

#### Pixel Comparison (event_sim.cpp)

Implement a lightweight pixel comparison directly in C — no npm dependency.
Uses the same **YIQ color distance** metric as pixelmatch for perceptual
accuracy:

```cpp
// YIQ-based perceptual color distance (same algorithm as pixelmatch)
// Returns squared weighted delta; compare against threshold² directly
static float pixel_yiq_distance(uint32_t rgba1, uint32_t rgba2) {
    float r1 = (rgba1 & 0xFF) / 255.0f;
    float g1 = ((rgba1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((rgba1 >> 16) & 0xFF) / 255.0f;
    float r2 = (rgba2 & 0xFF) / 255.0f;
    float g2 = ((rgba2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((rgba2 >> 16) & 0xFF) / 255.0f;

    float y  = r1*0.29889531f + g1*0.58662247f + b1*0.11448223f;
    float i  = r1*0.59597799f - g1*0.27417610f - b1*0.32180189f;
    float q  = r1*0.21147017f - g1*0.52261711f + b1*0.31114694f;
    float y2 = r2*0.29889531f + g2*0.58662247f + b2*0.11448223f;
    float i2 = r2*0.59597799f - g2*0.27417610f - b2*0.32180189f;
    float q2 = r2*0.21147017f - g2*0.52261711f + b2*0.31114694f;

    float dy = y - y2, di = i - i2, dq = q - q2;
    return 0.5053f*dy*dy + 0.299f*di*di + 0.1957f*dq*dq;
}

static void assert_snapshot_impl(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    const char* ref_path = ev->snapshot_reference;
    float threshold_pct = ev->snapshot_threshold > 0 ? ev->snapshot_threshold : 1.0f;

    // Step 1: Ensure surface is rendered (force full render)
    if (uicon->document && uicon->document->view_tree) {
        RadiantState* state = (RadiantState*)uicon->document->state;
        if (state) state->is_dirty = true;
        render_html_doc(uicon, uicon->document->view_tree, nullptr);
        if (state) {
            state->is_dirty = false;
            state->needs_repaint = false;
            dirty_clear(&state->dirty_tracker);
        }
    }

    ImageSurface* actual = uicon->surface;
    if (!actual || !actual->pixels) {
        sim_fail(ctx, "assert_snapshot: no rendered surface");
        return;
    }

    // Step 2: Optionally save actual PNG for debugging
    if (ev->snapshot_actual_path) {
        save_surface_to_png(actual, ev->snapshot_actual_path);
    }

    // Step 3: Load reference PNG via lib/image.h
    int ref_w, ref_h, ref_channels;
    unsigned char* ref_pixels = image_load(ref_path, &ref_w, &ref_h,
                                            &ref_channels, 4);
    if (!ref_pixels) {
        sim_fail(ctx, "assert_snapshot: failed to load reference: %s", ref_path);
        return;
    }

    // Step 4: Dimension check
    if (ref_w != actual->width || ref_h != actual->height) {
        sim_fail(ctx, "assert_snapshot: size mismatch: actual %dx%d vs ref %dx%d",
                 actual->width, actual->height, ref_w, ref_h);
        image_free(ref_pixels);
        return;
    }

    // Step 5: Pixel comparison
    int total = ref_w * ref_h;
    int mismatched = 0;
    const float YIQ_THRESHOLD_SQ = 0.1f * 0.1f;  // matches pixelmatch threshold=0.1

    uint32_t* actual_px = (uint32_t*)actual->pixels;
    uint32_t* ref_px = (uint32_t*)ref_pixels;
    int actual_stride = actual->pitch / 4;

    // Allocate diff buffer only when save_diff is requested
    uint32_t* diff_px = nullptr;
    if (ev->snapshot_diff_path) {
        diff_px = (uint32_t*)mem_calloc(total, sizeof(uint32_t), MEM_CAT_RENDER);
    }

    for (int y = 0; y < ref_h; y++) {
        for (int x = 0; x < ref_w; x++) {
            uint32_t a = actual_px[y * actual_stride + x];
            uint32_t r = ref_px[y * ref_w + x];
            if (a != r) {
                float dist = pixel_yiq_distance(a, r);
                if (dist > YIQ_THRESHOLD_SQ) {
                    mismatched++;
                    if (diff_px) diff_px[y * ref_w + x] = 0xFF0000FF; // red
                } else {
                    if (diff_px) diff_px[y * ref_w + x] = a;
                }
            } else {
                if (diff_px) diff_px[y * ref_w + x] = a;
            }
        }
    }

    float mismatch_pct = (float)mismatched / (float)total * 100.0f;

    // Step 6: Save diff image on mismatch
    if (diff_px && ev->snapshot_diff_path && mismatched > 0) {
        ImageSurface diff_surf = {};
        diff_surf.width = ref_w;
        diff_surf.height = ref_h;
        diff_surf.pitch = ref_w * 4;
        diff_surf.pixels = diff_px;
        save_surface_to_png(&diff_surf, ev->snapshot_diff_path);
    }
    if (diff_px) mem_free(diff_px);
    image_free(ref_pixels);

    // Step 7: Pass/fail
    if (mismatch_pct <= threshold_pct) {
        sim_pass(ctx, "assert_snapshot: %.2f%% mismatch (threshold %.1f%%) ref=%s",
                 mismatch_pct, threshold_pct, ref_path);
    } else {
        sim_fail(ctx, "assert_snapshot: %.2f%% mismatch > threshold %.1f%% ref=%s",
                 mismatch_pct, threshold_pct, ref_path);
    }
}
```

### Reference Image Directory

```
test/ui/snapshots/
  phase7a_initial.png         # resize: 1200×800 initial layout
  phase7a_narrow.png          # resize: 500×600 narrow layout
  phase7a_restored.png        # resize: back to 1200×800
  phase7b_top.png             # scroll: at scroll_y=0
  phase7b_scrolled.png        # scroll: after scroll down ~600px
  phase7b_section_c.png       # scroll: Section C in view
  phase7b_back_top.png        # scroll: back to top
  phase7c_t0.png              # animation: t=0
  phase7c_t1000.png           # animation: t=1s
  phase7c_t1500.png           # animation: t=1.5s
  phase7c_t2000.png           # animation: t=2s
  phase7c_t3000.png           # animation: t=3s
```

---

## Browser Reference Capture Script

A Puppeteer script captures all reference PNGs. Each mutation type uses a
browser-native mechanism to reproduce the exact state:

- **Resize:** `page.setViewport()` → `page.screenshot()`
- **Scroll:** `window.scrollBy()` / `element.scrollIntoView()` → `page.screenshot()`
- **Animation:** `document.getAnimations().forEach(a => { a.pause(); a.currentTime = N; })` → `page.screenshot()`

### `test/ui/capture_mutation_snapshots.js`

```js
#!/usr/bin/env node
/**
 * Capture browser reference PNGs for Phase 7 mutation tests.
 *
 * Usage:
 *   node capture_mutation_snapshots.js              # Capture all
 *   node capture_mutation_snapshots.js --force      # Re-capture existing
 *   node capture_mutation_snapshots.js --test 7a    # Capture one phase
 */
const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

const SNAP_DIR = path.join(__dirname, 'snapshots');
const DEFAULT_VP = { width: 1200, height: 800, deviceScaleFactor: 1 };

async function captureResize(browser) {
    const page = await browser.newPage();
    const url = fileUrl('test_phase7a_resize_mutation.html');
    await page.setViewport(DEFAULT_VP);
    await page.goto(url, { waitUntil: 'networkidle0' });
    await delay(200);

    // 1200×800 initial
    await page.screenshot({ path: snap('phase7a_initial.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Resize to 500×600
    await page.setViewport({ width: 500, height: 600, deviceScaleFactor: 1 });
    await delay(200);
    await page.screenshot({ path: snap('phase7a_narrow.png'),
        clip: { x: 0, y: 0, width: 500, height: 600 } });

    // Restore to 1200×800
    await page.setViewport(DEFAULT_VP);
    await delay(200);
    await page.screenshot({ path: snap('phase7a_restored.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    await page.close();
    console.log('  ✅ Phase 7a: 3 resize snapshots');
}

async function captureScroll(browser) {
    const page = await browser.newPage();
    const url = fileUrl('test_phase7b_scroll_mutation.html');
    await page.setViewport(DEFAULT_VP);
    await page.goto(url, { waitUntil: 'networkidle0' });
    await delay(200);

    // At top
    await page.screenshot({ path: snap('phase7b_top.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Scroll down ~600px (matching "scroll dy=-20" which scrolls ~600px)
    await page.evaluate(() => window.scrollBy(0, 600));
    await delay(200);
    await page.screenshot({ path: snap('phase7b_scrolled.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Scroll to Section C
    await page.evaluate(() =>
        document.querySelector('#sec-c').scrollIntoView({ behavior: 'instant' }));
    await delay(200);
    await page.screenshot({ path: snap('phase7b_section_c.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Back to top
    await page.evaluate(() => window.scrollTo(0, 0));
    await delay(200);
    await page.screenshot({ path: snap('phase7b_back_top.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    await page.close();
    console.log('  ✅ Phase 7b: 4 scroll snapshots');
}

async function captureAnimation(browser) {
    const page = await browser.newPage();
    const url = fileUrl('test_phase7c_animation_mutation.html');
    await page.setViewport(DEFAULT_VP);
    await page.goto(url, { waitUntil: 'networkidle0' });
    await delay(200);

    // Pause all CSS animations at t=0 via Web Animations API
    await page.evaluate(() =>
        document.getAnimations().forEach(a => a.pause()));
    await delay(100);
    await page.screenshot({ path: snap('phase7c_t0.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Seek to t=1000ms
    await page.evaluate(() =>
        document.getAnimations().forEach(a => { a.currentTime = 1000; }));
    await delay(100);
    await page.screenshot({ path: snap('phase7c_t1000.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Seek to t=1500ms
    await page.evaluate(() =>
        document.getAnimations().forEach(a => { a.currentTime = 1500; }));
    await delay(100);
    await page.screenshot({ path: snap('phase7c_t1500.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Seek to t=2000ms
    await page.evaluate(() =>
        document.getAnimations().forEach(a => { a.currentTime = 2000; }));
    await delay(100);
    await page.screenshot({ path: snap('phase7c_t2000.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    // Seek to t=3000ms
    await page.evaluate(() =>
        document.getAnimations().forEach(a => { a.currentTime = 3000; }));
    await delay(100);
    await page.screenshot({ path: snap('phase7c_t3000.png'),
        clip: { x: 0, y: 0, width: 1200, height: 800 } });

    await page.close();
    console.log('  ✅ Phase 7c: 5 animation snapshots');
}

function snap(name) { return path.join(SNAP_DIR, name); }
function fileUrl(name) { return `file://${path.resolve(__dirname, name)}`; }
function delay(ms) { return new Promise(r => setTimeout(r, ms)); }

async function main() {
    if (!fs.existsSync(SNAP_DIR)) fs.mkdirSync(SNAP_DIR, { recursive: true });
    const browser = await puppeteer.launch({
        headless: true,
        args: ['--no-sandbox', '--disable-gpu', '--font-render-hinting=none',
               '--disable-lcd-text', '--disable-font-subpixel-positioning']
    });
    console.log('🖼️  Capturing mutation test snapshots...\n');
    await captureResize(browser);
    await captureScroll(browser);
    await captureAnimation(browser);
    await browser.close();
    console.log('\n✅ Done. References in test/ui/snapshots/');
}

main().catch(e => { console.error(e); process.exit(1); });
```

**Key technique for animation snapshots:** The Web Animations API
(`document.getAnimations()`) provides `pause()` and `currentTime` setter.
This allows deterministic seeking to any animation time point without
wall-clock timing uncertainty. Both Radiant's `advance_time` and the
browser's `a.currentTime = N` reach the same logical time, making the
comparison meaningful.

### Makefile Target

```makefile
capture-mutation-snapshots:
	cd test/ui && node capture_mutation_snapshots.js $(if $(force),--force,)
```

---

## Design

### 7a — Resize Mutation Testing

**Goal:** After a `resize` event, the automation framework calls
`render_html_doc()` so that assertions and `render` snapshots reflect the
post-resize state.

#### Implementation

Modify `SIM_EVENT_RESIZE` handler in `event_sim.cpp`:

```cpp
case SIM_EVENT_RESIZE: {
    int new_css_w = ev->x;
    int new_css_h = ev->y;
    float pr = uicon->pixel_ratio > 0 ? uicon->pixel_ratio : 1.0f;
    int new_phys_w = (int)(new_css_w * pr);
    int new_phys_h = (int)(new_css_h * pr);
    log_info("event_sim: resize to %dx%d CSS (%dx%d physical)",
             new_css_w, new_css_h, new_phys_w, new_phys_h);

    uicon->viewport_width = new_css_w;
    uicon->viewport_height = new_css_h;
    uicon->window_width = new_phys_w;
    uicon->window_height = new_phys_h;

    // Recreate surface at new dimensions
    extern void ui_context_create_surface(UiContext* uicon, int w, int h);
    ui_context_create_surface(uicon, new_phys_w, new_phys_h);

    // Full reflow at new viewport
    extern void reflow_html_doc(DomDocument* doc);
    if (uicon->document) {
        reflow_html_doc(uicon->document);
    }

    // >>> NEW: render to update surface pixels <<<
    extern void render_html_doc(UiContext*, ViewTree*, const char*);
    if (uicon->document && uicon->document->view_tree) {
        // Mark dirty so render_html_doc takes full-clear path
        RadiantState* state = (RadiantState*)uicon->document->state;
        if (state) state->is_dirty = true;
        render_html_doc(uicon, uicon->document->view_tree, nullptr);
        if (state) {
            state->is_dirty = false;
            state->needs_repaint = false;
            dirty_clear(&state->dirty_tracker);
        }
    }
    break;
}
```

#### New Assertions

No new assertion types needed. Existing assertions work once render is called:

- `assert_rect` — verify element repositioned after resize (e.g., flex wrap)
- `assert_style` — verify `width`/`height` recomputed
- `render` — capture post-resize snapshot for visual comparison

#### Test Plan

**File:** `test/ui/test_phase7a_resize_mutation.html`
```html
<style>
  .container { display: flex; flex-wrap: wrap; gap: 10px; }
  .box { width: 200px; height: 100px; background: #4a90d9; }
  #info { font-size: 14px; }
</style>
<div id="info">Viewport test</div>
<div class="container">
  <div class="box" id="box1">Box 1</div>
  <div class="box" id="box2">Box 2</div>
  <div class="box" id="box3">Box 3</div>
  <div class="box" id="box4">Box 4</div>
</div>
```

**File:** `test/ui/ui_phase7a_resize_mutation.json`
```json
{
  "name": "Phase 7a - Resize mutation with visual verification",
  "html": "test/ui/test_phase7a_resize_mutation.html",
  "events": [
    {"type": "log", "message": "=== Phase 7a: Resize Mutation ==="},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 1: Initial layout at 1200x800"},
    {"type": "assert_rect", "target": {"selector": "#box1"},
     "width": 200, "height": 100, "tolerance": 2},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7a_initial.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7a_initial_actual.png",
     "save_diff": "./temp/phase7a_initial_diff.png"},

    {"type": "log", "message": "Step 2: Resize to 500x600 — boxes should wrap"},
    {"type": "resize", "width": 500, "height": 600},
    {"type": "wait", "ms": 100},

    {"type": "log", "message": "Step 3: Verify box positions changed (flex wrap)"},
    {"type": "assert_rect", "target": {"selector": "#box1"},
     "width": 200, "height": 100, "tolerance": 2},
    {"type": "assert_position", "element_a": {"selector": "#box1"},
     "element_b": {"selector": "#box3"}, "relation": "above"},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7a_narrow.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7a_narrow_actual.png",
     "save_diff": "./temp/phase7a_narrow_diff.png"},

    {"type": "log", "message": "Step 4: Resize back to 1200x800"},
    {"type": "resize", "width": 1200, "height": 800},
    {"type": "wait", "ms": 100},

    {"type": "log", "message": "Step 5: Verify restored layout matches initial"},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7a_restored.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7a_restored_actual.png",
     "save_diff": "./temp/phase7a_restored_diff.png"},

    {"type": "log", "message": "=== Phase 7a complete ==="}
  ]
}
```

**Verification:** 3 `assert_snapshot` checks compare Radiant output against
browser references after each resize step. `phase7a_initial` and
`phase7a_restored` references should be identical (same viewport). On failure,
diff PNGs in `./temp/` highlight mismatched pixels in red.

---

### 7b — Scroll Mutation Testing

**Goal:** After a `scroll` event, the automation framework re-renders so that
`render` snapshots and coordinate-based assertions reflect the scrolled
viewport.

#### Implementation

**Option A — Render after scroll** (minimal change):

Add render call after scroll dispatch in `event_sim.cpp`:

```cpp
case SIM_EVENT_SCROLL:
    log_info("event_sim: scroll at (%d,%d) offset=(%.2f,%.2f)",
             ev->x, ev->y, ev->scroll_dx, ev->scroll_dy);
    sim_scroll(uicon, ev->x, ev->y, ev->scroll_dx, ev->scroll_dy);

    // >>> NEW: re-render after scroll to update surface pixels <<<
    extern void render_html_doc(UiContext*, ViewTree*, const char*);
    if (uicon->document && uicon->document->view_tree) {
        RadiantState* state = (RadiantState*)uicon->document->state;
        if (state) state->is_dirty = true;  // full repaint (scroll moves everything)
        render_html_doc(uicon, uicon->document->view_tree, nullptr);
        if (state) {
            state->is_dirty = false;
            state->needs_repaint = false;
            dirty_clear(&state->dirty_tracker);
        }
    }
    break;
```

**Option B — New `scroll_to` event** (higher-level):

Add a new event type that scrolls to an absolute position or brings a target
element into view, then re-renders:

```json
{"type": "scroll_to", "y": 500}
{"type": "scroll_to", "target": {"selector": "#section-b"}}
```

This is more ergonomic for tests than computing pixel-level scroll deltas.
Implementation sets `state->scroll_y` directly (or computes from element
bounds), then calls `render_html_doc()`.

**Recommendation:** Implement both. Option A fixes the existing `scroll`
event. Option B adds a higher-level `scroll_to` for convenience.

#### New Event: `scroll_to`

```
SimEventType: SIM_EVENT_SCROLL_TO

Fields:
  - target_selector / target_text  (optional: scroll to bring element into view)
  - y                              (optional: absolute scroll position in CSS px)
  - x                              (optional: horizontal scroll position)
  - behavior                       (optional: "instant" or "smooth", default "instant")
```

**Parsing** (in `event_sim.cpp` JSON parser):
```cpp
} else if (str_eq_const(type_str, type_len, "scroll_to")) {
    ev->type = SIM_EVENT_SCROLL_TO;
    // Parse target selector or absolute position
}
```

**Dispatch:**
```cpp
case SIM_EVENT_SCROLL_TO: {
    RadiantState* state = uicon->document ? uicon->document->state : nullptr;
    if (!state) break;

    if (ev->target_selector) {
        // Find element, compute its position, set scroll_y to bring it into view
        View* target = find_element_by_selector(uicon->document,
                                                 ev->target_selector, 0);
        if (target) {
            float elem_y = view_get_absolute_y(target);
            float viewport_h = (float)uicon->viewport_height;
            // Scroll so element is 50px from top of viewport
            state->scroll_y = fmaxf(0, elem_y - 50.0f);
        }
    } else {
        state->scroll_y = ev->expected_scroll_y;
        state->scroll_x = ev->expected_scroll_x;
    }

    // Re-render at new scroll position
    state->is_dirty = true;
    render_html_doc(uicon, uicon->document->view_tree, nullptr);
    state->is_dirty = false;
    state->needs_repaint = false;
    dirty_clear(&state->dirty_tracker);
    break;
}
```

#### Test Plan

**File:** `test/ui/test_phase7b_scroll_mutation.html`
```html
<style>
  body { margin: 0; }
  .section { height: 600px; padding: 20px; }
  #sec-a { background: #e8f4fd; }
  #sec-b { background: #fde8e8; }
  #sec-c { background: #e8fde8; }
  .marker { font-size: 24px; font-weight: bold; }
</style>
<div id="sec-a" class="section">
  <p class="marker">Section A — Top</p>
</div>
<div id="sec-b" class="section">
  <p class="marker">Section B — Middle</p>
</div>
<div id="sec-c" class="section">
  <p class="marker">Section C — Bottom</p>
</div>
```

**File:** `test/ui/ui_phase7b_scroll_mutation.json`
```json
{
  "name": "Phase 7b - Scroll mutation with visual verification",
  "html": "test/ui/test_phase7b_scroll_mutation.html",
  "events": [
    {"type": "log", "message": "=== Phase 7b: Scroll Mutation ==="},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 1: Verify at top, snapshot initial viewport"},
    {"type": "assert_scroll", "y": 0, "tolerance": 5},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7b_top.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7b_top_actual.png",
     "save_diff": "./temp/phase7b_top_diff.png"},

    {"type": "log", "message": "Step 2: Scroll down via mouse wheel"},
    {"type": "scroll", "x": 400, "y": 300, "dx": 0, "dy": -20},
    {"type": "wait", "ms": 100},

    {"type": "log", "message": "Step 3: Verify scroll moved, snapshot scrolled state"},
    {"type": "assert_scroll", "y": 0, "tolerance": 5, "negate": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7b_scrolled.png",
     "threshold": 2.0,
     "save_actual": "./temp/phase7b_scrolled_actual.png",
     "save_diff": "./temp/phase7b_scrolled_diff.png"},

    {"type": "log", "message": "Step 4: scroll_to Section C"},
    {"type": "scroll_to", "target": {"selector": "#sec-c"}},
    {"type": "wait", "ms": 100},

    {"type": "log", "message": "Step 5: Verify Section C in view"},
    {"type": "assert_visible", "target": {"selector": "#sec-c"}, "visible": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7b_section_c.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7b_section_c_actual.png",
     "save_diff": "./temp/phase7b_section_c_diff.png"},

    {"type": "log", "message": "Step 6: scroll_to absolute position 0 (back to top)"},
    {"type": "scroll_to", "y": 0},
    {"type": "wait", "ms": 100},

    {"type": "log", "message": "Step 7: Verify back at top, snapshot matches initial"},
    {"type": "assert_scroll", "y": 0, "tolerance": 5},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7b_back_top.png",
     "threshold": 1.0,
     "save_actual": "./temp/phase7b_back_top_actual.png",
     "save_diff": "./temp/phase7b_back_top_diff.png"},

    {"type": "log", "message": "=== Phase 7b complete ==="}
  ]
}
```

**Verification:** 4 `assert_snapshot` checks at each scroll position. The
`phase7b_top` reference shows blue Section A; `phase7b_section_c` shows green
Section C. `phase7b_scrolled` has `threshold: 2.0` because the exact scroll
offset from `dy=-20` may differ slightly between Radiant's scroll
acceleration and `window.scrollBy(0, 600)`.

---

### 7c — Animation Mutation Testing

**Goal:** Provide deterministic control over animation timing so tests can
assert intermediate animation states at specific time points.

#### Problem

The animation scheduler uses wall-clock time (`glfwGetTime()` in GUI mode,
`get_monotonic_time()` for internal timers). In headless mode:
- `event_sim_update()` receives advancing `current_time` but
  `animation_scheduler_tick()` is never called.
- Even if tick were called, the time advances by event intervals (50ms
  default), which is too coarse and non-deterministic for testing "at t=1.5s
  the opacity should be 0.5".

> **Implementation Note — Deterministic Animation Snapshots**
>
> Both Radiant and the browser must be seeked to the **exact same logical
> time** for `assert_snapshot` comparison to be meaningful.
>
> **Browser side:** The [Web Animations API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Animations_API)
> exposes every active CSS animation/transition as an `Animation` object via
> `document.getAnimations()`. Each object supports:
> - `animation.pause()` — freezes playback, preventing wall-clock drift
> - `animation.currentTime = N` — seeks to exactly N milliseconds
>
> This is fully deterministic — no `requestAnimationFrame` timing races, no
> `setTimeout` approximations. The Puppeteer capture script pauses all
> animations immediately after page load, then seeks to each target time:
> ```js
> await page.evaluate((t) => {
>     document.getAnimations().forEach(a => {
>         a.pause();
>         a.currentTime = t;
>     });
> }, 1000); // seek to t=1000ms
> await page.screenshot({ path: 'snapshot_t1000.png' });
> ```
>
> **Radiant side:** The `advance_time` event below controls
> `animation_scheduler_tick()` with a computed time value
> (`base_time + step_duration * i`), reaching the same logical time.
> Both sides use `linear` easing in test animations to eliminate
> interpolation differences between browser and Radiant easing solvers.

#### New Event: `advance_time`

A new event type that advances the animation scheduler's clock by a specified
duration and ticks all active animations:

```json
{"type": "advance_time", "ms": 1000}
{"type": "advance_time", "ms": 500, "steps": 30}
```

**Fields:**
- `ms` — time to advance in milliseconds
- `steps` — number of tick steps to simulate (default: `ms / 16`, i.e., 60fps
  granularity). More steps = smoother interpolation. For testing, even 1 step
  is sufficient to reach the target time.

**SimEventType:** `SIM_EVENT_ADVANCE_TIME`

**Parsing:**
```cpp
} else if (str_eq_const(type_str, type_len, "advance_time")) {
    ev->type = SIM_EVENT_ADVANCE_TIME;
    ev->wait_ms = json_int(item, "ms", 0);
    ev->advance_steps = json_int(item, "steps", 0);  // 0 = auto
}
```

**Dispatch:**
```cpp
case SIM_EVENT_ADVANCE_TIME: {
    RadiantState* state = uicon->document ? uicon->document->state : nullptr;
    if (!state || !state->animation_scheduler) {
        log_warn("event_sim: advance_time but no animation scheduler");
        break;
    }

    int total_ms = ev->wait_ms;
    int steps = ev->advance_steps > 0 ? ev->advance_steps : (total_ms / 16 + 1);
    double step_duration = (double)total_ms / 1000.0 / steps;
    double base_time = state->animation_scheduler->current_time;

    log_info("event_sim: advance_time %dms in %d steps (%.3fs step)",
             total_ms, steps, step_duration);

    for (int i = 1; i <= steps; i++) {
        double t = base_time + step_duration * i;
        animation_scheduler_tick(state->animation_scheduler, t,
                                 &state->dirty_tracker);
    }

    // Re-render to reflect animation state
    state->is_dirty = true;
    render_html_doc(uicon, uicon->document->view_tree, nullptr);
    state->is_dirty = false;
    state->needs_repaint = false;
    dirty_clear(&state->dirty_tracker);
    break;
}
```

#### New Assertion: `assert_computed_style`

To verify animated CSS property values at a specific point in time, enhance
`assert_style` to handle animated properties. Current `assert_style` reads
from the CSS cascade (static). Animated values live on the `ViewSpan` in
`in_line->opacity`, `bound->background->bg_color`, etc.

Add an `animated` flag to `assert_style`:

```json
{"type": "assert_style", "target": {"selector": "#box"},
 "property": "opacity", "equals": "0.5", "tolerance": 0.1, "animated": true}
```

When `animated: true`, the assertion reads from the ViewSpan's live
properties instead of the CSS cascade:

| Property | Source |
|----------|--------|
| `opacity` | `span->in_line->opacity` |
| `background-color` | `span->bound->background->bg_color` (RGBA) |
| `transform` | `span->bound->transform` matrix |

For non-animated assertions, the existing cascade-based path remains.

#### Test Plan

**File:** `test/ui/test_phase7c_animation_mutation.html`
```html
<style>
  @keyframes fadeInOut {
    0%   { opacity: 0; }
    50%  { opacity: 1; }
    100% { opacity: 0; }
  }
  @keyframes colorCycle {
    0%   { background-color: #ff0000; }
    50%  { background-color: #00ff00; }
    100% { background-color: #0000ff; }
  }
  #fade-box {
    width: 200px; height: 100px;
    background: #4a90d9;
    animation: fadeInOut 2s linear infinite;
  }
  #color-box {
    width: 200px; height: 100px;
    animation: colorCycle 3s linear forwards;
  }
</style>
<div id="fade-box">Fade</div>
<div id="color-box">Color</div>
```

**File:** `test/ui/ui_phase7c_animation_mutation.json`
```json
{
  "name": "Phase 7c - Animation mutation with deterministic timing",
  "html": "test/ui/test_phase7c_animation_mutation.html",
  "events": [
    {"type": "log", "message": "=== Phase 7c: Animation Mutation ==="},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 1: Snapshot initial state (t=0)"},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7c_t0.png",
     "threshold": 2.0,
     "save_actual": "./temp/phase7c_t0_actual.png",
     "save_diff": "./temp/phase7c_t0_diff.png"},

    {"type": "log", "message": "Step 2: Advance 1s — fadeInOut at 50% (opacity=1.0)"},
    {"type": "advance_time", "ms": 1000, "steps": 60},
    {"type": "assert_style", "target": {"selector": "#fade-box"},
     "property": "opacity", "equals": "1.0", "tolerance": 0.1, "animated": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7c_t1000.png",
     "threshold": 5.0,
     "save_actual": "./temp/phase7c_t1000_actual.png",
     "save_diff": "./temp/phase7c_t1000_diff.png"},

    {"type": "log", "message": "Step 3: Advance to 1.5s — colorCycle at 50% (green)"},
    {"type": "advance_time", "ms": 500, "steps": 30},
    {"type": "assert_style", "target": {"selector": "#color-box"},
     "property": "background-color", "equals": "#00ff00",
     "tolerance": 30, "animated": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7c_t1500.png",
     "threshold": 5.0,
     "save_actual": "./temp/phase7c_t1500_actual.png",
     "save_diff": "./temp/phase7c_t1500_diff.png"},

    {"type": "log", "message": "Step 4: Advance to 2s — fadeInOut back to opacity=0"},
    {"type": "advance_time", "ms": 500, "steps": 30},
    {"type": "assert_style", "target": {"selector": "#fade-box"},
     "property": "opacity", "equals": "0.0", "tolerance": 0.1, "animated": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7c_t2000.png",
     "threshold": 5.0,
     "save_actual": "./temp/phase7c_t2000_actual.png",
     "save_diff": "./temp/phase7c_t2000_diff.png"},

    {"type": "log", "message": "Step 5: Advance to 3s — colorCycle at end (blue)"},
    {"type": "advance_time", "ms": 1000, "steps": 60},
    {"type": "assert_style", "target": {"selector": "#color-box"},
     "property": "background-color", "equals": "#0000ff",
     "tolerance": 30, "animated": true},
    {"type": "assert_snapshot",
     "reference": "test/ui/snapshots/phase7c_t3000.png",
     "threshold": 5.0,
     "save_actual": "./temp/phase7c_t3000_actual.png",
     "save_diff": "./temp/phase7c_t3000_diff.png"},

    {"type": "log", "message": "=== Phase 7c complete ==="}
  ]
}
```

**Verification:** 5 `assert_snapshot` checks at t=0, 1s, 1.5s, 2s, 3s compare
against browser references captured via `document.getAnimations().currentTime`.
Animation snapshots use `threshold: 5.0` (relaxed) because:
- Radiant uses cubic-bezier Newton-Raphson solver vs browser's native easing
- Sub-pixel alpha blending may differ slightly
- Font rendering affects surrounding text pixels

The `assert_style` checks with `animated: true` provide precise numeric
verification of interpolated values (opacity, background-color) independent
of pixel comparison.

---

## Implementation Summary

### New Event Types

| Event | Fields | Purpose |
|-------|--------|---------|
| `assert_snapshot` | `reference`, `threshold`, `save_diff`, `save_actual` | Pixel-compare rendered surface against browser reference PNG |
| `scroll_to` | `target`, `y`, `x` | Scroll to absolute position or element |
| `advance_time` | `ms`, `steps` | Advance animation scheduler clock deterministically |

### Modified Event Types

| Event | Change |
|-------|--------|
| `resize` | Add `render_html_doc()` call after reflow |
| `scroll` | Add `render_html_doc()` call after dispatch |

### Modified Assertions

| Assertion | Change |
|-----------|--------|
| `assert_style` | Add `animated` flag to read live ViewSpan properties |

### Files to Modify

| File | Changes |
|------|---------|
| `radiant/event_sim.hpp` | Add `SIM_EVENT_ASSERT_SNAPSHOT`, `SIM_EVENT_SCROLL_TO`, `SIM_EVENT_ADVANCE_TIME` to enum; add `snapshot_*`, `advance_steps` fields to `SimEvent` |
| `radiant/event_sim.cpp` | `assert_snapshot` with YIQ pixel comparison; JSON parsing for new events; dispatch handlers with render calls; modify resize/scroll handlers; animated `assert_style` path |
| `test/ui/capture_mutation_snapshots.js` | New Puppeteer script to capture browser reference PNGs (resize, scroll, animation) |
| `test/ui/snapshots/*.png` | 12 browser reference PNGs (3 resize + 4 scroll + 5 animation) |
| `test/ui/test_phase7a_resize_mutation.html` | New test HTML |
| `test/ui/ui_phase7a_resize_mutation.json` | New test JSON (3 `assert_snapshot`) |
| `test/ui/test_phase7b_scroll_mutation.html` | New test HTML |
| `test/ui/ui_phase7b_scroll_mutation.json` | New test JSON (4 `assert_snapshot`) |
| `test/ui/test_phase7c_animation_mutation.html` | New test HTML |
| `test/ui/ui_phase7c_animation_mutation.json` | New test JSON (5 `assert_snapshot`) |

### Phasing

| Phase | Scope | Snapshot Assertions | Structural Assertions |
|-------|-------|--------------------|-----------------------|
| 7a | Resize: render after reflow + snapshot comparison | 3 `assert_snapshot` | 2 (`assert_rect`, `assert_position`) |
| 7b | Scroll: render after scroll + `scroll_to` + snapshots | 4 `assert_snapshot` | 4 (`assert_scroll` × 3, `assert_visible`) |
| 7c | Animation: `advance_time` + animated `assert_style` + snapshots | 5 `assert_snapshot` | 4 `assert_style` (animated) |
| **Total** | | **12 snapshot checks** | **10 structural checks** |

### Threshold Strategy

| Category | Default Threshold | Rationale |
|----------|------------------|-----------|
| Resize | 1.0% | Layout is deterministic; only font hinting differs |
| Scroll | 1.0–2.0% | Scroll offset may differ by ±1px; relaxed for `dy=-20` |
| Animation | 5.0% | Easing interpolation, alpha blending, and timing differences compound |

### Test Execution

All test files are auto-discovered by `test_ui_automation_gtest.cpp`. Run:

```bash
# Capture browser references (one-time, or after HTML changes)
cd test/ui && node capture_mutation_snapshots.js

# Build and run mutation tests
make build-test
./test/test_ui_automation.exe --gtest_filter=*phase7*

# Or run one test headless
./lambda.exe view test/ui/test_phase7a_resize_mutation.html \
    --event-file test/ui/ui_phase7a_resize_mutation.json --headless --no-log
```

On failure, inspect `./temp/*_diff.png` to see mismatched pixels highlighted
in red, and `./temp/*_actual.png` to see what Radiant produced.

---

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| `render_html_doc` in event handler is slow for large pages | Only called after mutation events, not on every event. Tests use small pages. |
| Animation timing drift from floating-point accumulation | Use `base_time + step * i` instead of cumulative addition |
| `assert_style` with `animated: true` reads stale ViewSpan if tick not called | `advance_time` always ticks before render; document this requirement |
| Snapshot mismatch across platforms (font rendering) | Platform-specific reference PNGs (like render tests); per-test threshold overrides |
| Browser reference PNGs need regeneration after HTML changes | `capture_mutation_snapshots.js --force` re-captures all; Makefile target provided |
| `scroll_to` element not in DOM | Log error + fail assertion (consistent with existing target resolution) |
| Animation `threshold: 5.0` too loose to catch real bugs | Paired with precise `assert_style animated:true` checks that catch value errors regardless of pixel diff |
