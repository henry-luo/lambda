# Radiant Layout & Render Performance Profiling

## Date: December 27, 2025

## Test Case
```bash
./lambda.exe view ./test/input/comprehensive_test.md
```
- File: 471 lines, 12,300 characters
- Contains: Markdown with headers, code blocks, tables, lists, emoji characters

---

## 1. Initial State

### Timing Breakdown (Before Optimization)

| Phase | Time | Details |
|-------|------|---------|
| **Load markdown** | 72.4ms | Parse: 14ms, DOM: 15ms, CSS parse: 4.6ms, CSS cascade: 38.5ms |
| **Layout** | 2,402.7ms | style_resolve: 870.8ms (661 calls), text: 44.1ms, block: 2,748.4ms |
| **Render** | 7,251.9ms | - |
| **Total** | ~9.8 seconds | User: 8.31s, System: 1.50s |

### Identified Bottlenecks

1. **Font Database Lookups** - `load_styled_font()` was calling `font_database_find_best_match()` for every font setup, even for repeated fonts
2. **Redundant Font Setup** - `setup_font()` called during both layout (661 calls) and render (98 calls)
3. **Glyph Fallback Searches** - For emoji/CJK characters, iterating through 7 fallback fonts repeatedly
4. **DEBUG Logging Overhead** - 112,000+ log lines including hot-path font parsing logs

### Profiling Data

Initial render profiling revealed:
- `setup_font` calls during render: 98 calls = **760.4ms** (7.8ms per call)
- `load_glyph` calls: 5,176 calls = 7.6ms (fast)
- `draw_glyph` calls: 2,588 calls = 0.1ms (fast)

The `setup_font` function was the main bottleneck, calling expensive database lookups repeatedly.

---

## 2. Optimizations Performed

### 2.1 Font Style Cache (Primary Optimization)

**File:** `radiant/font.cpp` - `load_styled_font()`

**Problem:** Every call to `load_styled_font()` performed:
1. `font_database_find_best_match()` - expensive database search
2. Font file loading (if not cached by file path)

**Solution:** Added early cache lookup using a style-based key:

```cpp
// Create cache key with (family, weight, style, size)
StrBuf* style_cache_key = strbuf_create(font_name);
strbuf_append_str(style_cache_key, font_style->font_weight == CSS_VALUE_BOLD ? ":bold:" : ":normal:");
strbuf_append_str(style_cache_key, font_style->font_style == CSS_VALUE_ITALIC ? "italic:" : "normal:");
strbuf_append_int(style_cache_key, (int)font_style->font_size);

// Check cache FIRST - before expensive database lookup
FontfaceEntry* entry = hashmap_get(uicon->fontface_map, &search_key);
if (entry) {
    return entry->face;  // Cache hit - skip database lookup entirely
}
```

**Impact:** 760ms → 0.2ms for render-phase font setup (3,800x improvement)

### 2.2 Glyph Fallback Cache

**File:** `radiant/font.cpp` - `load_glyph()`, `radiant/view.hpp` - `UiContext`

**Problem:** For characters not in the primary font (emoji, CJK), the code iterated through 7 fallback fonts:
- Apple Color Emoji
- PingFang SC
- Heiti SC
- Hiragino Sans
- Helvetica Neue
- Arial Unicode MS
- Times New Roman

This happened for every occurrence of such characters, including repeated ones.

**Solution:** Added `glyph_fallback_cache` hashmap to `UiContext`:

```cpp
// In view.hpp - UiContext struct
struct hashmap* glyph_fallback_cache;  // maps codepoint -> fallback FT_Face

// In font.cpp - load_glyph()
GlyphFallbackEntry* cached = hashmap_get(uicon->glyph_fallback_cache, &search_key);
if (cached) {
    if (cached->fallback_face == NULL) {
        return NULL;  // Negative cache - no fallback has this glyph
    }
    // Use cached fallback face directly
    FT_Load_Glyph(cached->fallback_face, char_index, load_flags);
    return cached->fallback_face->glyph;
}
```

**Impact:** Reduced fallback font searches from 120+ to 5 (one per unique codepoint)

### 2.3 Log Level Configuration

**File:** `log.conf`

**Problem:** DEBUG logging was enabled, causing:
- 10,806 "Parsed family name" logs during font database initialization
- Excessive logging in hot paths (font loading, glyph rendering)

**Solution:** Changed default log level from DEBUG to INFO:

```
[rules]
default.INFO "log.txt"; indented
```

**Impact:** Document parsing: 72ms → 1.9ms (38x improvement)

### 2.4 Removed Hot-Path Debug Logs

**Files:** `lib/font_config.c`, `radiant/font.cpp`, `radiant/event.cpp`

Removed `log_debug()` calls from frequently-executed code paths:
- Font name parsing (called thousands of times during DB init)
- `load_styled_font()` success logs
- Event targeting logs

---

## 3. Final Results

### Timing Breakdown (After Optimization)

| Phase | Time | Improvement |
|-------|------|-------------|
| **Load markdown** | 1.9ms | 38x faster |
| **Layout** | 15.2ms | 154x faster |
| **Render** | 7.8ms | 921x faster |
| **Total** | **0.78 seconds** | **12.8x faster** |

### Detailed Metrics

| Metric | Before | After | Factor |
|--------|--------|-------|--------|
| Total wall time | ~10s | 0.78s | **12.8x** |
| Parse markdown | 14.0ms | 1.1ms | 12.7x |
| Build DOM tree | 15.3ms | 0.2ms | 76.5x |
| CSS parse | 4.6ms | 0.3ms | 15.3x |
| CSS cascade | 38.5ms | 0.3ms | 128x |
| style_resolve | 870.8ms | 1.6ms | **544x** |
| layout_block | 2,397.8ms | 9.0ms | 266x |
| render_block_view | 7,250.9ms | 7.5ms | **967x** |
| setup_font (render) | 760.4ms | 0.2ms | **3,802x** |

### Render Statistics

| Metric | Count | Time |
|--------|-------|------|
| load_glyph calls | 5,176 | 5.6ms |
| draw_glyph calls | 2,588 | 0.1ms |
| setup_font calls | 98 | 0.2ms |

### Regression Tests

All tests pass after optimization:
- Lambda baseline: **73/73** ✅
- Radiant baseline: **1,523/1,523** ✅

---

## Key Takeaways

1. **Cache by request parameters, not by result** - The original code cached FT_Face by file path, but lookups still went through the database. Caching by the input parameters (family, weight, style, size) allows skipping expensive lookups entirely.

2. **Profile before optimizing** - Adding timing instrumentation revealed that `setup_font` (760ms) was the bottleneck, not `load_glyph` (7.6ms) or `draw_glyph` (0.1ms) as initially suspected.

3. **Negative caching matters** - For glyphs not found in any fallback font, caching the negative result prevents repeated searches through all 7 fallback fonts.

4. **Logging has cost** - Even DEBUG-level logs that are "disabled" may have runtime cost if the logging framework still evaluates arguments. Use `log.conf` to set appropriate levels for production.
