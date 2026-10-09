#include "view.hpp"
#include "layout.hpp"
#include "radiant.hpp"
#include "../lib/font/font.h"  // unified font module — font_face_register, font_family_exists
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_font_face.hpp"
#include "../lambda/network/network_resource_manager.h"
extern "C" {
#include "../lib/url.h"
#include "../lib/memtrack.h"
#include "../lib/lambda_alloca.h"
#include "../lib/str.h"
}
#include "../lib/mem_grow.hpp"
#include <string.h>
#include <stdlib.h>
#include "../lib/file.h"

bool radiant_is_supported_web_font_source(const char* url, const char* format) {
    if (format && *format) {
        if (str_icmp_cstr(format, "woff2") == 0 ||
            str_icmp_cstr(format, "woff") == 0 ||
            str_icmp_cstr(format, "truetype") == 0 ||
            str_icmp_cstr(format, "opentype") == 0 ||
            str_icmp_cstr(format, "ttf") == 0 ||
            str_icmp_cstr(format, "otf") == 0) {
            return true;
        }
        return false;
    }

    return url_text_path_has_ext_ci(url, "woff2") ||
           url_text_path_has_ext_ci(url, "woff") ||
           url_text_path_has_ext_ci(url, "ttf") ||
           url_text_path_has_ext_ci(url, "otf") ||
           url_text_path_has_ext_ci(url, "ttc");
}

static bool resolve_font_source_path(char** source, const char* base_path,
                                     NetworkResourceManager* resource_manager,
                                     const char* format) {
    if (!source || !*source ||
        strncmp(*source, "data:", 5) == 0) {
        return false;
    }

    char* replacement = nullptr;
    bool pending = false;
    if (radiant_url_is_http(*source)) {
        if (!radiant_is_supported_web_font_source(*source, format)) return false;
        // keep downloaded URLs in the original src list; a later local-only
        // registration otherwise shadows the completed network face.
        replacement = resource_manager_copy_ready_resource_path(resource_manager, *source, &pending);
    } else {
        char resolved[2048];
        if (!base_path || !radiant_resolve_layout_relative_resource_path(
                *source, base_path, resolved, sizeof(resolved))) return false;
        replacement = mem_strdup(resolved, MEM_CAT_FONT);
    }
    if (!replacement) return pending;
    lam::Temp<char> previous(*source);  // the replacement takes the slot
    *source = replacement;
    return false;
}

// Text flow logging categories
log_category_t* font_log = NULL;
log_category_t* text_log = NULL;
log_category_t* layout_log = NULL;

// Initialize logging categories for text flow
void init_text_flow_logging(void) {
    font_log = log_get_category("radiant.font");
    text_log = log_get_category("radiant.text");
    layout_log = log_get_category("radiant.layout");

    if (!font_log || !text_log || !layout_log) {
        log_warn("Failed to initialize text flow logging categories");
    } else {
        log_info("Text flow logging categories initialized");
    }
}

