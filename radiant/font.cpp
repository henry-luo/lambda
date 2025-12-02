
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "view.hpp"
#include "font_face.h"
#include "font_lookup_platform.h"
#include "../lib/log.h"
#include "../lib/font_config.h"

/* Explicit strdup declaration for compatibility */
extern char *strdup(const char *s);
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
    log_debug("hashing fontface: %s", fontface->name);
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
        log_debug("Font database lookup failed, trying platform-specific lookup for: %s", font_name);
        char* result = find_font_path_fallback(font_name);
        if (result) {
            log_debug("Found font via platform lookup: %s", result);
            return result;
        }
        return NULL;
    }

    // Just take the first match for now - could be enhanced to prefer normal weight/style
    FontEntry* font = (FontEntry*)matches->data[0];
    char* result = strdup(font->file_path);
    
    log_debug("Found font '%s' at: %s", font_name, font->file_path);
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
        log_debug("Fontface loaded from cache: %s", name_and_size->str);
        strbuf_free(name_and_size);
        return entry->face;
    }
    else {
        log_debug("Fontface not found in cache: %s", name_and_size->str);
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
            // Set height of the font
            FT_Set_Pixel_Sizes(face, 0, font_size);
            log_debug("Font loaded: %s, size: %dpx", font_name, font_size);
            // Set font size using 26.6 fixed point for sub-pixel precision
            // Convert float font_size to 26.6 fixed point (multiply by 64)
            // FT_F26Dot6 char_size = (FT_F26Dot6)(font_size * 64.0);
            // FT_Set_Char_Size(face, 0, char_size, 96, 96); // 96 DPI for screen
            // log_debug("Font loaded: %s, size: %.1fpx (26.6 fixed: %ld)", font_name, font_size, char_size);

            // put the font face into the hashmap
            if (uicon->fontface_map) {
                // copy the font name
                char* name = (char*)malloc(name_and_size->length + 1);
                memcpy(name, name_and_size->str, name_and_size->length);
                name[name_and_size->length] = '\0';
                FontfaceEntry new_entry = {.name=name, .face=face};
                hashmap_set(uicon->fontface_map, &new_entry);
            }
        }
        free(font_path);
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
    log_debug("load_styled_font: font_name='%s', font_weight=%d, CSS_VALUE_BOLD=%d",
        font_name, font_style->font_weight, CSS_VALUE_BOLD);
    
    // Use FontDatabaseCriteria to find font with specific weight and style
    FontDatabaseCriteria criteria;
    memset(&criteria, 0, sizeof(criteria));
    strncpy(criteria.family_name, font_name, sizeof(criteria.family_name) - 1);
    
    // Convert CSS weight to font weight
    if (font_style->font_weight == CSS_VALUE_BOLD) {
        criteria.weight = 700;  // Bold
    } else {
        criteria.weight = 400;  // Normal
    }
    
    // Convert CSS style to font style
    if (font_style->font_style == CSS_VALUE_ITALIC) {
        criteria.style = FONT_STYLE_ITALIC;
    } else {
        criteria.style = FONT_STYLE_NORMAL;
    }
    
    // Try to find a font matching the criteria
    FontDatabaseResult result = font_database_find_best_match(uicon->font_db, &criteria);
    FT_Face face = NULL;
    
    // If score is too low (< 0.5), the match is poor - try platform lookup instead
    const float SCORE_THRESHOLD = 0.5f;
    bool use_database_result = (result.font && result.font->file_path && result.match_score >= SCORE_THRESHOLD);
    
    if (use_database_result) {
        FontEntry* font = result.font;
        // Create cache key for this specific font file and size
        StrBuf* cache_key = strbuf_create(font->file_path);
        strbuf_append_str(cache_key, ":");
        strbuf_append_int(cache_key, (int)font_style->font_size);
        
        // Initialize fontface map if needed
        if (uicon->fontface_map == NULL) {
            uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0,
                fontface_hash, fontface_compare, NULL, NULL);
        }
        
        // Check cache first
        if (uicon->fontface_map) {
            FontfaceEntry search_key = {.name = cache_key->str, .face = NULL};
            FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, &search_key);
            if (entry) {
                log_debug("Fontface loaded from cache: %s", cache_key->str);
                strbuf_free(cache_key);
                return entry->face;
            }
        }
        
        // Load the font file (use collection_index for TTC files)
        FT_Long face_index = font->is_collection ? font->collection_index : 0;
        if (FT_New_Face(uicon->ft_library, font->file_path, face_index, &face) == 0) {
            FT_Set_Pixel_Sizes(face, 0, font_style->font_size);
            
            // Cache the loaded font
            if (uicon->fontface_map) {
                char* name = (char*)malloc(cache_key->length + 1);
                memcpy(name, cache_key->str, cache_key->length);
                name[cache_key->length] = '\0';
                FontfaceEntry new_entry = {.name=name, .face=face};
                hashmap_set(uicon->fontface_map, &new_entry);
            }
            
            log_info("Loading styled font: %s (family: %s, weight: %d, style: %s, %s), ascd: %f, desc: %f, em size: %f, font height: %f",
                font_name, font->family_name, font->weight, 
                font_style_to_string(font->style),
                font->is_collection ? "TTC" : "single",
                face->size->metrics.ascender / 64.0, face->size->metrics.descender / 64.0,
                face->units_per_EM / 64.0, face->size->metrics.height / 64.0);
        } else {
            log_error("Failed to load font face for: %s (found font: %s)", font_name, font->file_path);
        }
        
        strbuf_free(cache_key);
    } else {
        log_warn("Font database lookup failed for: %s (weight: %d, style: %d)", font_name, criteria.weight, criteria.style);
        
        // Fallback: Try platform-specific font lookup
        log_debug("Trying platform-specific lookup for: %s", font_name);
        char* font_path = find_font_path_fallback(font_name);
        if (font_path) {
            log_debug("Found font via platform lookup: %s", font_path);
            
            // Create cache key for this font
            StrBuf* cache_key = strbuf_create(font_path);
            strbuf_append_str(cache_key, ":");
            strbuf_append_int(cache_key, (int)font_style->font_size);
            
            // Initialize fontface map if needed
            if (uicon->fontface_map == NULL) {
                uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0,
                    fontface_hash, fontface_compare, NULL, NULL);
            }
            
            // Check cache first
            if (uicon->fontface_map) {
                FontfaceEntry search_key = {.name = cache_key->str, .face = NULL};
                FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, &search_key);
                if (entry) {
                    log_debug("Fontface loaded from cache (platform): %s", cache_key->str);
                    strbuf_free(cache_key);
                    free(font_path);
                    return entry->face;
                }
            }
            
            // Load the font file
            if (FT_New_Face(uicon->ft_library, font_path, 0, &face) == 0) {
                FT_Set_Pixel_Sizes(face, 0, font_style->font_size);
                
                // Cache the loaded font
                if (uicon->fontface_map) {
                    char* name = (char*)malloc(cache_key->length + 1);
                    memcpy(name, cache_key->str, cache_key->length);
                    name[cache_key->length] = '\0';
                    FontfaceEntry new_entry = {.name=name, .face=face};
                    hashmap_set(uicon->fontface_map, &new_entry);
                }
                
                log_info("Loaded font via platform lookup: %s (path: %s)", font_name, font_path);
            } else {
                log_error("Failed to load font face via platform lookup: %s (path: %s)", font_name, font_path);
            }
            
            strbuf_free(cache_key);
            free(font_path);
        } else {
            log_error("Platform lookup also failed for: %s", font_name);
        }
    }
    
    return face;
}

FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint, bool for_rendering) {
    FT_GlyphSlot slot = NULL;  FT_Error error;
    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    // FT_LOAD_NO_HINTING matches browser closely, whereas FT_LOAD_FORCE_AUTOHINT makes the text narrower
    FT_Int32 load_flags = for_rendering ? (FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
    if (char_index > 0) {
        error = FT_Load_Glyph(face, char_index, load_flags);
        if (!error) { slot = face->glyph;  return slot; }
    }

    // failed to load glyph under current font, try fallback fonts
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
                    slot = fallback_face->glyph;
                    return slot;
                }
            }
            log_debug("Failed to load glyph from fallback font: %s, U+%04X", *font_ptr, codepoint);
        }
        font_ptr++;
    }
    return NULL;
}

void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    fbox->style = fprop;
    fbox->current_font_size = fprop->font_size;

    // Try @font-face descriptors first, then fall back to system fonts
    const char* family_to_load = fprop->family;
    bool is_fallback = false;
    log_debug("Setting up font: family_to_load '%s', size %.1f", family_to_load, fprop->font_size);
    fbox->ft_face = load_font_with_descriptors(uicon, family_to_load, fprop, &is_fallback);

    // If @font-face loading failed, fall back to original method
    if (!fbox->ft_face) {
        fbox->ft_face = load_styled_font(uicon, family_to_load, fprop);
    }
    
    // If font loading failed, try fallback fonts
    if (!fbox->ft_face) {
        log_warn("Font '%s' not found, trying fallbacks...", family_to_load);
        
        // Try some common fallback fonts
        const char* fallbacks[] = {
            "Helvetica",        // Common on macOS
            "SF Pro Display",   // New macOS default
            "Arial Unicode MS", // Available on most systems
            "DejaVu Sans",      // Common on Linux
            "Times New Roman",  // Serif fallback
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

    // Use sub-pixel rendering flags for better quality
    FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
    if (FT_Load_Char(fbox->ft_face, ' ', load_flags)) {
        log_warn("Could not load space character for font: %s", family_to_load);
        fbox->style->space_width = fbox->ft_face->size->metrics.y_ppem / 64.0;
    } else {
        // Use float precision for space width calculation
        fbox->style->space_width = fbox->ft_face->glyph->advance.x / 64.0;
    }
    FT_Bool use_kerning = FT_HAS_KERNING(fbox->ft_face);
    fbox->style->has_kerning = use_kerning;
    fbox->style->ascender = fbox->ft_face->size->metrics.ascender / 64.0;
    fbox->style->descender = -fbox->ft_face->size->metrics.descender / 64.0;
    fbox->style->font_height = fbox->ft_face->size->metrics.height / 64.0;
    log_debug("Font setup complete: %s (space_width: %.1f, has_kerning: %s)",
        family_to_load, fbox->style->space_width, fbox->style->has_kerning ? "yes" : "no");
}

bool fontface_entry_free(const void *item, void *udata) {
    FontfaceEntry* entry = (FontfaceEntry*)item;
    free(entry->name);
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
}

// todo: cache glyph advance_x
