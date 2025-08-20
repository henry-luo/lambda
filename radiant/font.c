
#include "view.h"

#include "../lib/log.h"
typedef struct FontfaceEntry {
    char* name;
    FT_Face face;
} FontfaceEntry;

int fontface_compare(const void *a, const void *b, void *udata) {
    const FontfaceEntry *fa = a;
    const FontfaceEntry *fb = b;
    return strcmp(fa->name, fb->name);
}

uint64_t fontface_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontfaceEntry *fontface = item;
    // xxhash3 is a fast hash function
    return hashmap_xxhash3(fontface->name, strlen(fontface->name), seed0, seed1);
}

char* load_font_path(FcConfig *font_config, const char* font_name) {
    // search for font
    FcPattern *pattern = FcNameParse((const FcChar8 *)font_name);
    FcConfigSubstitute(font_config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;  FcChar8 *file = NULL;  char *path = NULL;
    FcPattern *match = FcFontMatch(font_config, pattern, &result);
    if (!match) { printf("Font not found\n"); }
    else {
        // get font file path
        if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
            log_debug("Failed to get font file path: %s", font_name);
        } else {
            log_debug("Found font '%s' at: %s", font_name, file);
            path = strdup((const char *)file);  // need to strdup, as file will be destroyed by FcPatternDestroy later
        }
    }
    if (match) FcPatternDestroy(match);
    if (pattern) FcPatternDestroy(pattern);  
    return path;
}

FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size) {
    // check the hashmap first
    if (uicon->fontface_map == NULL) {
        // create a new hash map. 2nd argument is the initial capacity. 
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0, 
            fontface_hash, fontface_compare, NULL, NULL);
    }
    StrBuf* name_and_size = strbuf_create(font_name);
    strbuf_append_str(name_and_size, ":");  
    strbuf_append_int(name_and_size, font_size);
    FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, 
        &(FontfaceEntry){.name = name_and_size->str});
    if (entry) {
        printf("Fontface loaded from cache: %s\n", name_and_size->str);
        strbuf_free(name_and_size);
        return entry->face;
    }
    else {
        printf("Fontface not found in cache: %s\n", name_and_size->str);
    }

    FT_Face face = NULL;
    char* font_path = load_font_path(uicon->font_config, font_name);
    if (font_path) {
        // load the font
        printf("Loading font at: %s\n", font_path);
        if (FT_New_Face(uicon->ft_library, (const char *)font_path, 0, &face)) {
            printf("Could not load font\n");  
            face = NULL;
        } else {
            // Set height of the font
            FT_Set_Pixel_Sizes(face, 0, font_size);
            printf("Font loaded: %s, height:%ld, ascend:%ld, descend:%ld, em size: %d\n", 
                face->family_name, face->size->metrics.height >> 6,
                face->size->metrics.ascender >> 6, face->size->metrics.descender >> 6,
                face->units_per_EM >> 6);
            // put the font face into the hashmap
            if (uicon->fontface_map) {
                // copy the font name
                char* name = (char*)malloc(name_and_size->length + 1);  strcpy(name, name_and_size->str);
                hashmap_set(uicon->fontface_map, &(FontfaceEntry){.name=name, .face=face});   
            }
        }
        free(font_path);
    }
    strbuf_free(name_and_size);
    return face;
}

FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style) {
    StrBuf* name;
    name = strbuf_create(font_name);
    if (font_style->font_weight == LXB_CSS_VALUE_BOLD) {
        if (font_style->font_style == LXB_CSS_VALUE_ITALIC) { 
            strbuf_append_str(name, ":bolditalic");
        } else {
            strbuf_append_str(name, ":bold");
        }
    }
    else if (font_style->font_style == LXB_CSS_VALUE_ITALIC) { 
        strbuf_append_str(name, ":italic");
    }
    FT_Face face = load_font_face(uicon, name->str, font_style->font_size);
    printf("Loading font: %s, ascd: %ld, desc: %ld\n", 
        name->str, face->size->metrics.ascender >> 6, face->size->metrics.descender >> 6);
    strbuf_free(name);
    return face;
}

FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint) {
    FT_GlyphSlot slot = NULL;  FT_Error error;
    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    if (char_index > 0) {
        error = FT_Load_Glyph(face, char_index, FT_LOAD_RENDER);
        if (!error) { slot = face->glyph;  return slot; } 
    }
    
    // failed to load glyph under current font, try fallback fonts
    log_debug("failed to load glyph: %u", codepoint);
    char** font_ptr = uicon->fallback_fonts;
    while (*font_ptr) {
        log_debug("trying fallback font '%s' for char: %u", *font_ptr, codepoint);
        FT_Face fallback_face = load_styled_font(uicon, *font_ptr, font_style);
        if (fallback_face) {
            char_index = FT_Get_Char_Index(fallback_face, codepoint);
            if (char_index > 0) {
                error = FT_Load_Glyph(fallback_face, char_index, FT_LOAD_RENDER);
                if (!error) { slot = fallback_face->glyph;  return slot; } 
            }
            log_debug("failed to load glyph from fallback font: %s, %u", *font_ptr, codepoint);
        }
        font_ptr++;
    }
    return NULL;
}

void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop) {
    fbox->style = *fprop;
    fbox->face = load_styled_font(uicon, fprop->family ? fprop->family : font_name, fprop);
    if (FT_Load_Char(fbox->face, ' ', FT_LOAD_RENDER)) {
        fprintf(stderr, "could not load space character\n");
        fbox->space_width = fbox->face->size->metrics.y_ppem >> 6;
    } else {
        fbox->space_width = fbox->face->glyph->advance.x >> 6;
    }
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
        printf("Cleaning up font faces\n");
        hashmap_scan(uicon->fontface_map, fontface_entry_free, NULL);
        hashmap_free(uicon->fontface_map);
        uicon->fontface_map = NULL;
    }
}

// todo: cache glyph advance_x
