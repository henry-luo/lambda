#include "layout.h"

void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt);
void print_view_tree(ViewGroup* view_block, StrBuf* buf, int indent);
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);

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

void layout_init(LayoutContext* lycon, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->ui_context = uicon;
    // most browsers use a generic sans-serif font as the default
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 px for most browsers
    lycon->font.face = load_font_face(uicon, "Arial", 16);
    if (FT_Load_Char(lycon->font.face, ' ', FT_LOAD_RENDER)) {
        fprintf(stderr, "could not load space character\n");
        lycon->font.space_width = lycon->font.face->size->metrics.height >> 6;
    } else {
        lycon->font.space_width = lycon->font.face->glyph->advance.x >> 6;
    }
    lycon->font.style.font_style = LXB_CSS_VALUE_NORMAL;
    lycon->font.style.font_weight = LXB_CSS_VALUE_NORMAL;
    lycon->font.style.text_deco = LXB_CSS_VALUE_NONE;
}

void layout_cleanup(LayoutContext* lycon) {
    FT_Done_Face(lycon->font.face);
}

void layout_html_doc(UiContext* uicon, lxb_html_document_t *doc, bool is_reflow) {
    LayoutContext lycon;
    if (is_reflow) {
        // free existing view tree
        if (uicon->view_tree->root) free_view(uicon, uicon->view_tree->root);
        view_pool_destroy(uicon->view_tree);
    }
    view_pool_init(uicon->view_tree);

    lxb_dom_element_t *body = (lxb_dom_element_t*)lxb_html_document_body_element(doc);
    if (body) {
        printf("start to layout DOM tree\n");
        
        layout_init(&lycon, uicon);
        uicon->view_tree->root = alloc_view(&lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)body);
        
        lycon.parent = (ViewGroup*)uicon->view_tree->root;
        lycon.block.width = uicon->window_width * uicon->pixel_ratio;  
        lycon.block.height = uicon->window_width * uicon->pixel_ratio;
        lycon.block.advance_y = 0;  lycon.block.max_width = 800;
        lycon.block.line_height = round(1.2 * 16 * uicon->pixel_ratio);  
        lycon.block.text_align = LXB_CSS_VALUE_LEFT;
        lycon.line.is_line_start = true;
        printf("start to layout body\n");
        layout_block(&lycon, (lxb_html_element_t*)body);
        printf("end layout\n");

        layout_cleanup(&lycon);
        StrBuf* buf = strbuf_new(4096);
        print_view_tree((ViewGroup*)uicon->view_tree->root, buf, 0);
        printf("=================\nView tree:\n");
        printf("%s", buf->b);
        printf("=================\n");
    }
}

