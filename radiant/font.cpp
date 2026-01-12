
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "view.hpp"
#include "font_face.h"
#include "font_lookup_platform.h"
#include "../lib/log.h"
#include "../lib/font_config.h"
#include "../lib/memtrack.h"

/**
 * Resolve CSS generic font family to system font names.
 * Chrome default fonts with cross-platform fallbacks:
 * - serif: Times New Roman (Mac/Win) → Liberation Serif (Linux)
 * - sans-serif: Arial (Mac/Win) → Liberation Sans (Linux)
 * - monospace: Courier New (Mac/Win) → Liberation Mono (Linux)
 * Returns a list of font names to try in order.
 */
static const char** resolve_generic_family(const char* family) {
    // Cross-platform font families (Mac → Linux equivalents)
    static const char* serif_fonts[] = {
        "Times New Roman", "Liberation Serif", "Times", "Nimbus Roman", "Georgia", "DejaVu Serif", NULL
    };
    static const char* sans_serif_fonts[] = {
        "Arial", "Liberation Sans", "Helvetica", "Nimbus Sans", "DejaVu Sans", NULL
    };
    static const char* monospace_fonts[] = {
        "Menlo", "Monaco", "Courier New", "Liberation Mono", "Courier", "Nimbus Mono PS", "DejaVu Sans Mono", NULL
    };
    static const char* cursive_fonts[] = {"Comic Sans MS", "Apple Chancery", NULL};
    static const char* fantasy_fonts[] = {"Impact", "Papyrus", NULL};
    // Modern CSS generic families (CSS Fonts Level 4)
    // ui-monospace: platform's default monospace UI font
    static const char* ui_monospace_fonts[] = {
        "SF Mono", "Menlo", "Monaco", "Consolas", "Liberation Mono", "Courier New", NULL
    };
    // system-ui: platform's default system UI font
    static const char* system_ui_fonts[] = {
        "SF Pro Display", "SF Pro", ".AppleSystemUIFont", "Segoe UI", "Roboto", "Liberation Sans", "Arial", NULL
    };

    if (!family) return NULL;

    if (strcmp(family, "serif") == 0) return serif_fonts;
    if (strcmp(family, "sans-serif") == 0) return sans_serif_fonts;
    if (strcmp(family, "monospace") == 0) return monospace_fonts;
    if (strcmp(family, "cursive") == 0) return cursive_fonts;
    if (strcmp(family, "fantasy") == 0) return fantasy_fonts;
    // Modern CSS Fonts Level 4 generic families
    if (strcmp(family, "ui-monospace") == 0) return ui_monospace_fonts;
    if (strcmp(family, "system-ui") == 0) return system_ui_fonts;
    if (strcmp(family, "ui-serif") == 0) return serif_fonts;
    if (strcmp(family, "ui-sans-serif") == 0) return sans_serif_fonts;
    if (strcmp(family, "ui-rounded") == 0) return sans_serif_fonts;
    // Apple/Safari-specific system font keywords (treat same as system-ui)
    // -apple-system: Apple's system font (San Francisco on macOS/iOS)
    // BlinkMacSystemFont: Chrome's equivalent for macOS system font
    if (strcmp(family, "-apple-system") == 0) return system_ui_fonts;
    if (strcmp(family, "BlinkMacSystemFont") == 0) return system_ui_fonts;

    // Cross-platform font aliases (map Windows/Mac fonts to Linux equivalents)
    // These are not generic families but common specific fonts that need cross-platform mapping
    if (strcmp(family, "Times New Roman") == 0 || strcmp(family, "Times") == 0) {
        return serif_fonts;  // Times → Liberation Serif on Linux
    }
    if (strcmp(family, "Arial") == 0 || strcmp(family, "Helvetica") == 0) {
        return sans_serif_fonts;  // Arial/Helvetica → Liberation Sans on Linux
    }
    if (strcmp(family, "Courier New") == 0 || strcmp(family, "Courier") == 0) {
        return monospace_fonts;  // Courier → Liberation Mono on Linux
    }

    return NULL;  // not a generic family or known alias
}

// Glyph fallback cache: caches which FT_Face (from fallback fonts) can render a given codepoint
typedef struct GlyphFallbackEntry {
    uint32_t codepoint;
    FT_Face fallback_face;  // NULL if no fallback found (negative cache)
} GlyphFallbackEntry;

