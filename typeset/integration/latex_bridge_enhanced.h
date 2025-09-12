#ifndef LATEX_BRIDGE_ENHANCED_H
#define LATEX_BRIDGE_ENHANCED_H

#include "latex_bridge.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced LaTeX element processing with advanced layout support

// Advanced font and style structures
typedef struct {
    char* family;               // Font family (serif, sans-serif, monospace)
    double size;                // Font size in points
    bool is_bold;               // Bold weight
    bool is_italic;             // Italic style
    bool is_underlined;         // Underlined
    bool is_small_caps;         // Small capitals
    ViewColor color;            // Text color
} LatexFontStyle;

typedef struct {
    double left_margin;         // Left margin in points
    double right_margin;        // Right margin in points
    double top_margin;          // Top margin in points
    double bottom_margin;       // Bottom margin in points
    double line_spacing;        // Line spacing multiplier (1.0 = single, 1.5 = one-and-half, etc.)
    double paragraph_spacing;   // Space before/after paragraphs
    double indent;              // First-line indent
    int alignment;              // Text alignment (use LATEX_ALIGN_* constants)
} LatexParagraphStyle;

// Alignment constants
typedef enum {
    LATEX_ALIGN_LEFT,
    LATEX_ALIGN_CENTER,
    LATEX_ALIGN_RIGHT,
    LATEX_ALIGN_JUSTIFY
} LatexAlignment;

// List type constants
typedef enum {
    LATEX_LIST_ITEMIZE,     // Bulleted list
    LATEX_LIST_ENUMERATE,   // Numbered list
    LATEX_LIST_DESCRIPTION  // Description list
} LatexListType;

typedef struct {
    int type;                   // List type (use LATEX_LIST_* constants)
    int level;                  // Nesting level (0 = top level)
    char* bullet_style;         // Custom bullet style
    double indent;              // List indentation
    double item_spacing;        // Space between items
} LatexListStyle;

// Enhanced LaTeX element processing functions
ViewNode* process_latex_document_enhanced(TypesetEngine* engine, Item document);
ViewNode* process_latex_section_enhanced(TypesetEngine* engine, Item section, int level);
ViewNode* process_latex_paragraph_enhanced(TypesetEngine* engine, Item paragraph);
ViewNode* process_latex_text_formatting_enhanced(TypesetEngine* engine, Item text_element);
ViewNode* process_latex_list_enhanced(TypesetEngine* engine, Item list);
ViewNode* process_latex_list_item_enhanced(TypesetEngine* engine, Item item, LatexListStyle* list_style);
ViewNode* process_latex_table_enhanced(TypesetEngine* engine, Item table);
ViewNode* process_latex_math_enhanced(TypesetEngine* engine, Item math_element);
ViewNode* process_latex_figure_enhanced(TypesetEngine* engine, Item figure);

// Font and style management
LatexFontStyle* latex_font_style_create_default();
LatexFontStyle* latex_font_style_from_command(Item font_command);
void latex_font_style_apply_bold(LatexFontStyle* style);
void latex_font_style_apply_italic(LatexFontStyle* style);
void latex_font_style_apply_typewriter(LatexFontStyle* style);
void latex_font_style_destroy(LatexFontStyle* style);

LatexParagraphStyle* latex_paragraph_style_create_default();
LatexParagraphStyle* latex_paragraph_style_from_environment(Item environment);
void latex_paragraph_style_destroy(LatexParagraphStyle* style);

LatexListStyle* latex_list_style_create(int type, int level);
void latex_list_style_destroy(LatexListStyle* style);

// Layout and positioning
void apply_latex_font_style_to_node(ViewNode* node, LatexFontStyle* style);
void apply_latex_paragraph_style_to_node(ViewNode* node, LatexParagraphStyle* style);
void calculate_latex_text_layout(ViewNode* text_node, double available_width);
void calculate_latex_list_layout(ViewNode* list_node, LatexListStyle* style);

// Enhanced AST analysis
bool is_latex_font_command(Item element);
bool is_latex_sectioning_command(Item element);
bool is_latex_list_environment(Item element);
bool is_latex_math_environment(Item element);
bool is_latex_table_environment(Item element);

int get_latex_section_level(const char* section_command);
const char* get_latex_list_bullet_style(int list_type, int level);
double get_latex_font_size_for_section(int section_level);

// Enhanced document structure analysis
typedef struct {
    char** section_titles;      // Array of section titles
    int* section_levels;        // Array of section levels
    int section_count;          // Number of sections
    bool has_title_page;        // Document has title page
    bool has_table_of_contents; // Document has TOC
    bool has_bibliography;      // Document has bibliography
    bool has_index;             // Document has index
} LatexDocumentStructure;

LatexDocumentStructure* analyze_latex_document_structure(Item document);
void latex_document_structure_destroy(LatexDocumentStructure* structure);

// Enhanced view tree creation with layout
ViewTree* create_enhanced_view_tree_from_latex_ast(TypesetEngine* engine, Item latex_ast);

#ifdef __cplusplus
}
#endif

#endif // LATEX_BRIDGE_ENHANCED_H
