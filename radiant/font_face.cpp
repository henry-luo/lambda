#include "font_face.h"
#include "layout.hpp"
#include "../lib/font_config.h"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_font_face.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/base64.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
}
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <stdlib.h>
#include <string>  // TODO: Required by WOFF2 library API (WOFF2StringOut)

// WOFF2 decompression support
#include <woff2/decode.h>
#include <woff2/output.h>

// Font face cache entry - matches the one in font.cpp
typedef struct FontfaceEntry {
    char* name;
    FT_Face face;
} FontfaceEntry;

// Hash and compare functions for fontface cache - declarations match font.cpp
static int fontface_compare_local(const void *a, const void *b, void *udata) {
    (void)udata;
    const FontfaceEntry *fa = (const FontfaceEntry*)a;
    const FontfaceEntry *fb = (const FontfaceEntry*)b;
    if (!fa || !fb || !fa->name || !fb->name) return (fa == fb) ? 0 : -1;
    return strcmp(fa->name, fb->name);
}

static uint64_t fontface_hash_local(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontfaceEntry *fontface = (const FontfaceEntry*)item;
    if (!fontface || !fontface->name) return 0;
    return hashmap_xxhash3(fontface->name, strlen(fontface->name), seed0, seed1);
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

void setup_text_flow_log_categories(void) {
    init_text_flow_logging();
}

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

void log_font_cache_hit(const char* family_name, int font_size) {
    if (font_log) {
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

FontFaceDescriptor* create_font_face_descriptor(LayoutContext* lycon) {
    if (!lycon) {
        clog_error(font_log, "Invalid LayoutContext for create_font_face_descriptor");
        return NULL;
    }

    FontFaceDescriptor* descriptor = (FontFaceDescriptor*)mem_calloc(1, sizeof(FontFaceDescriptor), MEM_CAT_LAYOUT);
    if (!descriptor) {
        clog_error(font_log, "Failed to allocate FontFaceDescriptor");
        return NULL;
    }

    memset(descriptor, 0, sizeof(FontFaceDescriptor));
    descriptor->font_style = CSS_VALUE_NORMAL;
    descriptor->font_weight = CSS_VALUE_NORMAL;
    descriptor->font_display = CSS_VALUE_AUTO;
    descriptor->is_loaded = false;
    descriptor->loaded_face = NULL;
    descriptor->char_width_cache = NULL;
    descriptor->metrics_computed = false;

    return descriptor;
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
}

// Character width caching
void cache_character_width(FontFaceDescriptor* descriptor, uint32_t codepoint, int width) {
    if (!descriptor) return;

    if (!descriptor->char_width_cache) {
        descriptor->char_width_cache = hashmap_new(sizeof(uint32_t) + sizeof(int), 128, 0, 0,
                                                  NULL, NULL, NULL, NULL);
    }

    if (descriptor->char_width_cache) {
        // Store codepoint and width as key-value pair
        struct { uint32_t codepoint; int width; } entry = {codepoint, width};
        hashmap_set(descriptor->char_width_cache, &entry);

        clog_debug(font_log, "Cached character width: U+%04X = %d", codepoint, width);
    }
}

int get_cached_char_width(FontFaceDescriptor* descriptor, uint32_t codepoint) {
    if (!descriptor || !descriptor->char_width_cache) {
        return -1; // Cache miss
    }

    typedef struct { uint32_t codepoint; int width; } CharWidthEntry;
    CharWidthEntry search_key = {codepoint, 0};
    CharWidthEntry* entry = (CharWidthEntry*)hashmap_get(descriptor->char_width_cache, &search_key);

    if (entry) {
        clog_debug(font_log, "Character width cache hit: U+%04X = %d", codepoint, entry->width);
        return entry->width;
    }

    return -1; // Cache miss
}

// Data URI font cache entry - stores decoded font data for FT_New_Memory_Face
// Note: FreeType does NOT copy the font data, so we must keep it alive
typedef struct DataUriFontCacheEntry {
    char* uri_hash;           // hash key for the data URI (we hash because full URI can be huge)
    uint8_t* font_data;       // decoded font data buffer
    size_t font_data_size;    // size of font data
    FT_Face face;             // loaded FT_Face (optional, may load multiple sizes)
} DataUriFontCacheEntry;

// Global data URI font cache (using hashmap)
static struct hashmap* data_uri_font_cache = NULL;

static int data_uri_font_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const DataUriFontCacheEntry *fa = (const DataUriFontCacheEntry*)a;
    const DataUriFontCacheEntry *fb = (const DataUriFontCacheEntry*)b;
    if (!fa || !fb || !fa->uri_hash || !fb->uri_hash) return (fa == fb) ? 0 : -1;
    return strcmp(fa->uri_hash, fb->uri_hash);
}

static uint64_t data_uri_font_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const DataUriFontCacheEntry *entry = (const DataUriFontCacheEntry*)item;
    if (!entry || !entry->uri_hash) return 0;
    return hashmap_xxhash3(entry->uri_hash, strlen(entry->uri_hash), seed0, seed1);
}

// Create a short hash key from data URI (first 64 chars of base64 portion)
static char* create_data_uri_hash_key(const char* data_uri) {
    if (!data_uri) return NULL;

    // Find the comma separator (start of data portion)
    const char* comma = strchr(data_uri, ',');
    if (!comma) {
        // No comma - use first 64 chars of whole URI
        size_t len = strlen(data_uri);
        if (len > 64) len = 64;
        char* key = (char*)mem_alloc(len + 1, MEM_CAT_LAYOUT);
        if (key) {
            strncpy(key, data_uri, len);
            key[len] = '\0';
        }
        return key;
    }

    // Use prefix + first 64 chars of data portion for uniqueness
    const char* data = comma + 1;
    size_t prefix_len = comma - data_uri + 1; // include comma
    size_t data_len = strlen(data);
    if (data_len > 64) data_len = 64;

    char* key = (char*)mem_alloc(prefix_len + data_len + 1, MEM_CAT_LAYOUT);
    if (key) {
        memcpy(key, data_uri, prefix_len);
        memcpy(key + prefix_len, data, data_len);
        key[prefix_len + data_len] = '\0';
    }
    return key;
}

// Check if data is WOFF2 format by checking magic number
static bool is_woff2_data(const uint8_t* data, size_t size) {
    // WOFF2 signature: 'wOF2' (0x774F4632)
    if (size < 4) return false;
    return data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == '2';
}

// Decompress WOFF2 to TTF/OTF font data
// Returns newly allocated buffer with decompressed font, or NULL on failure
// Caller is responsible for freeing the returned buffer
static uint8_t* woff2_decompress_to_ttf(const uint8_t* woff2_data, size_t woff2_size, size_t* out_size) {
    if (!woff2_data || woff2_size == 0 || !out_size) {
        return NULL;
    }

    *out_size = 0;

    // Compute final decompressed size
    size_t ttf_size = woff2::ComputeWOFF2FinalSize(woff2_data, woff2_size);
    if (ttf_size == 0) {
        clog_error(font_log, "WOFF2: ComputeWOFF2FinalSize failed or returned 0");
        return NULL;
    }

    log_debug("WOFF2: decompressing %zu bytes to estimated %zu bytes", woff2_size, ttf_size);

    // Use std::string as output buffer (WOFF2StringOut handles resizing)
    std::string ttf_buffer;
    ttf_buffer.reserve(ttf_size);
    woff2::WOFF2StringOut output(&ttf_buffer);

    // Perform decompression
    if (!woff2::ConvertWOFF2ToTTF(woff2_data, woff2_size, &output)) {
        clog_error(font_log, "WOFF2: ConvertWOFF2ToTTF decompression failed");
        return NULL;
    }

    // Allocate output buffer and copy data
    size_t actual_size = ttf_buffer.size();
    uint8_t* result = (uint8_t*)mem_alloc(actual_size, MEM_CAT_LAYOUT);
    if (!result) {
        clog_error(font_log, "WOFF2: failed to allocate %zu bytes for decompressed font", actual_size);
        return NULL;
    }

    memcpy(result, ttf_buffer.data(), actual_size);
    *out_size = actual_size;

    log_debug("WOFF2: successfully decompressed to %zu bytes TTF", actual_size);
    return result;
}

// Load font from data URI - decodes base64 and uses FT_New_Memory_Face
// Caches the decoded font data to avoid re-decoding
FT_Face load_font_from_data_uri(UiContext* uicon, const char* data_uri, FontProp* style) {
    if (!uicon || !data_uri) {
        clog_error(font_log, "Invalid parameters for load_font_from_data_uri");
        return NULL;
    }

    if (!is_data_uri(data_uri)) {
        clog_error(font_log, "Not a data URI: %.60s...", data_uri);
        return NULL;
    }

    log_debug("load_font_from_data_uri: attempting to load from data URI");

    // Initialize cache if needed
    if (!data_uri_font_cache) {
        data_uri_font_cache = hashmap_new(sizeof(DataUriFontCacheEntry), 16, 0, 0,
            data_uri_font_hash, data_uri_font_compare, NULL, NULL);
        if (!data_uri_font_cache) {
            clog_error(font_log, "Failed to create data URI font cache");
            return NULL;
        }
    }

    // Create hash key for cache lookup
    char* hash_key = create_data_uri_hash_key(data_uri);
    if (!hash_key) {
        clog_error(font_log, "Failed to create hash key for data URI");
        return NULL;
    }

    // Check cache first
    DataUriFontCacheEntry search_key = {.uri_hash = hash_key, .font_data = NULL, .font_data_size = 0, .face = NULL};
    DataUriFontCacheEntry* cached = (DataUriFontCacheEntry*)hashmap_get(data_uri_font_cache, &search_key);

    uint8_t* font_data = NULL;
    size_t font_data_size = 0;

    if (cached && cached->font_data && cached->font_data_size > 0) {
        // Cache hit - reuse decoded data
        log_debug("load_font_from_data_uri: cache hit for data URI");
        font_data = cached->font_data;
        font_data_size = cached->font_data_size;
        mem_free(hash_key);
        hash_key = NULL; // don't free twice
    } else {
        // Cache miss - decode the data URI
        log_debug("load_font_from_data_uri: cache miss, decoding data URI");

        char mime_type[128] = {0};
        uint8_t* raw_data = parse_data_uri(data_uri, mime_type, sizeof(mime_type), &font_data_size);

        if (!raw_data || font_data_size == 0) {
            clog_error(font_log, "Failed to decode data URI font data");
            mem_free(hash_key);
            return NULL;
        }

        log_debug("load_font_from_data_uri: decoded %zu bytes (mime: %s)", font_data_size, mime_type);

        // Check if this is WOFF2 format and decompress if needed
        if (is_woff2_data(raw_data, font_data_size)) {
            log_debug("load_font_from_data_uri: detected WOFF2 format, decompressing...");
            size_t ttf_size = 0;
            uint8_t* ttf_data = woff2_decompress_to_ttf(raw_data, font_data_size, &ttf_size);
            free(raw_data);  // free the WOFF2 data (from parse_data_uri which uses stdlib malloc)

            if (!ttf_data || ttf_size == 0) {
                clog_error(font_log, "WOFF2 decompression failed");
                mem_free(hash_key);
                return NULL;
            }

            font_data = ttf_data;
            font_data_size = ttf_size;
            log_debug("load_font_from_data_uri: WOFF2 decompressed to %zu bytes TTF", font_data_size);
        } else {
            // Not WOFF2 - use raw data directly (TTF/OTF)
            font_data = raw_data;
        }

        // Cache the decoded/decompressed data for future use
        // Note: We must keep font_data alive as long as FreeType might use it
        DataUriFontCacheEntry new_entry = {
            .uri_hash = hash_key,  // transfer ownership
            .font_data = font_data,
            .font_data_size = font_data_size,
            .face = NULL  // will be set after loading
        };
        hashmap_set(data_uri_font_cache, &new_entry);
        hash_key = NULL; // ownership transferred to cache
    }

    // Load font from memory using FT_New_Memory_Face
    // IMPORTANT: FreeType does NOT copy the buffer, so font_data must stay alive
    FT_Face face = NULL;
    FT_Error error = FT_New_Memory_Face(uicon->ft_library, font_data, (FT_Long)font_data_size, 0, &face);

    if (error) {
        clog_error(font_log, "FT_New_Memory_Face failed: error=%d (data size=%zu)", error, font_data_size);
        if (hash_key) mem_free(hash_key);
        return NULL;
    }

    // Set font size in PHYSICAL pixels for HiDPI displays
    // style->font_size is in CSS logical pixels, must multiply by pixel_ratio for HiDPI
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    float css_font_size = style ? style->font_size : 16;
    float physical_font_size = css_font_size * pixel_ratio;
    error = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)physical_font_size);
    if (error) {
        log_error("[FONT-FACE-DATAURI] FT_Set_Pixel_Sizes failed: error=%d, physical_size=%.0f", error, physical_font_size);
        FT_Done_Face(face);
        if (hash_key) mem_free(hash_key);
        return NULL;
    }

    // Verify the size was set correctly
    log_info("[FONT-FACE-DATAURI] Loaded %s: css_size=%.1f, physical_size=%.0f, y_ppem=%d",
        face->family_name ? face->family_name : "(unknown)",
        css_font_size, physical_font_size, face->size->metrics.y_ppem >> 6);

    if (hash_key) mem_free(hash_key);
    return face;
}

