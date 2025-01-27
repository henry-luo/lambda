#include <lexbor/html/html.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/css/css.h>
#include <lexbor/css/parser.h>
#include <stdio.h>

int main(void) {
    // Example HTML source with inline CSS
    const char *html_source = "<html><head><style>h1 { color: red; }</style></head><body><h1>Hello, World!</h1><p>This is a paragraph.</p></body></html>";

    // Create the HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (document == NULL) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return EXIT_FAILURE;
    }

    // Parse the HTML source
    lxb_status_t status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return EXIT_FAILURE;
    }

    // Get the <style> element content
    lxb_dom_element_t *head = lxb_html_document_head_element(document);
    if (head != NULL) {
        lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(head));
        while (child != NULL) {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *element = lxb_dom_interface_element(child);
                const lxb_char_t *tag_name = lxb_dom_element_local_name(element, NULL);
                printf("Head element: %s\n", (const char *)tag_name);

                if (strcmp((const char *)tag_name, "style") == 0) {
                    lxb_dom_node_t *text_node = lxb_dom_node_first_child(child);
                    if (text_node != NULL && text_node->type == LXB_DOM_NODE_TYPE_TEXT) {
                        const char *css_content = lxb_dom_interface_text(text_node)->char_data.data.data;
                        printf("CSS Content: \n%s\n", css_content);

                        // Parse the CSS content
                        lxb_css_parser_t *css_parser = lxb_css_parser_create();
                        if (css_parser == NULL) {
                            fprintf(stderr, "Failed to create CSS parser.\n");
                            lxb_html_document_destroy(document);
                            return EXIT_FAILURE;
                        }

                        lxb_css_parser_init(css_parser, NULL);

                        lxb_css_stylesheet_t *stylesheet = lxb_css_stylesheet_parse(css_parser, 
                            (const lxb_char_t *)css_content, strlen(css_content));
                        if (stylesheet == NULL) {
                            fprintf(stderr, "Failed to parse CSS.\n");
                            lxb_css_parser_destroy(css_parser, true);
                            lxb_html_document_destroy(document);
                            return EXIT_FAILURE;
                        }

                        printf("CSS parsed successfully.\n");

                        // Clean up CSS parser
                        lxb_css_parser_destroy(css_parser, true);
                    }
                }
            }
            child = lxb_dom_node_next(child);
        }
    }
    else {
        fprintf(stderr, "Failed to find <head> element.\n");
    }

    // Get the body element
    lxb_dom_element_t *body = lxb_html_document_body_element(document);
    if (body == NULL) {
        fprintf(stderr, "Failed to find <body> element.\n");
        lxb_html_document_destroy(document);
        return EXIT_FAILURE;
    }

    // Iterate over the children of the body element
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(body));
    while (child != NULL) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *element = lxb_dom_interface_element(child);
            const lxb_char_t *tag_name = lxb_dom_element_tag_name(element, NULL);

            // Print the tag name
            printf("Element: %s\n", (const char *)tag_name);

            // If the element has text content, print it
            lxb_dom_node_t *text_node = lxb_dom_node_first_child(child);
            if (text_node != NULL && text_node->type == LXB_DOM_NODE_TYPE_TEXT) {
                printf(" Text: %s\n", lxb_dom_interface_text(text_node)->char_data.data.data);
            }
        }

        // Move to the next sibling
        child = lxb_dom_node_next(child);
    }

    // Clean up and destroy the document
    lxb_html_document_destroy(document);
    return EXIT_SUCCESS;
}

// clang -v layout.c -o layout -I/opt/homebrew/opt/lexbor/include -L/opt/homebrew/opt/lexbor/lib -llexbor