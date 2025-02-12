#include "layout.h"
#include <stdio.h>
#include "./lib/string_buffer/string_buffer.h"

static lxb_status_t serialize_callback(const lxb_char_t *data, size_t len, void *ctx) {
    // Append data to string buffer
    lxb_char_t **output = (lxb_char_t **)ctx;
    size_t old_len = *output ? strlen((char *)*output) : 0;
    *output = realloc(*output, old_len + len + 1);
    if (*output == NULL) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }
    
    memcpy(*output + old_len, data, len);
    (*output)[old_len + len] = '\0';
    
    return LXB_STATUS_OK;
}

lxb_html_document_t* parse_html_doc(const char *html_source) {
    // create HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return NULL;
    }
    // init CSS on document, otherwise CSS declarations will not be parsed
    lxb_status_t status = lxb_html_document_css_init(document);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to CSS initialization\n");
        return NULL;
    }

    // parse the HTML source
    status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return NULL;
    }

    // serialize document to string for debugging
    lxb_char_t *output = NULL;
    lxb_dom_document_t *dom_document = &document->dom_document;
    status = lxb_html_serialize_tree_cb(dom_document, serialize_callback, &output);
    if (status != LXB_STATUS_OK || output == NULL) {
        fprintf(stderr, "Failed to serialize document\n");
    } else {
        printf("Serialized HTML:\n%s\n", output);
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