// Font loading with @font-face support - handles both local files and data URIs
FT_Face load_local_font_file(UiContext* uicon, const char* font_path, FontProp* style) {
    if (!uicon || !font_path) {
        clog_error(font_log, "Invalid parameters for load_local_font_file");
        return NULL;
    }

    // Check if this is a data URI - route to memory-based loading
    if (is_data_uri(font_path)) {
        log_debug("load_local_font_file: detected data URI, using memory-based loading");
        return load_font_from_data_uri(uicon, font_path, style);
    }

    log_font_loading_attempt("local font", font_path);

    FT_Face face = NULL;
    FT_Error error = FT_New_Face(uicon->ft_library, font_path, 0, &face);

    if (error) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "FreeType error %d loading %s", error, font_path);
        log_font_loading_result("local font", false, error_msg);
        clog_error(font_log, "FT_New_Face failed: error=%d, path=%s", error, font_path);
        return NULL;
    }

    // CRITICAL FIX: Set font size in PHYSICAL pixels for HiDPI displays
    // style->font_size is in CSS logical pixels, must multiply by pixel_ratio for HiDPI
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    float css_font_size = style ? style->font_size : 16;
    float physical_font_size = css_font_size * pixel_ratio;

    // Debug: log face flags to understand why size might not be set
    log_debug("[FONT-FACE-DEBUG] %s: flags=0x%lx, scalable=%d, fixed_sizes=%d, num_fixed=%d",
        face->family_name,
        face->face_flags,
        (face->face_flags & FT_FACE_FLAG_SCALABLE) != 0,
        (face->face_flags & FT_FACE_FLAG_FIXED_SIZES) != 0,
        face->num_fixed_sizes);

    // For fonts with fixed bitmap sizes (color emoji, bitmap fonts), use FT_Select_Size
    if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) && face->num_fixed_sizes > 0 &&
        !(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
        // Pick the closest available size
        int best_idx = 0, best_diff = INT_MAX;
        for (int i = 0; i < face->num_fixed_sizes; i++) {
            int ppem = face->available_sizes[i].y_ppem >> 6;
            int diff = abs(ppem - (int)physical_font_size);
            if (diff < best_diff) { best_diff = diff; best_idx = i; }
        }
        error = FT_Select_Size(face, best_idx);
        log_info("[FONT-FACE] Selected fixed size %d for %s (requested %.0f)",
            face->available_sizes[best_idx].y_ppem >> 6, face->family_name, physical_font_size);
    } else {
        // Scalable font - set requested pixel size
        error = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)physical_font_size);
    }

    if (error) {
        log_error("[FONT-FACE] FT_Set_Pixel_Sizes/FT_Select_Size failed: error=%d, physical_size=%.0f", error, physical_font_size);
        FT_Done_Face(face);
        return NULL;
    }

    // Verify the size was set correctly
    if (face->size && face->size->metrics.y_ppem == 0) {
        log_warn("[FONT-FACE] WARNING: y_ppem=0 after FT_Set_Pixel_Sizes! height=%ld, ascender=%ld",
            face->size->metrics.height, face->size->metrics.ascender);
        // The metrics ARE set (height is non-zero), but y_ppem wasn't stored
        // This happens with some WOFF fonts - the size is effectively set but y_ppem reports 0
        // We can work around this by trusting that the font is sized correctly based on height
    }
    log_info("[FONT-FACE] Loaded %s: css_size=%.1f, physical_size=%.0f, y_ppem=%d, height=%ld",
        face->family_name, css_font_size, physical_font_size,
        face->size ? (face->size->metrics.y_ppem >> 6) : 0,
        face->size ? (face->size->metrics.height) : 0);
    log_font_loading_result(face->family_name, true, NULL);
    return face;
}

