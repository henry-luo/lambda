#include "document.h"
#include <stdlib.h>
#include <string.h>

// Document creation and management
Document* document_create(Context* ctx) {
    Document* doc = malloc(sizeof(Document));
    if (!doc) return NULL;
    
    memset(doc, 0, sizeof(Document));
    
    // Create root document node
    doc->root = docnode_create(DOC_NODE_DOCUMENT);
    if (!doc->root) {
        free(doc);
        return NULL;
    }
    
    doc->lambda_context = ctx;
    doc->needs_pagination = true;
    doc->page_count = 0;
    doc->current_page_height = 0.0f;
    
    return doc;
}

void document_destroy(Document* doc) {
    if (!doc) return;
    
    if (doc->root) {
        docnode_destroy(doc->root);
    }
    
    if (doc->page_settings) {
        page_settings_destroy(doc->page_settings);
    }
    
    if (doc->stylesheet) {
        stylesheet_destroy(doc->stylesheet);
    }
    
    free(doc->title);
    free(doc->author);
    free(doc->subject);
    free(doc->keywords);
    
    free(doc);
}

// Document node creation and management
DocNode* docnode_create(DocNodeType type) {
    DocNode* node = malloc(sizeof(DocNode));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(DocNode));
    node->type = type;
    node->needs_layout = true;
    
    return node;
}

void docnode_destroy(DocNode* node) {
    if (!node) return;
    
    // Destroy all children
    DocNode* child = node->first_child;
    while (child) {
        DocNode* next = child->next_sibling;
        docnode_destroy(child);
        child = next;
    }
    
    // Free text content
    free(node->text_content);
    
    // Free styles (if not shared)
    if (node->text_style) {
        text_style_unref(node->text_style);
    }
    if (node->layout_style) {
        layout_style_unref(node->layout_style);
    }
    
    // Free type-specific data
    free(node->type_specific_data);
    
    free(node);
}

void docnode_append_child(DocNode* parent, DocNode* child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    child->next_sibling = NULL;
    
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
        child->prev_sibling = NULL;
    } else {
        child->prev_sibling = parent->last_child;
        parent->last_child->next_sibling = child;
        parent->last_child = child;
    }
}

void docnode_remove_child(DocNode* parent, DocNode* child) {
    if (!parent || !child || child->parent != parent) return;
    
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        parent->first_child = child->next_sibling;
    }
    
    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        parent->last_child = child->prev_sibling;
    }
    
    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;
}

// Content manipulation
void docnode_set_text_content(DocNode* node, const char* text) {
    if (!node) return;
    
    free(node->text_content);
    
    if (text) {
        node->text_content = malloc(strlen(text) + 1);
        if (node->text_content) {
            strcpy(node->text_content, text);
        }
    } else {
        node->text_content = NULL;
    }
}

const char* docnode_get_text_content(DocNode* node) {
    return node ? node->text_content : NULL;
}

void docnode_set_lambda_content(DocNode* node, Item lambda_item) {
    if (!node) return;
    node->lambda_content = lambda_item;
}

Item docnode_get_lambda_content(DocNode* node) {
    return node ? node->lambda_content : ITEM_NULL;
}

// Style application
void docnode_apply_text_style(DocNode* node, TextStyle* style) {
    if (!node || !style) return;
    
    if (node->text_style) {
        text_style_unref(node->text_style);
    }
    
    node->text_style = text_style_ref(style);
}

void docnode_apply_layout_style(DocNode* node, LayoutStyle* style) {
    if (!node || !style) return;
    
    if (node->layout_style) {
        layout_style_unref(node->layout_style);
    }
    
    node->layout_style = layout_style_ref(style);
}

// Document properties
void document_set_title(Document* doc, const char* title) {
    if (!doc) return;
    
    free(doc->title);
    
    if (title) {
        doc->title = malloc(strlen(title) + 1);
        if (doc->title) {
            strcpy(doc->title, title);
        }
    } else {
        doc->title = NULL;
    }
}

void document_set_author(Document* doc, const char* author) {
    if (!doc) return;
    
    free(doc->author);
    
    if (author) {
        doc->author = malloc(strlen(author) + 1);
        if (doc->author) {
            strcpy(doc->author, author);
        }
    } else {
        doc->author = NULL;
    }
}

// Helper functions for creating specific node types
DocNode* create_text_node(const char* text) {
    DocNode* node = docnode_create(DOC_NODE_TEXT);
    if (node && text) {
        docnode_set_text_content(node, text);
    }
    return node;
}

DocNode* create_paragraph_node(void) {
    return docnode_create(DOC_NODE_PARAGRAPH);
}

DocNode* create_heading_node(int level, const char* text) {
    DocNode* node = docnode_create(DOC_NODE_HEADING);
    if (node) {
        // Store heading level in type-specific data
        int* level_ptr = malloc(sizeof(int));
        if (level_ptr) {
            *level_ptr = level;
            node->type_specific_data = level_ptr;
        }
        
        if (text) {
            docnode_set_text_content(node, text);
        }
    }
    return node;
}

DocNode* create_math_node(Item math_expr, bool is_inline) {
    DocNode* node = docnode_create(is_inline ? DOC_NODE_MATH_INLINE : DOC_NODE_MATH_BLOCK);
    if (node) {
        docnode_set_lambda_content(node, math_expr);
    }
    return node;
}

DocNode* create_list_node(bool ordered) {
    DocNode* node = docnode_create(DOC_NODE_LIST);
    if (node) {
        // Store list type in type-specific data
        bool* ordered_ptr = malloc(sizeof(bool));
        if (ordered_ptr) {
            *ordered_ptr = ordered;
            node->type_specific_data = ordered_ptr;
        }
    }
    return node;
}

DocNode* create_list_item_node(DocNode* content) {
    DocNode* node = docnode_create(DOC_NODE_LIST_ITEM);
    if (node && content) {
        docnode_append_child(node, content);
    }
    return node;
}

// Tree traversal utilities
DocNode* docnode_find_by_type(DocNode* root, DocNodeType type) {
    if (!root) return NULL;
    
    if (root->type == type) {
        return root;
    }
    
    // Search children
    DocNode* child = root->first_child;
    while (child) {
        DocNode* found = docnode_find_by_type(child, type);
        if (found) return found;
        child = child->next_sibling;
    }
    
    return NULL;
}

void docnode_walk_tree(DocNode* root, void (*callback)(DocNode*, void*), void* user_data) {
    if (!root || !callback) return;
    
    callback(root, user_data);
    
    DocNode* child = root->first_child;
    while (child) {
        docnode_walk_tree(child, callback, user_data);
        child = child->next_sibling;
    }
}

// Document validation
bool document_validate(Document* doc) {
    if (!doc || !doc->root) return false;
    
    // Basic validation - ensure we have a valid tree structure
    return true; // TODO: Implement proper validation
}

void document_mark_for_layout(Document* doc) {
    if (!doc) return;
    
    doc->needs_pagination = true;
    
    // Mark all nodes for layout
    void mark_node_for_layout(DocNode* node, void* data) {
        (void)data; // Unused parameter
        node->needs_layout = true;
    }
    
    docnode_walk_tree(doc->root, mark_node_for_layout, NULL);
}
