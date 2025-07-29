#ifndef LAMBDA_BRIDGE_H
#define LAMBDA_BRIDGE_H

#include "../typeset.h"

// Bridge between Lambda AST and typesetting system

// Document creation from Lambda items (creates view tree directly)
ViewTree* create_view_tree_from_lambda_item(TypesetEngine* engine, Item root_item);
ViewTree* create_view_tree_from_markdown_item(TypesetEngine* engine, Item md_ast);
ViewTree* create_view_tree_from_latex_item(TypesetEngine* engine, Item latex_ast);
ViewTree* create_view_tree_from_html_item(TypesetEngine* engine, Item html_ast);
ViewTree* create_view_tree_from_math_item(TypesetEngine* engine, Item math_ast);

// Legacy document creation (for backward compatibility)
Document* create_document_from_lambda_item(TypesetEngine* engine, Item root_item);
Document* create_document_from_markdown_item(TypesetEngine* engine, Item md_ast);
Document* create_document_from_latex_item(TypesetEngine* engine, Item latex_ast);
Document* create_document_from_html_item(TypesetEngine* engine, Item html_ast);
Document* create_document_from_math_item(TypesetEngine* engine, Item math_ast);

// Lambda AST node type detection
typedef enum {
    LAMBDA_NODE_UNKNOWN,
    LAMBDA_NODE_ELEMENT,        // Lambda element with operator
    LAMBDA_NODE_LIST,           // Lambda list
    LAMBDA_NODE_MAP,            // Lambda map
    LAMBDA_NODE_STRING,         // Lambda string
    LAMBDA_NODE_SYMBOL,         // Lambda symbol
    LAMBDA_NODE_NUMBER,         // Lambda number
    LAMBDA_NODE_BOOLEAN,        // Lambda boolean
    LAMBDA_NODE_NULL            // Lambda null
} LambdaNodeType;

LambdaNodeType detect_lambda_node_type(Item item);
const char* get_lambda_element_operator(Item element);
Item get_lambda_element_child(Item element, int index);
int get_lambda_element_child_count(Item element);

// View tree construction (new primary API)
ViewNode* convert_lambda_item_to_viewnode(TypesetEngine* engine, Item item);
ViewNode* convert_lambda_element_to_viewnode(TypesetEngine* engine, Item element);
ViewNode* convert_lambda_list_to_viewnode(TypesetEngine* engine, Item list);
ViewNode* convert_lambda_map_to_viewnode(TypesetEngine* engine, Item map);
ViewNode* convert_lambda_string_to_viewnode(TypesetEngine* engine, Item string);

// Document tree construction (legacy - for backward compatibility)
DocNode* convert_lambda_item_to_docnode(TypesetEngine* engine, Item item);
DocNode* convert_lambda_element_to_docnode(TypesetEngine* engine, Item element);
DocNode* convert_lambda_list_to_docnode(TypesetEngine* engine, Item list);
DocNode* convert_lambda_map_to_docnode(TypesetEngine* engine, Item map);
DocNode* convert_lambda_string_to_docnode(TypesetEngine* engine, Item string);

// Markdown-specific conversion (view tree)
ViewNode* convert_markdown_heading_to_viewnode(TypesetEngine* engine, Item heading_element);
ViewNode* convert_markdown_paragraph_to_viewnode(TypesetEngine* engine, Item paragraph_element);
ViewNode* convert_markdown_list_to_viewnode(TypesetEngine* engine, Item list_element);
ViewNode* convert_markdown_list_item_to_viewnode(TypesetEngine* engine, Item item_element);
ViewNode* convert_markdown_emphasis_to_viewnode(TypesetEngine* engine, Item emphasis_element);
ViewNode* convert_markdown_strong_to_viewnode(TypesetEngine* engine, Item strong_element);
ViewNode* convert_markdown_code_to_viewnode(TypesetEngine* engine, Item code_element);
ViewNode* convert_markdown_code_block_to_viewnode(TypesetEngine* engine, Item code_block_element);
ViewNode* convert_markdown_link_to_viewnode(TypesetEngine* engine, Item link_element);
ViewNode* convert_markdown_image_to_viewnode(TypesetEngine* engine, Item image_element);
ViewNode* convert_markdown_table_to_viewnode(TypesetEngine* engine, Item table_element);
ViewNode* convert_markdown_blockquote_to_viewnode(TypesetEngine* engine, Item blockquote_element);
ViewNode* convert_markdown_horizontal_rule_to_viewnode(TypesetEngine* engine, Item hr_element);

