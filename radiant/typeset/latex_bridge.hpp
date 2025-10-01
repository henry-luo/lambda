#pragma once

#include "typeset_engine.hpp"
#include "../../lambda/lambda.h"
#include "../../lambda/input/input.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for math integration
void parse_math(Input* input, const char* math_string, const char* flavor);
Item input_latex(Input* input, const char* latex_content);
Item input_math(Input* input, const char* math_content);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// Bridge Lambda AST to Radiant views (reusing existing view system)
class LaTeXRadiantBridge {
private:
    RadiantTypesetEngine* engine;
    VariableMemPool* pool;
    UiContext* ui_context;
    
    // Math integration state
    Input* math_input;
    Context* lambda_context;
    
public:
    // Constructor/Destructor
    LaTeXRadiantBridge(RadiantTypesetEngine* engine);
    ~LaTeXRadiantBridge();
    
    // Main conversion function
    ViewTree* convert_latex_ast_to_radiant(Item latex_ast);
    
    // Element-specific conversions (reuse existing views)
    ViewBlock* convert_document_element(Item element);
    ViewBlock* convert_section_element(Item element, int level = 1);
    ViewBlock* convert_paragraph_element(Item element);
    ViewSpan* convert_text_element(Item element);
    ViewSpan* convert_math_inline(Item math_element);      // Reuse ViewSpan
    ViewBlock* convert_math_display(Item math_element);    // Reuse ViewBlock
    ViewTable* convert_table_element(Item element);        // Reuse existing ViewTable
    ViewBlock* convert_list_element(Item element);         // Reuse ViewBlock
    
    // Math integration with existing input-math.cpp
    ViewSpan* process_math_expression(const char* math_content, bool is_display);
    ViewSpan* integrate_with_input_math(Item math_ast);
    Item parse_math_content(const char* math_string, const char* flavor);
    
    // Text formatting support
    ViewSpan* convert_formatted_text(Item text_element, const char* format_type);
    ViewSpan* apply_text_formatting(ViewSpan* span, const char* latex_command);
    
    // Document structure support
    ViewBlock* convert_chapter_element(Item element);
    ViewBlock* convert_subsection_element(Item element, int level);
    ViewBlock* convert_abstract_element(Item element);
    ViewBlock* convert_title_element(Item element);
    
    // List processing
    ViewBlock* convert_itemize_element(Item element);
    ViewBlock* convert_enumerate_element(Item element);
    ViewBlock* convert_description_element(Item element);
    ViewBlock* convert_list_item(Item item_element, const char* list_type, int item_number);
    
    // Table processing (leverage existing ViewTable system)
    ViewTable* convert_tabular_element(Item element);
    ViewTableRow* convert_table_row(Item row_element);
    ViewTableCell* convert_table_cell(Item cell_element);
    
    // Utility functions
    void apply_latex_attributes(View* view, Item element);
    void setup_document_metadata(ViewTree* tree, Item document);
    void process_latex_preamble(Item preamble, TypesetOptions* options);
    
    // Element analysis
    bool is_math_element(Item element);
    bool is_display_math(Item element);
    const char* get_element_tag(Item element);
    const char* get_element_content(Item element);
    Array* get_element_children(Item element);
    
    // Error handling
    void log_conversion_error(const char* message, Item element);
    bool validate_element_structure(Item element);

private:
    // Internal helpers
    void initialize_math_integration();
    void cleanup_math_integration();
    ViewSpan* create_text_span(const char* text, FontProp* font = nullptr);
    ViewBlock* create_block_container(const char* css_class = nullptr);
    void apply_default_styling(View* view, const char* element_type);
    
    // Math processing helpers
    ViewSpan* process_latex_math_element(Item math_element);
    ViewSpan* process_ascii_math_element(Item math_element);
    ViewSpan* create_math_symbol(const char* symbol, bool is_display);
    
    // Text processing helpers
    ViewText* create_text_node(const char* content);
    ViewSpan* wrap_text_in_span(ViewText* text_node);
    void apply_font_properties(ViewSpan* span, const char* font_family, int font_size, int font_weight);
};

// Utility functions for Lambda integration
extern "C" {
    // C interface for direct Lambda integration
    ViewTree* latex_bridge_convert_ast(RadiantTypesetEngine* engine, Item latex_ast);
    
    // Math processing integration
    ViewSpan* latex_bridge_process_math(RadiantTypesetEngine* engine, const char* math_content, bool is_display);
    
    // Element type checking
    bool latex_bridge_is_math_element(Item element);
    bool latex_bridge_is_section_element(Item element);
    bool latex_bridge_is_table_element(Item element);
}

#endif // __cplusplus
