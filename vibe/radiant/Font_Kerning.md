# Font Kerning in Radiant

## Overview

Radiant uses a dual-mode kerning system: **FreeType kern tables** as the primary path and **CoreText GPOS** (macOS only) as a fallback for fonts that lack a readable kern table. All kerning values are returned in CSS pixels.

The public API is codepoint-based: callers pass Unicode codepoints, and `font_get_kerning()` handles index conversion and backend selection internally.

## Architecture

```
Callers (layout_text.cpp, intrinsic_sizing.cpp)
    │
    ▼
font_get_kerning(handle, left_cp, right_cp)   ← codepoint-based API
    │
    ├── FT_HAS_KERNING? ──▶ FT_Get_Kerning() ──▶ return (delta.x / 64) / pixel_ratio
    │
    └── else (macOS) ──▶ font_get_kerning_coretext()
                              │
                              ├── kern_cache hit? ──▶ return cached value
                              │
                              └── font_platform_get_pair_kerning() ──▶ CTLine measurement
                                      │
                                      └── cache result, return / pixel_ratio
```

## Font Classification

Not all fonts expose kerning the same way. The loader classifies fonts at handle creation time:

| Font | kern table | GPOS table | FT_HAS_KERNING | CoreText created? |
|------|-----------|------------|-----------------|-------------------|
| SFNS.ttf (System Font) | None | 346 KB | false | **Yes** — no kern table at all |
| Times.ttc (macOS system) | AAT format (708 B) | None | false | **No** — kern table exists but FreeType can't read AAT |
| Times New Roman.ttf | MS format (5220 B) | 39 KB | true | No — FreeType reads kern natively |

The key insight: `FT_HAS_KERNING` only reflects whether FreeType can **read** the kern table (Microsoft format). Apple AAT kern tables are invisible to FreeType. To avoid enabling CoreText kerning for fonts that previously had no kerning in Radiant (which would change line wrapping), we use `FT_Load_Sfnt_Table('kern')` to detect the physical presence of any kern table regardless of format.

## Files & Components

### Data Structures — `lib/font/font_internal.h`

```c
typedef struct KernPairEntry {
    uint32_t left_cp;
    uint32_t right_cp;
    float    kerning;       // in CSS pixels
} KernPairEntry;
```

`FontHandle` fields (macOS only):
- `void* ct_font_ref` — CoreText font reference, created at physical pixel size
- `struct hashmap* kern_cache` — LRU-like cache of `KernPairEntry`, max 4096 entries

### Handle Creation — `lib/font/font_loader.c`

In `create_handle()`, after the FreeType face is opened and sized:

```c
#ifdef __APPLE__
if (!FT_HAS_KERNING(face)) {
    FT_ULong kern_len = 0;
    FT_Error kern_err = FT_Load_Sfnt_Table(face, FT_MAKE_TAG('k','e','r','n'),
                                            0, NULL, &kern_len);
    bool has_any_kern_table = (kern_err == 0 && kern_len > 0);
    if (!has_any_kern_table) {
        handle->ct_font_ref = font_platform_create_ct_font(
            FT_Get_Postscript_Name(face), face->family_name, physical_size);
    }
}
#endif
```

Three conditions must ALL be true for CoreText kerning to activate:
1. `FT_HAS_KERNING(face)` is false — FreeType cannot read the kern table
2. `FT_Load_Sfnt_Table('kern')` finds nothing — no kern table exists in any format
3. macOS platform

### Metrics Flag — `lib/font/font_metrics.c`

```c
m->has_kerning = FT_HAS_KERNING(face);
#ifdef __APPLE__
if (!m->has_kerning && handle->ct_font_ref) {
    m->has_kerning = true;
}
#endif
```

`FontMetrics.has_kerning` is the flag checked by layout callers. It's true when either FreeType or CoreText can provide kerning.

### Kerning Lookup — `lib/font/font_glyph.c`

**Primary API** — `font_get_kerning(handle, left_cp, right_cp)`:
1. If `FT_HAS_KERNING`: convert codepoints → glyph indices via `FT_Get_Char_Index`, call `FT_Get_Kerning(FT_KERNING_DEFAULT)`, return `(delta.x / 64.0f) / pixel_ratio`
2. Else on macOS: call `font_get_kerning_coretext()` which checks the hashmap cache first, then falls through to `font_platform_get_pair_kerning()`

**Legacy API** — `font_get_kerning_by_index(handle, left_idx, right_idx)`:
- Takes glyph indices directly, FreeType only, no CoreText fallback. Retained for internal use but no longer called by layout.

