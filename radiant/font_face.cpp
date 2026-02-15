#include "font_face.h"
#include "layout.hpp"
#include "../lib/font/font.h"  // unified font module — font_face_register, font_family_exists
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_font_face.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
}
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <stdlib.h>

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

void setup_text_flow_log_categories(void) {
    init_text_flow_logging();
}

// ============================================================================

// Structured logging for font operations (replace printf)
void log_font_loading_attempt(const char* family_name, const char* path) {
    if (font_log) {
        clog_debug(font_log, "Attempting to load font: %s from path: %s", family_name, path);
    }
}

void log_font_loading_result(const char* family_name, bool success, const char* error) {
    if (font_log) {
        if (success) {
            clog_info(font_log, "Successfully loaded font: %s", family_name);
        } else {
            clog_error(font_log, "Failed to load font: %s - %s", family_name, error ? error : "unknown error");
        }
    }
}

void log_font_fallback_triggered(const char* requested, const char* fallback) {
    if (font_log) {
        clog_warn(font_log, "Font fallback triggered: %s -> %s", requested, fallback);
    }
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

    // Get base path from document URL
    const char* base_path = nullptr;
    if (lycon->doc && lycon->doc->url) {
        base_path = url_to_local_path(lycon->doc->url);
    }

    // Parse using CSS module
    CssFontFaceDescriptor* css_desc = css_parse_font_face_content(content, nullptr);
    if (!css_desc) return;

    // Resolve URL
    if (css_desc->src_url && base_path) {
        char* resolved = css_resolve_font_url(css_desc->src_url, base_path, nullptr);
        if (resolved) {
            mem_free(css_desc->src_url);
            css_desc->src_url = resolved;
        }
    }

    // Convert to FontFaceDescriptor and register
    FontFaceDescriptor* descriptor = (FontFaceDescriptor*)mem_calloc(1, sizeof(FontFaceDescriptor), MEM_CAT_LAYOUT);
    if (descriptor) {
        descriptor->family_name = css_desc->family_name ? mem_strdup(css_desc->family_name, MEM_CAT_LAYOUT) : nullptr;
        descriptor->src_local_path = css_desc->src_url ? mem_strdup(css_desc->src_url, MEM_CAT_LAYOUT) : nullptr;
        descriptor->font_style = css_desc->font_style;
        descriptor->font_weight = css_desc->font_weight;
        descriptor->font_display = css_desc->font_display;
        descriptor->is_loaded = false;

        register_font_face(lycon->ui_context, descriptor);
    }

    css_font_face_descriptor_free(css_desc);
}

