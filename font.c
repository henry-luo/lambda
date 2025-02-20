
#include "view.h"

FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size) {
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