bool resolve_font_path_from_descriptor(FontFaceDescriptor* descriptor, char** resolved_path) {
    if (!descriptor || !resolved_path) {
        return false;
    }

    // Try local path first
    if (descriptor->src_local_path) {
        *resolved_path = mem_strdup(descriptor->src_local_path, MEM_CAT_LAYOUT);
        clog_debug(font_log, "Resolved font path from local path: %s", *resolved_path);
        return true;
    }

    // Try local name (would need FontConfig lookup)
    if (descriptor->src_local_name) {
        clog_debug(font_log, "Font resolution by local name not yet implemented: %s", descriptor->src_local_name);
        return false;
    }

    clog_warn(font_log, "No resolvable font source in descriptor for: %s", descriptor->family_name);
    return false;
}

FT_Face load_font_with_descriptors(UiContext* uicon, const char* family_name,
                                   FontProp* style, bool* is_fallback) {
    if (!uicon || !family_name) { return NULL; }

    // Create a cache key including family, weight, style, PHYSICAL size to avoid redundant font loading
    // This is critical for performance - avoids reloading the same font file thousands of times
    // Use physical pixel size (CSS size * pixel_ratio) for consistency with load_styled_font cache
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    float css_font_size = style ? style->font_size : 16;
    int physical_font_size = (int)(css_font_size * pixel_ratio);

    StrBuf* cache_key = strbuf_create("@fontface:");
    if (!cache_key) {
        log_error("Failed to create cache key strbuf");
        return NULL;
    }
    strbuf_append_str(cache_key, family_name);
    strbuf_append_str(cache_key, style && style->font_weight == CSS_VALUE_BOLD ? ":bold:" : ":normal:");
    strbuf_append_str(cache_key, style && style->font_style == CSS_VALUE_ITALIC ? "italic:" : "normal:");
    strbuf_append_int(cache_key, physical_font_size);

    // Initialize fontface map if needed (uses same format as font.cpp)
    if (uicon->fontface_map == NULL) {
        uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0,
            fontface_hash_local, fontface_compare_local, NULL, NULL);
    }

    // Check cache first - avoid expensive font file loading for repeated requests
    if (uicon->fontface_map && cache_key->str) {
        FontfaceEntry search_key = {.name = cache_key->str, .face = NULL};
        FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, &search_key);
        if (entry) {
            strbuf_free(cache_key);
            if (is_fallback) *is_fallback = (entry->face == NULL);
            return entry->face;  // cache hit - skip font file loading
        }
    }

    FT_Face result_face = NULL;

    // Search registered @font-face descriptors first
    if (uicon->font_faces && uicon->font_face_count > 0) {
        FontFaceDescriptor* best_match = NULL;
        float best_score = 0.0f;

        for (int i = 0; i < uicon->font_face_count; i++) {
            FontFaceDescriptor* descriptor = uicon->font_faces[i];
            if (!descriptor || !descriptor->family_name) continue;

            // Check if family name matches (case-insensitive per CSS spec)
            int cmp_result = str_icmp(descriptor->family_name, strlen(descriptor->family_name), family_name, strlen(family_name));
            if (cmp_result == 0) {
                // Calculate match score based on weight and style
                float score = 0.5f; // Base score for family name match

                if (style) {
                    // Weight match (most important for visual accuracy)
                    if (descriptor->font_weight == style->font_weight) {
                        score += 0.3f;
                    }

                    // Style match (italic/normal)
                    if (descriptor->font_style == style->font_style) {
                        score += 0.2f;
                    }
                }

                if (score > best_score) {
                    best_match = descriptor;
                    best_score = score;
                }
            }
        }

        // Load the best matching font - try multiple src entries if available
        if (best_match) {
            log_info("Found @font-face match for: %s (score=%.2f, weight=%d, style=%d)",
                      family_name, best_score, best_match->font_weight, best_match->font_style);

            FT_Face face = NULL;

            // Try src_entries array first (multiple formats with fallback)
            if (best_match->src_entries && best_match->src_count > 0) {
                // Preferred format order: woff2, woff, truetype/ttf, opentype/otf
                // Skip embedded-opentype (EOT) as FreeType doesn't support it
                const char* preferred_formats[] = {"woff2", "woff", "truetype", "opentype", NULL};

                // First pass: try preferred formats in order
                for (int pref = 0; preferred_formats[pref] && !face; pref++) {
                    const char* pref_fmt = preferred_formats[pref];
                    for (int j = 0; j < best_match->src_count && !face; j++) {
                        if (!best_match->src_entries[j].path) continue;

                        const char* fmt = best_match->src_entries[j].format;
                        if (fmt && str_ieq(fmt, strlen(fmt), pref_fmt, strlen(pref_fmt))) {
                            log_debug("Trying preferred format '%s': %s", pref_fmt, best_match->src_entries[j].path);
                            face = load_local_font_file(uicon, best_match->src_entries[j].path, style);
                            if (face) {
                                log_info("Successfully loaded @font-face '%s' from %s (format: %s)",
                                    family_name, best_match->src_entries[j].path, pref_fmt);
                            }
                        }
                    }
                }

                // Second pass: try any remaining non-EOT format
                if (!face) {
                    for (int j = 0; j < best_match->src_count && !face; j++) {
                        if (!best_match->src_entries[j].path) continue;

                        const char* fmt = best_match->src_entries[j].format;
                        // Skip EOT format (FreeType doesn't support it)
                        if (fmt && str_ieq_const(fmt, strlen(fmt), "embedded-opentype")) {
                            log_debug("Skipping embedded-opentype format: %s", best_match->src_entries[j].path);
                            continue;
                        }

                        log_debug("Trying src entry %d: %s (format: %s)", j,
                            best_match->src_entries[j].path, fmt ? fmt : "(none)");
                        face = load_local_font_file(uicon, best_match->src_entries[j].path, style);
                        if (face) {
                            log_info("Successfully loaded @font-face '%s' from %s",
                                family_name, best_match->src_entries[j].path);
                        }
                    }
                }
            }

            // Fallback to src_local_path if no src_entries or all failed
            if (!face && best_match->src_local_path) {
                log_debug("Trying fallback src_local_path: %s", best_match->src_local_path);
                face = load_local_font_file(uicon, best_match->src_local_path, style);
                if (face) {
                    log_info("Successfully loaded @font-face '%s' from fallback: %s",
                        family_name, best_match->src_local_path);
                } else {
                    log_warn("Failed to load @font-face file: %s", best_match->src_local_path);
                }
            }

            if (face) {
                best_match->loaded_face = face;
                best_match->is_loaded = true;
                if (is_fallback) *is_fallback = false;
                result_face = face;
            } else {
                log_warn("All font sources failed for @font-face: %s", family_name);
            }
        }
    }

    // Fall back to system fonts if no @font-face match (and not already found)
    if (!result_face) {
        clog_debug(font_log, "No @font-face match found, falling back to system fonts for: %s", family_name);
        if (is_fallback) *is_fallback = true;

        // Early-exit optimization: Check if font family exists in database before expensive lookups
        ArrayList* family_matches = font_database_find_all_matches(uicon->font_db, family_name);
        bool family_exists = (family_matches && family_matches->length > 0);

        if (family_exists) {
            arraylist_free(family_matches);
            // load_styled_font already caches its results, so we skip caching here
            strbuf_free(cache_key);
            return load_styled_font(uicon, family_name, style);
        } else {
            // Font doesn't exist in database - skip expensive platform lookup
            clog_info(font_log, "Font family '%s' not in database, skipping platform lookup", family_name);
            if (family_matches) arraylist_free(family_matches);
        }
    }

    // Cache the @font-face result (don't cache system font results - they're cached by load_styled_font)
    // Only cache non-NULL results to avoid FT_Done_Face on NULL during cleanup
    if (uicon->fontface_map && result_face) {
        char* name = (char*)mem_alloc(cache_key->length + 1, MEM_CAT_LAYOUT);
        memcpy(name, cache_key->str, cache_key->length);
        name[cache_key->length] = '\0';
        FontfaceEntry new_entry = {.name = name, .face = result_face};
        hashmap_set(uicon->fontface_map, &new_entry);
    }
    strbuf_free(cache_key);

    return result_face;
}

