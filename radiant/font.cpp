
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "view.hpp"
#include "../lambda/input/css/css_value.hpp"

#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/font/font.h"
#include "../lib/memtrack.h"

char* load_font_path(FontContext *font_ctx, const char* font_name) {
    if (!font_ctx || !font_name) {
        log_warn("load_font_path: invalid parameters: font_ctx=%p, font_name=%p", font_ctx, font_name);
        return NULL;
    }

    // Use the unified font module to find the best Regular-weight font file path
    return font_find_path(font_ctx, font_name);
}

static float resolved_space_width(UiContext* uicon, FontHandle* handle, const FontStyleDesc* style) {
    float raster_scale = ui_context_raster_scale(uicon);
    LoadedGlyph* glyph = font_load_glyph(handle, style, (uint32_t)' ', false);
    if (glyph && glyph->advance_x > 0.0f) {
        return glyph->advance_x / raster_scale;
    }

    const FontMetrics* m = font_get_metrics(handle);
    if (m && m->space_width > 0.0f) {
        return m->space_width;
    }

    GlyphInfo sp = font_get_glyph(handle, (uint32_t)' ');
    if (sp.advance_x > 0.0f) {
        return sp.advance_x;
    }

    return 0.0f;
}

void font_prop_release_handle(FontProp* fprop) {
    if (!fprop) return;
    font_cache_unpin_handle(fprop->font_handle);
    fprop->font_handle = NULL;
}

bool css_font_metric_unit_px(FontHandle* handle, const FontStyleDesc* style,
    CssUnit unit, float computed_size, bool upright, float* pixels) {
    if (!pixels || computed_size < 0.0f) return false;
    if (unit == CSS_UNIT_EX) {
        *pixels = computed_size * font_get_x_height_ratio(handle);
        return true;
    }
    if (unit == CSS_UNIT_CAP) {
        const FontMetrics* metrics = handle ? font_get_metrics(handle) : nullptr;
        float size = font_handle_get_size_px(handle);
        // FontMetrics are already CSS pixels; only glyph advances require physical scaling.
        float cap = metrics ? (metrics->cap_height > 0.0f ? metrics->cap_height : metrics->ascender) : 0.0f;
        *pixels = size > 0.0f ? cap * computed_size / size : computed_size;
        return true;
    }
    if (unit != CSS_UNIT_CH && unit != CSS_UNIT_IC) return false;
    float fallback = unit == CSS_UNIT_CH && !upright ? 0.5f : 1.0f;
    *pixels = computed_size * fallback;
    if (handle && style) {
        LoadedGlyph* glyph = font_load_glyph(handle, style,
            unit == CSS_UNIT_CH ? (uint32_t)'0' : 0x6C34, false);
        float physical_size = font_handle_get_physical_size_px(handle);
        float advance = glyph ? (upright ? fabsf(glyph->advance_y) : glyph->advance_x) : 0.0f;
        if (advance > 0.0f && physical_size > 0.0f)
            *pixels = advance * computed_size / physical_size;
    }
    return true;
}

bool css_font_line_height_px(FontContext* fonts, const FontStyleDesc* style,
    const CssValue* line_height, float* pixels) {
    if (!style || !line_height || !pixels) return false;
    if (line_height->type == CSS_VALUE_TYPE_NUMBER) {
        *pixels = line_height->data.number.value * style->size_px;
        return true;
    }
    if (line_height->type == CSS_VALUE_TYPE_LENGTH && line_height->data.length.unit == CSS_UNIT_PX) {
        *pixels = line_height->data.length.value;
        return true;
    }
    if (line_height->type != CSS_VALUE_TYPE_KEYWORD || line_height->data.keyword != CSS_VALUE_NORMAL) return false;
    if (style->size_px == 0.0f) { *pixels = 0.0f; return true; }
    FontHandle* handle = fonts ? font_resolve(fonts, style) : nullptr;
    if (!handle) return false;
    // normal leading uses the computed font; zoom and glyph fallback cannot alter this unit basis.
    *pixels = font_calc_normal_line_height(handle);
    font_handle_release(handle);
    return true;
}