**Cache** — per-handle hashmap with xxHash3:
- Key: `(left_cp << 32) | right_cp` packed into 64 bits
- Max 4096 entries; when full, clears entirely and rebuilds (simple eviction)
- Created lazily on first CoreText kerning lookup

### CoreText Measurement — `lib/font/font_platform.c`

`font_platform_get_pair_kerning(ct_font_ref, left_cp, right_cp)`:
1. Encode both codepoints to UTF-16 (with surrogate pairs for codepoints > U+FFFF)
2. Get nominal advance for left glyph via `CTFontGetAdvancesForGlyphs()`
3. Create `CFAttributedString` with both characters, build `CTLineRef`
4. Extract first `CTRunRef`, get actual first-glyph advance via `CTRunGetAdvances()`
5. Return `actual_advance - nominal_advance` (the kerning delta in physical pixels)

`font_platform_create_ct_font(postscript_name, family_name, size_px)`:
- Tries PostScript name first (exact match), falls back to family name
- Creates `CTFontRef` at the specified size

### Handle Cleanup — `lib/font/font_context.c`

In `font_handle_release()`:
- `hashmap_free(handle->kern_cache)` if non-NULL
- `font_platform_destroy_ct_font(handle->ct_font_ref)` (→ `CFRelease`) if non-NULL

## Layout Integration

### Text Layout — `radiant/layout_text.cpp`

Kerning state is tracked per line box (`Linebox` in `layout.hpp`):
- `uint32_t prev_codepoint` — reset to 0 at each new line

Per-character loop:
```c
if (lycon->font.style->has_kerning) {
    if (lycon->line.prev_codepoint) {
        float kerning_css = font_get_kerning(handle, lycon->line.prev_codepoint, codepoint);
        if (kerning_css != 0.0f) {
            // first char in rect: shift x position
            // subsequent chars: adjust rect width
        }
    }
    lycon->line.prev_codepoint = codepoint;
}
```

- Kerning is applied **between** adjacent characters on the same line
- At line breaks, `prev_codepoint` resets — no cross-line kerning
- The kerning value adjusts either the rect's x (first character) or width (subsequent)

### Intrinsic Sizing — `radiant/intrinsic_sizing.cpp`

Three measurement functions follow the same pattern:
- `measure_text_intrinsic_widths()` — min-content / max-content width
- `estimate_text_line_count()` — line count estimation
- Space character handling in word-break logic

All use `prev_codepoint` tracking and `font_get_kerning()`:
```c
if (has_kerning && prev_codepoint) {
    kerning = font_get_kerning(handle, prev_codepoint, codepoint);
}
```

## Design Decisions

### Why not use CoreText for all fonts?

CoreText reads both kern and GPOS tables natively, and often provides more complete kerning coverage. However:

1. **Baseline stability**: Enabling CoreText for fonts that previously had no kerning in Radiant changes text widths, which changes line wrapping, which cascades to element positions. This would regress existing baseline tests.

2. **AAT kern format gap**: Some macOS system fonts (e.g., Times.ttc) have kern tables in Apple AAT format that FreeType cannot read. These fonts report `FT_HAS_KERNING=false` even though kern data exists. Enabling CoreText for these would add kerning that wasn't there before.

3. **Advance differences**: FreeType and CoreText compute different glyph advances for the same font at the same size (~4% difference at correct optical size). Mixing CoreText kerning with FreeType advances would compound measurement errors.

CoreText is only enabled for fonts that have **no kern table in any format** — specifically fonts designed for OpenType GPOS-only kerning, like Apple's System Font (SFNS.ttf / SF Pro).

### Why codepoint-based API instead of glyph indices?

The previous approach used `font_get_kerning_by_index()` with a secondary codepoint-based fallback. This two-step pattern was error-prone:
- Callers had to manage both `prev_glyph_index` and `prev_codepoint`
- The fallback path could accidentally apply CoreText kerning to fonts that should only use FreeType

The unified `font_get_kerning()` API takes codepoints and handles index conversion internally. It routes to the correct backend based on `FT_HAS_KERNING`, making the caller code simpler and the backend selection deterministic.

### Cache sizing

The 4096-entry limit was chosen empirically. At ~640K pair lookups/sec via CoreText, the cache ensures most repeated pairs hit the hashmap. The clear-on-full strategy is acceptable because:
- Most documents use a small set of character pairs
- 4096 entries covers typical paragraph text comfortably
- Cache rebuilds are cheap relative to layout cost