// Process all @font-face rules from a stylesheet - uses css_font_face.hpp module
void process_font_face_rules_from_stylesheet(UiContext* uicon, CssStylesheet* stylesheet, const char* base_path) {
    if (!uicon || !stylesheet) {
        return;
    }

    clog_info(font_log, "Processing @font-face rules from stylesheet (base: %s)",
              base_path ? base_path : "(none)");

    int count = 0;
    CssFontFaceDescriptor** css_descs = css_extract_font_faces(stylesheet, base_path, nullptr, &count);

    if (!css_descs || count == 0) {
        clog_debug(font_log, "No @font-face rules found");
        return;
    }

    for (int i = 0; i < count; i++) {
        CssFontFaceDescriptor* css_desc = css_descs[i];
        if (!css_desc) continue;

        // Skip fonts without any loadable source
        if ((!css_desc->src_urls || css_desc->src_count == 0) && !css_desc->src_url && !css_desc->src_local) {
            clog_debug(font_log, "Skipping @font-face '%s': no local source available",
                       css_desc->family_name ? css_desc->family_name : "(unnamed)");
            css_font_face_descriptor_free(css_desc);
            continue;
        }

        // Convert to FontFaceDescriptor and register
        FontFaceDescriptor* descriptor = (FontFaceDescriptor*)mem_calloc(1, sizeof(FontFaceDescriptor), MEM_CAT_LAYOUT);
        if (descriptor) {
            descriptor->family_name = css_desc->family_name ? mem_strdup(css_desc->family_name, MEM_CAT_LAYOUT) : nullptr;
            descriptor->src_local_path = css_desc->src_url ? mem_strdup(css_desc->src_url, MEM_CAT_LAYOUT) : nullptr;
            descriptor->font_style = css_desc->font_style;
            descriptor->font_weight = css_desc->font_weight;
            descriptor->font_display = css_desc->font_display;
            descriptor->is_loaded = false;

            // Copy src_urls array for multi-format fallback
            if (css_desc->src_urls && css_desc->src_count > 0) {
                descriptor->src_entries = (FontFaceSrc*)mem_calloc(css_desc->src_count, sizeof(FontFaceSrc), MEM_CAT_LAYOUT);
                if (descriptor->src_entries) {
                    descriptor->src_count = css_desc->src_count;
                    for (int j = 0; j < css_desc->src_count; j++) {
                        descriptor->src_entries[j].path = css_desc->src_urls[j].url ? mem_strdup(css_desc->src_urls[j].url, MEM_CAT_LAYOUT) : nullptr;
                        descriptor->src_entries[j].format = css_desc->src_urls[j].format ? mem_strdup(css_desc->src_urls[j].format, MEM_CAT_LAYOUT) : nullptr;
                    }
                    clog_debug(font_log, "Copied %d src entries for @font-face '%s'",
                        descriptor->src_count, descriptor->family_name);
                }
            }

            register_font_face(uicon, descriptor);
        }

        css_font_face_descriptor_free(css_desc);
    }

    mem_free(css_descs);
    clog_info(font_log, "Registered %d @font-face descriptors", count);
}

// Helper function to process all @font-face rules from a document's stylesheets
void process_document_font_faces(UiContext* uicon, DomDocument* doc) {
    if (!uicon || !doc) return;
    if (!doc->stylesheets || doc->stylesheet_count == 0) return;

    // Default base path from document URL (used for inline styles)
    char* doc_base_path = url_to_local_path(doc->url);

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets[i];
        if (!stylesheet) continue;

        // Use stylesheet's origin_url if available, otherwise fall back to document URL
        // This is important for external CSS files where font URLs are relative to the CSS file
        const char* base_path = doc_base_path;
        char* stylesheet_path = nullptr;

        if (stylesheet->origin_url) {
            // origin_url can be either a plain file path or a file:// URL
            // Check if it starts with "/" (plain file path) or "file://" (URL)
            if (stylesheet->origin_url[0] == '/') {
                // Plain file path - use directly
                stylesheet_path = strdup(stylesheet->origin_url);  // must use strdup to match url_to_local_path
                if (stylesheet_path) {
                    base_path = stylesheet_path;
                    clog_debug(font_log, "Using stylesheet origin_url (plain path) for font resolution: %s", base_path);
                }
            } else if (strncmp(stylesheet->origin_url, "file://", 7) == 0) {
                // URL - parse and convert
                Url* stylesheet_url = url_parse(stylesheet->origin_url);
                if (stylesheet_url) {
                    stylesheet_path = url_to_local_path(stylesheet_url);
                    url_destroy(stylesheet_url);
                    if (stylesheet_path) {
                        base_path = stylesheet_path;
                        clog_debug(font_log, "Using stylesheet origin_url (file URL) for font resolution: %s", base_path);
                    }
                }
            }
        }

        process_font_face_rules_from_stylesheet(uicon, stylesheet, base_path);

        if (stylesheet_path) {
            free(stylesheet_path);  // from url_to_local_path() or strdup() which use stdlib
        }
    }

    if (doc_base_path) {
        free(doc_base_path);  // from url_to_local_path() which uses stdlib
    }
}

