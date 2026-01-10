// tex_radiant_font.cpp - Font Provider implementation using Radiant's FreeType infrastructure
//
// Bridges the TeX typesetting engine with Radiant's font loading and rendering system.

#include "tex_radiant_font.hpp"
#include "../../radiant/font.h"
#include "../../radiant/view.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/memtrack.h"

#include <string.h>
#include <math.h>

namespace tex {

// ============================================================================
// RadiantFontProvider Implementation
// ============================================================================

// Cache key structure for font metrics
struct MetricsCacheKey {
    const char* family;
    float size;
    bool bold;
    bool italic;
};

static int metrics_cache_compare(const void* a, const void* b, void* udata) {
    const MetricsCacheKey* ka = (const MetricsCacheKey*)a;
    const MetricsCacheKey* kb = (const MetricsCacheKey*)b;
    int cmp = strcmp(ka->family, kb->family);
    if (cmp != 0) return cmp;
    if (ka->size != kb->size) return (ka->size < kb->size) ? -1 : 1;
    if (ka->bold != kb->bold) return ka->bold ? 1 : -1;
    if (ka->italic != kb->italic) return ka->italic ? 1 : -1;
    return 0;
}

static uint64_t metrics_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const MetricsCacheKey* key = (const MetricsCacheKey*)item;
    // hash the family + size + flags
    uint32_t h = 0;
    const char* s = key->family;
    while (*s) h = h * 31 + *s++;
    h = h * 31 + (uint32_t)(key->size * 100);
    h = h * 31 + (key->bold ? 1 : 0);
    h = h * 31 + (key->italic ? 2 : 0);
    return h;
}

struct MetricsCacheEntry {
    MetricsCacheKey key;
    FontMetrics* metrics;
};

RadiantFontProvider::RadiantFontProvider(UiContext* uicon, Arena* arena)
    : uicon_(uicon)
    , arena_(arena)
    , math_font_family_("Latin Modern Math")
    , text_font_family_("Latin Modern Roman")
    , metrics_cache_(nullptr)
    , script_ratio_(0.7f)          // TeX default: 70% of base size for scripts
    , scriptscript_ratio_(0.5f)    // TeX default: 50% of base size for scriptscripts
{
    metrics_cache_ = hashmap_new(sizeof(MetricsCacheEntry), 32, 0, 0,
        metrics_cache_hash, metrics_cache_compare, nullptr, nullptr);
}

const FontMetrics* RadiantFontProvider::get_font(
    FontFamily family,
    bool bold,
    bool italic,
    float size_pt
) {
    // Map FontFamily to font name
    const char* family_name = nullptr;
    switch (family) {
        case FontFamily::Roman:
            family_name = text_font_family_;
            break;
        case FontFamily::Italic:
            family_name = text_font_family_;
            italic = true;
            break;
        case FontFamily::Bold:
            family_name = text_font_family_;
            bold = true;
            break;
        case FontFamily::BoldItalic:
            family_name = text_font_family_;
            bold = true;
            italic = true;
            break;
        case FontFamily::Typewriter:
            family_name = "Latin Modern Mono";
            break;
        case FontFamily::MathItalic:
            family_name = math_font_family_;
            italic = true;
            break;
        case FontFamily::MathSymbol:
            family_name = math_font_family_;
            break;
        case FontFamily::MathExtension:
            family_name = math_font_family_;
            break;
        default:
            family_name = text_font_family_;
            break;
    }

    // Check cache
    MetricsCacheKey key = { family_name, size_pt, bold, italic };
    MetricsCacheEntry* entry = (MetricsCacheEntry*)hashmap_get(metrics_cache_, &key);
    if (entry) {
        return entry->metrics;
    }

    // Load font face
    FT_Face face = load_face(family_name, bold, italic, size_pt);
    if (!face) {
        // fallback to default font
        face = load_face("serif", bold, italic, size_pt);
        if (!face) {
            log_error("TeX font: failed to load any font for family=%s size=%.1f", family_name, size_pt);
            return nullptr;
        }
    }

    // Create font metrics
    FontMetrics::Type metrics_type;
    switch (family) {
        case FontFamily::MathSymbol:
            metrics_type = FontMetrics::Type::MathSymbol;
            break;
        case FontFamily::MathExtension:
            metrics_type = FontMetrics::Type::MathExtension;
            break;
        case FontFamily::MathItalic:
            metrics_type = FontMetrics::Type::MathItalic;
            break;
        default:
            metrics_type = FontMetrics::Type::Text;
            break;
    }

    FontMetrics* metrics = create_font_metrics(face, size_pt, metrics_type);

    // Cache the result
    char* cached_family = (char*)arena_alloc(arena_, strlen(family_name) + 1);
    strcpy(cached_family, family_name);
    MetricsCacheEntry new_entry = { { cached_family, size_pt, bold, italic }, metrics };
    hashmap_set(metrics_cache_, &new_entry);

    return metrics;
}

