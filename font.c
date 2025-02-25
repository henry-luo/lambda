
#include "view.h"

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

FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size) {
    // check the hashmap first
    if (uicon->fontface_map == NULL) {
        // create a new hash map. 2nd argument is the initial capacity. 
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->fontface_map = hashmap_new(sizeof(FontfaceEntry), 10, 0, 0, 
            fontface_hash, fontface_compare, NULL, NULL);
    }
    FontfaceEntry* entry = (FontfaceEntry*) hashmap_get(uicon->fontface_map, 
        &(FontfaceEntry){.name = (char*)font_name});
    if (entry) {
        printf("Fontface loaded from cache: %s\n", font_name);
        return entry->face;
    }
    else {
        printf("Fontface not found in cache: %s\n", font_name);
    }

    // todo: cache the fonts loaded
    FT_Face face = NULL;
    // search for font
    FcPattern *pattern = FcNameParse((const FcChar8 *)font_name);
    FcConfigSubstitute(uicon->font_config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *match = FcFontMatch(uicon->font_config, pattern, &result);
    if (!match) {
        printf("Font not found\n");
    }
    else {
        FcChar8 *file = NULL;
        // get font file path
        if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
            printf("Failed to get font file path\n");
        } else {
            printf("Found font at: %s\n", file);
            // load the font
            if (FT_New_Face(uicon->ft_library, (const char *)file, 0, &face)) {
                printf("Could not load font\n");  
                face = NULL;
            } else {
                // Set height of the font
                FT_Set_Pixel_Sizes(face, 0, font_size * uicon->pixel_ratio);
                printf("Font loaded: %s, height:%ld, ascend:%ld, descend:%ld, em size: %d\n", 
                    face->family_name, face->size->metrics.height >> 6,
                    face->size->metrics.ascender >> 6, face->size->metrics.descender >> 6,
                    face->units_per_EM >> 6);
                // put the font face into the hashmap
                if (uicon->fontface_map) {
                    // copy the font name
                    int slen = strlen(font_name);
                    char* name = (char*)malloc(slen + 1);  strcpy(name, font_name);
                    hashmap_set(uicon->fontface_map, &(FontfaceEntry){.name=name, .face=face});   
                }
            }            
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return face;
}

FT_Face load_styled_font(UiContext* uicon, FT_Face parent, FontProp* font_style) {
    StrBuf* name;
    name = strbuf_create(parent->family_name);
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
    FT_Face face = load_font_face(uicon, name->s, font_style->font_size);
    printf("Loading font: %s, %d, pa ascd: %ld, ascd: %ld, pa desc: %ld, desc: %ld\n", 
        name->s, parent->units_per_EM >> 6, parent->size->metrics.ascender >> 6, face->size->metrics.ascender >> 6,
        parent->size->metrics.descender >> 6, face->size->metrics.descender >> 6);
    strbuf_free(name);
    return face;
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

// todo: cache glyph advvance_x
