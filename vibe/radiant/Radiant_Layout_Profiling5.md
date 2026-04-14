# Lazy Image Decode: Eliminating PNG Decompression from Layout

**Status:** Implemented  
**Author:** Lambda Team  
**Date:** April 2026  
**Prerequisite:** Radiant_Layout_Profiling4.md (font/text optimizations)

---

## 1. Motivation

After the font/text optimizations in Profiling4 brought md_zod-readme from 2.65s to 1.32s, the page remained the slowest in the markdown corpus. Profiling revealed the remaining bottleneck was not in layout algorithms but in **full PNG image decoding** during the layout phase — work that only needs image dimensions, not pixel data.

---

## 2. Profiling Method

### 2.1 Built-in Timers (release build)

```
time ./release/lambda layout test/layout/data/markdown/md_zod-readme.html
```

**Before optimization:**
- User CPU: **1.06s**
- `[LAYOUT_PROF]`: **1444.4ms**

### 2.2 macOS `sample` Profiler (CPU sampling, release binary)

```bash
./release/lambda layout md_zod-readme.html &
LPID=$!
sample $LPID -f temp/zod_profile.txt 2
```

**Result: 855 of 924 samples (92.5%) in `png_read_image`.**

Call chain:
```
855 png_read_image
  855 png_read_IDAT_data
    855 inflate
      432 adler32_z
      211 inflate_fast
      120 png_read_filter_row_paeth4_neon
      63  png_combine_row
       29 inflate_table
```

Only 64/924 samples (7%) were in actual layout logic.

### 2.3 Built-in SLOW MEASURE Timer

```
SLOW MEASURE: img took 1153ms
  SLOW MEASURE: a took 1154ms
    SLOW BLOCK: td took 1154ms (count=244)
      SLOW BLOCK: table took 1168ms (count=244)
```

A single `<img>` measurement inside a `<td>` cost **1153ms** — 80% of the total 1444ms layout time. The cost cascades up through containing blocks.

### 2.4 Image Inventory

The zod README HTML contains ~30 images (PNG, SVG, JPG). Local file sizes:

| Image | Size | Pixel Dimensions |
|-------|-----:|-----------------|
| i-imgur_kTiLtZt.png | 837K | 2776×920 |
| github_52b3039d.png | 333K | 1200×423 |
| user-images_175336533.png | 325K | 3320×1800 |
| locize_github_locize.png | 202K | — |
| i-imgur_w1GE8ao.png | 132K | 2776×920 |
| ~20 avatar PNGs | 10–93K each | 200×200 |

The 2776×920 PNG decodes to 10.2 MB RGBA (2776 × 920 × 4 bytes), all decompressed during layout just to read two integers: width and height.

---

## 3. Root Cause

### 3.1 The Loading Path

```
layout_flex_content()
  → calculate_item_intrinsic_sizes()     [layout_flex_measurement.cpp]
    → load_image()                        [surface.cpp]
      → image_load()                      [lib/image.c]
        → load_png()
          → png_read_info()               ← reads IHDR (dimensions available here)
          → png_read_image()              ← FULL PIXEL DECODE (the 92.5% bottleneck)
```

`load_image()` returned an `ImageSurface` with `width`, `height`, AND fully decoded `pixels`. The layout engine only needed `width` and `height` to compute flex item intrinsic sizes. The pixel data isn't used until the render phase.

### 3.2 Why It's Expensive

PNG decompression involves:
1. **DEFLATE decompression** — zlib `inflate()` on the entire compressed data stream
2. **Filter reversal** — `png_read_filter_row_paeth4_neon` undoes per-row prediction filters
3. **Row combination** — `png_combine_row` assembles final pixel data
4. **Memory allocation** — `width × height × 4` bytes per image (10 MB for the 2776×920 image)

For the zod README, this work runs for every PNG image during the first layout pass, even though the render phase may never execute (e.g., `layout` command).

### 3.3 Image Cache Already Existed — But Cached Decoded Data

`load_image()` had a `hashmap`-based image cache (`uicon->image_cache`) that prevented re-loading the same URL. However, the first load still did full decode. The cache prevented redundant decodes across measurement passes, but the single initial decode was the dominant cost.

---

## 4. Solution: Lazy Image Decode

### 4.1 Approach

**Read only the image file header during layout to get dimensions. Defer full pixel decode to render time.**