static FontFaceDescriptor* font_face_descriptor_from_css(CssFontFaceDescriptor* css_desc) {
    // staged in Temp owners until complete; UiContext owns the result across layout passes
    lam::Temp<FontFaceDescriptor> descriptor = lam::temp_array_zero<FontFaceDescriptor>(1, MEM_CAT_LAYOUT);
    if (!descriptor) return nullptr;
    lam::Temp<char> family_name(css_desc->family_name
        ? mem_strdup(css_desc->family_name, MEM_CAT_LAYOUT) : nullptr);
    lam::Temp<char> src_local_path(css_desc->src_url
        ? mem_strdup(css_desc->src_url, MEM_CAT_LAYOUT) : nullptr);
    descriptor->font_style = css_desc->font_style;
    descriptor->font_weight = css_desc->font_weight;
    descriptor->font_display = css_desc->font_display;
    if (css_desc->unicode_range_count > 0 && css_desc->unicode_ranges) {
        descriptor->unicode_ranges = lam::own_arr((FontFaceUnicodeRange*)mem_calloc(
            (size_t)css_desc->unicode_range_count, sizeof(FontFaceUnicodeRange), MEM_CAT_LAYOUT));
        if (!descriptor->unicode_ranges) return nullptr;
        for (int i = 0; i < css_desc->unicode_range_count; i++) {
            descriptor->unicode_ranges[i].start_codepoint =
                css_desc->unicode_ranges[i].start_codepoint;
            descriptor->unicode_ranges[i].end_codepoint =
                css_desc->unicode_ranges[i].end_codepoint;
        }
        descriptor->unicode_range_count = css_desc->unicode_range_count;
    }
    // keep installed names and URLs in their declared fallback order.
    if (css_desc->src_urls && css_desc->src_count > 0) {
        int loadable_src_count = 0;
        for (int j = 0; j < css_desc->src_count; j++) {
            if (css_desc->src_urls[j].url) loadable_src_count++;
        }
        descriptor->src_entries = lam::own_arr(loadable_src_count > 0
            ? (FontFaceSrc*)mem_calloc(loadable_src_count, sizeof(FontFaceSrc), MEM_CAT_LAYOUT)
            : nullptr);
        if (descriptor->src_entries) {
            descriptor->src_count = loadable_src_count;
            int dst = 0;
            for (int j = 0; j < css_desc->src_count; j++) {
                if (!css_desc->src_urls[j].url) continue;
                descriptor->src_entries[dst].path = lam::own(mem_strdup(css_desc->src_urls[j].url, MEM_CAT_LAYOUT));
                descriptor->src_entries[dst].format = lam::own(css_desc->src_urls[j].format ? mem_strdup(css_desc->src_urls[j].format, MEM_CAT_LAYOUT) : nullptr);
                descriptor->src_entries[dst].is_local = css_desc->src_urls[j].is_local;
                dst++;
            }
            clog_debug(font_log, "Copied %d src entries for @font-face '%s'",
                descriptor->src_count, css_desc->family_name);
        }
    }
    descriptor->family_name = lam::own(family_name.release());
    descriptor->src_local_path = lam::own(src_local_path.release());
    return descriptor.release();
}

// CSS @font-face parsing integration - uses css_font_face.hpp module
void parse_font_face_rule(LayoutContext* lycon, void* rule) {
    if (!lycon || !rule) {
        clog_debug(font_log, "parse_font_face_rule: invalid parameters");
        return;
    }

    CssRule* css_rule = (CssRule*)rule;
    if (css_rule->type != CSS_RULE_FONT_FACE) {
        clog_debug(font_log, "parse_font_face_rule: not a font-face rule");
        return;
    }

    const char* content = css_rule->data.generic_rule.content;
    if (!content) {
        clog_warn(font_log, "parse_font_face_rule: no content in rule");
        return;
    }

    // Resource ownership stays explicit: this helper always returns a font-owned copy.
    lam::Temp<char> owned_base_path(radiant_document_resource_base(
        lycon->doc, MEM_CAT_FONT));
    const char* base_path = owned_base_path.get();

    // Parse using CSS module
    CssFontFaceDescriptor* css_desc = css_parse_font_face_content(content, nullptr);
    if (!css_desc) return;

    css_font_face_resolve_sources(css_desc, base_path, nullptr);

    // Convert to FontFaceDescriptor and register
    FontFaceDescriptor* descriptor = font_face_descriptor_from_css(css_desc);
    if (descriptor) {
        descriptor->is_loaded = false;

        register_font_face(lycon->ui_context, descriptor);
    }

    css_font_face_descriptor_free(css_desc);
}