const FontMetrics* RadiantFontProvider::get_math_symbol_font(float size_pt) {
    return get_font(FontFamily::MathSymbol, false, false, size_pt);
}

const FontMetrics* RadiantFontProvider::get_math_extension_font(float size_pt) {
    return get_font(FontFamily::MathExtension, false, false, size_pt);
}

const FontMetrics* RadiantFontProvider::get_math_text_font(float size_pt, bool italic) {
    return get_font(italic ? FontFamily::MathItalic : FontFamily::Roman, false, false, size_pt);
}

float RadiantFontProvider::style_size(MathStyle style, float base_size) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayCramped:
        case MathStyle::Text:
        case MathStyle::TextCramped:
            return base_size;

        case MathStyle::Script:
        case MathStyle::ScriptCramped:
            return base_size * script_ratio_;

        case MathStyle::Scriptscript:
        case MathStyle::ScriptscriptCramped:
            return base_size * scriptscript_ratio_;

        default:
            return base_size;
    }
}

// ============================================================================
// Glyph-level queries
// ============================================================================

GlyphMetrics RadiantFontProvider::get_glyph_metrics(uint32_t codepoint, float size_pt, FontFamily family) {
    GlyphMetrics result = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    FT_Face face = get_ft_face(family, false, false, size_pt);
    if (!face) return result;

    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) return result;

    // load glyph metrics (no rendering)
    FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
    if (error) return result;

    FT_GlyphSlot slot = face->glyph;
    // FreeType metrics are in 26.6 fixed-point format
    result.width = slot->metrics.width / 64.0f;
    result.height = slot->metrics.height / 64.0f;
    result.depth = 0;  // computed below if we have bearing info
    result.advance = slot->metrics.horiAdvance / 64.0f;
    result.italic_correction = 0;  // would need additional OpenType table parsing

    // height = bearingY (ascent above baseline)
    // depth = height - bearingY (descent below baseline)
    float bearing_y = slot->metrics.horiBearingY / 64.0f;
    result.height = bearing_y;
    result.depth = (slot->metrics.height / 64.0f) - bearing_y;

    return result;
}

float RadiantFontProvider::get_kerning(uint32_t left, uint32_t right, float size_pt, FontFamily family) {
    FT_Face face = get_ft_face(family, false, false, size_pt);
    if (!face || !FT_HAS_KERNING(face)) return 0.0f;

    FT_UInt left_index = FT_Get_Char_Index(face, left);
    FT_UInt right_index = FT_Get_Char_Index(face, right);
    if (left_index == 0 || right_index == 0) return 0.0f;

    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(face, left_index, right_index, FT_KERNING_DEFAULT, &kerning);
    if (error) return 0.0f;

    return kerning.x / 64.0f;
}

bool RadiantFontProvider::has_glyph(uint32_t codepoint, FontFamily family) {
    FT_Face face = get_ft_face(family, false, false, 12.0f);  // size doesn't matter for this check
    if (!face) return false;
    return FT_Get_Char_Index(face, codepoint) != 0;
}

FT_Face RadiantFontProvider::get_ft_face(FontFamily family, bool bold, bool italic, float size_pt) {
    const char* family_name = nullptr;
    switch (family) {
        case FontFamily::Roman:
        case FontFamily::Italic:
        case FontFamily::Bold:
        case FontFamily::BoldItalic:
            family_name = text_font_family_;
            break;
        case FontFamily::Typewriter:
            family_name = "Latin Modern Mono";
            break;
        case FontFamily::MathItalic:
        case FontFamily::MathSymbol:
        case FontFamily::MathExtension:
            family_name = math_font_family_;
            break;
        default:
            family_name = text_font_family_;
            break;
    }

    return load_face(family_name, bold, italic, size_pt);
}

// ============================================================================
// Math-specific queries
// ============================================================================

