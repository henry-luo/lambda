#pragma once

#include "../view.hpp"
#include "../../lambda/lambda.h"
#include "../../lib/mem-pool/include/mem_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct TypesetOptions TypesetOptions;
typedef struct ViewTree ViewTree;

// Typesetting options for LaTeX documents
typedef struct TypesetOptions {
    // Page settings
    double page_width;          // Page width in points (default: 612)
    double page_height;         // Page height in points (default: 792)
    double margin_left;         // Left margin in points (default: 72)
    double margin_right;        // Right margin in points (default: 72)
    double margin_top;          // Top margin in points (default: 72)
    double margin_bottom;       // Bottom margin in points (default: 72)
    
    // Typography
    char* default_font_family;  // Default font family
    double default_font_size;   // Default font size (12pt default)
    double line_height;         // Line height multiplier (1.2 default)
    double paragraph_spacing;   // Paragraph spacing
    
    // Math settings
    char* math_font_family;     // Mathematical font family
    double math_font_scale;     // Math font scaling factor
    bool use_display_math;      // Enable display math mode
    
    // Quality settings
    bool optimize_layout;       // Optimize layout for performance
    bool show_debug_info;       // Show debug information
} TypesetOptions;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// Main Radiant-integrated typeset engine
class RadiantTypesetEngine {
private:
    UiContext* ui_context;           // Radiant UI context
    VariableMemPool* pool;           // Memory management
    TypesetOptions* default_options; // Default typeset options
    
    // Statistics
    struct {
        int documents_processed;
        int pages_generated;
        double total_layout_time;
        size_t memory_usage;
    } stats;

public:
    // Constructor/Destructor
    RadiantTypesetEngine(UiContext* ui_context);
    ~RadiantTypesetEngine();
    
    // Main typesetting function - produces Radiant ViewTree
    ViewTree* typeset_latex_document(Item latex_ast, TypesetOptions* options = nullptr);
    
    // LaTeX-specific processing (reuse existing Radiant views)
    ViewBlock* process_latex_document(Item document_node);
    ViewBlock* process_latex_section(Item section_node, int level);
    ViewBlock* process_latex_paragraph(Item paragraph_node);
    ViewSpan* process_latex_math_inline(Item math_node);      // Reuse ViewSpan
    ViewBlock* process_latex_math_display(Item math_node);    // Reuse ViewBlock
    ViewTable* process_latex_table(Item table_node);         // Reuse ViewTable
    ViewBlock* process_latex_list(Item list_node);           // Reuse ViewBlock
    
    // Text formatting (extend existing ViewText/ViewSpan)
    ViewSpan* process_text_formatting(Item text_node, const char* format_type);
    ViewSpan* apply_font_styling(ViewSpan* span, const char* font_command);
    
    // Integration with Radiant layout system
    void apply_latex_styling(ViewBlock* view, const char* latex_class);
    void setup_page_layout(ViewTree* tree, TypesetOptions* options);
    
    // Options management
    TypesetOptions* create_default_options();
    void destroy_options(TypesetOptions* options);
    
    // Statistics and debugging
    void reset_stats();
    void print_stats() const;
    
    // Getters
    UiContext* get_ui_context() const { return ui_context; }
    VariableMemPool* get_memory_pool() const { return pool; }
};

// Utility functions
extern "C" {
    // C interface for Lambda integration
    ViewTree* radiant_typeset_latex(UiContext* ui_context, Item latex_ast, TypesetOptions* options);
    
    // Options management
    TypesetOptions* typeset_options_create_default(void);
    void typeset_options_destroy(TypesetOptions* options);
    
    // Constants
    #define TYPESET_DEFAULT_PAGE_WIDTH 612.0        // Letter width in points
    #define TYPESET_DEFAULT_PAGE_HEIGHT 792.0       // Letter height in points
    #define TYPESET_DEFAULT_MARGIN 72.0             // 1 inch in points
    #define TYPESET_DEFAULT_FONT_SIZE 12.0
    #define TYPESET_DEFAULT_LINE_HEIGHT 1.2
    
    // Units conversion (everything internal is in points)
    #define POINTS_PER_INCH 72.0
    #define POINTS_PER_MM 2.834645669
    #define POINTS_PER_CM 28.34645669
}

#endif // __cplusplus