static int glyph_fallback_compare(const void *a, const void *b, void *udata) {
    const GlyphFallbackEntry *ga = (const GlyphFallbackEntry*)a;
    const GlyphFallbackEntry *gb = (const GlyphFallbackEntry*)b;
    return ga->codepoint == gb->codepoint ? 0 : (ga->codepoint < gb->codepoint ? -1 : 1);
}

static uint64_t glyph_fallback_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const GlyphFallbackEntry *entry = (const GlyphFallbackEntry*)item;
    return hashmap_xxhash3(&entry->codepoint, sizeof(entry->codepoint), seed0, seed1);
}

typedef struct FontfaceEntry {
    char* name;
    FT_Face face;
} FontfaceEntry;

int fontface_compare(const void *a, const void *b, void *udata) {
    const FontfaceEntry *fa = (const FontfaceEntry*)a;
    const FontfaceEntry *fb = (const FontfaceEntry*)b;
    return strcmp(fa->name, fb->name);
}

uint64_t fontface_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontfaceEntry *fontface = (const FontfaceEntry*)item;
    // xxhash3 is a fast hash function
    // log_debug("hashing fontface: %s", fontface->name);  // Too verbose - called on every hash lookup
    return hashmap_xxhash3(fontface->name, strlen(fontface->name), seed0, seed1);
}

char* load_font_path(FontDatabase *font_db, const char* font_name) {
    if (!font_db || !font_name) {
        log_warn("Invalid parameters: font_db=%p, font_name=%p", font_db, font_name);
        return NULL;
    }

    // Simple font lookup by family name - find any font in the family
    ArrayList* matches = font_database_find_all_matches(font_db, font_name);
    if (!matches || matches->length == 0) {
        if (font_log) {
            clog_warn(font_log, "Font not found in database: %s", font_name);
        } else {
            log_warn("Font not found in database: %s", font_name);
        }
        if (matches) arraylist_free(matches);

        // Fallback: Try platform-specific font lookup
        char* result = find_font_path_fallback(font_name);
        if (result) {
            return result;
        }
        return NULL;
    }

    // Prefer Regular/Normal style font over Bold/Italic variants
    // Iterate through matches to find the best match
    FontEntry* best_font = nullptr;
    int best_score = -1;
    for (int i = 0; i < matches->length; i++) {
        FontEntry* font = (FontEntry*)matches->data[i];
        int score = 0;
        // Prefer non-italic, non-bold fonts
        if (font->weight == 400) score += 10;  // Regular weight
        else if (font->weight < 500) score += 5;  // Light to normal weight
        if (font->style == FONT_STYLE_NORMAL) score += 10;  // Not italic
        // Avoid TTC files for ThorVG (it doesn't support TrueType Collections)
        if (font->file_path && !strstr(font->file_path, ".ttc")) score += 5;
        
        if (score > best_score) {
            best_score = score;
            best_font = font;
        }
    }
    
    FontEntry* font = best_font ? best_font : (FontEntry*)matches->data[0];
    char* result = mem_strdup(font->file_path, MEM_CAT_FONT);
    arraylist_free(matches);
    return result;
}

