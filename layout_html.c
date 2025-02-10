#include "layout.h"
#include <stdio.h>
#include "./lib/string_buffer/string_buffer.h"
/*
1. loop through html tree >> body >> children - map to style node, and construct style tree;
    define style struct: BlockStyle; (style def: html elmt, html attr, inline style, CSS style)
    defined style >> computed style -> layout;
    let html dom represent defined style, and we only store computed style in style tree;
2. tag div >> block layout, span >> inline layout;
3. inline: FreeType to get text metrics >> emit view node and construct view tree;
4. render view tree;
*/

View* layout_style_tree(UiContext* uicon, StyleBlock* style_root);
void render_html_doc(UiContext* uicon, View* root_view);
StyleBlock* compute_doc_style(StyleContext* context, lxb_dom_element_t *element);

View* layout_html_doc(UiContext* uicon, lxb_html_document_t *doc) {
    StyleContext context;
    lxb_dom_element_t *body = lxb_html_document_body_element(doc);
    if (body) {
        // compute: html elmt tree >> computed style tree
        context.parent = NULL;  context.prev_node = NULL;
        StyleBlock* style_tree = compute_doc_style(&context, body);
        assert(style_tree->display == LXB_CSS_VALUE_BLOCK);
        // layout: computed style tree >> view tree
        printf("start to layout style tree\n");
        return layout_style_tree(uicon, style_tree);
    }
    return NULL;
}

lxb_html_document_t* parse_html_doc(const char *html_source) {
    // Create the HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return NULL;
    }
    // parse the HTML source
    lxb_status_t status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return NULL;
    }
    return document;
}

// Function to read and display the content of a text file
StrBuf* readTextFile(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        perror("Error opening file"); 
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    StrBuf* buf = strbuf_new(fileSize + 1);
    if (buf == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf->b, 1, fileSize, file);
    buf->b[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}

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
                // Set font size
                FT_Set_Pixel_Sizes(face, 0, 32); // font_size);
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
    printf("Loading font: %s, %ld\n", name->b, parent->size->metrics.height);
    FT_Face face = load_font_face(uicon, name->b, 16); // parent->size->metrics.height);
    strbuf_free(name);
    return face;
}

