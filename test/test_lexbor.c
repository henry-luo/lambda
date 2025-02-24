#include <lexbor/html/html.h>

lxb_status_t style_print_callback(const lxb_char_t *data, size_t len, void *ctx) {
    printf("style rule: %.*s\n", (int) len, (const char *) data);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_print(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    printf("style entry: %ld\n", declr->type);
    lxb_css_rule_declaration_serialize(declr, style_print_callback, NULL);
    return LXB_STATUS_OK;
}

int main(int argc, const char *argv[]) {
    lxb_status_t status;
    const lxb_char_t *tag_name;
    lxb_html_document_t *document;

    static const lxb_char_t html[] = "<html><body><div style='color:red;'>Works fine!</div></body></html>";
    size_t html_len = sizeof(html) - 1;

    document = lxb_html_document_create();
    if (document == NULL) {
        exit(EXIT_FAILURE);
    }
    // init CSS on document, otherwise CSS declarations will not be parsed
    status = lxb_html_document_css_init(document);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to CSS initialization\n");
        return EXIT_FAILURE;
    }    

    status = lxb_html_document_parse(document, html, html_len);
    if (status != LXB_STATUS_OK) {
        exit(EXIT_FAILURE);
    }

    tag_name = lxb_dom_element_qualified_name(lxb_dom_interface_element(document->body), NULL);
    printf("Body element tag name: %s\n", tag_name);

    lxb_dom_node_t *body_elmt = lxb_dom_interface_element(document->body);
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(body_elmt));
    if (child != NULL) {
        tag_name = lxb_dom_element_qualified_name(lxb_dom_interface_element(child), NULL);
        printf("Child element tag name: %s\n", tag_name);
        
        lxb_dom_element_t *child_element = lxb_dom_interface_element(child);
        lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(child_element);
        while (attr != NULL) {
            const lxb_char_t *attr_name = lxb_dom_attr_local_name(attr, NULL);
            const lxb_char_t *attr_value = lxb_dom_attr_value(attr, NULL);
            printf("Attribute: %s = %s\n", attr_name, attr_value);
            attr = attr->next;
        }

        // resolve CSS styles
        lxb_html_element_t *elmt = lxb_html_interface_element(child);
        if (elmt->style) {
            printf("printing CSS styles\n");
            lxb_dom_document_t *ddoc = lxb_dom_interface_node(elmt)->owner_document;
            lxb_html_document_t *doc = lxb_html_interface_document(ddoc);
            lexbor_avl_foreach(doc->css.styles, elmt->style, lxb_html_element_style_print, NULL);
        }  
        else {
            printf("No CSS styles found\n");
        }

    } else {
        printf("No child elements found.\n");
    }

    lxb_html_document_destroy(document);
    return EXIT_SUCCESS;
}

// compile: zig cc test_lexbor.c -llexbor -o test_lexbor -I/usr/local/include -L/usr/local/lib