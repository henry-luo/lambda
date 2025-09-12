#ifndef LATEX_BRIDGE_H
#define LATEX_BRIDGE_H

#include "../typeset.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LaTeX-specific bridge for converting Lambda LaTeX AST to typeset structures
// This is a separate bridge to avoid disrupting existing Markdown/HTML pipelines

// LaTeX element types detected from Lambda AST
typedef enum {
    LATEX_ELEMENT_UNKNOWN,
    LATEX_ELEMENT_DOCUMENT,
    LATEX_ELEMENT_DOCUMENTCLASS,
    LATEX_ELEMENT_USEPACKAGE,
    LATEX_ELEMENT_TITLE,
    LATEX_ELEMENT_AUTHOR,
    LATEX_ELEMENT_DATE,
    LATEX_ELEMENT_MAKETITLE,
    LATEX_ELEMENT_ABSTRACT,
    LATEX_ELEMENT_SECTION,
    LATEX_ELEMENT_SUBSECTION,
    LATEX_ELEMENT_SUBSUBSECTION,
    LATEX_ELEMENT_PARAGRAPH,
    LATEX_ELEMENT_SUBPARAGRAPH,
    LATEX_ELEMENT_TEXTBF,
    LATEX_ELEMENT_TEXTIT,
    LATEX_ELEMENT_TEXTTT,
    LATEX_ELEMENT_EMPH,
    LATEX_ELEMENT_UNDERLINE,
    LATEX_ELEMENT_ITEMIZE,
    LATEX_ELEMENT_ENUMERATE,
    LATEX_ELEMENT_ITEM,
    LATEX_ELEMENT_DESCRIPTION,
    LATEX_ELEMENT_FIGURE,
    LATEX_ELEMENT_TABLE,
    LATEX_ELEMENT_TABULAR,
    LATEX_ELEMENT_INCLUDEGRAPHICS,
    LATEX_ELEMENT_CAPTION,
    LATEX_ELEMENT_LABEL,
    LATEX_ELEMENT_REF,
    LATEX_ELEMENT_CITE,
    LATEX_ELEMENT_FOOTNOTE,
    LATEX_ELEMENT_VERBATIM,
    LATEX_ELEMENT_LSTLISTING,
    LATEX_ELEMENT_EQUATION,
    LATEX_ELEMENT_ALIGN,
    LATEX_ELEMENT_MATH_INLINE,
    LATEX_ELEMENT_MATH_DISPLAY,
    LATEX_ELEMENT_NEWLINE,
    LATEX_ELEMENT_PAGEBREAK,
    LATEX_ELEMENT_CLEARPAGE,
    LATEX_ELEMENT_HSPACE,
    LATEX_ELEMENT_VSPACE,
    LATEX_ELEMENT_HREF,
    LATEX_ELEMENT_URL,
    LATEX_ELEMENT_TEXTCOLOR,
    LATEX_ELEMENT_COLORBOX,
    LATEX_ELEMENT_FBOX
} LatexElementType;

// LaTeX document metadata structure
typedef struct {
    char* document_class;       // e.g., "article", "book", "report"
    char* document_options;     // e.g., "12pt,a4paper"
    char* title;
    char* author;
    char* date;
    char** packages;            // Array of used packages
    int package_count;
    bool has_title_page;
    bool has_abstract;
} LatexDocumentMetadata;

// Main LaTeX bridge functions - NEW ENTRY POINTS
ViewTree* create_view_tree_from_latex_ast(TypesetEngine* engine, Item latex_ast);

// LaTeX element detection and classification
LatexElementType detect_latex_element_type(Item element);
bool is_latex_structure_element(LatexElementType type);
bool is_latex_text_formatting_element(LatexElementType type);
bool is_latex_math_element(LatexElementType type);
bool is_latex_list_element(LatexElementType type);
bool is_latex_table_element(LatexElementType type);
bool is_latex_figure_element(LatexElementType type);

// LaTeX document metadata extraction
LatexDocumentMetadata* extract_latex_metadata(Item latex_document);
void latex_metadata_destroy(LatexDocumentMetadata* metadata);

// LaTeX to ViewTree conversion (main implementation)
ViewNode* convert_latex_element_to_viewnode(TypesetEngine* engine, Item element);
ViewNode* convert_latex_document_to_viewnode(TypesetEngine* engine, Item document);
ViewNode* convert_latex_section_to_viewnode(TypesetEngine* engine, Item section, int level);
ViewNode* convert_latex_paragraph_to_viewnode(TypesetEngine* engine, Item paragraph);
ViewNode* convert_latex_text_formatting_to_viewnode(TypesetEngine* engine, Item element);
ViewNode* convert_latex_list_to_viewnode(TypesetEngine* engine, Item list);
ViewNode* convert_latex_list_item_to_viewnode(TypesetEngine* engine, Item item);
ViewNode* convert_latex_table_to_viewnode(TypesetEngine* engine, Item table);
ViewNode* convert_latex_figure_to_viewnode(TypesetEngine* engine, Item figure);
ViewNode* convert_latex_math_to_viewnode(TypesetEngine* engine, Item math);
ViewNode* convert_latex_verbatim_to_viewnode(TypesetEngine* engine, Item verbatim);

// LaTeX-specific layout and styling
void apply_latex_document_styling(ViewTree* tree, LatexDocumentMetadata* metadata);
void apply_latex_section_styling(ViewNode* node, int section_level);
void apply_latex_text_formatting(ViewNode* node, LatexElementType format_type);

// LaTeX utility functions
const char* get_latex_element_operator(Item element);
Item get_latex_element_content(Item element);
Item* get_latex_element_arguments(Item element, int* arg_count);
const char* get_latex_command_name(Item element);
const char* get_latex_environment_name(Item element);

// LaTeX-specific error handling
typedef struct {
    char* message;
    Item problematic_element;
    int line_number;
    int column_number;
} LatexConversionError;

LatexConversionError* latex_conversion_error_create(const char* message, Item element);
void latex_conversion_error_destroy(LatexConversionError* error);

// LaTeX debugging utilities
void debug_print_latex_element(Item element, int indent);
void debug_print_latex_metadata(LatexDocumentMetadata* metadata);

#ifdef __cplusplus
}
#endif

#endif // LATEX_BRIDGE_H