// Markdown-specific conversion (document tree - legacy)
DocNode* convert_markdown_heading(TypesetEngine* engine, Item heading_element);
DocNode* convert_markdown_paragraph(TypesetEngine* engine, Item paragraph_element);
DocNode* convert_markdown_list(TypesetEngine* engine, Item list_element);
DocNode* convert_markdown_list_item(TypesetEngine* engine, Item item_element);
DocNode* convert_markdown_emphasis(TypesetEngine* engine, Item emphasis_element);
DocNode* convert_markdown_strong(TypesetEngine* engine, Item strong_element);
DocNode* convert_markdown_code(TypesetEngine* engine, Item code_element);
DocNode* convert_markdown_code_block(TypesetEngine* engine, Item code_block_element);
DocNode* convert_markdown_link(TypesetEngine* engine, Item link_element);
DocNode* convert_markdown_image(TypesetEngine* engine, Item image_element);
DocNode* convert_markdown_table(TypesetEngine* engine, Item table_element);
DocNode* convert_markdown_blockquote(TypesetEngine* engine, Item blockquote_element);
DocNode* convert_markdown_horizontal_rule(TypesetEngine* engine, Item hr_element);

// LaTeX-specific conversion
DocNode* convert_latex_document(TypesetEngine* engine, Item document_element);
DocNode* convert_latex_section(TypesetEngine* engine, Item section_element);
DocNode* convert_latex_subsection(TypesetEngine* engine, Item subsection_element);
DocNode* convert_latex_paragraph(TypesetEngine* engine, Item paragraph_element);
DocNode* convert_latex_text(TypesetEngine* engine, Item text_element);
DocNode* convert_latex_command(TypesetEngine* engine, Item command_element);
DocNode* convert_latex_environment(TypesetEngine* engine, Item environment_element);
DocNode* convert_latex_math_inline(TypesetEngine* engine, Item math_element);
DocNode* convert_latex_math_display(TypesetEngine* engine, Item math_element);
DocNode* convert_latex_equation(TypesetEngine* engine, Item equation_element);
DocNode* convert_latex_align(TypesetEngine* engine, Item align_element);
DocNode* convert_latex_table(TypesetEngine* engine, Item table_element);
DocNode* convert_latex_figure(TypesetEngine* engine, Item figure_element);
DocNode* convert_latex_itemize(TypesetEngine* engine, Item itemize_element);
DocNode* convert_latex_enumerate(TypesetEngine* engine, Item enumerate_element);

// HTML-specific conversion
DocNode* convert_html_element(TypesetEngine* engine, Item html_element);
DocNode* convert_html_text(TypesetEngine* engine, Item text_element);
DocNode* convert_html_heading(TypesetEngine* engine, Item heading_element);
DocNode* convert_html_paragraph(TypesetEngine* engine, Item paragraph_element);
DocNode* convert_html_div(TypesetEngine* engine, Item div_element);
DocNode* convert_html_span(TypesetEngine* engine, Item span_element);
DocNode* convert_html_list(TypesetEngine* engine, Item list_element);
DocNode* convert_html_table(TypesetEngine* engine, Item table_element);
DocNode* convert_html_image(TypesetEngine* engine, Item image_element);
DocNode* convert_html_link(TypesetEngine* engine, Item link_element);

