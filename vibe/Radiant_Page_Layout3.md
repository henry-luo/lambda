# Radiant Page Layout Enhancement Proposal

## Overview

This document outlines enhancements to Radiant's CSS layout engine, with a primary focus on correcting the `line-height: normal` computation to match browser behavior (Chrome Blink).

---

## Priority 1: Default Line-Height Computation ✅ COMPLETED

### Problem Statement (Original)

Radiant's original `calc_normal_line_height()` implementation differed from browser behavior in several key ways:

1. **Synthesized a 0.1em line gap** when fonts have no native line gap (browsers use 0)
2. **Used float division** instead of rounding each component separately
3. **Did not read OS/2 sTypo\* values** directly from font tables (relied solely on FreeType's computed metrics)

### Implementation Status

**Status:** ✅ **COMPLETED** - All 294 baseline layout tests now pass.

### Final Implementation

**File:** `radiant/layout.cpp`

The implementation follows Chrome's behavior with a key insight: Chrome checks the **USE_TYPO_METRICS** flag (fsSelection bit 7) to decide which metrics to use.

```cpp
// Structure for OS/2 sTypo metrics (defined in layout.hpp)
struct TypoMetrics {
    float ascender;      // sTypoAscender in CSS pixels
    float descender;     // sTypoDescender in CSS pixels (positive value)
    float line_gap;      // sTypoLineGap in CSS pixels (floored at 0)
    bool valid;
    bool use_typo_metrics;  // fsSelection bit 7
};

// Read OS/2 table metrics using FreeType
TypoMetrics get_os2_typo_metrics(FT_Face face) {
    TypoMetrics result = {0, 0, 0, false, false};

    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (!os2) {
        return result;  // No OS/2 table available
    }

    float scale = (float)face->size->metrics.y_ppem / face->units_per_EM;
    result.use_typo_metrics = (os2->fsSelection & 0x0080) != 0;  // Bit 7
    result.ascender = os2->sTypoAscender * scale;
    result.descender = -os2->sTypoDescender * scale;  // Make positive
    result.line_gap = (os2->sTypoLineGap > 0) ? (os2->sTypoLineGap * scale) : 0.0f;
    result.valid = true;
    return result;
}

// Calculate normal line height following Chrome's algorithm
float calc_normal_line_height(FT_Face face) {
    TypoMetrics typo = get_os2_typo_metrics(face);

    // Only use sTypo metrics when USE_TYPO_METRICS flag is set (fsSelection bit 7)
    if (typo.valid && typo.use_typo_metrics) {
        // Chrome formula: round each component separately, then sum
        long asc_r = lroundf(typo.ascender);
        long desc_r = lroundf(typo.descender);
        long gap_r = lroundf(typo.line_gap);
        return (float)(asc_r + desc_r + gap_r);
    }

    // Fallback: Use FreeType's height directly (HHEA-based)
    // This matches Chrome's behavior for fonts without USE_TYPO_METRICS
    float ft_height = face->size->metrics.height / 64.0f;
    return roundf(ft_height);
}
```

### Key Implementation Insights

1. **USE_TYPO_METRICS Flag is Critical**: Chrome only uses sTypo metrics when the font explicitly sets fsSelection bit 7. Most common fonts (Times New Roman, Arial on some systems) do NOT have this flag set.

2. **FreeType's `height` Field**: When USE_TYPO_METRICS is not set, Chrome uses the HHEA metrics. FreeType's `face->size->metrics.height` provides this value directly, which may differ from `ascender + descender` for some fonts.

3. **Why Not `ascender + descender`?**: Some fonts have inconsistent HHEA values where `height ≠ ascender + descender`. For example, Times New Roman at 16px:
   - `ascender` = 15, `descender` = 4, sum = 19
   - `height` = 18 (what Chrome uses)

   Using `height` directly matches browser behavior.

### Rounding Rationale

Rounding is needed because:
1. **Font metrics are in font design units** and become fractional when scaled to CSS pixels
2. **CSS line-height: normal produces integer values** in all browsers
3. **Chrome rounds each component separately** for sTypo metrics, but uses the already-rounded `height` field for HHEA

### Test Results

| Test Suite | Before | After |
|------------|--------|-------|
| Baseline (294 tests) | 290 pass | **294 pass (100%)** |

### Files Modified

- `radiant/layout.hpp` - Added `TypoMetrics` struct and function declarations
- `radiant/layout.cpp` - Implemented `get_os2_typo_metrics()` and updated `calc_normal_line_height()`
- `radiant/layout_block.cpp` - Updated `init_ascender`/`init_descender` to use appropriate metrics
- `radiant/layout_text.cpp` - Updated `max_ascender`/`max_descender` in `output_text()`

---

### CSS Specification Reference

**Source:** [CSS Inline Layout Module Level 3 - §5.1 Line Height](https://www.w3.org/TR/css-inline-3/#line-height-property)

> **`normal`**
>
> Indicates that the UA should choose an "appropriate" value based on the font metrics. It is recommended that the line-height be set based on the `sTypoAscender`, `sTypoDescender`, and `sTypoLineGap` metrics from the OS/2 table of the primary font (or equivalent metrics from other font formats), with line gap floored at zero.

**Key points:**
1. Prefer OS/2 table metrics: `sTypoAscender`, `sTypoDescender`, `sTypoLineGap`
2. Line gap must be **floored at zero** (not synthesized)
3. Fallback to HHEA metrics if OS/2 unavailable

---

### Chrome Blink Implementation Reference

#### Source Files Analyzed:
- `third_party/blink/renderer/platform/fonts/simple_font_data.cc`
- `third_party/blink/renderer/platform/fonts/font_metrics.h`
- `third_party/blink/renderer/core/css/resolver/style_builder_converter.cc`

#### Line Spacing Calculation (simple_font_data.cc, lines 168-176)

```cpp
void SimpleFontData::PlatformInit(
    bool subpixel_ascent_descent,
    const FontPlatformData& platform_data) {
  // ...

  SkFontMetrics metrics;
  paint.refTypeface()->getMetrics(&metrics);

  float ascent = SkScalarRoundToScalar(-metrics.fAscent);
  float descent = SkScalarRoundToScalar(metrics.fDescent);
  float line_gap = SkScalarRoundToScalar(metrics.fLeading);

  font_metrics_.SetLineSpacing(
      lroundf(ascent) + lroundf(descent) + lroundf(line_gap));
  // ...
}
```

**Chrome's formula:**
```
line_spacing = round(ascent) + round(descent) + round(line_gap)
```

#### Reading OS/2 Typo Metrics (simple_font_data.cc, lines 361-374)

```cpp
std::optional<std::pair<float, float>> SimpleFontData::TypoAscenderAndDescender()
    const {
  sk_sp<SkTypeface> typeface = platform_data_.Typeface();
  constexpr size_t kOs2Offset = 68;  // sTypoAscender offset in OS/2 table
  int16_t typo_ascender_raw, typo_descender_raw;
  size_t bytes_read = typeface->getTableData(
      SkSetFourByteTag('O', 'S', '/', '2'), kOs2Offset, sizeof(int16_t) * 2,
      &typo_ascender_raw);
  if (bytes_read != sizeof(int16_t) * 2) {
    return {};
  }
  // Convert big-endian to host byte order
  int16_t typo_ascender = base::numerics::ByteSwapToLE16(typo_ascender_raw);
  int16_t typo_descender = base::numerics::ByteSwapToLE16(typo_descender_raw);

  float upem = typeface->getUnitsPerEm();
  return std::make_pair(typo_ascender / upem, typo_descender / upem);
}
```

#### Skia's fLeading (Line Gap)

Chrome uses Skia's `SkFontMetrics.fLeading` which contains:
- **HHEA lineGap** (default)
- **OS/2 sTypoLineGap** if `USE_TYPO_METRICS` flag (fsSelection bit 7) is set

#### ConvertLineHeight for "normal" (style_builder_converter.cc, lines 2108-2143)

```cpp
Length StyleBuilderConverter::ConvertLineHeight(
    StyleResolverState& state,
    const CSSValue& value) {
  // ...
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    if (identifier_value->GetValueID() == CSSValueID::kNormal) {
      return ComputedStyleInitialValues::InitialLineHeight();
    }
  }
  // ...
}
```

Chrome returns a special "normal" sentinel value, then resolves it during layout using the formula above.

---

### MDN Documentation Reference

**Source:** [MDN - line-height](https://developer.mozilla.org/en-US/docs/Web/CSS/line-height)

> **`normal`**
>
> Depends on the user agent. Desktop browsers (including Firefox) use a default value of roughly **1.2**, depending on the element's `font-family`.

**Note:** The "1.2" is a rough approximation. The actual value comes from font metrics, not a fixed multiplier. Different fonts produce different "normal" line heights.

---

### Proposed Fix for Radiant

#### Step 1: Read OS/2 Table Metrics via FreeType

```cpp
#include <freetype/tttables.h>

struct TypoMetrics {
    float ascender;      // sTypoAscender in CSS pixels
    float descender;     // sTypoDescender in CSS pixels (positive value)
    float line_gap;      // sTypoLineGap in CSS pixels
    bool valid;
};

TypoMetrics get_os2_typo_metrics(FT_Face face) {
    TypoMetrics result = {0, 0, 0, false};

    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (!os2) {
        return result;  // No OS/2 table available
    }

    // Convert from font units to CSS pixels
    float scale = (float)face->size->metrics.y_ppem / face->units_per_EM;

    result.ascender = os2->sTypoAscender * scale;
    result.descender = -os2->sTypoDescender * scale;  // Make positive
    result.line_gap = std::max(0, (int)os2->sTypoLineGap) * scale;  // Floor at 0
    result.valid = true;

    return result;
}
```

#### Step 2: Update calc_normal_line_height()

```cpp
float calc_normal_line_height(FT_Face face) {
    // Try OS/2 sTypo* metrics first (CSS spec preferred)
    TypoMetrics typo = get_os2_typo_metrics(face);

    if (typo.valid) {
        // Chrome formula: round each component, then sum
        return lroundf(typo.ascender) + lroundf(typo.descender) + lroundf(typo.line_gap);
    }

    // Fallback: use FreeType's computed metrics (HHEA-based)
    FT_Size_Metrics& metrics = face->size->metrics;
    float ascender = metrics.ascender / 64.0f;
    float descender = -metrics.descender / 64.0f;

    // Do NOT synthesize line gap - CSS spec says floor to 0
    float line_gap = 0.0f;
    float ft_height = metrics.height / 64.0f;
    if (ft_height > ascender + descender) {
        line_gap = ft_height - ascender - descender;
    }

    return lroundf(ascender) + lroundf(descender) + lroundf(line_gap);
}
```

#### Step 3: Add USE_TYPO_METRICS Flag Check (Optional Enhancement)

Some fonts set `fsSelection` bit 7 to indicate that typo metrics should be used for line spacing:

```cpp
bool use_typo_metrics(FT_Face face) {
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (!os2 || os2->version < 4) {
        return false;
    }
    return (os2->fsSelection & (1 << 7)) != 0;  // Bit 7: USE_TYPO_METRICS
}
```

---

### Implementation Checklist

- [x] Add `get_os2_typo_metrics()` function to read OS/2 table ✅
- [x] Update `calc_normal_line_height()` to match Chrome's algorithm ✅
- [x] Remove 0.1em synthesized line gap ✅
- [x] Implement USE_TYPO_METRICS flag check (fsSelection bit 7) ✅
- [x] Use FreeType's `height` field directly for HHEA fallback ✅
- [x] Update `init_ascender`/`init_descender` for consistent metrics ✅
- [x] Update `max_ascender`/`max_descender` in text output ✅
- [x] All 294 baseline layout tests passing ✅

---

### Testing Strategy

1. **Create test fonts** with known OS/2 metrics
2. **Browser comparison tests:**
   - Render same text in Chrome, capture line box height
   - Render same text in Radiant, compare
3. **Edge cases:**
   - Fonts with no OS/2 table (use HHEA fallback)
   - Fonts with negative or zero sTypoLineGap
   - Fonts with USE_TYPO_METRICS flag set/unset
4. **Existing layout tests:** Ensure no regressions in `test/layout/`

---

## Priority 2: Future Layout Enhancements

### 2.1 Text Baseline Alignment

- Implement proper `vertical-align` for inline elements
- Support `baseline`, `sub`, `super`, `top`, `middle`, `bottom`

### 2.2 First/Last Baseline Sets

CSS Inline Level 3 defines "first baseline set" and "last baseline set" for:
- Multi-line flex items
- Grid items spanning multiple rows
- Inline-block alignment

### 2.3 Leading Distribution

CSS Inline Level 3 `text-box-trim` and `text-box-edge` properties for precise control over:
- Leading trimming above first line
- Leading trimming below last line
- Aligning text to cap height or x-height

### 2.4 Line Grid Alignment

Support for `line-grid` and `line-snap` properties to align baselines to a document-wide grid.

---

## Summary

| Priority | Enhancement | Status |
|----------|-------------|--------|
| **1** | Fix `line-height: normal` computation | ✅ **COMPLETED** |
| 2 | Baseline alignment (`vertical-align`) | Future |
| 3 | First/last baseline sets | Future |
| 4 | Leading distribution (`text-box-trim`) | Future |
| 5 | Line grid alignment | Future |

Priority 1 is complete. The `line-height: normal` computation now matches Chrome's behavior, and all 294 baseline layout tests pass.