Image formats store dimensions in their headers:
- **PNG**: Width and height are in the IHDR chunk — bytes 16–23 after the 8-byte signature (24 bytes total)
- **JPEG**: `tjDecompressHeader3()` reads SOF marker without decompressing pixel data
- **GIF**: Width and height are at bytes 6–9 of the file header (10 bytes total)
- **SVG**: Already lazy — `rdt_picture_load()` parses vector data; rasterization is deferred to `render_svg()` in `render_image_content()`

### 4.2 Implementation

#### 4.2.1 Header-Only Dimension Reading (`lib/image.c`, `lib/image.h`)

Added two new functions:

```c
int image_get_dimensions(const char* filename, int* width, int* height);
int image_get_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height);
```

Each dispatches to a format-specific reader:

| Format | Function | Bytes Read | Method |
|--------|----------|-----------|--------|
| PNG | `get_png_dimensions()` | 24 | Read raw bytes, extract big-endian uint32 at offsets 16 and 20, verify PNG signature |
| JPEG | `get_jpeg_dimensions()` | ≤4096 | `tjDecompressHeader3()` — parses SOF marker only |
| GIF | `get_gif_dimensions()` | 10 | Read raw bytes, extract little-endian uint16 at offsets 6 and 8, verify "GIF8xa" signature |

Fallback: If header read fails, the full `image_load()` / `image_load_from_memory()` path is used.

#### 4.2.2 Lazy `ImageSurface` (`radiant/view.hpp`, `radiant/surface.cpp`)

Added three fields to `ImageSurface`:

```c
char* source_path;           // local file path for lazy decode (NULL if already decoded)
unsigned char* source_data;  // in-memory data for lazy decode of HTTP images
size_t source_data_len;      // length of source_data
```

Modified `load_image()` in `surface.cpp`:
- **Local files**: Call `image_get_dimensions()`. If successful, create `ImageSurface` with `width`/`height` set but `pixels = NULL`. Store `source_path` for deferred decode.
- **HTTP images**: Call `image_get_dimensions_from_memory()`. If successful, create `ImageSurface` with `pixels = NULL`. Store `source_data` + `source_data_len` (the downloaded buffer) for deferred decode.
- **Fallback**: If header-only read fails, full decode as before.
- **SVGs**: Unchanged (already lazy).
- **Data URIs**: Unchanged (decoded immediately — typically small inline images).

Added `image_surface_ensure_decoded()`:
- If `pixels != NULL`: no-op (already decoded)
- If `source_path != NULL`: call `image_load()`, set `pixels` and `pitch`, free `source_path`
- If `source_data != NULL`: call `image_load_from_memory()`, set `pixels` and `pitch`, free `source_data`

Updated `image_surface_destroy()` to free `source_path` and `source_data`.

#### 4.2.3 Decode-on-Demand in Render Paths

Two render paths need actual pixel data:

1. **`render_image_content()`** in `radiant/render.cpp` — `<img>` elements:
   ```cpp
   // before blit_surface_scaled (non-SVG path):
   image_surface_ensure_decoded(img);
   ```

2. **`render_background_image()`** in `radiant/render_background.cpp` — CSS `background-image`:
   ```cpp
   // before the tiling loop (non-SVG path):
   if (!is_svg) {
       image_surface_ensure_decoded(img);
   }
   ```

Both insertions are no-ops for SVGs (which use `pic` not `pixels`) and for already-decoded images. The existing `blit_surface_scaled()` null-check on `src->pixels` provides a safety net.

---

## 5. Results

### 5.1 Layout Performance (release build, md_zod-readme)

| Metric | Before | After | Improvement |
|--------|-------:|------:|-------------|
| User CPU time | 1.06s | **0.16s** | **6.6× faster** |
| Layout wall time | 1444ms | ~166ms | **~8.7× faster** |
| Image decode during layout | 1153ms | **~0ms** | **eliminated** |

Three consecutive runs:
```
0.16s user 0.31s system 96% cpu 0.485 total
0.16s user 0.30s system 96% cpu 0.471 total
0.15s user 0.29s system 97% cpu 0.455 total
```

### 5.2 Combined Improvement (from Profiling4 baseline → Profiling5)

| Stage | Time | Notes |
|-------|-----:|-------|
| Original (pre-Profiling4) | 2.65s | Font resolution + text + image decode |
| After Profiling4 (font/text) | 1.32s | Font optimized, image still full-decode |
| **After Profiling5 (lazy image)** | **0.16s** | Header-only image reading during layout |
| **Total speedup** | **16.6×** | from 2.65s to 0.16s |