/* Original lexbor-dependent code - commented out:
void parse_font_face_rule_OLD(LayoutContext* lycon, lxb_css_rule_t* rule) {
    if (!lycon) {
        clog_error(font_log, "Invalid LayoutContext for parse_font_face_rule");
        return;
    }

    // For hardcoded implementation, rule can be NULL
    if (!rule) {
        clog_info(font_log, "Processing hardcoded @font-face rule (rule=NULL)");
    }

    clog_info(font_log, "Processing @font-face rules for Liberation font family");

    // Register Liberation Sans variants
    const char* liberation_sans_fonts[] = {
        "./test/layout/font/LiberationSans-Regular.ttf",
        "./test/layout/font/LiberationSans-Bold.ttf",
        "./test/layout/font/LiberationSans-Italic.ttf",
        "./test/layout/font/LiberationSans-BoldItalic.ttf"
    };

    CssEnum weights[] = {CSS_VALUE_NORMAL, CSS_VALUE_BOLD, CSS_VALUE_NORMAL, CSS_VALUE_BOLD};
    CssEnum styles[] = {CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_ITALIC, CSS_VALUE_ITALIC};

    for (int i = 0; i < 4; i++) {
        FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
        if (!descriptor) {
            clog_error(font_log, "Failed to create font face descriptor");
            continue;
        }

        descriptor->family_name = mem_strdup("Liberation Sans", MEM_CAT_LAYOUT);
        descriptor->src_local_path = mem_strdup(liberation_sans_fonts[i], MEM_CAT_LAYOUT);
        descriptor->font_style = styles[i];
        descriptor->font_weight = weights[i];
        descriptor->font_display = CSS_VALUE_AUTO;
        descriptor->is_loaded = false;

        register_font_face(lycon->ui_context, descriptor);

        clog_info(font_log, "Registered @font-face: %s -> %s (weight=%d, style=%d)",
                  descriptor->family_name, descriptor->src_local_path, weights[i], styles[i]);
    }
}
*/

void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor) {
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
        uicon->font_faces = (FontFaceDescriptor**)mem_calloc(uicon->font_face_capacity, sizeof(FontFaceDescriptor*), MEM_CAT_LAYOUT);
        uicon->font_face_count = 0;

        if (!uicon->font_faces) {
            clog_error(font_log, "Failed to allocate font_faces array");
            return;
        }
    }

    // Expand array if needed
    if (uicon->font_face_count >= uicon->font_face_capacity) {
        int new_capacity = uicon->font_face_capacity * 2;
        FontFaceDescriptor** new_array = (FontFaceDescriptor**)mem_realloc(
            uicon->font_faces, new_capacity * sizeof(FontFaceDescriptor*), MEM_CAT_LAYOUT);

        if (!new_array) {
            clog_error(font_log, "Failed to expand font_faces array");
            return;
        }

        uicon->font_faces = new_array;
        uicon->font_face_capacity = new_capacity;
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
        // map CssEnum weight/style → FontWeight/FontSlant
        FontWeight fw = FONT_WEIGHT_NORMAL;
        if (descriptor->font_weight == CSS_VALUE_BOLD) fw = FONT_WEIGHT_BOLD;
        else if (descriptor->font_weight >= 100 && descriptor->font_weight <= 900)
            fw = (FontWeight)descriptor->font_weight;

        FontSlant fs = FONT_SLANT_NORMAL;
        if (descriptor->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
        else if (descriptor->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

        // build sources array from descriptor's src_entries + src_local_path
        int src_count = descriptor->src_count;
        if (!src_count && descriptor->src_local_path) src_count = 1;

        FontFaceSource* sources = nullptr;
        if (src_count > 0) {
            sources = (FontFaceSource*)alloca(src_count * sizeof(FontFaceSource));
            memset(sources, 0, src_count * sizeof(FontFaceSource));

            if (descriptor->src_entries && descriptor->src_count > 0) {
                for (int i = 0; i < descriptor->src_count; i++) {
                    sources[i].path   = descriptor->src_entries[i].path;
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

        if (font_face_register(uicon->font_ctx, &face_desc)) {
            clog_debug(font_log, "register_font_face: bridged to unified font module for '%s'",
                       descriptor->family_name);
        }
    }
}
