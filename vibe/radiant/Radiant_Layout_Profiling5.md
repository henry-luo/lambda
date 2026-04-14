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

### 7.1 Fuzzy Crash: Nested `position:fixed display:flex` Timeout (pre-existing)

Two fuzzer test files fail (pre-existing, not caused by this change):

- `crash_nested_fixed_flex` — 100 nested `position:fixed display:flex` divs
- `timeout_nested_fixed_flex_200` — 200 nested variant, times out after 15s

**Root cause:** `position:fixed` elements enter through `layout_abs_block()` → `layout_block()` → `layout_flex_content()`, which performs expensive multi-pass flex measurement before hitting the `MAX_FLEX_DEPTH` guard. The guard fires correctly but the setup work before it is O(n²) in nesting depth.

**Current mitigations:**
- `MAX_FLEX_DEPTH = 16` — limits recursive flex nesting
- `MAX_ABS_DEPTH = 300` — limits positioned element recursion
- Early guard in `layout_flex_content()` before expensive setup
- Separate `flex_depth` / `flex_node_count` counters (not shared with normal `node_count`)

**Possible improvements:**
- Move the depth/node guard in `layout_flex_content()` to before `layout_flex_container_with_nested_content()` to avoid the multi-pass setup entirely
- Add a fast path in `layout_abs_block()` that checks `flex_depth` before calling into block+flex layout
- Profile the specific O(n²) pattern to find where the quadratic work occurs

### 7.2 PNG Render of Large Pages Crashes on Auto-Sizing (pre-existing)

```
./release/lambda render md_zod-readme.html -o output.png
→ Auto-sized output dimensions: 22684×58217
→ Segfault (5.3 GB surface allocation)
```

The `render_html_to_png()` auto-sizing mode computes content bounds and creates a surface to fit. For long pages, this produces enormous surface allocations. This is not related to lazy loading — the same crash occurs without the optimization.

**Possible improvements:**
- Cap maximum auto-size surface dimensions (e.g., 8192×8192 or 16384×16384)
- Tile-based rendering: render in chunks and stitch the output
- Warn or error when auto-sized dimensions exceed a threshold

### 7.3 `layout` Command Output Shows Minimal Height (132.8px)

The `layout` command produces a view tree with `hg:132.8` for the root `<html>` element on the zod README. This is because `overflow:auto` on the root clips the visible area to the viewport height. The full content is scrollable but the view tree only reflects the initial viewport dimensions for the root. This is correct CSS behavior but may surprise users expecting the full content height.

### 7.4 No Lazy Decode for Data URIs

Data URI images (`data:image/png;base64,...`) are still fully decoded immediately in `load_image()`. These are typically small (inline icons, badges) so the impact is negligible. If a document embeds large data URI images, they would not benefit from lazy decode. Supporting this would require storing the decoded base64 buffer, adding complexity for minimal gain.

### 7.5 Debug Build Logging Overhead

The debug build produces ~1M log lines (80 MB) for the zod README, adding ~1.8s overhead (2.86s debug vs 1.06s release, before lazy loading). With lazy loading, the debug build runs in ~2.1s (vs 0.16s release). The log I/O dominates debug build timing. This is by-design but worth noting — always use release build for performance testing.