### 5.3 Test Suite Validation

All tests pass with zero regressions:

| Suite | Result |
|-------|--------|
| Layout Baseline | ✅ 4080 passed, 0 failed |
| WPT CSS Text | ✅ 518 passed |
| Layout Page Suite | ✅ 39 passed |
| UI Automation | ✅ 47 passed |
| View Page & Markdown | ✅ 97 passed |
| Pretext Corpus | ✅ 78 passed |
| Lambda Baseline | ✅ 566 passed |
| Fuzzy Crash | 15 passed, 2 failed (pre-existing) |

---

## 6. Files Modified

| File | Change |
|------|--------|
| `lib/image.h` | Added `image_get_dimensions()` and `image_get_dimensions_from_memory()` declarations |
| `lib/image.c` | Added header-only dimension readers for PNG (24 bytes), JPEG (tjDecompressHeader3), GIF (10 bytes); plus from-memory variants |
| `radiant/view.hpp` | Added `source_path`, `source_data`, `source_data_len` fields to `ImageSurface` |
| `radiant/surface.cpp` | Modified `load_image()` for lazy decode; added `image_surface_ensure_decoded()`; updated `image_surface_destroy()` |
| `radiant/render.cpp` | Added `image_surface_ensure_decoded(img)` before `blit_surface_scaled` in `render_image_content()` |
| `radiant/render_background.cpp` | Added `image_surface_ensure_decoded(img)` before raster tile loop in `render_background_image()` |

---

## 7. Remaining Issues

### 7.1 Fuzzy Crash: Nested `position:fixed display:flex` Timeout (fixed)

Two fuzzer test files previously caused an exponential-blowup timeout:

- `crash_nested_fixed_flex` — 50 nested `position:fixed display:flex` divs (was >20s, now 0.5s)
- `timeout_nested_fixed_flex_200` — 200 nested variant

**Root cause:** `layout_final_flex_content()` has a fallback DOM traversal path used when `flex->item_count == 0`. All 50 divs are `position:fixed`, so `should_skip_flex_item()` excludes them all from the flex items array (`item_count == 0`). However, `init_flex_item_view()` was called for each child first (for measurement), which sets `view_type = RDT_VIEW_BLOCK`. The fallback traversal iterates all DOM children checking `view_type == RDT_VIEW_BLOCK` — matching every `position:fixed` child — and calls `layout_flex_item_content()` on them. This causes each fixed child to be laid out twice (once via the fallback, once via `layout_flex_absolute_children()`), creating O(2^n) exponential blowup: D2 laid out twice, each D2 causes D3 twice, etc.

**Fix:** In the fallback DOM traversal inside `layout_final_flex_content()`, added a skip check for `position:absolute` and `position:fixed` children before calling `layout_flex_item_content()`. These are always handled by `layout_flex_absolute_children()` and must never be laid out via the content path.

**File:** `radiant/layout_flex_multipass.cpp` — `else` branch of `layout_final_flex_content()`

**Result:** `crash_nested_fixed_flex.html` (50 divs) completes in 0.53s (was >20s).