FT_Face load_font_face(UiContext* uicon, const char* font_name, float font_size) {
    // check the hashmap first
    if (uicon->fontface_map == NULL) {
        // create a new hash map. 2nd argument is the initial capacity.
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0,
            fontface_hash, fontface_compare, NULL, NULL);
    }
    StrBuf* name_and_size = strbuf_create(font_name);
    strbuf_append_str(name_and_size, ":");
    strbuf_append_int(name_and_size, (int)font_size);
    FontfaceEntry search_key = {.name = name_and_size->str, .face = NULL};
    FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, &search_key);
    if (entry) {
        strbuf_free(name_and_size);
        return entry->face;
    }

    FT_Face face = NULL;
    char* font_path = load_font_path(uicon->font_db, font_name);
    if (font_path) {
        // load the font
        log_font_loading_attempt(font_name, font_path);
        if (FT_New_Face(uicon->ft_library, (const char *)font_path, 0, &face)) {
            log_font_loading_result(font_name, false, "FreeType error");
            face = NULL;
        } else {
            // For color emoji fonts (like Apple Color Emoji) with fixed bitmap sizes,
            // we need to use FT_Select_Size instead of FT_Set_Pixel_Sizes
            if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
                (face->face_flags & FT_FACE_FLAG_COLOR) &&
                face->num_fixed_sizes > 0) {
                // Find the best matching fixed size for the requested font_size
                int best_idx = 0;
                int best_diff = INT_MAX;
                for (int i = 0; i < face->num_fixed_sizes; i++) {
                    int ppem = face->available_sizes[i].y_ppem >> 6;
                    int diff = abs(ppem - (int)font_size);
                    if (diff < best_diff) {
                        best_diff = diff;
                        best_idx = i;
                    }
                }
                FT_Select_Size(face, best_idx);
                log_debug("Color emoji font loaded: %s, selected fixed size index: %d (ppem: %ld)",
                    font_name, best_idx, face->available_sizes[best_idx].y_ppem >> 6);
            } else {
                // Set height of the font
                FT_Set_Pixel_Sizes(face, 0, font_size);
                log_debug("Font loaded: %s, size: %dpx", font_name, font_size);
            }
            // Set font size using 26.6 fixed point for sub-pixel precision
            // Convert float font_size to 26.6 fixed point (multiply by 64)
            // FT_F26Dot6 char_size = (FT_F26Dot6)(font_size * 64.0);
            // FT_Set_Char_Size(face, 0, char_size, 96, 96); // 96 DPI for screen
            // log_debug("Font loaded: %s, size: %.1fpx (26.6 fixed: %ld)", font_name, font_size, char_size);

            // put the font face into the hashmap
            if (uicon->fontface_map) {
                // copy the font name
                char* name = (char*)mem_alloc(name_and_size->length + 1, MEM_CAT_FONT);
                memcpy(name, name_and_size->str, name_and_size->length);
                name[name_and_size->length] = '\0';
                FontfaceEntry new_entry = {.name=name, .face=face};
                hashmap_set(uicon->fontface_map, &new_entry);
            }
        }
        mem_free(font_path);
    }
    strbuf_free(name_and_size);
    // units_per_EM is the font design size, and does not change with font pixel size
    if (face) {
        log_info("Font loaded: %s, height:%f, ascend:%f, descend:%f, em size: %f",
            face->family_name, face->size->metrics.height / 64.0,
            face->size->metrics.ascender / 64.0, face->size->metrics.descender / 64.0, face->units_per_EM / 64.0);
    } else {
        log_error("Failed to load font: %s", font_name);
    }
    return face;
}

FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style) {
    // Apply pixel ratio to get physical pixel size for HiDPI displays
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    float physical_font_size = font_style->font_size * pixel_ratio;

    log_debug("[FONT LOAD] font=%s, css_size=%.2f, pixel_ratio=%.2f, physical_size=%.2f",
              font_name, font_style->font_size, pixel_ratio, physical_font_size);

    // Create cache key with (family, weight, style, physical_size) - deterministic based on input parameters
    StrBuf* style_cache_key = strbuf_create(font_name);
    strbuf_append_str(style_cache_key, font_style->font_weight == CSS_VALUE_BOLD ? ":bold:" : ":normal:");
    strbuf_append_str(style_cache_key, font_style->font_style == CSS_VALUE_ITALIC ? "italic:" : "normal:");
    strbuf_append_int(style_cache_key, (int)physical_font_size);

    // Initialize fontface map if needed
    if (uicon->fontface_map == NULL) {
        uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0,
            fontface_hash, fontface_compare, NULL, NULL);
    }

    // Check cache first - this avoids expensive database lookup for repeated fonts
    if (uicon->fontface_map) {
        FontfaceEntry search_key = {.name = style_cache_key->str, .face = NULL};
        FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, &search_key);
        if (entry) {
            strbuf_free(style_cache_key);
            return entry->face;  // cache hit - skip database lookup
        }
    }

    // Cache miss - do the full database lookup
    FontDatabaseCriteria criteria;
    memset(&criteria, 0, sizeof(criteria));
    strncpy(criteria.family_name, font_name, sizeof(criteria.family_name) - 1);
    criteria.weight = (font_style->font_weight == CSS_VALUE_BOLD) ? 700 : 400;
    criteria.style = (font_style->font_style == CSS_VALUE_ITALIC) ? FONT_STYLE_ITALIC : FONT_STYLE_NORMAL;

    FontDatabaseResult result = font_database_find_best_match(uicon->font_db, &criteria);
    FT_Face face = NULL;

    const float SCORE_THRESHOLD = 0.5f;
    bool use_database_result = (result.font && result.font->file_path && result.match_score >= SCORE_THRESHOLD);

    if (use_database_result) {
        FontEntry* font = result.font;
        FT_Long face_index = font->is_collection ? font->collection_index : 0;
        log_debug("[FONT PATH] Loading font: %s from path: %s (index=%ld)", font_name, font->file_path, face_index);
        if (FT_New_Face(uicon->ft_library, font->file_path, face_index, &face) == 0) {
            // For color emoji fonts with fixed bitmap sizes, use FT_Select_Size
            if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
                (face->face_flags & FT_FACE_FLAG_COLOR) &&
                face->num_fixed_sizes > 0) {
                int best_idx = 0, best_diff = INT_MAX;
                for (int i = 0; i < face->num_fixed_sizes; i++) {
                    int ppem = face->available_sizes[i].y_ppem >> 6;
                    int diff = abs(ppem - (int)physical_font_size);
                    if (diff < best_diff) { best_diff = diff; best_idx = i; }
                }
                FT_Select_Size(face, best_idx);
            } else {
                FT_Set_Pixel_Sizes(face, 0, physical_font_size);
            }
            log_info("Loading styled font: %s (family: %s, weight: %d, style: %s, physical_size: %.0f)",
                font_name, font->family_name, font->weight, font_style_to_string(font->style), physical_font_size);
        } else {
            log_error("Failed to load font face for: %s (found font: %s)", font_name, font->file_path);
        }
    } else {
        // Font not found in database, fall back to platform-specific lookup
        char* font_path = find_font_path_fallback(font_name);
        if (font_path) {
            if (FT_New_Face(uicon->ft_library, font_path, 0, &face) == 0) {
                // For color emoji fonts with fixed bitmap sizes, use FT_Select_Size
                if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
                    (face->face_flags & FT_FACE_FLAG_COLOR) &&
                    face->num_fixed_sizes > 0) {
                    int best_idx = 0, best_diff = INT_MAX;
                    for (int i = 0; i < face->num_fixed_sizes; i++) {
                        int ppem = face->available_sizes[i].y_ppem >> 6;
                        int diff = abs(ppem - (int)physical_font_size);
                        if (diff < best_diff) { best_diff = diff; best_idx = i; }
                    }
                    FT_Select_Size(face, best_idx);
                } else {
                    FT_Set_Pixel_Sizes(face, 0, physical_font_size);
                }
                log_info("Loaded font via platform lookup: %s (path: %s, physical_size: %.0f)", font_name, font_path, physical_font_size);
            } else {
                log_error("Failed to load font face via platform lookup: %s (path: %s)", font_name, font_path);
            }
            mem_free(font_path);
        } else {
            log_error("Platform lookup also failed for: %s", font_name);
        }
    }

    // Cache result under style key for fast lookup on next call
    // Cache both successful (face != NULL) and failed (face == NULL) lookups to avoid retrying
    {
        char* name = (char*)mem_alloc(style_cache_key->length + 1, MEM_CAT_FONT);
        memcpy(name, style_cache_key->str, style_cache_key->length);
        name[style_cache_key->length] = '\0';
        FontfaceEntry new_entry = {.name=name, .face=face};
        hashmap_set(uicon->fontface_map, &new_entry);
    }
    strbuf_free(style_cache_key);
    return face;
}

FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint, bool for_rendering) {
    FT_GlyphSlot slot = NULL;  FT_Error error;
    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);

    // Debug: Log the face's current pixel size and glyph metrics
    static int debug_count = 0;
    if (for_rendering && debug_count < 100) {
        // y_ppem is in pixels, height is in 26.6 fixed-point
        log_debug("[GLYPH LOAD] face=%s, char_index=%u, y_ppem=%d, height=%.1f, css_size=%.2f, codepoint=U+%04X",
                  face->family_name,
                  char_index,
                  face->size ? face->size->metrics.y_ppem : 0,
                  face->size ? (face->size->metrics.height / 64.0) : 0,
                  font_style ? font_style->font_size : 0.0f, codepoint);
        debug_count++;
    }

    // FT_LOAD_NO_HINTING matches browser closely, whereas FT_LOAD_FORCE_AUTOHINT makes the text narrower
    // FT_LOAD_COLOR is required for color emoji fonts (Apple Color Emoji, Noto Color Emoji, etc.)
    FT_Int32 load_flags = for_rendering ? (FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_COLOR) : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING | FT_LOAD_COLOR);
    if (char_index > 0) {
        error = FT_Load_Glyph(face, char_index, load_flags);
        if (!error) { slot = face->glyph;  return slot; }
    }

    // failed to load glyph under current font, check fallback cache first
    if (uicon->glyph_fallback_cache == NULL) {
        uicon->glyph_fallback_cache = hashmap_new(sizeof(GlyphFallbackEntry), 256, 0, 0,
            glyph_fallback_hash, glyph_fallback_compare, NULL, NULL);
    }

    // check cache for this codepoint
    GlyphFallbackEntry search_key = {.codepoint = codepoint, .fallback_face = NULL};
    GlyphFallbackEntry* cached = (GlyphFallbackEntry*)hashmap_get(uicon->glyph_fallback_cache, &search_key);
    if (cached) {
        if (cached->fallback_face == NULL) {
            // negative cache hit - we already know no fallback has this glyph
            return NULL;
        }
        // cache hit - use the cached fallback face
        char_index = FT_Get_Char_Index(cached->fallback_face, codepoint);
        if (char_index > 0) {
            error = FT_Load_Glyph(cached->fallback_face, char_index, load_flags);
            if (!error) {
                slot = cached->fallback_face->glyph;
                return slot;
            }
        }
    }

    // cache miss - search through fallback fonts
    log_debug("Failed to load glyph: U+%04X", codepoint);
    char** font_ptr = uicon->fallback_fonts;
    while (*font_ptr) {
        log_debug("Trying fallback font '%s' for char: U+%04X", *font_ptr, codepoint);
        FT_Face fallback_face = load_styled_font(uicon, *font_ptr, font_style);
        if (fallback_face) {
            char_index = FT_Get_Char_Index(fallback_face, codepoint);
            if (char_index > 0) {
                error = FT_Load_Glyph(fallback_face, char_index, load_flags);
                if (!error) {
                    log_font_fallback_triggered(face ? face->family_name : "unknown", *font_ptr);
                    // cache this codepoint -> fallback_face mapping
                    GlyphFallbackEntry new_entry = {.codepoint = codepoint, .fallback_face = fallback_face};
                    hashmap_set(uicon->glyph_fallback_cache, &new_entry);
                    slot = fallback_face->glyph;
                    return slot;
                }
            }
            log_debug("Failed to load glyph from fallback font: %s, U+%04X", *font_ptr, codepoint);
        }
        font_ptr++;
    }

    // negative cache - no fallback font has this glyph
    GlyphFallbackEntry negative_entry = {.codepoint = codepoint, .fallback_face = NULL};
    hashmap_set(uicon->glyph_fallback_cache, &negative_entry);
    return NULL;
}

