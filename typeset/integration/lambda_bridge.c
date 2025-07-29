#include "lambda_bridge.h"
#include "../view/view_tree.h"
#include "../serialization/lambda_serializer.h"
#include "../../lambda/lambda.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Basic view tree creation from Lambda markdown elements

ViewTree* create_view_tree_from_lambda_item(TypesetEngine* engine, Item root_item) {
    if (!engine || root_item.type != ITEM_ELEMENT) {
        return NULL;
    }
    
    // Create view tree
    ViewTree* tree = view_tree_create();
    if (!tree) return NULL;
    
    // Set basic metadata
    tree->title = strdup("Markdown Document");
    tree->author = strdup("Lambda User");
    
    // Create a simple page layout (A4 size)
    tree->document_size.width = 595.276;  // A4 width in points
    tree->document_size.height = 841.89;  // A4 height in points
    
    // Create a page
    ViewPage* page = create_simple_page(1, tree->document_size);
    tree->pages = malloc(sizeof(ViewPage*));
    tree->pages[0] = page;
    tree->page_count = 1;
    
    // Convert root element to view nodes
    ViewNode* document_node = convert_lambda_item_to_viewnode(engine, root_item);
    if (document_node) {
        tree->root = document_node;
        page->page_node = document_node;
        view_node_retain(document_node);
        
        // Layout the document on the page
        layout_document_on_page(document_node, page);
    }
    
    return tree;
}

ViewNode* convert_lambda_item_to_viewnode(TypesetEngine* engine, Item item) {
    if (!engine) return NULL;
    
    switch (item.type) {
        case ITEM_ELEMENT:
            return convert_lambda_element_to_viewnode(engine, item);
        case ITEM_STRING:
            return convert_lambda_string_to_viewnode(engine, item);
        case ITEM_LIST:
            return convert_lambda_list_to_viewnode(engine, item);
        default:
            return NULL;
    }
}

ViewNode* convert_lambda_element_to_viewnode(TypesetEngine* engine, Item element) {
    if (!engine || element.type != ITEM_ELEMENT) return NULL;
    
    Element* elem = (Element*)element.ptr;
    const char* tag_name = get_element_operator(elem);
    
    if (!tag_name) return NULL;
    
    // Create appropriate view node based on element type
    ViewNode* node = NULL;
    
    if (starts_with(tag_name, "h") && strlen(tag_name) == 2 && tag_name[1] >= '1' && tag_name[1] <= '6') {
        // Heading element
        node = create_heading_viewnode(engine, elem, tag_name[1] - '0');
    } else if (strcmp(tag_name, "p") == 0) {
        // Paragraph element
        node = create_paragraph_viewnode(engine, elem);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        // List element
        node = create_list_viewnode(engine, elem, strcmp(tag_name, "ol") == 0);
    } else if (strcmp(tag_name, "li") == 0) {
        // List item element
        node = create_list_item_viewnode(engine, elem);
    } else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        // Emphasis element
        node = create_emphasis_viewnode(engine, elem, false);
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        // Strong element  
        node = create_emphasis_viewnode(engine, elem, true);
    } else if (strcmp(tag_name, "code") == 0) {
        // Inline code element
        node = create_code_viewnode(engine, elem, true);
    } else if (strcmp(tag_name, "pre") == 0) {
        // Code block element
        node = create_code_viewnode(engine, elem, false);
    } else if (strcmp(tag_name, "hr") == 0) {
        // Horizontal rule
        node = create_horizontal_rule_viewnode(engine, elem);
    } else {
        // Generic block or inline element
        node = create_generic_viewnode(engine, elem);
    }
    
    if (!node) return NULL;
    
    // Set semantic role
    node->semantic_role = strdup(tag_name);
    
    // Process children
    if (elem->type && ((TypeElmt*)elem->type)->content_length > 0) {
        List* content_list = (List*)elem;
        for (int i = 0; i < content_list->length; i++) {
            Item child_item = list_get(content_list, i);
            ViewNode* child_node = convert_lambda_item_to_viewnode(engine, child_item);
            if (child_node) {
                view_node_add_child(node, child_node);
                view_node_release(child_node); // Released because add_child retains
            }
        }
    }
    
    return node;
}

ViewNode* convert_lambda_string_to_viewnode(TypesetEngine* engine, Item string_item) {
    if (!engine || string_item.type != ITEM_STRING) return NULL;
    
    String* str = (String*)string_item.ptr;
    const char* text = get_string_value(str);
    
    if (!text || strlen(text) == 0) return NULL;
    
    // Create text run node
    ViewNode* node = view_node_create_text_run(text, NULL, 12.0); // Default 12pt font
    if (!node) return NULL;
    
    node->semantic_role = strdup("text");
    
    return node;
}

ViewNode* convert_lambda_list_to_viewnode(TypesetEngine* engine, Item list_item) {
    if (!engine || list_item.type != ITEM_LIST) return NULL;
    
    // Create a group node to hold the list items
    ViewNode* group_node = view_node_create_group("list-content");
    if (!group_node) return NULL;
    
    List* list = (List*)list_item.ptr;
    for (int i = 0; i < list->length; i++) {
        Item child_item = list_get(list, i);
        ViewNode* child_node = convert_lambda_item_to_viewnode(engine, child_item);
        if (child_node) {
            view_node_add_child(group_node, child_node);
            view_node_release(child_node);
        }
    }
    
    return group_node;
}

