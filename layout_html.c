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

View* layout_style_tree(StyleElement* style_root);
void render_html_doc(View* root_view);

StyleElement* compute_style(StyleContext* context, lxb_dom_element_t *element) {
    StyleElement *style = calloc(1, sizeof(StyleElement));
    if (style == NULL) {
        fprintf(stderr, "Failed to allocate memory for style.\n");
        return NULL;
    }

    // should check ns as well 
    if (element->node.local_name == LXB_TAG_H1 || element->node.local_name == LXB_TAG_P ||
        element->node.local_name == LXB_TAG_BODY) {
        style->display = LXB_CSS_VALUE_BLOCK;
    }
    else {
        style->display = LXB_CSS_VALUE_INLINE;
    }
    // Print the display property value
    // printf("display: %s\n", lxb_css_value_data(style->display).data);

    // link the elmt style to the style tree
    style->parent = context->parent;  style->node = element;
    if (context->prev_node != NULL) { context->prev_node->next = style; }
    else { context->parent->child = style; }
    
    // compute child style 
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(element));
    printf("child element: %d, %s\n", child->local_name, 
        (const char *)lxb_dom_element_local_name(child, NULL));
    if (child) {
        StyleElement* parent_style = context->parent;
        context->parent = style;  context->prev_node = NULL;
        do {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *element = lxb_dom_interface_element(child);
                const lxb_char_t *tag_name = lxb_dom_element_local_name(element, NULL);
                printf("Element: %s\n", (const char *)tag_name);
                compute_style(context, element);
            }
            else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                const char* text = lxb_dom_interface_text(child)->char_data.data.data;
                printf(" Text: %s\n", text);
                StyleText* style = calloc(1, sizeof(StyleText));
                style->str = text;  style->node = child;
                style->display = RDT_DISPLAY_TEXT;
                if (context->prev_node) { context->prev_node->next = style; }
                else { context->parent->child = style; }
                context->prev_node = style;
            }
            child = lxb_dom_node_next(child);
        } while (child);
        context->parent = parent_style;
    }
    context->prev_node = style;
    return style;
}

View* layout_html_doc(lxb_html_document_t *doc) {
    StyleContext context;
    lxb_dom_element_t *body = lxb_html_document_body_element(doc);
    if (body) {
        // compute: html elmt tree >> computed style tree
        context.parent = body;  context.prev_node = NULL;
        StyleElement* style_tree = compute_style(&context, body);
        // layout: computed style tree >> view tree
        return layout_style_tree(style_tree);
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

int main(void) {
    // load sample HTML source
    StrBuf* source_buf = readTextFile("sample.html");

    lxb_html_document_t* document = parse_html_doc(source_buf->b);
    // layout html doc 
    if (document) {
        View* root_view = layout_html_doc(document);
        // render html doc
        if (root_view) render_html_doc(root_view);
    }

    strbuf_free(source_buf);
}