// Math-specific conversion (view tree)
ViewNode* convert_math_expression_to_viewnode(TypesetEngine* engine, Item math_expr, bool is_inline);
ViewNode* convert_math_operator_to_viewnode(TypesetEngine* engine, Item operator_element);
ViewNode* convert_math_fraction_to_viewnode(TypesetEngine* engine, Item fraction_element);
ViewNode* convert_math_superscript_to_viewnode(TypesetEngine* engine, Item sup_element);
ViewNode* convert_math_subscript_to_viewnode(TypesetEngine* engine, Item sub_element);
ViewNode* convert_math_radical_to_viewnode(TypesetEngine* engine, Item radical_element);
ViewNode* convert_math_matrix_to_viewnode(TypesetEngine* engine, Item matrix_element);
ViewNode* convert_math_function_to_viewnode(TypesetEngine* engine, Item function_element);
ViewNode* convert_math_symbol_to_viewnode(TypesetEngine* engine, Item symbol_element);
ViewNode* convert_math_number_to_viewnode(TypesetEngine* engine, Item number_element);

// Math-specific conversion (document tree - legacy)
DocNode* convert_math_expression(TypesetEngine* engine, Item math_expr, bool is_inline);
DocNode* convert_math_operator(TypesetEngine* engine, Item operator_element);
DocNode* convert_math_fraction(TypesetEngine* engine, Item fraction_element);
DocNode* convert_math_superscript(TypesetEngine* engine, Item sup_element);
DocNode* convert_math_subscript(TypesetEngine* engine, Item sub_element);
DocNode* convert_math_radical(TypesetEngine* engine, Item radical_element);
DocNode* convert_math_matrix(TypesetEngine* engine, Item matrix_element);
DocNode* convert_math_function(TypesetEngine* engine, Item function_element);
DocNode* convert_math_symbol(TypesetEngine* engine, Item symbol_element);
DocNode* convert_math_number(TypesetEngine* engine, Item number_element);

// Style resolution from Lambda data
TextStyle* resolve_lambda_text_style(TypesetEngine* engine, Item style_item);
LayoutStyle* resolve_lambda_layout_style(TypesetEngine* engine, Item style_item);
Font* resolve_lambda_font(TypesetEngine* engine, Item font_item);
Color resolve_lambda_color(Item color_item);

// Lambda map/attribute processing
typedef struct LambdaAttributes {
    Item* attributes;           // Array of attribute items
    int attribute_count;        // Number of attributes
    char** attribute_names;     // Array of attribute names
} LambdaAttributes;

LambdaAttributes* extract_lambda_attributes(Item element);
void lambda_attributes_destroy(LambdaAttributes* attrs);
Item get_lambda_attribute(LambdaAttributes* attrs, const char* name);
const char* get_lambda_attribute_string(LambdaAttributes* attrs, const char* name);
int get_lambda_attribute_int(LambdaAttributes* attrs, const char* name, int default_value);
float get_lambda_attribute_float(LambdaAttributes* attrs, const char* name, float default_value);
bool get_lambda_attribute_bool(LambdaAttributes* attrs, const char* name, bool default_value);
Color get_lambda_attribute_color(LambdaAttributes* attrs, const char* name, Color default_color);

// Style inheritance and cascading
void apply_lambda_styles_to_docnode(DocNode* node, Item style_item);
void inherit_lambda_styles(DocNode* child, DocNode* parent);
void cascade_lambda_styles(DocNode* root);

// Document metadata extraction
void extract_document_metadata(Document* doc, Item root_item);
void set_document_title_from_lambda(Document* doc, Item title_item);
void set_document_author_from_lambda(Document* doc, Item author_item);
void extract_page_settings_from_lambda(Document* doc, Item settings_item);

// Lambda function integration helpers
Item create_lambda_page_list(Context* ctx, DocumentOutput* output);
Item create_lambda_page_item(Context* ctx, PageOutput* page);
Item create_lambda_svg_string(Context* ctx, String* svg_content);
Item create_lambda_render_stats(Context* ctx, DocumentOutput* output);

// Error handling and validation
typedef struct LambdaConversionError {
    char* message;              // Error message
    Item problematic_item;      // Item that caused the error
    int line_number;            // Line number (if available)
    int column_number;          // Column number (if available)
    struct LambdaConversionError* next; // Next error in chain
} LambdaConversionError;