**Layout limits centralized:** All layout safety constants were moved to a new `radiant/layout_guards.h`, included transitively via `layout.hpp`:

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_LAYOUT_DEPTH` | 300 | DOM nesting depth limit (flow + abs path, same `lycon->depth` counter) |
| `MAX_LAYOUT_NODES` | 50000 | Total node count per layout pass |
| `MAX_FLEX_DEPTH` | 16 | Flex-in-flex nesting limit |
| `MAX_IFRAME_DEPTH` | 3 | Iframe recursion limit |

**Removed redundancies:**
- `MAX_ABS_DEPTH = 300` in `layout_positioned.cpp` — identical value and same counter as `MAX_LAYOUT_DEPTH`; replaced with `MAX_LAYOUT_DEPTH`
- Duplicate `const int MAX_IFRAME_DEPTH = 3` local variables in both `layout_flex_multipass.cpp` and `layout_block.cpp` — removed; constant now comes from `layout_guards.h`
- File-static `MAX_FLEX_DEPTH = 16` in `layout_flex_multipass.cpp` — removed; constant now comes from `layout_guards.h`


### 7.2 Tile-Based Rendering (implemented)

```
./release/lambda render md_zod-readme.html -o output.png
→ Auto-sized output dimensions: 22684×58217
→ Segfault (5.3 GB surface allocation)
```

**Implemented:** Tile-based rendering is now active for any auto-sized PNG output whose total pixel count exceeds **32 M pixels** (≈ 128 MB of RGBA; a 1200 px wide page triggers this at ~26 000 px tall).

Instead of allocating a single surface for the full page, `render_html_doc_tiled()` renders horizontal strips of **4096 physical pixels** each, writing each strip's rows directly to an open `libpng` streaming context via `png_write_row()`. Peak pixel-buffer memory is `O(total_width × 4096)` regardless of page length — ~20 MB for a 1200 px wide page.

**Coordinate translation** ensures each tile strip sees the correct subset of the view tree:
- `ImageSurface::tile_offset_y` — subtracted from the absolute Y row index in `fill_surface_rect()`, `blit_surface_scaled()`, `draw_glyph()`, and `draw_color_glyph()` so direct pixel writes land in the tile buffer
- `rdt_vector_set_tile_offset_y()` — wraps every ThorVG shape in a translated scene (`tvg_paint_translate(scene, 0, -tile_y)`) so vector graphics also render at the correct tile-relative position
- The render clip (`rdcon.block.clip.top/bottom`) is set to the page-absolute tile bounds so out-of-tile content is naturally skipped

The non-tiled (`render_html_doc`) path is completely unchanged; `tile_offset_y` defaults to 0 (zero-initialised) and the offset arithmetic is a no-op.

**Files modified:**

| File | Change |
|------|--------|
| `radiant/view.hpp` | Added `tile_offset_y` field to `ImageSurface` |
| `radiant/surface.cpp` | `fill_surface_rect`, `blit_surface_scaled` subtract `tile_offset_y` from row index |
| `radiant/render.cpp` | `draw_glyph`, `draw_color_glyph` subtract `tile_offset_y`; added `render_html_doc_tiled()` |
| `radiant/render.hpp` | Declared `render_html_doc_tiled()` |
| `radiant/rdt_vector.hpp` | Declared `rdt_vector_set_tile_offset_y()` |
| `radiant/rdt_vector_tvg.cpp` | Added `tile_offset_y` to `RdtVectorImpl`; scene-wrap translation in `tvg_push_draw_remove`; added setter |
| `radiant/render_img.cpp` | `render_html_to_png()` routes to tiled path when `output_width × output_height > 32 M` |

### 7.3 Centralized Layout Limits (implemented)

Layout safety constants were previously scattered as `static const int` or `const int` locals across 5 source files, with duplicate definitions and redundant guards.

**Before:**

| Constant | Value | Location | Issue |
|----------|-------|----------|-------|
| `MAX_LAYOUT_DEPTH` | 300 | `layout.cpp` (local in `layout_flow_node`) | Only accessible in one function |
| `MAX_LAYOUT_NODES` | 50000 | `layout.cpp` (local in `layout_flow_node`) | Only accessible in one function |
| `MAX_ABS_DEPTH` | 300 | `layout_positioned.cpp` (local in `layout_abs_block`) | **Redundant** — same value AND same `lycon->depth` counter as `MAX_LAYOUT_DEPTH` |
| `MAX_FLEX_DEPTH` | 16 | `layout_flex_multipass.cpp` (file-static) | Only accessible in one file |
| `MAX_IFRAME_DEPTH` | 3 | `layout_flex_multipass.cpp` (local) | **Duplicate** — identical local in `layout_block.cpp` |
| `MAX_IFRAME_DEPTH` | 3 | `layout_block.cpp` (local) | **Duplicate** |

**After:** Single `radiant/layout_guards.h`, included transitively via `layout.hpp`:

```c
constexpr int MAX_LAYOUT_DEPTH = 300;    // DOM nesting depth (flow + abs paths share lycon->depth)
constexpr int MAX_LAYOUT_NODES = 50000;  // total node count per layout pass
constexpr int MAX_FLEX_DEPTH   = 16;     // flex-in-flex nesting limit
constexpr int MAX_IFRAME_DEPTH = 3;      // iframe recursion limit
```

**Changes:**
- Removed `MAX_ABS_DEPTH` entirely — replaced with `MAX_LAYOUT_DEPTH` in `layout_positioned.cpp`
- Removed duplicate `MAX_IFRAME_DEPTH` locals from `layout_flex_multipass.cpp` and `layout_block.cpp`
- Removed file-static `MAX_FLEX_DEPTH` from `layout_flex_multipass.cpp`
- Removed local `MAX_LAYOUT_DEPTH` and `MAX_LAYOUT_NODES` from `layout.cpp`

**Not centralized** (kept in place — algorithm-internal, single-file use):

| Constant | Value | Location | Reason |
|----------|-------|----------|--------|
| `MAX_VALIDATE_DEPTH` | 32 | `layout_flex_multipass.cpp` | Debug-only coordinate validator |
| `MAX_MEASURE_DEPTH` | 32 | `layout_flex_measurement.cpp` | Measurement recursion guard |
| `MAX_GRID_SIZE` | 16 | `grid_utils.cpp` | Grid template parsing |
| `MAX_GRID_ITEMS` | 256 | `grid_sizing_algorithm.hpp`, `grid_placement.hpp` | Grid-internal (duplicate, but self-contained in grid subsystem) |
| `MAX_CSS_TREE_DEPTH` | 512 | `cmd_layout.cpp` | CSS command parsing |

### 7.4 Nested Flex Layout Optimization (A, B, C implemented)

For n deeply nested flex containers (each containing one flex child), the layout had redundant work at each nesting level. Three optimizations were implemented to eliminate this; a fourth (D) remains as a future architectural proposal.

**Previous call chain per nesting level:**

```
layout_flex_content(D_i)                              // entry point
  → init_flex_container(D_i)                          // PASS 1: alloc FlexContainerLayout A
  → collect_and_prepare_flex_items(D_i)               //   measure each child → fills A
  → layout_flex_container_with_nested_content(D_i)    // PASS 2: flex algorithm
      → init_flex_container(D_i)                      //   alloc FlexContainerLayout B (A orphaned!)
      → collect_and_prepare_flex_items(D_i)           //   ← REDUNDANT: same items, re-measured
      → run_enhanced_flex_algorithm(D_i)              //   flexbox sizing (grow/shrink/wrap)
      → layout_final_flex_content(D_i)                //   content layout
          → layout_flex_item_content(D_{i+1})         //     ← RECURSE into child
  → cleanup_flex_container (frees A)                  // A was never used
  → layout_flex_absolute_children(D_i)                // PASS 3: abs/fixed children