static bool font_handle_matches_prop(FontHandle* handle, FontProp* fprop,
                                     const char* family,
                                     FontWeight weight, FontSlant slant,
                                     float raster_scale) {
    if (!handle || !fprop || !family) return false;
    const char* handle_family = NULL;
    float handle_size = 0.0f;
    FontWeight handle_weight = FONT_WEIGHT_NORMAL;
    FontSlant handle_slant = FONT_SLANT_NORMAL;
    if (!font_handle_get_style(handle, &handle_family, &handle_size,
                               &handle_weight, &handle_slant)) {
        return false;
    }
    bool family_matches = handle_family && strcmp(handle_family, family) == 0;
#ifdef __APPLE__
    if (!family_matches && handle_family &&
        (str_icmp_cstr(family, "system-ui") == 0 ||
         str_icmp_cstr(family, "-apple-system") == 0 ||
         str_icmp_cstr(family, "BlinkMacSystemFont") == 0)) {
        // CoreText reports the resolved macOS system face as "System Font";
        // treating that as stale made event reflows retain replacement handles.
        family_matches = str_icmp_cstr(handle_family, "System Font") == 0;
    }
#endif
    return family_matches &&
        handle_size == font_prop_used_size(fprop) &&
        fabsf(font_handle_get_physical_size_px(handle) -
              font_prop_used_size(fprop) * raster_scale) <= 0.0001f &&
        handle_weight == weight &&
        handle_slant == slant;
}

static void populate_font_prop_metrics(UiContext* uicon, FontProp* fprop,
                                       FontHandle* handle,
                                       const FontStyleDesc* style) {
    if (!fprop || !handle || !style) return;
    const FontMetrics* m = font_get_metrics(handle);
    if (!m) return;

    fprop->space_width = resolved_space_width(uicon, handle, style);
    fprop->average_char_width = m->average_char_width > 0.0f
        ? m->average_char_width : fprop->space_width;
    float lh_asc, lh_desc;
    font_get_normal_lh_split(handle, &lh_asc, &lh_desc);
    fprop->ascender = lh_asc;
    fprop->descender = lh_desc;
    fprop->font_height = m->hhea_line_height;
    fprop->has_kerning = m->has_kerning;
    if (fprop->font_kerning == CSS_VALUE_NONE) {
        fprop->has_kerning = false;
    }
}

void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    fbox->style = lam::up(fprop);
    fbox->current_font_size = font_prop_used_size(fprop);

    if (!uicon || !uicon->font_ctx) {
        log_error("setup_font: missing UiContext or FontContext");
        return;
    }

    // Some intrinsic-layout paths create a FontProp only for a non-family
    // property (for example list-marker spacing). A missing family still has
    // the CSS initial serif value; do not pass an invalid style to the resolver.
    const char* family = fprop->family;
    if ((!family || !family[0]) && uicon) family = uicon->default_font.family;

    FontStyleDesc style = font_style_desc_from_prop(fprop);
    style.family = family;
    FontWeight fw = style.weight;
    FontSlant fs = style.slant;

    float raster_scale = ui_context_raster_scale(uicon);
    if (font_handle_matches_prop(fprop->font_handle, fprop, family, fw, fs,
                                 raster_scale)) {
        populate_font_prop_metrics(uicon, fprop, fprop->font_handle, &style);
        return;
    }

    font_prop_release_handle(fprop);

    // font_resolve handles everything: @font-face descriptors, generic families,
    // database lookup, platform fallback, and fallback font chain — all with caching.
    FontHandle* handle = family ? font_resolve(uicon->font_ctx, &style) : NULL;
    if (handle) {
        fprop->font_handle = lam::counted(handle);
        // Transfer font_resolve's caller ref into the cache-managed alias.
        font_cache_adopt_handle_alias(handle);

        // populate FontProp derived fields from unified metrics
        populate_font_prop_metrics(uicon, fprop, handle, &style);
        return;
    }

    log_error("setup_font: font_resolve failed for '%s' (and all fallbacks)",
              family ? family : "(null)");
}

void fontface_cleanup(UiContext* uicon) {
    if (!uicon) return;

    for (int i = 0; i < uicon->font_face_count; i++) {
        FontFaceDescriptor* descriptor = uicon->font_faces ? uicon->font_faces[i] : NULL;
        if (!descriptor) continue;

        lam::Temp<FontFaceDescriptor> owned(descriptor);  // the registry releases each descriptor
        lam::free_owned(descriptor->family_name);
        lam::free_owned(descriptor->src_local_path);
        lam::free_owned(descriptor->src_local_name);
        if (descriptor->src_entries) {
            for (int j = 0; j < descriptor->src_count; j++) {
                lam::free_owned(descriptor->src_entries[j].path);
                lam::free_owned(descriptor->src_entries[j].format);
            }
            lam::free_owned(descriptor->src_entries);
        }
        lam::free_owned(descriptor->unicode_ranges);
    }

    lam::free_owned(uicon->font_faces);
    uicon->font_face_count = 0;
    uicon->font_face_capacity = 0;
}