// Process all @font-face rules from a stylesheet - uses css_font_face.hpp module
void process_font_face_rules_from_stylesheet(UiContext* uicon, CssStylesheet* stylesheet,
        const char* base_path, bool data_only, NetworkResourceManager* resource_manager) {
    if (!uicon || !stylesheet) {
        return;
    }

    clog_info(font_log, "Processing @font-face rules from stylesheet (base: %s)",
              base_path ? base_path : "(none)");

    int count = 0;
    CssFontFaceDescriptor** css_descs = css_extract_font_faces(stylesheet, base_path, nullptr, &count);
    // extraction can allocate an array even when every descriptor is rejected (D4.5.1v4).
    lam::Temp<CssFontFaceDescriptor*> descriptor_array(css_descs);

    if (!css_descs || count == 0) {
        clog_debug(font_log, "No @font-face rules found");
        return;
    }

    for (int i = 0; i < count; i++) {
        CssFontFaceDescriptor* css_desc = css_descs[i];
        if (!css_desc) continue;
        bool has_pending_sources = false;

        // only the ordered list retains format hints; its scalar compatibility
        // URL must not reintroduce an unsupported source after list filtering.
        has_pending_sources |= resolve_font_source_path(&css_desc->src_url, base_path,
            !data_only && css_desc->src_count == 0 ? resource_manager : nullptr, nullptr);
        if (css_desc->src_urls) {
            for (int j = 0; j < css_desc->src_count; j++) {
                if (!css_desc->src_urls[j].is_local)
                    has_pending_sources |= resolve_font_source_path(&css_desc->src_urls[j].url, base_path,
                        data_only ? nullptr : resource_manager, css_desc->src_urls[j].format);
            }
        }

        // Remote web fonts are discovered by the network resource manager and
        // installed when their downloads complete; synchronously downloading
        // every @font-face source here blocks large docs before layout starts.
        if (css_desc->src_urls) {
            for (int j = 0; j < css_desc->src_count; j++) {
                if (!css_desc->src_urls[j].is_local && radiant_url_is_http(css_desc->src_urls[j].url)) {
                    if (!radiant_is_supported_web_font_source(
                            css_desc->src_urls[j].url, css_desc->src_urls[j].format)) {
                        clog_debug(font_log, "Skipping unsupported remote font source: %s (format: %s)",
                                   css_desc->src_urls[j].url,
                                   css_desc->src_urls[j].format ? css_desc->src_urls[j].format : "?");
                    }
                    lam::Temp<char> dropped(css_desc->src_urls[j].url);
                    css_desc->src_urls[j].url = nullptr;
                }
            }
        }
        if (radiant_url_is_http(css_desc->src_url)) {
            lam::Temp<char> dropped(css_desc->src_url);
            css_desc->src_url = nullptr;
        }

        // SVG image documents can use embedded font bytes without fetching external resources.
        if (data_only) {
            if (css_desc->src_url && strncmp(css_desc->src_url, "data:", 5) != 0) {
                lam::Temp<char> dropped(css_desc->src_url);
                css_desc->src_url = nullptr;
            }
            for (int j = 0; css_desc->src_urls && j < css_desc->src_count; j++) {
                char*& source = css_desc->src_urls[j].url;
                if (source && !css_desc->src_urls[j].is_local && strncmp(source, "data:", 5) != 0) {
                    lam::Temp<char> dropped(source); source = nullptr;
                }
            }
        }

        bool has_loadable_source = css_desc->src_url || css_desc->src_local;
        if (css_desc->src_urls) {
            for (int j = 0; j < css_desc->src_count; j++) {
                if (css_desc->src_urls[j].url) {
                    has_loadable_source = true;
                    break;
                }
            }
        }

        // Skip fonts without any loadable source
        if (!has_loadable_source) {
            clog_debug(font_log, "Skipping @font-face '%s': no local source available",
                       css_desc->family_name ? css_desc->family_name : "(unnamed)");
            css_font_face_descriptor_free(css_desc);
            continue;
        }

        // Convert to FontFaceDescriptor and register
        FontFaceDescriptor* descriptor = font_face_descriptor_from_css(css_desc);
        if (descriptor) {
            descriptor->is_loaded = false;



            register_font_face(uicon, descriptor, has_pending_sources);
        }

        css_font_face_descriptor_free(css_desc);
    }

    clog_info(font_log, "Registered %d @font-face descriptors", count);
}