SizedDelimiter RadiantFontProvider::get_sized_delimiter(
    uint32_t codepoint,
    float target_height,
    MathStyle style
) {
    SizedDelimiter result;
    result.glyph_codepoint = codepoint;
    result.is_extensible = false;
    result.repeat_count = 0;

    // get base size for this style
    float base_size = 10.0f;  // default
    float size = style_size(style, base_size);

    FT_Face face = get_ft_face(FontFamily::MathExtension, false, false, size);
    if (!face) {
        // return base glyph with measured size
        GlyphMetrics gm = get_glyph_metrics(codepoint, size, FontFamily::MathExtension);
        result.height = gm.height;
        result.depth = gm.depth;
        result.width = gm.width;
        result.advance = gm.advance;
        return result;
    }

    // Try to find a size variant that fits
    // OpenType MATH table has vertical glyph variants; we try progressively larger sizes
    float current_height = 0.0f;
    float best_size = size;

    for (int scale = 1; scale <= 5; scale++) {
        float try_size = size * (1.0f + scale * 0.25f);
        GlyphMetrics gm = get_glyph_metrics(codepoint, try_size, FontFamily::MathExtension);
        float total_height = gm.height + gm.depth;
        if (total_height >= target_height || scale == 5) {
            result.height = gm.height;
            result.depth = gm.depth;
            result.width = gm.width;
            result.advance = gm.advance;
            best_size = try_size;
            break;
        }
        current_height = total_height;
    }

    // if still too small, mark as extensible (would need assembly)
    if (current_height < target_height * 0.9f) {
        result.is_extensible = true;
        // for extensible delimiters, we'd build from pieces
        // This is a simplified version; full implementation would use OpenType MATH table
        result.repeat_count = (int)ceilf((target_height - current_height) / (current_height * 0.5f));
    }

    return result;
}

RadicalData RadiantFontProvider::get_radical_data(float radicand_height, MathStyle style) {
    RadicalData result;

    // square root codepoint
    uint32_t sqrt_codepoint = 0x221A;  // √
    float size = style_size(style, 10.0f);

    GlyphMetrics gm = get_glyph_metrics(sqrt_codepoint, size, FontFamily::MathExtension);

    result.surd_width = gm.width;
    result.surd_height = gm.height + gm.depth;
    result.rule_thickness = size * 0.04f;  // roughly 0.4pt at 10pt
    result.rule_kern = size * 0.02f;       // gap above rule
    result.degree_kern = size * -0.5f;     // kern before degree
    result.has_degree = false;

    return result;
}

float RadiantFontProvider::get_accent_attachment(uint32_t base_codepoint, float size_pt) {
    // accent attachment point is typically the center of the base glyph
    GlyphMetrics gm = get_glyph_metrics(base_codepoint, size_pt, FontFamily::MathItalic);
    return gm.width / 2.0f;
}

// ============================================================================
// Font configuration
// ============================================================================

void RadiantFontProvider::set_math_font(const char* family_name) {
    math_font_family_ = family_name;
}

void RadiantFontProvider::set_text_font(const char* family_name) {
    text_font_family_ = family_name;
}

// ============================================================================
// Internal helpers
// ============================================================================

FT_Face RadiantFontProvider::load_face(const char* family, bool bold, bool italic, float size_pt) {
    // create FontProp for styled font loading
    FontProp style;
    memset(&style, 0, sizeof(style));
    style.font_size = size_pt;
    style.font_weight = bold ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
    style.font_style = italic ? CSS_VALUE_ITALIC : CSS_VALUE_NORMAL;

    return load_styled_font(uicon_, family, &style);
}