// Enhanced font matching (basic implementation)
float calculate_font_match_score(FontFaceDescriptor* descriptor, FontMatchCriteria* criteria) {
    if (!descriptor || !criteria) {
        return 0.0f;
    }

    float score = 0.0f;

    // Family name match (most important)
    if (descriptor->family_name && criteria->family_name) {
        if (strcmp(descriptor->family_name, criteria->family_name) == 0) {
            score += 0.5f;
        }
    }

    // Style match
    if (descriptor->font_style == criteria->style) {
        score += 0.25f;
    }

    // Weight match
    if (descriptor->font_weight == criteria->weight) {
        score += 0.25f;
    }

    clog_debug(font_log, "Font match score for %s: %.2f", descriptor->family_name, score);
    return score;
}

FontMatchResult find_best_font_match(UiContext* uicon, FontMatchCriteria* criteria) {
    FontMatchResult result = {0};

    if (!uicon || !criteria) {
        return result;
    }

    clog_debug(font_log, "Finding best font match for: %s", criteria->family_name);

    // For now, fall back to existing font loading
    // In a full implementation, we would search @font-face descriptors
    FontProp font_prop = {
        .font_size = (float)criteria->size,
        .font_style = criteria->style,
        .font_weight = criteria->weight
    };
    result.face = load_styled_font(uicon, criteria->family_name, &font_prop);
    result.match_score = result.face ? 1.0f : 0.0f;
    result.is_exact_match = result.face != NULL;
    result.requires_synthesis = false;
    result.supports_codepoint = true; // Assume yes for now

    return result;
}