// Helper function to process all @font-face rules from a document's stylesheets
void process_document_font_faces(UiContext* uicon, DomDocument* doc) {
    if (!uicon || !doc) return;
    if (doc->font_faces_processed) return;
    // Geometry can be queried while load scripts run; make this idempotent so
    // that early font readiness and the normal final-load phase share one registry.
    if (!doc->stylesheets || doc->stylesheet_count == 0) {
        doc->font_faces_processed = true;
        return;
    }

    // Resource ownership stays explicit: this helper always returns a font-owned copy.
    lam::Temp<char> owned_doc_base_path(radiant_document_resource_base(
        doc, MEM_CAT_FONT));
    const char* doc_base_path = owned_doc_base_path.get();

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets[i];
        if (!stylesheet) continue;

        // An external stylesheet owns the base for its relative font URLs.
        const char* base_path = doc_base_path;
        lam::Temp<char> stylesheet_path(stylesheet->origin_url
            ? radiant_resolve_resource_path(stylesheet->origin_url,
                doc_base_path, false, MEM_CAT_FONT) : nullptr);
        if (stylesheet_path) base_path = stylesheet_path.get();

        process_font_face_rules_from_stylesheet(uicon, stylesheet, base_path, false,
            doc->resource_manager);
    }
    doc->font_faces_processed = true;
}

void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor, bool has_pending_sources) {
    if (!uicon || !descriptor) {
        clog_error(font_log, "Invalid parameters for register_font_face");
        return;
    }

    log_debug("register_font_face: registering %s -> %s",
              descriptor->family_name ? descriptor->family_name : "(null)",
              descriptor->src_local_path ? descriptor->src_local_path : "(null)");

    // Initialize @font-face storage if needed
    if (!uicon->font_faces) {
        uicon->font_face_capacity = 10;
        uicon->font_faces = lam::own_arr((FontFaceDescriptor**)mem_calloc(uicon->font_face_capacity, sizeof(FontFaceDescriptor*), MEM_CAT_LAYOUT));
        uicon->font_face_count = 0;

        if (!uicon->font_faces) {
            clog_error(font_log, "Failed to allocate font_faces array");
            return;
        }
    }

    // Expand array if needed
    if (uicon->font_face_count >= uicon->font_face_capacity) {
        if (!lam::mem_grow_array(&uicon->font_faces, &uicon->font_face_capacity,
                                 uicon->font_face_count + 1, 16, MEM_CAT_LAYOUT)) {
            clog_error(font_log, "Failed to expand font_faces array");
            return;
        }
    }

    // Store the descriptor
    uicon->font_faces[uicon->font_face_count] = descriptor;
    uicon->font_face_count++;

    clog_info(font_log, "Registered @font-face: %s -> %s (total: %d)",
              descriptor->family_name, descriptor->src_local_path, uicon->font_face_count);

    // ---- Bridge to unified font module ----
    // Also register with FontContext so that font_resolve() can find @font-face
    // descriptors directly, without going through load_font_with_descriptors().
    if (uicon->font_ctx && descriptor->family_name) {
        FontWeight fw = radiant_font_weight_from_css(descriptor->font_weight);
        FontSlant fs = radiant_font_slant_from_css(descriptor->font_style);

        // build sources array from descriptor's src_entries + src_local_path
        int src_count = descriptor->src_count;
        if (!src_count && descriptor->src_local_path) src_count = 1;

        FontFaceSource* sources = nullptr;
        if (src_count > 0) {
            sources = LAMBDA_ALLOCA(src_count, FontFaceSource);
            memset(sources, 0, src_count * sizeof(FontFaceSource));

            if (descriptor->src_entries && descriptor->src_count > 0) {
                for (int i = 0; i < descriptor->src_count; i++) {
                    if (descriptor->src_entries[i].is_local)
                        sources[i].local_name = descriptor->src_entries[i].path;
                    else sources[i].path = descriptor->src_entries[i].path;
                    sources[i].format = descriptor->src_entries[i].format;
                }
            } else if (descriptor->src_local_path) {
                sources[0].path   = descriptor->src_local_path;
                sources[0].format = nullptr;
            }
        }

        FontFaceDesc face_desc = {};
        face_desc.family       = descriptor->family_name;
        face_desc.weight       = fw;
        face_desc.slant        = fs;
        face_desc.sources      = sources;
        face_desc.source_count = src_count;
        face_desc.unicode_ranges = descriptor->unicode_ranges;
        face_desc.unicode_range_count = descriptor->unicode_range_count;
        face_desc.has_pending_sources = has_pending_sources;

        if (font_face_register(uicon->font_ctx, &face_desc)) {
            clog_debug(font_log, "register_font_face: bridged to unified font module for '%s'",
                       descriptor->family_name);
        }
    }
}
