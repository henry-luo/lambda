
#include "view.h"
#include "./lib/hashmap.h"

typedef struct FontfaceEntry {
    char* name;
    FT_Face face;
} FontfaceEntry;

// int fontface_compare(const void *a, const void *b, void *udata) {
//     const FontfaceEntry *fa = a;
//     const FontfaceEntry *fb = b;
//     return strcmp(fa->name, fb->name);
// }

// bool fontface_iter(const void *item, void *udata) {
//     const FontfaceEntry *fontface = item;
//     printf("Font face name: %s\n", fontface->name);
//     return true;
// }

// uint64_t fontface_hash(const void *item, uint64_t seed0, uint64_t seed1) {
//     const FontfaceEntry *fontface = item;
//     return hashmap_sip(fontface->name, strlen(fontface->name), seed0, seed1);
// }

FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size) {
    // check the hashmap first
    if (uicon->fontfaces.zig_hash_map == NULL) {
        const unsigned initial_size = 10;
        struct hashmap_s* hashmap = calloc(1, sizeof(struct hashmap_s));
        if (hashmap_create(initial_size, hashmap)) {  // error
            printf("Failed to create fontface hashmap\n");
            return NULL;
        }
        else {
            uicon->fontfaces.zig_hash_map = hashmap;
        }
    }
    FontfaceEntry* entry = hashmap_get(uicon->fontfaces.zig_hash_map, font_name, strlen(font_name));
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
                if (uicon->fontfaces.zig_hash_map) {
                    entry = malloc(sizeof(FontfaceEntry));
                    entry->face = face;
                    // copy the font name
                    int slen = strlen(font_name);
                    entry->name = (char*)malloc(slen + 1);  strcpy(entry->name, font_name);
                    hashmap_put(uicon->fontfaces.zig_hash_map, font_name, slen, entry);   
                }
            }            
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return face;
}

FT_Face load_styled_font(UiContext* uicon, FT_Face parent, FontProp* font_style) {
    StrBuf* name = strbuf_create(parent->family_name);
    if (font_style->font_weight == LXB_CSS_VALUE_BOLD) {
        strbuf_append_str(name, ":bold");
        if (font_style->font_style == LXB_CSS_VALUE_ITALIC) { 
            strbuf_append_str(name, ":bolditalic");
        }
    }
    else if (font_style->font_style == LXB_CSS_VALUE_ITALIC) { 
        strbuf_append_str(name, ":italic");
    }
    FT_Face face = load_font_face(uicon, name->b, (parent->units_per_EM >> 6) / uicon->pixel_ratio);
    printf("Loading font: %s, %d, pa ascd: %ld, ascd: %ld, pa desc: %ld, desc: %ld\n", 
        name->b, parent->units_per_EM >> 6, parent->size->metrics.ascender >> 6, face->size->metrics.ascender >> 6,
        parent->size->metrics.descender >> 6, face->size->metrics.descender >> 6);
    strbuf_free(name);
    return face;
}

int iterate(void* const context, void* const value) {
    FontfaceEntry* entry = value;
    free(entry->name);
    FT_Done_Face(entry->face);
    free(entry);
    return 0;
}

void fontface_cleanup(UiContext* uicon) {
    // loop through the hashmap and free the font faces
    if (uicon->fontfaces.zig_hash_map) {
        printf("Cleaning up font faces\n");
        hashmap_iterate(uicon->fontfaces.zig_hash_map, iterate, NULL);
        hashmap_destroy(uicon->fontfaces.zig_hash_map);
        free(uicon->fontfaces.zig_hash_map);
        uicon->fontfaces.zig_hash_map = NULL;
    }
}