FontMetrics* RadiantFontProvider::create_font_metrics(FT_Face face, float size_pt, FontMetrics::Type type) {
    FontMetrics* metrics = (FontMetrics*)arena_alloc(arena_, sizeof(FontMetrics));
    memset(metrics, 0, sizeof(FontMetrics));

    if (!face) return metrics;

    // FreeType metrics are in 26.6 fixed-point
    float scale = face->size->metrics.y_ppem / (float)face->units_per_EM;

    metrics->type = type;
    metrics->size = size_pt;
    metrics->em = size_pt;
    metrics->ex = face->size->metrics.height / 64.0f * 0.5f;  // approximation

    // get x-height from 'x' glyph if possible
    FT_UInt x_index = FT_Get_Char_Index(face, 'x');
    if (x_index && FT_Load_Glyph(face, x_index, FT_LOAD_NO_BITMAP) == 0) {
        metrics->ex = face->glyph->metrics.height / 64.0f;
    }

    // basic line metrics
    metrics->ascender = face->size->metrics.ascender / 64.0f;
    metrics->descender = -face->size->metrics.descender / 64.0f;  // FreeType descender is negative
    metrics->line_height = face->size->metrics.height / 64.0f;

    // TeX-specific math parameters (derived from font metrics or defaults)
    metrics->axis_height = metrics->ex * 0.5f;  // roughly half x-height
    metrics->rule_thickness = size_pt * 0.04f;  // 0.4pt at 10pt

    // math symbol parameters (from TeX)
    metrics->symbol_params.axis_height = metrics->axis_height;
    metrics->symbol_params.default_rule_thickness = metrics->rule_thickness;
    metrics->symbol_params.x_height = metrics->ex;
    metrics->symbol_params.quad = size_pt;  // 1em
    metrics->symbol_params.num1 = metrics->axis_height + 0.5f * metrics->rule_thickness + 0.25f * metrics->ex;
    metrics->symbol_params.num2 = metrics->axis_height + 0.5f * metrics->rule_thickness;
    metrics->symbol_params.num3 = metrics->axis_height + 0.5f * metrics->rule_thickness;
    metrics->symbol_params.denom1 = metrics->axis_height - 0.5f * metrics->rule_thickness + 0.25f * metrics->ex;
    metrics->symbol_params.denom2 = metrics->axis_height - 0.5f * metrics->rule_thickness;
    metrics->symbol_params.sup1 = metrics->ex * 0.7f;
    metrics->symbol_params.sup2 = metrics->ex * 0.6f;
    metrics->symbol_params.sup3 = metrics->ex * 0.5f;
    metrics->symbol_params.sub1 = metrics->ex * 0.25f;
    metrics->symbol_params.sub2 = metrics->ex * 0.3f;
    metrics->symbol_params.sup_drop = metrics->ex * 0.25f;
    metrics->symbol_params.sub_drop = metrics->ex * 0.05f;
    metrics->symbol_params.delim1 = 2.39f * metrics->ex;
    metrics->symbol_params.delim2 = 1.01f * metrics->ex;

    // math extension parameters
    metrics->extension_params.big_op_spacing1 = metrics->rule_thickness * 3;
    metrics->extension_params.big_op_spacing2 = metrics->rule_thickness * 3;
    metrics->extension_params.big_op_spacing3 = metrics->rule_thickness * 2;
    metrics->extension_params.big_op_spacing4 = metrics->rule_thickness * 5;
    metrics->extension_params.big_op_spacing5 = metrics->rule_thickness;

    // populate glyph widths for common characters
    populate_glyph_metrics(metrics, face);

    return metrics;
}

void RadiantFontProvider::populate_glyph_metrics(FontMetrics* metrics, FT_Face face) {
    // populate widths for ASCII range and common math symbols
    for (int i = 32; i < 127; i++) {
        FT_UInt index = FT_Get_Char_Index(face, i);
        if (index && FT_Load_Glyph(face, index, FT_LOAD_NO_BITMAP) == 0) {
            metrics->char_widths[i - 32] = face->glyph->metrics.horiAdvance / 64.0f;
        }
    }

    // get space width
    FT_UInt space_index = FT_Get_Char_Index(face, ' ');
    if (space_index && FT_Load_Glyph(face, space_index, FT_LOAD_NO_BITMAP) == 0) {
        metrics->space_width = face->glyph->metrics.horiAdvance / 64.0f;
    } else {
        metrics->space_width = metrics->em * 0.25f;  // fallback
    }
}

// ============================================================================
// OpenType MATH table reading
// ============================================================================

// Attempt to read MATH constants from OpenType font
// Returns true if MATH table exists and was successfully read
bool read_math_constants(FT_Face face, MathConstants* out) {
    if (!face || !out) return false;

    // Check if font has MATH table
    FT_ULong length = 0;
    FT_Error error = FT_Load_Sfnt_Table(face, FT_MAKE_TAG('M','A','T','H'), 0, nullptr, &length);
    if (error || length == 0) {
        log_debug("TeX font: No MATH table in font %s", face->family_name);
        return false;
    }

    // For now, return defaults - full MATH table parsing is complex
    // A complete implementation would parse the MathConstants subtable
    *out = default_math_constants();
    return false;  // indicate we didn't actually read from font
}

// ============================================================================
// Default Math Constants (TeX values)
// ============================================================================