// Font fallback chain management (basic implementation)
FontFallbackChain* build_fallback_chain(UiContext* uicon, const char* css_font_family) {
    if (!uicon || !css_font_family) {
        return NULL;
    }

    FontFallbackChain* chain = (FontFallbackChain*)mem_alloc(sizeof(FontFallbackChain), MEM_CAT_LAYOUT);
    if (!chain) {
        clog_error(font_log, "Failed to allocate FontFallbackChain");
        return NULL;
    }

    memset(chain, 0, sizeof(FontFallbackChain));

    // Simple implementation: just use the requested family + system fallbacks
    chain->family_count = 1;
    chain->family_names = (char**)mem_alloc(sizeof(char*), MEM_CAT_LAYOUT);
    chain->family_names[0] = mem_strdup(css_font_family, MEM_CAT_LAYOUT);
    chain->system_fonts = uicon->fallback_fonts; // Use existing fallback fonts
    chain->cache_enabled = true;

    clog_debug(font_log, "Built fallback chain for: %s", css_font_family);
    return chain;
}

bool font_supports_codepoint(FT_Face face, uint32_t codepoint) {
    if (!face) {
        return false;
    }

    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    bool supports = (char_index > 0);

    clog_debug(font_log, "Font %s %s codepoint U+%04X",
              face->family_name, supports ? "supports" : "does not support", codepoint);

    return supports;
}