LambdaConversionError* lambda_conversion_error_create(const char* message, Item item);
void lambda_conversion_error_destroy(LambdaConversionError* error);
void lambda_conversion_error_chain_destroy(LambdaConversionError* first_error);

// Conversion context
typedef struct LambdaConversionContext {
    TypesetEngine* engine;      // Typesetting engine
    Context* lambda_context;    // Lambda context
    Document* target_document;  // Target document being built
    
    // Current state
    DocNode* current_parent;    // Current parent node
    TextStyle* current_text_style; // Current text style
    LayoutStyle* current_layout_style; // Current layout style
    
    // Error tracking
    LambdaConversionError* first_error; // First error in chain
    LambdaConversionError* last_error;  // Last error in chain
    int error_count;            // Total error count
    
    // Options
    bool strict_mode;           // Strict conversion mode
    bool preserve_whitespace;   // Preserve whitespace
    bool convert_unicode;       // Convert Unicode characters
    
    // Statistics
    int nodes_converted;        // Number of nodes converted
    int warnings_generated;     // Number of warnings
} LambdaConversionContext;

LambdaConversionContext* lambda_conversion_context_create(TypesetEngine* engine, Context* lambda_ctx);
void lambda_conversion_context_destroy(LambdaConversionContext* ctx);
void lambda_conversion_context_add_error(LambdaConversionContext* ctx, const char* message, Item item);
void lambda_conversion_context_push_parent(LambdaConversionContext* ctx, DocNode* parent);
DocNode* lambda_conversion_context_pop_parent(LambdaConversionContext* ctx);

// Advanced conversion features
typedef struct ConversionOptions {
    // General options
    bool preserve_source_formatting;   // Preserve original formatting
    bool optimize_document_structure;  // Optimize document structure
    bool merge_adjacent_text_nodes;    // Merge adjacent text nodes
    
    // Math conversion options
    bool inline_simple_math;           // Render simple math inline
    bool use_display_mode_for_blocks;  // Use display mode for block math
    float math_scale_factor;           // Scale factor for math
    
    // Style options
    bool apply_default_styles;         // Apply default styles
    bool inherit_parent_styles;        // Inherit from parent elements
    bool resolve_relative_units;       // Resolve relative units to absolute
    
    // Error handling
    bool skip_invalid_elements;        // Skip invalid elements
    bool generate_fallback_content;    // Generate fallback content
    bool report_warnings;              // Report conversion warnings
} ConversionOptions;

ConversionOptions* conversion_options_create_default(void);
void conversion_options_destroy(ConversionOptions* options);
void set_conversion_options(LambdaConversionContext* ctx, ConversionOptions* options);

// Specialized converters
DocNode* convert_lambda_table_to_docnode(TypesetEngine* engine, Item table_item);
DocNode* convert_lambda_figure_to_docnode(TypesetEngine* engine, Item figure_item);
DocNode* convert_lambda_caption_to_docnode(TypesetEngine* engine, Item caption_item);
DocNode* convert_lambda_footnote_to_docnode(TypesetEngine* engine, Item footnote_item);
DocNode* convert_lambda_reference_to_docnode(TypesetEngine* engine, Item reference_item);

// Text processing utilities
char* extract_text_content_from_lambda_item(Item item);
char* normalize_whitespace(const char* text);
char* convert_unicode_entities(const char* text);
bool is_whitespace_only(const char* text);

// Utility functions for Lambda item inspection
bool lambda_item_is_element(Item item);
bool lambda_item_is_list(Item item);
bool lambda_item_is_map(Item item);
bool lambda_item_is_string(Item item);
bool lambda_item_is_number(Item item);
bool lambda_item_has_operator(Item item, const char* operator_name);
int lambda_item_get_list_length(Item item);
Item lambda_item_get_list_element(Item item, int index);

// Debug and inspection
void print_lambda_item_debug(Item item, int indent);
char* lambda_item_to_debug_string(Item item);
void validate_lambda_conversion(Document* doc, Item original_item);

#endif // LAMBDA_BRIDGE_H
