# Font Reduction — CMU & KaTeX Fonts

## Summary

Reduced `lambda/input/latex/fonts/` from **7.4 MB → 2.9 MB** (61% reduction) by removing unused font families and converting WOFF1 → WOFF2.

## Changes

### 1. Removed 8 Inactive CMU Font Families (−3.5 MB)

These families were commented out in `cmu.css` and never loaded at runtime:

| Directory | Size |
|-----------|------|
| Bright | 748 KB |
| Bright Semibold | 368 KB |
| Classical Serif Italic | 244 KB |
| Concrete | 956 KB |
| Sans Demi-Condensed | 184 KB |
| Typewriter Light | 416 KB |
| Typewriter Variable | 368 KB |
| Upright Italic | 200 KB |

### 2. Converted Active CMU .woff → .woff2 (−30%)

Converted 15 active font files from WOFF1 (zlib) to WOFF2 (Brotli) using `fonttools`:

| Font Family | Files | Before (.woff) | After (.woff2) |
|-------------|-------|----------------|----------------|
| Serif | 4 (regular, bold, italic, bold-italic) | 1.1 MB | 768 KB |
| Serif Slanted | 2 (regular, bold) | 648 KB | 455 KB |
| Sans | 4 (regular, bold, italic, bold-oblique) | 836 KB | 589 KB |
| Typewriter | 4 (regular, bold, italic, bold-italic) | 924 KB | 662 KB |
| Typewriter Slanted | 1 (oblique) | 280 KB | 198 KB |

### 3. Updated CSS Files

Updated all `@font-face` declarations from `format('woff')` → `format('woff2')` in:

- `cmu.css` — also removed commented-out `@import` lines for deleted families
- `cmu-combined.css` — inlined font-face rules used by the layout engine
- `Sans/cmun-sans.css`
- `Serif/cmun-serif.css`
- `Serif Slanted/cmun-serif-slanted.css`
- `Typewriter/cmun-typewriter.css`
- `Typewriter Slanted/cmun-typewriter-slanted.css`

## Before / After (CMU)

| | Files | Size |
|--|-------|------|
| **Before** | 32 .woff + 20 .woff2 + 15 .css | 7.4 MB |
| **After** | 35 .woff2 + 7 .css | 2.9 MB |

### 4. Removed 10 Redundant KaTeX Fonts (−158 KB)

KaTeX ships its own versions of Main, Math, SansSerif, and Typewriter fonts. These duplicate the CMU fonts already loaded and were overridden (or now overridden) by CMU equivalents in `base.css` via `!important`.

| Removed Font | Files | Size | Replaced by |
|---|---|---|---|
| KaTeX_Main (regular, bold, italic, bold-italic) | 4 | 81 KB | Computer Modern Serif |
| KaTeX_Math (italic, bold-italic) | 2 | 32 KB | Computer Modern Serif |
| KaTeX_SansSerif (regular, bold, italic) | 3 | 32 KB | Computer Modern Sans |
| KaTeX_Typewriter (regular) | 1 | 13 KB | Computer Modern Typewriter |

**Kept** (10 files, ~87 KB) — specialty math fonts with no CMU equivalent:
- KaTeX_AMS (27 KB) — blackboard bold `\mathbb`, AMS symbols
- KaTeX_Caligraphic (12 KB) — `\mathcal`
- KaTeX_Fraktur (22 KB) — `\mathfrak`
- KaTeX_Script (9 KB) — `\mathscr`
- KaTeX_Size1–Size4 (17 KB) — large math delimiters `\big`, `\Big`, `\bigg`, `\Bigg`

CSS changes:
- `katex.css` — removed 10 `@font-face` declarations for deleted fonts
- `base.css` — added `!important` override for `.katex .mathtt` / `.katex .texttt` → Computer Modern Typewriter

## Before / After (Overall)

| | Files | Size |
|--|-------|------|
| **Original** | 32 .woff + 20 .woff2 + 15 .css | 7.4 MB |
| **After CMU cleanup** | 35 .woff2 + 7 .css | 2.9 MB |
| **After KaTeX cleanup** | 25 .woff2 + 7 .css | 2.7 MB |

## Runtime Compatibility

The runtime already supported WOFF2 decompression via `font_decompress_woff2()` in `lib/font/font_decompress.cpp` (uses Google's woff2 library bundled in `lib/font/woff2/`). No C++ code changes were needed.