FT_Face resolve_font_for_codepoint(FontFallbackChain* chain, uint32_t codepoint, FontProp* style) {
    if (!chain) {
        return NULL;
    }

    clog_debug(font_log, "Resolving font for codepoint U+%04X", codepoint);

    // Check cache first
    if (chain->cache_enabled && chain->codepoint_font_cache) {
        FT_Face* cached_face = (FT_Face*)hashmap_get(chain->codepoint_font_cache, &codepoint);
        if (cached_face && *cached_face) {
            clog_debug(font_log, "Font cache hit for codepoint U+%04X", codepoint);
            return *cached_face;
        }
    }

    // Search through system fallback fonts
    // chain->system_fonts is an array of font family names from ui_context.cpp
    if (chain->system_fonts) {
        for (char** font_ptr = chain->system_fonts; *font_ptr != NULL; font_ptr++) {
            const char* fallback_font = *font_ptr;
            clog_debug(font_log, "  Trying fallback font: %s for U+%04X", fallback_font, codepoint);

            // Try to load this font
            // We need access to UiContext to load fonts, but it's not in chain
            // For now, we'll store the best candidate and return NULL
            // The calling code (find_font_for_codepoint in text_metrics.cpp) will handle loading
            clog_debug(font_log, "  Font: %s is a candidate for U+%04X", fallback_font, codepoint);
        }
    }

    clog_debug(font_log, "Font fallback chain resolution: no font cached, caller should try fallbacks");
    return NULL;
}