```

Each level called `init_flex_container` + `collect_and_prepare_flex_items` **twice** — allocating two `FlexContainerLayout` structs and discarding the first. Each collection call does: CSS style re-resolution, measurement cache invalidation, `init_flex_item_view`, `measure_flex_child_content` (subtree traversal), percentage re-resolution, and flex property setup.

**Optimized call chain (after A):**

```
layout_flex_content(D_i)                              // entry point
  → layout_flex_container_with_nested_content(D_i)    // single pass: init + collect + algorithm
      → init_flex_container(D_i)                      //   alloc FlexContainerLayout (once)
      → collect_and_prepare_flex_items(D_i)           //   measure + prepare (once)
      → run_enhanced_flex_algorithm(D_i)              //   flexbox sizing
      → layout_final_flex_content(D_i)                //   content layout
          → layout_flex_item_content(D_{i+1})         //     RECURSE
      → cleanup_flex_container                        //   free
  → layout_flex_absolute_children(D_i)                // abs/fixed children
```

#### A. Eliminate redundant re-collection (implemented)

Removed `init_flex_container` + `collect_and_prepare_flex_items` + `cleanup_flex_container` from `layout_flex_content()`. Now `layout_flex_container_with_nested_content()` is the sole site that initializes, collects, runs the algorithm, and cleans up.

**File:** `radiant/layout_flex_multipass.cpp` — removed ~15 lines from `layout_flex_content()`

This eliminates one `mem_calloc(FlexContainerLayout)`, one full DOM traversal of all children (style re-resolution, view init, measurement, percentage resolution, flex property setup), and one `cleanup_flex_container` per flex container.

#### B. Definite-size measurement fast path (implemented)

In `collect_and_prepare_flex_items()`, skip `measure_flex_child_content()` when a flex item has both explicit non-percentage `width` and `height` from CSS. The item's dimensions are fully determined and don't need content measurement.

**File:** `radiant/layout_flex.cpp` — added 6-line guard before `measure_flex_child_content(lycon, child)`

```cpp
bool has_definite_w = (item_block->blk && item_block->blk->given_width >= 0
                       && isnan(item_block->blk->given_width_percent));
