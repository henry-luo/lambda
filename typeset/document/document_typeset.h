#pragma once

#include "../typeset.h"
#include "../math_typeset.h"
#include "../../lambda/lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

// Document typesetting that combines markdown and math
// Integrates with existing format-md.cpp and new math typesetting system

// Document typesetting options
typedef struct {
    TypesetOptions base_options;        // Base typesetting options
    MathTypesetOptions math_options;    // Math-specific options
    
    // Document layout options
    bool render_math_as_svg;           // Render math as embedded SVG
    bool inline_math_baseline_align;   // Align inline math with text baseline
    double math_scale_factor;          // Scale factor for math expressions
    
    // Style options
    char* document_title;              // Document title
    char* document_author;             // Document author
    bool include_table_of_contents;    // Generate table of contents
    bool number_sections;              // Number section headings
    
    // Output options
    char* output_format;               // "svg", "pdf", "html"
    bool standalone_output;            // Generate complete document
} DocumentTypesetOptions;

// Document typesetting result
typedef struct {
    ViewTree* view_tree;               // Complete document view tree
    StrBuf* rendered_output;           // Final rendered output (SVG/HTML/etc)
    
    // Statistics
    int total_pages;
    int math_expressions_count;
    int inline_math_count;
    int display_math_count;
    double typeset_time_ms;
    size_t output_size_bytes;
    
    // Error information
    bool has_errors;
    char* error_message;
} DocumentTypesetResult;

// Main document typesetting functions

/**
 * Typeset a complete markdown document with math expressions
 * @param lambda_element Root element from Lambda markdown parser
 * @param options Document typesetting options
 * @return Complete typeset result with view tree and rendered output
 */
DocumentTypesetResult* typeset_markdown_document(Element* lambda_element, 
                                                DocumentTypesetOptions* options);

/**
 * Typeset markdown from string content
 * @param markdown_content Markdown content string
 * @param options Document typesetting options
 * @return Complete typeset result
 */
DocumentTypesetResult* typeset_markdown_from_string(const char* markdown_content,
                                                   DocumentTypesetOptions* options);

/**
 * Process mixed markdown and math content
 * Handles inline math ($...$) and display math ($$...$$) in markdown
 * @param lambda_element Root markdown element with embedded math
 * @param options Typesetting options
 * @return ViewTree with properly typeset text and math
 */
ViewTree* process_markdown_with_math(Element* lambda_element, 
                                    DocumentTypesetOptions* options);

// Document structure processing

/**
 * Process document sections and create hierarchical structure
 * @param lambda_element Document root element
 * @param view_tree Target view tree
 * @param options Typesetting options
 * @return Success/failure status
 */
bool process_document_structure(Element* lambda_element, ViewTree* view_tree,
                               DocumentTypesetOptions* options);

/**
 * Process math elements within document context
 * @param math_element Math element from markdown parser
 * @param context Document context (inline/display)
 * @param options Typesetting options
 * @return ViewNode with typeset math
 */
ViewNode* process_math_in_document(Element* math_element, 
                                  const char* context,
                                  DocumentTypesetOptions* options);

/**
 * Create table of contents from document structure
 * @param lambda_element Document root
 * @param options Typesetting options
 * @return ViewNode representing table of contents
 */
ViewNode* create_table_of_contents(Element* lambda_element,
                                  DocumentTypesetOptions* options);

// Layout and positioning

/**
 * Layout text and math elements with proper baseline alignment
 * @param text_node Text content node
 * @param math_nodes Array of math nodes to integrate
 * @param math_count Number of math nodes
 * @param options Layout options
 * @return Combined layout node
 */
ViewNode* layout_text_with_math(ViewNode* text_node, 
                               ViewNode** math_nodes, 
                               int math_count,
                               DocumentTypesetOptions* options);

/**
 * Calculate document page layout with proper pagination
 * @param content_tree Content view tree
 * @param options Document options
 * @return Paginated view tree
 */
ViewTree* calculate_document_pagination(ViewTree* content_tree,
                                       DocumentTypesetOptions* options);

// Output generation

/**
 * Render complete document to SVG
 * @param document_tree Complete document view tree
 * @param options Rendering options
 * @return SVG string buffer
 */
StrBuf* render_document_to_svg(ViewTree* document_tree,
                              DocumentTypesetOptions* options);

/**
 * Generate standalone SVG document with embedded CSS
 * @param document_tree Document view tree
 * @param options Document options
 * @return Complete SVG document
 */
StrBuf* generate_standalone_svg_document(ViewTree* document_tree,
                                        DocumentTypesetOptions* options);

// Utility functions

/**
 * Create default document typesetting options
 * @return Default options structure
 */
DocumentTypesetOptions* create_default_document_options(void);

/**
 * Destroy document typesetting options
 * @param options Options to destroy
 */
void destroy_document_options(DocumentTypesetOptions* options);

/**
 * Destroy document typesetting result
 * @param result Result to destroy
 */
void destroy_document_result(DocumentTypesetResult* result);

/**
 * Extract math expressions from markdown element tree
 * @param lambda_element Root element
 * @param math_elements Output array for math elements
 * @param max_elements Maximum number of elements to extract
 * @return Number of math expressions found
 */
int extract_math_expressions(Element* lambda_element, 
                            Element** math_elements,
                            int max_elements);

/**
 * Validate document structure for typesetting
 * @param lambda_element Document root
 * @return Validation result
 */
bool validate_document_structure(Element* lambda_element);

#ifdef __cplusplus
}
#endif