void cache_codepoint_font_mapping(FontFallbackChain* chain, uint32_t codepoint, FT_Face face) {
    if (!chain || !face) {
        return;
    }

    if (!chain->codepoint_font_cache) {
        chain->codepoint_font_cache = hashmap_new(sizeof(uint32_t) + sizeof(FT_Face), 256, 0, 0,
                                                 NULL, NULL, NULL, NULL);
    }

    if (chain->codepoint_font_cache) {
        struct { uint32_t codepoint; FT_Face face; } entry = {codepoint, face};
        hashmap_set(chain->codepoint_font_cache, &entry);

        clog_debug(font_log, "Cached font mapping: U+%04X -> %s", codepoint, face->family_name);
    }
}

// Enhanced metrics functions (basic implementation)
void compute_enhanced_font_metrics(EnhancedFontBox* fbox) {
    if (!fbox || !fbox->face) {
        return;
    }

    if (fbox->metrics_computed) {
        return; // Already computed
    }

    FT_Face face = fbox->face;
    EnhancedFontMetrics* metrics = &fbox->metrics;

    // Basic metrics from FreeType
    metrics->ascender = face->size->metrics.ascender / 64.0;
    metrics->descender = face->size->metrics.descender / 64.0;
    metrics->height = face->size->metrics.height / 64.0;
    metrics->line_gap = metrics->height - (metrics->ascender - metrics->descender);

    // OpenType metrics (if available)
    if (face->face_flags & FT_FACE_FLAG_SFNT) {
        // These would need proper OpenType table parsing
        metrics->typo_ascender = metrics->ascender;
        metrics->typo_descender = metrics->descender;
        metrics->typo_line_gap = metrics->line_gap;
        metrics->win_ascent = metrics->ascender;
        metrics->win_descent = -metrics->descender;
        metrics->hhea_ascender = metrics->ascender;
        metrics->hhea_descender = metrics->descender;
        metrics->hhea_line_gap = metrics->line_gap;
    }

    // Estimate x-height and cap-height
    // In a full implementation, these would be read from the font's OS/2 table
    metrics->x_height = metrics->ascender * 0.5; // Rough estimate
    metrics->cap_height = metrics->ascender * 0.7; // Rough estimate

    metrics->baseline_offset = 0; // No adjustment by default
    metrics->metrics_computed = true;
    fbox->metrics_computed = true;

    clog_debug(font_log, "Computed enhanced metrics for %s: asc=%d, desc=%d, height=%d",
              face->family_name, metrics->ascender, metrics->descender, metrics->height);
}

