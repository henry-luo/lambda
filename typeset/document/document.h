#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "../typeset.h"

// Document node types
typedef enum {
    DOC_NODE_DOCUMENT,
    DOC_NODE_PAGE,
    DOC_NODE_PARAGRAPH,
    DOC_NODE_TEXT,
    DOC_NODE_MATH_BLOCK,
    DOC_NODE_MATH_INLINE,
    DOC_NODE_LIST,
    DOC_NODE_LIST_ITEM,
    DOC_NODE_TABLE,
    DOC_NODE_TABLE_ROW,
    DOC_NODE_TABLE_CELL,
    DOC_NODE_IMAGE,
    DOC_NODE_HEADING,
    DOC_NODE_CODE_BLOCK,
    DOC_NODE_QUOTE,
    DOC_NODE_LINK,
    DOC_NODE_EMPHASIS,
    DOC_NODE_STRONG,
    DOC_NODE_LINE_BREAK,
    DOC_NODE_HORIZONTAL_RULE
} DocNodeType;

// Document node structure
struct DocNode {
    DocNodeType type;
    struct DocNode* parent;
    struct DocNode* first_child;
    struct DocNode* last_child;
    struct DocNode* next_sibling;
    struct DocNode* prev_sibling;
    
    // Style properties
    TextStyle* text_style;
    LayoutStyle* layout_style;
    
    // Content
    Item lambda_content;        // Original Lambda AST
    char* text_content;         // For text nodes
    void* type_specific_data;   // Node-specific data
    
    // Layout cache
    struct Box* layout_box;     // Associated layout box
    bool needs_layout;          // Layout invalidation flag
};

// Document structure
struct Document {
    DocNode* root;
    PageSettings* page_settings;
    FontManager* font_manager;
    StyleSheet* stylesheet;
    Context* lambda_context;
    
    // Document metadata
    char* title;
    char* author;
    char* subject;
    char* keywords;
    
    // Layout state
    float current_page_height;
    int page_count;
    bool needs_pagination;
};

// Document creation and destruction
Document* document_create(Context* ctx);
void document_destroy(Document* doc);

// Document tree manipulation
DocNode* docnode_create(DocNodeType type);
void docnode_destroy(DocNode* node);
void docnode_append_child(DocNode* parent, DocNode* child);
void docnode_remove_child(DocNode* parent, DocNode* child);
void docnode_insert_before(DocNode* reference, DocNode* new_node);
void docnode_insert_after(DocNode* reference, DocNode* new_node);

// Tree traversal
DocNode* docnode_first_child(DocNode* node);
DocNode* docnode_last_child(DocNode* node);
DocNode* docnode_next_sibling(DocNode* node);
DocNode* docnode_prev_sibling(DocNode* node);
DocNode* docnode_parent(DocNode* node);

// Content manipulation
void docnode_set_text_content(DocNode* node, const char* text);
void docnode_set_lambda_content(DocNode* node, Item lambda_item);
const char* docnode_get_text_content(DocNode* node);
Item docnode_get_lambda_content(DocNode* node);

// Style application
void docnode_apply_text_style(DocNode* node, TextStyle* style);
void docnode_apply_layout_style(DocNode* node, LayoutStyle* style);
void docnode_inherit_styles(DocNode* node);

// Document properties
void document_set_title(Document* doc, const char* title);
void document_set_author(Document* doc, const char* author);
void document_set_subject(Document* doc, const char* subject);
void document_set_keywords(Document* doc, const char* keywords);

// Document validation and analysis
bool document_validate(Document* doc);
void document_analyze_structure(Document* doc);
void document_mark_for_layout(Document* doc);

// Utility functions
DocNode* docnode_find_by_type(DocNode* root, DocNodeType type);
DocNode* docnode_find_by_content(DocNode* root, const char* text);
void docnode_walk_tree(DocNode* root, void (*callback)(DocNode*, void*), void* user_data);

// Type-specific node creation helpers
DocNode* create_text_node(const char* text);
DocNode* create_paragraph_node(void);
DocNode* create_heading_node(int level, const char* text);
DocNode* create_math_node(Item math_expr, bool is_inline);
DocNode* create_list_node(bool ordered);
DocNode* create_list_item_node(DocNode* content);
DocNode* create_table_node(int rows, int cols);
DocNode* create_code_block_node(const char* code, const char* language);

#endif // DOCUMENT_H