MathConstants default_math_constants() {
    MathConstants mc;
    memset(&mc, 0, sizeof(mc));

    // Values based on TeX defaults at 10pt
    // These are in font design units (typically per 1000 units = 1em)
    const int em = 1000;

    mc.scriptPercentScaleDown = 70;  // 70%
    mc.scriptScriptPercentScaleDown = 50;  // 50%
    mc.delimitedSubFormulaMinHeight = 240;
    mc.displayOperatorMinHeight = 240;

    // Radicals
    mc.radicalVerticalGap = 54;
    mc.radicalDisplayStyleVerticalGap = 108;
    mc.radicalRuleThickness = 40;
    mc.radicalExtraAscender = 40;
    mc.radicalKernBeforeDegree = -556;
    mc.radicalKernAfterDegree = -500;
    mc.radicalDegreeBottomRaisePercent = 60;

    // Fractions
    mc.fractionNumeratorShiftUp = 676;
    mc.fractionNumeratorDisplayStyleShiftUp = 676;
    mc.fractionDenominatorShiftDown = 686;
    mc.fractionDenominatorDisplayStyleShiftDown = 686;
    mc.fractionNumeratorGapMin = 40;
    mc.fractionNumDisplayStyleGapMin = 60;
    mc.fractionDenominatorGapMin = 40;
    mc.fractionDenomDisplayStyleGapMin = 60;
    mc.fractionRuleThickness = 40;

    // Sub/superscripts
    mc.superscriptShiftUp = 413;
    mc.superscriptShiftUpCramped = 413;
    mc.subscriptShiftDown = 150;
    mc.superscriptBaselineDropMax = 386;
    mc.subscriptBaselineDropMin = 50;
    mc.subscriptTopMax = 400;
    mc.superscriptBottomMin = 110;
    mc.subSuperscriptGapMin = 128;
    mc.superscriptBottomMaxWithSubscript = 400;

    // Limits
    mc.upperLimitGapMin = 90;
    mc.upperLimitBaselineRiseMin = 110;
    mc.lowerLimitGapMin = 90;
    mc.lowerLimitBaselineDropMin = 600;

    // Stacks
    mc.stackTopShiftUp = 676;
    mc.stackTopDisplayStyleShiftUp = 676;
    mc.stackBottomShiftDown = 686;
    mc.stackBottomDisplayStyleShiftDown = 686;
    mc.stackGapMin = 128;
    mc.stackDisplayStyleGapMin = 200;

    // Accents
    mc.accentBaseHeight = 450;
    mc.flattenedAccentBaseHeight = 657;

    // Axis
    mc.axisHeight = 250;

    return mc;
}

MathSymbolParams math_constants_to_symbol_params(const MathConstants& mc, float size_pt) {
    MathSymbolParams params;
    float scale = size_pt / 1000.0f;  // convert from design units

    params.axis_height = mc.axisHeight * scale;
    params.default_rule_thickness = mc.fractionRuleThickness * scale;

    // These need to be derived from font x-height, using reasonable defaults
    params.x_height = 430 * scale;
    params.quad = 1000 * scale;  // 1em

    params.num1 = mc.fractionNumeratorDisplayStyleShiftUp * scale;
    params.num2 = mc.fractionNumeratorShiftUp * scale;
    params.num3 = mc.fractionNumeratorShiftUp * scale;
    params.denom1 = mc.fractionDenominatorDisplayStyleShiftDown * scale;
    params.denom2 = mc.fractionDenominatorShiftDown * scale;

    params.sup1 = mc.superscriptShiftUp * scale;
    params.sup2 = mc.superscriptShiftUp * scale;
    params.sup3 = mc.superscriptShiftUpCramped * scale;
    params.sub1 = mc.subscriptShiftDown * scale;
    params.sub2 = mc.subscriptShiftDown * scale;
    params.sup_drop = mc.superscriptBaselineDropMax * scale;
    params.sub_drop = mc.subscriptBaselineDropMin * scale;

    params.delim1 = mc.delimitedSubFormulaMinHeight * scale;
    params.delim2 = mc.delimitedSubFormulaMinHeight * scale * 0.5f;

    return params;
}

MathExtensionParams math_constants_to_extension_params(const MathConstants& mc, float size_pt) {
    MathExtensionParams params;
    float scale = size_pt / 1000.0f;

    params.big_op_spacing1 = mc.upperLimitGapMin * scale;
    params.big_op_spacing2 = mc.lowerLimitGapMin * scale;
    params.big_op_spacing3 = mc.upperLimitBaselineRiseMin * scale;
    params.big_op_spacing4 = mc.lowerLimitBaselineDropMin * scale;
    params.big_op_spacing5 = mc.upperLimitGapMin * scale;

    return params;
}

// ============================================================================
// Delimiter Construction
// ============================================================================

bool get_delimiter_construction(
    FT_Face face,
    uint32_t codepoint,
    bool vertical,
    DelimiterConstruction* out,
    Arena* arena
) {
    if (!face || !out || !arena) return false;

    // Reset output
    memset(out, 0, sizeof(DelimiterConstruction));

    // For now, return false indicating no construction data available
    // A full implementation would parse OpenType MATH MathGlyphConstruction table
    // which contains:
    // - GlyphAssembly: instructions for building extensible glyphs
    // - MathGlyphVariantRecord[]: array of size variants

    return false;
}

} // namespace tex
