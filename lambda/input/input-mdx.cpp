#include "input.h"
#include <string.h>
#include <stdlib.h>

// Simple MDX parser that treats content as mixed markdown and JSX
static Element* create_mdx_document(Input* input, const char* content) {
    Element* root = input_create_element(input, "mdx_document");
    
    // For now, treat the entire content as markdown
    // This is a simplified implementation that can be enhanced later
    Item markdown_item = input_markup(input, content);
    
    if (markdown_item.item != ITEM_NULL && get_type_id(markdown_item) == LMD_TYPE_ELEMENT) {
        // Add the markdown element as content of the MDX document
        input_add_attribute_item_to_element(input, root, "content", markdown_item);
    }
    
    return root;
}

// Main MDX parsing function
void parse_mdx(Input* input, const char* mdx_string) {
    if (!mdx_string || !input) return;
    
    Element* root = create_mdx_document(input, mdx_string);
    if (root) {
        input->root = (Item){.element = root};
    }
}

// Public interface function
Item input_mdx(Input* input, const char* mdx_string) {
    if (!input || !mdx_string) {
        return (Item){.item = ITEM_NULL};
    }
    
    parse_mdx(input, mdx_string);
    return input->root;
}