void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    fbox->style = fprop;
    fbox->current_font_size = fprop->font_size;

    // Try @font-face descriptors first, then fall back to system fonts
    const char* family_to_load = fprop->family;
    bool is_fallback = false;
    fbox->ft_face = load_font_with_descriptors(uicon, family_to_load, fprop, &is_fallback);

    // If @font-face loading failed, fall back to original method
    if (!fbox->ft_face) {
        // Check if this is a CSS generic font family (serif, sans-serif, monospace, etc.)
        const char** generic_fonts = resolve_generic_family(family_to_load);
        if (generic_fonts) {
            // Try each font in the generic family's preference list
            for (int i = 0; generic_fonts[i] && !fbox->ft_face; i++) {
                log_debug("Resolving generic family '%s' to '%s'", family_to_load, generic_fonts[i]);
                fbox->ft_face = load_styled_font(uicon, generic_fonts[i], fprop);
                if (fbox->ft_face) {
                    log_info("Resolved generic family '%s' to '%s'", family_to_load, generic_fonts[i]);
                }
            }
        } else {
            // Not a generic family - check database for exact match
            ArrayList* family_matches = font_database_find_all_matches(uicon->font_db, family_to_load);
            bool family_exists = (family_matches && family_matches->length > 0);
            int match_count = family_matches ? family_matches->length : 0;

            if (family_exists) {
                // Family exists in database - do full styled lookup (weight, style matching)
                log_debug("Font family '%s' exists in database (%d matches), doing styled lookup", family_to_load, match_count);
                arraylist_free(family_matches);
                fbox->ft_face = load_styled_font(uicon, family_to_load, fprop);
            } else {
                // Family doesn't exist in database - skip expensive platform lookup, go straight to fallbacks
                log_debug("Font family '%s' not in database, skipping styled lookup (early-exit)", family_to_load);
                if (family_matches) arraylist_free(family_matches);
            }
        }
    }

    // If font loading failed, try fallback fonts
    if (!fbox->ft_face) {
        log_debug("Font '%s' not found, trying fallbacks...", family_to_load);

        // Try common cross-platform fallback fonts (prioritize Liberation/DejaVu on Linux, system fonts on Mac)
        const char* fallbacks[] = {
            "Liberation Sans",  // Common on Linux (Arial equivalent)
            "DejaVu Sans",      // Common on Linux
            "Helvetica",        // Common on macOS
            "Arial",            // Common on Windows/Mac
            "SF Pro Display",   // New macOS default
            "Arial Unicode MS", // Available on most systems
            "Liberation Serif", // Linux serif fallback (Times equivalent)
            "Times New Roman",  // Mac/Win serif fallback
            "Nimbus Sans",      // Linux sans fallback
            "AppleSDGothicNeo", // We know this one exists from our scan
            NULL
        };

        for (int i = 0; fallbacks[i] && !fbox->ft_face; i++) {
            log_debug("Trying fallback font: %s", fallbacks[i]);
            fbox->ft_face = load_styled_font(uicon, fallbacks[i], fprop);
            if (fbox->ft_face) {
                log_info("Using fallback font: %s for requested font: %s", fallbacks[i], family_to_load);
                break;
            }
        }
    }

    if (!fbox->ft_face) {
        log_error("Failed to setup font: %s (and all fallbacks)", family_to_load);
        return;
    }

    // Pixel ratio for converting physical font metrics back to CSS pixels for layout
    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;

    // Use sub-pixel rendering flags for better quality
    FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
    if (FT_Load_Char(fbox->ft_face, ' ', load_flags)) {
        log_warn("Could not load space character for font: %s", family_to_load);
        // Fallback: use y_ppem if available, otherwise derive from font_size
        float ppem = fbox->ft_face->size->metrics.y_ppem / 64.0f;
        if (ppem <= 0) {
            // y_ppem is 0 (common with WOFF fonts), use requested font size
            ppem = fprop->font_size * pixel_ratio;
        }
        fbox->style->space_width = ppem / pixel_ratio;
    } else {
        // Use float precision for space width calculation
        // Metrics from FreeType are in physical pixels, scale back to CSS pixels for layout
        fbox->style->space_width = (fbox->ft_face->glyph->advance.x / 64.0) / pixel_ratio;
    }
    FT_Bool use_kerning = FT_HAS_KERNING(fbox->ft_face);
    fbox->style->has_kerning = use_kerning;
    // Scale font metrics from physical pixels back to CSS pixels for layout
    fbox->style->ascender = (fbox->ft_face->size->metrics.ascender / 64.0) / pixel_ratio;
    fbox->style->descender = (-fbox->ft_face->size->metrics.descender / 64.0) / pixel_ratio;
    fbox->style->font_height = (fbox->ft_face->size->metrics.height / 64.0) / pixel_ratio;
    // Font setup complete - logging removed to avoid hot path overhead
}

bool fontface_entry_free(const void *item, void *udata) {
    FontfaceEntry* entry = (FontfaceEntry*)item;
    mem_free(entry->name);
    FT_Done_Face(entry->face);
    return true;
}

void fontface_cleanup(UiContext* uicon) {
    // loop through the hashmap and free the font faces
    if (uicon->fontface_map) {
        log_info("Cleaning up font faces");
        hashmap_scan(uicon->fontface_map, fontface_entry_free, NULL);
        hashmap_free(uicon->fontface_map);
        uicon->fontface_map = NULL;
    }
    // free glyph fallback cache (no resources to free per entry, just the hashmap)
    if (uicon->glyph_fallback_cache) {
        hashmap_free(uicon->glyph_fallback_cache);
        uicon->glyph_fallback_cache = NULL;
    }
}

// todo: cache glyph advance_x