bool has_definite_h = (item_block->blk && item_block->blk->given_height >= 0
                       && isnan(item_block->blk->given_height_percent));
if (!(has_definite_w && has_definite_h)) {
    measure_flex_child_content(lycon, child);
}
```

#### C. Smart measurement cache invalidation (implemented)

Previously, `collect_and_prepare_flex_items()` unconditionally called `invalidate_measurement_cache_for_node(child)` on every child, forcing re-measurement even when the container's content width hadn't changed. This defeated the `MeasurementCache` for nodes measured in a earlier pass.

**Fix:** Added `context_width` field to `MeasurementCacheEntry`. Now `store_in_measurement_cache()` records the container width used during measurement. The invalidation check compares the stored `context_width` against the current `container_content_width` and only invalidates when they differ by more than 0.5px.

**Files:**
- `radiant/layout_flex_measurement.hpp` — added `float context_width` to `MeasurementCacheEntry`
- `radiant/layout_flex_measurement.cpp` — added `context_width` parameter to `store_in_measurement_cache()`; stores `saved_context.block.content_width` at the measurement call site
- `radiant/layout_flex.cpp` — replaced unconditional `invalidate_measurement_cache_for_node(child)` with context-width comparison

#### D. Bottom-up measurement with node-level memoization (future proposal)

The Taffy/browser approach: measure nodes bottom-up. Each `DomElement` computes its intrinsic size once, caches the result on the node (not just in a global hash map), and parents read cached child sizes without re-entering children. The current code does top-down layout with trial `measure_flex_child_content` calls that re-traverse subtrees.

**Implementation sketch:**
1. Add `cached_intrinsic_width` / `cached_intrinsic_height` fields to `DomElement`
2. In `measure_flex_child_content`, set these after measurement
3. In `calculate_item_intrinsic_sizes` (flex algorithm), check node-level cache before re-measuring
4. Invalidate only when CSS properties affecting sizing change (not on every collection pass)

This is the architectural change for true O(n) but requires careful invalidation semantics.

#### Benchmark results

| Test case | Before | After | Speedup |
|-----------|-------:|------:|--------:|
| md_zod-readme.html (real page) | 180ms | 158ms | 1.14× |
| crash_nested_fixed_flex (50 fixed+flex) | 2.2ms | 1.3ms | 1.7× |
| nested_flex_200 (200 deep, 1 child) | 8.5ms | 9.0ms | ~1× (MAX_FLEX_DEPTH limits to 16) |
| flex_200x5 (200 deep, 5 children) | 47ms | 46ms | ~1× (same depth limit) |
| flex_16x50 (16 deep, 50 children) | 19ms | 17ms | 1.1× |

The improvement is modest on synthetic deep-nesting tests because `MAX_FLEX_DEPTH=16` limits actual flex processing to 16 levels regardless of nesting depth. The 14% improvement on `md_zod-readme.html` reflects real-world savings where flex containers are shallow but wide (many items).

#### Complexity summary

| Optimization | Status | Impact |
|-------------|--------|--------|
| A. Skip re-collection | Implemented | Eliminates 1 alloc + 1 full DOM traversal per flex container |
| B. Definite-size skip | Implemented | Skips measurement for items with explicit CSS dimensions |
| C. Smart cache invalidation | Implemented | Preserves cache when container width unchanged |
| D. Bottom-up memoization | Proposal | True O(n) for all nesting patterns |

### 7.5 `layout` Command Output Shows Minimal Height (132.8px)

The `layout` command produces a view tree with `hg:132.8` for the root `<html>` element on the zod README. This is because `overflow:auto` on the root clips the visible area to the viewport height. The full content is scrollable but the view tree only reflects the initial viewport dimensions for the root. This is correct CSS behavior but may surprise users expecting the full content height.

### 7.6 No Lazy Decode for Data URIs

Data URI images (`data:image/png;base64,...`) are still fully decoded immediately in `load_image()`. These are typically small (inline icons, badges) so the impact is negligible. If a document embeds large data URI images, they would not benefit from lazy decode. Supporting this would require storing the decoded base64 buffer, adding complexity for minimal gain.

### 7.7 Debug Build Logging Overhead

The debug build produces ~1M log lines (80 MB) for the zod README, adding ~1.8s overhead (2.86s debug vs 1.06s release, before lazy loading). With lazy loading, the debug build runs in ~2.1s (vs 0.16s release). The log I/O dominates debug build timing. This is by-design but worth noting — always use release build for performance testing.