int calculate_line_height_from_css(EnhancedFontBox* fbox, CssEnum line_height_css) {
    if (!fbox) {
        return 0;
    }

    compute_enhanced_font_metrics(fbox);

    // For now, use basic calculation
    // In a full implementation, this would handle CSS line-height values properly
    int base_height = fbox->metrics.height;

    clog_debug(font_log, "Calculated line height: %d", base_height);
    return base_height;
}

// High-DPI display support functions
void apply_pixel_ratio_to_font_metrics(EnhancedFontBox* fbox, float pixel_ratio) {
    if (!fbox || pixel_ratio <= 0.0f) {
        return;
    }

    fbox->pixel_ratio = pixel_ratio;
    fbox->high_dpi_aware = (pixel_ratio > 1.0f);

    clog_debug(font_log, "Applied pixel ratio %.2f to font metrics", pixel_ratio);
}

int scale_font_size_for_display(int base_size, float pixel_ratio) {
    if (pixel_ratio <= 0.0f) {
        return base_size;
    }

    int scaled_size = (int)(base_size * pixel_ratio);
    clog_debug(font_log, "Scaled font size: %d -> %d (ratio: %.2f)", base_size, scaled_size, pixel_ratio);
    return scaled_size;
}

void ensure_pixel_ratio_compatibility(EnhancedFontBox* fbox) {
    if (!fbox) {
        return;
    }

    // Ensure existing pixel_ratio handling is preserved
    if (fbox->pixel_ratio > 0.0f && fbox->high_dpi_aware) {
        clog_debug(font_log, "Pixel ratio compatibility ensured: %.2f", fbox->pixel_ratio);
    }
}

// Enhanced font system integration
void setup_font_enhanced(UiContext* uicon, EnhancedFontBox* fbox,
                        const char* font_name, FontProp* fprop) {
    if (!uicon || !fbox || !fprop) {
        clog_error(font_log, "Invalid parameters for setup_font_enhanced");
        return;
    }

    // Initialize enhanced font box
    memset(fbox, 0, sizeof(EnhancedFontBox));
    fbox->style = *fprop;
    fbox->current_font_size = fprop->font_size;
    fbox->cache_enabled = true;
    fbox->pixel_ratio = uicon->pixel_ratio; // Preserve existing pixel_ratio
    fbox->high_dpi_aware = (uicon->pixel_ratio > 1.0f);

    // Load font face
    fbox->face = load_styled_font(uicon, fprop->family ? fprop->family : font_name, fprop);

    if (fbox->face) {
        // Calculate space width
        FT_Int32 load_flags = (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING); // FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL;
        if (FT_Load_Char(fbox->face, ' ', load_flags)) {
            clog_warn(font_log, "Could not load space character for %s", font_name);
            // Handle WOFF fonts where y_ppem=0
            float ppem = fbox->face->size->metrics.y_ppem / 64.0f;
            if (ppem <= 0 && fbox->face->size && fbox->face->size->metrics.height > 0) {
                ppem = fbox->face->size->metrics.height / 64.0f / 1.2f;
            }
            fbox->space_width = ppem > 0 ? ppem : fprop->font_size * fbox->pixel_ratio;
        } else {
            fbox->space_width = fbox->face->glyph->advance.x / 64.0;
        }

        // Compute enhanced metrics
        compute_enhanced_font_metrics(fbox);

        clog_info(font_log, "Enhanced font setup complete: %s (size: %d, pixel_ratio: %.2f)",
                 font_name, fprop->font_size, fbox->pixel_ratio);
    } else {
        clog_error(font_log, "Failed to load font face for enhanced setup: %s", font_name);
    }
}
