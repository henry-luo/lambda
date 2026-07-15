#include "view.hpp"
#include "layout.hpp"
#include "../lib/font/font.h"  // unified font module — font_face_register, font_family_exists
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_font_face.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/memtrack.h"
#include "../lib/lambda_alloca.h"
#include "../lib/str.h"
}
#include "../lib/mem_grow.hpp"
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <stdlib.h>
#include "../lib/file.h"

static bool is_http_url(const char* url) {
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static bool is_supported_web_font_source(const char* url, const char* format) {
    if (format && *format) {
        if (strcasecmp(format, "woff2") == 0 ||
            strcasecmp(format, "woff") == 0 ||
            strcasecmp(format, "truetype") == 0 ||
            strcasecmp(format, "opentype") == 0 ||
            strcasecmp(format, "ttf") == 0 ||
            strcasecmp(format, "otf") == 0) {
            return true;
        }
        return false;
    }

    if (!url) return false;
    const char* clean_end = url + strlen(url);
    const char* query = strchr(url, '?');
    const char* fragment = strchr(url, '#');
    if (query && query < clean_end) clean_end = query;
    if (fragment && fragment < clean_end) clean_end = fragment;

    size_t len = (size_t)(clean_end - url);
    return (len >= 6 && strncasecmp(clean_end - 6, ".woff2", 6) == 0) ||
           (len >= 5 && strncasecmp(clean_end - 5, ".woff", 5) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".ttf", 4) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".otf", 4) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".ttc", 4) == 0);
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
    FontFaceDescriptor* descriptor = (FontFaceDescriptor*)mem_calloc(1, sizeof(FontFaceDescriptor), MEM_CAT_LAYOUT); // OBJ_HEAP_OK: UiContext owns @font-face descriptors across layout passes.
    if (!descriptor) return nullptr;
    descriptor->family_name = css_desc->family_name
        ? mem_strdup(css_desc->family_name, MEM_CAT_LAYOUT) : nullptr;
    descriptor->src_local_path = css_desc->src_url
        ? mem_strdup(css_desc->src_url, MEM_CAT_LAYOUT) : nullptr;
    descriptor->font_style = css_desc->font_style;
    descriptor->font_weight = css_desc->font_weight;
    descriptor->font_display = css_desc->font_display;
    return descriptor;
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
    char* owned_base_path = nullptr;
    if (lycon->doc && lycon->doc->url) {
        if (lycon->doc->url->scheme == URL_SCHEME_HTTP || lycon->doc->url->scheme == URL_SCHEME_HTTPS) {
            base_path = url_get_href(lycon->doc->url);
        } else {
            owned_base_path = url_to_local_path(lycon->doc->url);
            base_path = owned_base_path;
        }
    }

    // Parse using CSS module
    CssFontFaceDescriptor* css_desc = css_parse_font_face_content(content, nullptr);
    if (!css_desc) {
        if (owned_base_path) mem_free(owned_base_path);
        return;
    }

    // Resolve URL
    if (css_desc->src_url && base_path) {
        char* resolved = css_resolve_font_url(css_desc->src_url, base_path, nullptr);
        if (resolved) {
            mem_free(css_desc->src_url);
            css_desc->src_url = resolved;
        }
    }

    // Convert to FontFaceDescriptor and register
    FontFaceDescriptor* descriptor = font_face_descriptor_from_css(css_desc);
    if (descriptor) {
        descriptor->is_loaded = false;

        register_font_face(lycon->ui_context, descriptor);
    }

    css_font_face_descriptor_free(css_desc);
    if (owned_base_path) mem_free(owned_base_path);
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

        // Remote web fonts are discovered by the network resource manager and
        // installed when their downloads complete; synchronously downloading
        // every @font-face source here blocks large docs before layout starts.
        if (css_desc->src_urls) {
            for (int j = 0; j < css_desc->src_count; j++) {
                if (is_http_url(css_desc->src_urls[j].url)) {
                    if (!is_supported_web_font_source(css_desc->src_urls[j].url, css_desc->src_urls[j].format)) {
                        clog_debug(font_log, "Skipping unsupported remote font source: %s (format: %s)",
                                   css_desc->src_urls[j].url,
                                   css_desc->src_urls[j].format ? css_desc->src_urls[j].format : "?");
                    }
                    mem_free(css_desc->src_urls[j].url);
                    css_desc->src_urls[j].url = nullptr;
                }
            }
        }
        if (is_http_url(css_desc->src_url)) {
            mem_free(css_desc->src_url);
            css_desc->src_url = nullptr;
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

            // Copy src_urls array for multi-format fallback
            if (css_desc->src_urls && css_desc->src_count > 0) {
                int loadable_src_count = 0;
                for (int j = 0; j < css_desc->src_count; j++) {
                    if (css_desc->src_urls[j].url) loadable_src_count++;
                }
                descriptor->src_entries = loadable_src_count > 0
                    ? (FontFaceSrc*)mem_calloc(loadable_src_count, sizeof(FontFaceSrc), MEM_CAT_LAYOUT)
                    : nullptr;
                if (descriptor->src_entries) {
                    descriptor->src_count = loadable_src_count;
                    int dst = 0;
                    for (int j = 0; j < css_desc->src_count; j++) {
                        if (!css_desc->src_urls[j].url) continue;
                        descriptor->src_entries[dst].path = mem_strdup(css_desc->src_urls[j].url, MEM_CAT_LAYOUT);
                        descriptor->src_entries[dst].format = css_desc->src_urls[j].format ? mem_strdup(css_desc->src_urls[j].format, MEM_CAT_LAYOUT) : nullptr;
                        dst++;
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

    // Default base path from document URL (used for inline styles).
    const char* doc_base_path = nullptr;
    char* owned_doc_base_path = nullptr;
    if (doc->url) {
        if (doc->url->scheme == URL_SCHEME_HTTP || doc->url->scheme == URL_SCHEME_HTTPS) {
            doc_base_path = url_get_href(doc->url);
        } else {
            owned_doc_base_path = url_to_local_path(doc->url);
            doc_base_path = owned_doc_base_path;
        }
    }

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
                stylesheet_path = mem_strdup(stylesheet->origin_url, MEM_CAT_FONT);  // must use mem_strdup to match url_to_local_path
                if (stylesheet_path) {
                    base_path = stylesheet_path;
                    clog_debug(font_log, "Using stylesheet origin_url (plain path) for font resolution: %s", base_path);
                }
            } else if (strncmp(stylesheet->origin_url, "http://", 7) == 0 ||
                       strncmp(stylesheet->origin_url, "https://", 8) == 0) {
                stylesheet_path = mem_strdup(stylesheet->origin_url, MEM_CAT_FONT);
                if (stylesheet_path) {
                    base_path = stylesheet_path;
                    clog_debug(font_log, "Using stylesheet origin_url (remote URL) for font resolution: %s", base_path);
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
            } else {
                // Relative path - resolve to absolute using CWD so font paths are correct
                char* resolved = file_realpath(stylesheet->origin_url);
                if (resolved) {
                    stylesheet_path = resolved;  // file_realpath returns malloc'd string
                    base_path = stylesheet_path;
                    clog_debug(font_log, "Using stylesheet origin_url (resolved relative path) for font resolution: %s", base_path);
                }
            }
        }

        process_font_face_rules_from_stylesheet(uicon, stylesheet, base_path);

        if (stylesheet_path) {
            mem_free(stylesheet_path);  // from url_to_local_path() or strdup() which use stdlib
        }
    }

    if (owned_doc_base_path) {
        mem_free(owned_doc_base_path);  // from url_to_local_path()
    }
}

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
        // map CssEnum weight/style → FontWeight/FontSlant
        // Note: CSS_VALUE_NORMAL and CSS_VALUE_BOLD are CssEnum values (not
        // numeric weights), so check them explicitly before the numeric range.
        FontWeight fw = FONT_WEIGHT_NORMAL;
        if (descriptor->font_weight == CSS_VALUE_BOLD)
            fw = FONT_WEIGHT_BOLD;
        else if (descriptor->font_weight != CSS_VALUE_NORMAL &&
                 descriptor->font_weight >= 100 && descriptor->font_weight <= 900)
            fw = (FontWeight)descriptor->font_weight;

        FontSlant fs = FONT_SLANT_NORMAL;
        if (descriptor->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
        else if (descriptor->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

        // build sources array from descriptor's src_entries + src_local_path
        int src_count = descriptor->src_count;
        if (!src_count && descriptor->src_local_path) src_count = 1;

        FontFaceSource* sources = nullptr;
        if (src_count > 0) {
            sources = LAMBDA_ALLOCA(src_count, FontFaceSource);
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