// Specialized element conversion functions

ViewNode* create_heading_viewnode(TypesetEngine* engine, Element* elem, int level) {
    ViewNode* node = view_node_create(VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    // Set size based on heading level
    double font_size = 24.0 - (level - 1) * 2.0; // h1=24pt, h2=22pt, etc.
    if (font_size < 12.0) font_size = 12.0;
    
    node->size.height = font_size * 1.2; // Line height
    
    // Set position (will be adjusted during layout)
    node->position.x = 72.0; // 1 inch margin
    node->position.y = 0; // Will be set during layout
    
    return node;
}

ViewNode* create_paragraph_viewnode(TypesetEngine* engine, Element* elem) {
    ViewNode* node = view_node_create(VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    // Standard paragraph formatting
    node->size.height = 14.4; // 12pt * 1.2 line height
    node->position.x = 72.0; // 1 inch margin
    node->position.y = 0; // Will be set during layout
    
    return node;
}

ViewNode* create_list_viewnode(TypesetEngine* engine, Element* elem, bool is_ordered) {
    ViewNode* node = view_node_create(VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    node->semantic_role = strdup(is_ordered ? "ordered-list" : "unordered-list");
    node->position.x = 90.0; // Indented from margin
    node->position.y = 0; // Will be set during layout
    
    return node;
}

ViewNode* create_list_item_viewnode(TypesetEngine* engine, Element* elem) {
    ViewNode* node = view_node_create(VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    node->size.height = 14.4; // 12pt * 1.2 line height
    node->position.x = 0; // Relative to parent list
    node->position.y = 0; // Will be set during layout
    
    return node;
}

ViewNode* create_emphasis_viewnode(TypesetEngine* engine, Element* elem, bool is_strong) {
    ViewNode* node = view_node_create(VIEW_NODE_INLINE);
    if (!node) return NULL;
    
    node->semantic_role = strdup(is_strong ? "strong" : "emphasis");
    node->size.height = 12.0; // Inherit from parent
    
    return node;
}

ViewNode* create_code_viewnode(TypesetEngine* engine, Element* elem, bool is_inline) {
    ViewNode* node = view_node_create(is_inline ? VIEW_NODE_INLINE : VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    node->semantic_role = strdup(is_inline ? "inline-code" : "code-block");
    
    if (is_inline) {
        node->size.height = 12.0; // Same as text
    } else {
        node->size.height = 14.4; // Block height
        node->position.x = 90.0; // Indented
    }
    
    return node;
}

ViewNode* create_horizontal_rule_viewnode(TypesetEngine* engine, Element* elem) {
    ViewNode* node = view_node_create(VIEW_NODE_LINE);
    if (!node) return NULL;
    
    // Create a horizontal line across the page
    node->position.x = 72.0; // Start at margin
    node->position.y = 0; // Will be set during layout
    node->size.width = 451.276; // Page width minus margins
    node->size.height = 1.0; // Line thickness
    
    return node;
}

ViewNode* create_generic_viewnode(TypesetEngine* engine, Element* elem) {
    ViewNode* node = view_node_create(VIEW_NODE_BLOCK);
    if (!node) return NULL;
    
    node->size.height = 14.4; // Default height
    node->position.x = 72.0; // Default margin
    node->position.y = 0; // Will be set during layout
    
    return node;
}

// Layout functions

ViewPage* create_simple_page(int page_number, ViewSize page_size) {
    ViewPage* page = calloc(1, sizeof(ViewPage));
    if (!page) return NULL;
    
    page->page_number = page_number;
    page->page_size = page_size;
    page->is_landscape = false;
    
    // Set content area (1 inch margins on all sides)
    page->content_area.origin.x = 72.0;
    page->content_area.origin.y = 72.0;
    page->content_area.size.width = page_size.width - 144.0; // 2 inches total
    page->content_area.size.height = page_size.height - 144.0; // 2 inches total
    
    // Margin area is the full page
    page->margin_area.origin.x = 0;
    page->margin_area.origin.y = 0;
    page->margin_area.size = page_size;
    
    return page;
}

void layout_document_on_page(ViewNode* document_node, ViewPage* page) {
    if (!document_node || !page) return;
    
    double current_y = page->content_area.origin.y;
    double line_spacing = 6.0; // Extra spacing between elements
    
    // Simple vertical layout
    layout_node_recursive(document_node, page, &current_y, line_spacing);
}

void layout_node_recursive(ViewNode* node, ViewPage* page, double* current_y, double line_spacing) {
    if (!node || !page || !current_y) return;
    
    // Set position for this node
    if (node->type == VIEW_NODE_BLOCK || node->type == VIEW_NODE_LINE) {
        node->position.y = *current_y;
        node->size.width = page->content_area.size.width; // Full width for blocks
        *current_y += node->size.height + line_spacing;
    }
    
    // Layout children
    ViewNode* child = node->first_child;
    while (child) {
        layout_node_recursive(child, page, current_y, line_spacing);
        child = child->next_sibling;
    }
}

// Utility functions

const char* get_element_operator(Element* elem) {
    if (!elem || !elem->type) return NULL;
    
    TypeElmt* type_elem = (TypeElmt*)elem->type;
    if (!type_elem->op || !type_elem->op->key) return NULL;
    
    return get_string_value(type_elem->op->key);
}

bool starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}
