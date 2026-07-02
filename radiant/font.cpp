
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "view.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "font_face.h"

#include "../lib/log.h"
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
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0.0f) ? uicon->pixel_ratio : 1.0f;
    LoadedGlyph* glyph = font_load_glyph(handle, style, (uint32_t)' ', false);
    if (glyph && glyph->advance_x > 0.0f) {
        return glyph->advance_x / pixel_ratio;
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
    if (fprop->owns_font_handle && fprop->font_handle) {
        font_handle_release(fprop->font_handle);
    }
    fprop->font_handle = NULL;
    fprop->owns_font_handle = false;
}

static bool font_handle_matches_prop(FontHandle* handle, FontProp* fprop,
                                     FontWeight weight, FontSlant slant) {
    if (!handle || !fprop || !fprop->family) return false;
    const char* handle_family = NULL;
    float handle_size = 0.0f;
    FontWeight handle_weight = FONT_WEIGHT_NORMAL;
    FontSlant handle_slant = FONT_SLANT_NORMAL;
    if (!font_handle_get_style(handle, &handle_family, &handle_size,
                               &handle_weight, &handle_slant)) {
        return false;
    }
    bool family_matches = handle_family && strcmp(handle_family, fprop->family) == 0;
#ifdef __APPLE__
    if (!family_matches && handle_family && fprop->family &&
        (strcasecmp(fprop->family, "system-ui") == 0 ||
         strcasecmp(fprop->family, "-apple-system") == 0 ||
         strcasecmp(fprop->family, "BlinkMacSystemFont") == 0)) {
        // CoreText reports the resolved macOS system face as "System Font";
        // treating that as stale made event reflows retain replacement handles.
        family_matches = strcasecmp(handle_family, "System Font") == 0;
    }
#endif
    return family_matches &&
        handle_size == fprop->font_size &&
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
    fbox->style = fprop;
    fbox->current_font_size = fprop->font_size;
    fbox->font_handle = NULL;

    if (!uicon || !uicon->font_ctx) {
        log_error("setup_font: missing UiContext or FontContext");
        return;
    }

    // map CssEnum weight/style → FontWeight/FontSlant
    // CSS 2.1 §15.6: Use numeric weight for precise matching (100-900)
    FontWeight fw = FONT_WEIGHT_NORMAL;
    if (fprop->font_weight_numeric > 0) {
        // Use precise numeric weight from CSS (100-900)
        fw = (FontWeight)fprop->font_weight_numeric;
    } else if (fprop->font_weight == CSS_VALUE_BOLD || fprop->font_weight == CSS_VALUE_BOLDER) {
        fw = FONT_WEIGHT_BOLD;
    } else if (fprop->font_weight == CSS_VALUE_LIGHTER) {
        fw = FONT_WEIGHT_LIGHT;
    }

    FontSlant fs = FONT_SLANT_NORMAL;
    if (fprop->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
    else if (fprop->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

    FontStyleDesc style = {};
    style.family  = fprop->family;
    style.size_px = fprop->font_size;
    style.weight  = fw;
    style.slant   = fs;

    if (font_handle_matches_prop(fprop->font_handle, fprop, fw, fs)) {
        fbox->font_handle = fprop->font_handle;
        populate_font_prop_metrics(uicon, fprop, fprop->font_handle, &style);
        return;
    }

    font_prop_release_handle(fprop);

    // font_resolve handles everything: @font-face descriptors, generic families,
    // database lookup, platform fallback, and fallback font chain — all with caching.
    FontHandle* handle = font_resolve(uicon->font_ctx, &style);
    if (handle) {
        fbox->font_handle = handle;
        fprop->font_handle = handle;
        fprop->owns_font_handle = true;

        // populate FontProp derived fields from unified metrics
        populate_font_prop_metrics(uicon, fprop, handle, &style);
        return;
    }

    log_error("setup_font: font_resolve failed for '%s' (and all fallbacks)", fprop->family);
}

void fontface_cleanup(UiContext* uicon) {
    if (!uicon) return;

    for (int i = 0; i < uicon->font_face_count; i++) {
        FontFaceDescriptor* descriptor = uicon->font_faces ? uicon->font_faces[i] : NULL;
        if (!descriptor) continue;

        if (descriptor->family_name) mem_free(descriptor->family_name);
        if (descriptor->src_local_path) mem_free(descriptor->src_local_path);
        if (descriptor->src_local_name) mem_free(descriptor->src_local_name);
        if (descriptor->src_entries) {
            for (int j = 0; j < descriptor->src_count; j++) {
                if (descriptor->src_entries[j].path) mem_free(descriptor->src_entries[j].path);
                if (descriptor->src_entries[j].format) mem_free(descriptor->src_entries[j].format);
            }
            mem_free(descriptor->src_entries);
        }
        mem_free(descriptor);
    }

    if (uicon->font_faces) mem_free(uicon->font_faces);
    uicon->font_faces = NULL;
    uicon->font_face_count = 0;
    uicon->font_face_capacity = 0;
}
