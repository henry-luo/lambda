#include "format-latex-html.h"
#include "../mark_reader.hpp"
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
static void generate_latex_css(StringBuf* css_buf);
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth);
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth);
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class);
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class);
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag);
static void process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void append_escaped_text(StringBuf* html_buf, const char* text);
static void append_indent(StringBuf* html_buf, int depth);

// reader-based forward declarations
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth);
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth);
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth);

// Document metadata storage
typedef struct {
    char* title;
    char* author;
    char* date;
    bool in_document;
    int section_counter;
} DocumentState;

static DocumentState doc_state = {0};
// Main API function
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, Pool* pool) {
    if (!html_buf || !css_buf || !pool) {
        return;
    }

    printf("DEBUG: format_latex_to_html - html_buf=%p, css_buf=%p\n", html_buf, css_buf);

    // Initialize document state
    memset(&doc_state, 0, sizeof(DocumentState));

    // Start HTML document container
    stringbuf_append_str(html_buf, "<div class=\"latex-document\">\n");
    printf("DEBUG: Added HTML container to html_buf\n");


    // Check if we have a valid AST
    if (latex_ast.item == ITEM_NULL) {
    } else {
        // Process the LaTeX AST without automatic paragraph wrapper
        // Individual text content will be wrapped in paragraphs as needed
        printf("DEBUG: About to process LaTeX AST\n");
        
        // use MarkReader API
        ItemReader ast_reader(latex_ast.to_const());
        process_latex_element_reader(html_buf, ast_reader, pool, 1);
    }

    // Break down the CSS into smaller chunks to avoid C++ compiler issues with very long string literals

    // Document styles
    stringbuf_append_str(css_buf, ".latex-document {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Title styles
    stringbuf_append_str(css_buf, ".latex-title {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 2.5em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Author styles
    stringbuf_append_str(css_buf, ".latex-author {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Date styles
    stringbuf_append_str(css_buf, ".latex-date {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Section styles
    stringbuf_append_str(css_buf, ".latex-section {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.8em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "  border-bottom: 1px solid #ccc;\n");
    stringbuf_append_str(css_buf, "  padding-bottom: 0.5rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsection styles
    stringbuf_append_str(css_buf, ".latex-subsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.4em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1.5rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsubsection styles
    stringbuf_append_str(css_buf, ".latex-subsubsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Text formatting styles
    stringbuf_append_str(css_buf, ".latex-textbf {\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textit {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-emph {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-texttt {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Courier New', monospace;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-underline {\n");
    stringbuf_append_str(css_buf, "  text-decoration: underline;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-sout {\n");
    stringbuf_append_str(css_buf, "  text-decoration: line-through;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Font size classes
    stringbuf_append_str(css_buf, ".latex-tiny { font-size: 0.5em; }\n");
    stringbuf_append_str(css_buf, ".latex-small { font-size: 0.8em; }\n");
    stringbuf_append_str(css_buf, ".latex-normalsize { font-size: 1em; }\n");
    stringbuf_append_str(css_buf, ".latex-large { font-size: 1.2em; }\n");
    stringbuf_append_str(css_buf, ".latex-Large { font-size: 1.4em; }\n");
    stringbuf_append_str(css_buf, ".latex-huge { font-size: 2em; }\n");
    
    // Font family classes
    stringbuf_append_str(css_buf, ".latex-textrm { font-family: serif; }\n");
    stringbuf_append_str(css_buf, ".latex-textsf { font-family: sans-serif; }\n");
    
    // Font weight classes
    stringbuf_append_str(css_buf, ".latex-textmd { font-weight: normal; }\n");
    
    // Font shape classes
    stringbuf_append_str(css_buf, ".latex-textup { font-style: normal; }\n");
    stringbuf_append_str(css_buf, ".latex-textsl { font-style: oblique; }\n");
    stringbuf_append_str(css_buf, ".latex-textsc { font-variant: small-caps; }\n");
    
    // Reset to normal
    stringbuf_append_str(css_buf, ".latex-textnormal { font-family: serif; font-weight: normal; font-style: normal; font-variant: normal; }\n");
    
    // Verbatim styles
    stringbuf_append_str(css_buf, ".latex-verbatim { font-family: 'Courier New', 'Lucida Console', monospace; background-color: #f5f5f5; padding: 0.2em 0.4em; border-radius: 3px; }\n");

    // List styles
    stringbuf_append_str(css_buf, ".latex-itemize {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-enumerate {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-item {\n");
    stringbuf_append_str(css_buf, "  margin: 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Alignment environment styles
    stringbuf_append_str(css_buf, ".latex-center {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-flushleft {\n");
    stringbuf_append_str(css_buf, "  text-align: left;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-flushright {\n");
    stringbuf_append_str(css_buf, "  text-align: right;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Close document container
    stringbuf_append_str(html_buf, "</div>\n");
    printf("DEBUG: Closed HTML container\n");

    printf("DEBUG: HTML and CSS generation completed\n");
}

// Generate comprehensive CSS for LaTeX documents
static void generate_latex_css(StringBuf* css_buf) {
    if (!css_buf) {
        printf("DEBUG: css_buf is NULL!\n");
        return;
    }

    printf("DEBUG: css_buf=%p, pool=%p, str=%p, length=%zu, capacity=%zu\n",
           css_buf, css_buf->pool, css_buf->str, css_buf->length, css_buf->capacity);

    // Validate StringBuf structure before using it
    if (!css_buf->pool) {
        printf("DEBUG: css_buf->pool is NULL!\n");
        return;
    }

    if (css_buf->str && css_buf->capacity == 0) {
        printf("DEBUG: css_buf has str but zero capacity!\n");
        return;
    }

    if (css_buf->length > css_buf->capacity) {
        printf("DEBUG: css_buf length (%zu) > capacity (%zu)!\n", css_buf->length, css_buf->capacity);
        return;
    }

    // Generate basic CSS for LaTeX documents - broken into small chunks to avoid C++ compiler issues
    stringbuf_append_str(css_buf, ".latex-document {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textbf { font-weight: bold; }\n");
    stringbuf_append_str(css_buf, ".latex-textit { font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".latex-section { font-size: 1.8em; font-weight: bold; margin: 2rem 0 1rem 0; }\n");
    stringbuf_append_str(css_buf, ".latex-subsection { font-size: 1.4em; font-weight: bold; margin: 1.5rem 0 1rem 0; }\n");

    // Additional CSS for list environments
    stringbuf_append_str(css_buf, ".latex-itemize, .latex-enumerate { margin: 1rem 0; padding-left: 2rem; }\n");
    stringbuf_append_str(css_buf, ".latex-item { margin: 0.5rem 0; }\n");
}

// Process a LaTeX element and convert to HTML
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth) {
    if (item.item == ITEM_NULL) {
        return;
    }

    TypeId type = get_type_id(item);
    
    // CRITICAL DEBUG: Check for invalid type values
    if (type > LMD_TYPE_ERROR) {
        printf("ERROR: Invalid type %d detected in process_latex_element! Max valid type is %d\n", type, LMD_TYPE_ERROR);
        log_error("Item details - item.item=0x%llx, item.pointer=%llu", (unsigned long long)item.item, (unsigned long long)item.pointer);
        printf("ERROR: Stack trace: depth=%d\n", depth);
        return;
    }

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (!elem || !elem->type) {
            return;
        }

        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        if (!elmt_type) {
            return;
        }

        StrView name = elmt_type->name;
        if (!name.str || name.length == 0 || name.length > 100) return;

        // Convert name to null-terminated string for easier comparison
        char cmd_name[64];
        int name_len = name.length < 63 ? name.length : 63;
        strncpy(cmd_name, name.str, name_len);
        cmd_name[name_len] = '\0';
        
        // printf("DEBUG: Processing command '%s' (length: %d)\n", cmd_name, name_len);


        // Handle different LaTeX commands
        if (strcmp(cmd_name, "argument") == 0) {
            // Process argument content (nested LaTeX) without paragraph wrapping
            process_element_content_simple(html_buf, elem, pool, depth);
            return;
        } else if (strcmp(cmd_name, "documentclass") == 0) {
            // Skip documentclass - it's metadata
            return;
        }
        else if (strcmp(cmd_name, "title") == 0) {
            process_title(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "author") == 0) {
            process_author(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "date") == 0) {
            process_date(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "maketitle") == 0) {
            process_maketitle(html_buf, pool, depth);
        }
        else if (strcmp(cmd_name, "section") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-section");
        }
        else if (strcmp(cmd_name, "subsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsection");
        }
        else if (strcmp(cmd_name, "subsubsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsubsection");
        }
        else if (strcmp(cmd_name, "begin") == 0) {
            process_environment(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "center") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-center");
        }
        else if (strcmp(cmd_name, "flushleft") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-flushleft");
        }
        else if (strcmp(cmd_name, "flushright") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-flushright");
        }
        else if (strcmp(cmd_name, "quote") == 0) {
            process_quote(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "verbatim") == 0) {
            process_verbatim(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "textbf") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textbf", "span");
        }
        else if (strcmp(cmd_name, "textit") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textit", "span");
        }
        else if (strcmp(cmd_name, "emph") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-emph", "span");
        }
        else if (strcmp(cmd_name, "texttt") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-texttt", "span");
        }
        else if (strcmp(cmd_name, "underline") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-underline", "span");
        }
        else if (strcmp(cmd_name, "sout") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-sout", "span");
        }
        else if (strcmp(cmd_name, "tiny") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-tiny", "span");
        }
        else if (strcmp(cmd_name, "small") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-small", "span");
        }
        else if (strcmp(cmd_name, "normalsize") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-normalsize", "span");
        }
        else if (strcmp(cmd_name, "large") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-large", "span");
        }
        else if (strcmp(cmd_name, "Large") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-Large", "span");
        }
        else if (strcmp(cmd_name, "huge") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-huge", "span");
        }
        else if (strcmp(cmd_name, "textrm") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textrm", "span");
        }
        else if (strcmp(cmd_name, "textsf") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textsf", "span");
        }
        else if (strcmp(cmd_name, "textmd") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textmd", "span");
        }
        else if (strcmp(cmd_name, "textup") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textup", "span");
        }
        else if (strcmp(cmd_name, "textsl") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textsl", "span");
        }
        else if (strcmp(cmd_name, "textsc") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textsc", "span");
        }
        else if (strcmp(cmd_name, "textnormal") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-textnormal", "span");
        }
        else if (strcmp(cmd_name, "linebreak") == 0) {
            stringbuf_append_str(html_buf, "<br>");
        }
        else if (strcmp(cmd_name, "par") == 0) {
            // Par creates a paragraph break - handled by paragraph logic
            // This is a no-op in HTML since paragraph breaks are handled by the paragraph wrapper
        }
        else if (strcmp(cmd_name, "verb") == 0) {
            stringbuf_append_str(html_buf, "<code class=\"latex-verbatim\">");
            process_element_content_simple(html_buf, elem, pool, depth);
            stringbuf_append_str(html_buf, "</code>");
        }
        else if (strcmp(cmd_name, "thinspace") == 0) {
            stringbuf_append_str(html_buf, "&thinsp;"); // HTML thin space entity (U+2009)
        }
        else if (strcmp(cmd_name, "literal") == 0) {
            // Render literal character content with HTML escaping
            size_t start_pos = html_buf->length;
            process_element_content_simple(html_buf, elem, pool, depth);
            
            // Add trailing space after special characters that need it
            // This matches LaTeX behavior where \$ produces "$ " not "$"
            if (html_buf->length > start_pos && html_buf->str && html_buf->str->chars) {
                char last_char = html_buf->str->chars[html_buf->length - 1];
                // Add space after money symbols, hash, percent
                if (last_char == '$' || last_char == '#' || last_char == '%') {
                    stringbuf_append_char(html_buf, ' ');
                }
            }
        }
        else if (strcmp(cmd_name, "textbackslash") == 0) {
            stringbuf_append_char(html_buf, '\\'); // Render backslash
        }
        else if (strcmp(cmd_name, "item") == 0) {
            process_item(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "itemize") == 0) {
            printf("DEBUG: Processing itemize environment directly\n");
            process_itemize(html_buf, elem, pool, depth);
        }
        else if (strcmp(cmd_name, "enumerate") == 0) {
            printf("DEBUG: Processing enumerate environment directly\n");
            process_enumerate(html_buf, elem, pool, depth);
        }
        else {
            // Generic element - process children
            // printf("DEBUG: Processing generic element: '%s' (length: %d)\n", cmd_name, name_len);
            // printf("DEBUG: Checking texttt comparison: strcmp('%s', 'texttt') = %d\n", cmd_name, strcmp(cmd_name, "texttt"));
            process_element_content(html_buf, elem, pool, depth);
        }
    }
    else if (type == LMD_TYPE_STRING) {
        // Handle text content
        String* str = (String*)item.pointer;
        printf("DEBUG: LMD_TYPE_STRING - str=%p\n", str);
        if (str) {
            stringbuf_append_str(html_buf, str->chars);
        } else {
            printf("DEBUG: String pointer is null, skipping\n");
        }
    }
    else if (type == LMD_TYPE_ARRAY) {
        // Process array of elements
        Array* arr = item.array;
        if (arr && arr->items) {
            for (int i = 0; i < arr->length; i++) {
                process_latex_element(html_buf, arr->items[i], pool, depth);
            }
        }
    }
}

// Check if an element is a block-level element that should not be wrapped in paragraphs
static bool is_block_element(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) {
        return false;
    }

    Element* elem = item.element;
    if (!elem || !elem->type) {
        return false;
    }

    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) {
        return false;
    }

    // Convert name to null-terminated string
    char cmd_name[64];
    int name_len = elmt_type->name.length < 63 ? elmt_type->name.length : 63;
    strncpy(cmd_name, elmt_type->name.str, name_len);
    cmd_name[name_len] = '\0';

    // Block-level elements that should not be wrapped in paragraphs
    return (strcmp(cmd_name, "section") == 0 ||
            strcmp(cmd_name, "subsection") == 0 ||
            strcmp(cmd_name, "subsubsection") == 0 ||
            strcmp(cmd_name, "itemize") == 0 ||
            strcmp(cmd_name, "enumerate") == 0 ||
            strcmp(cmd_name, "quote") == 0 ||
            strcmp(cmd_name, "verbatim") == 0 ||
            strcmp(cmd_name, "center") == 0 ||
            strcmp(cmd_name, "flushleft") == 0 ||
            strcmp(cmd_name, "flushright") == 0 ||
            strcmp(cmd_name, "title") == 0 ||
            strcmp(cmd_name, "author") == 0 ||
            strcmp(cmd_name, "date") == 0 ||
            strcmp(cmd_name, "maketitle") == 0 ||
            strcmp(cmd_name, "par") == 0);
}

// Process element content without paragraph wrapping (for titles, etc.)
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem || !elem->items) {
        return;
    }

    // Process element items directly without paragraph wrapping
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        for (int i = 0; i < elem->length; i++) {
            Item content_item = elem->items[i];
            process_latex_element(html_buf, content_item, pool, depth);
        }
    }
}

// Process element content with intelligent paragraph wrapping
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem || !elem->items) {
        printf("DEBUG: process_element_content - elem or items is null\n");
        return;
    }

    printf("DEBUG: process_element_content - elem->length = %d\n", elem->length);

    // Process element items with intelligent paragraph grouping
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        bool in_paragraph = false;
        bool need_new_paragraph = false;

        for (int i = 0; i < elem->length; i++) {
            printf("DEBUG: Processing element content item %d\n", i);
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);
            printf("DEBUG: Content item %d has type %d\n", i, item_type);
            
            // CRITICAL DEBUG: Check for invalid type values
            if (item_type > LMD_TYPE_ERROR) {
                printf("ERROR: Invalid type %d detected in process_element_content! Max valid type is %d\n", item_type, LMD_TYPE_ERROR);
                log_error("Item details - item.item=0x%llx, item.pointer=%llu", (unsigned long long)content_item.item, (unsigned long long)content_item.pointer);
                printf("ERROR: Element index: %d, elem->length: %lld\n", i, elem->length);
                printf("ERROR: Raw memory dump of Item:\n");
                unsigned char* bytes = (unsigned char*)&content_item;
                for (int j = 0; j < sizeof(Item); j++) {
                    printf("  byte[%d] = 0x%02x\n", j, bytes[j]);
                }
                continue; // Skip processing this invalid item
            }

            bool is_block = is_block_element(content_item);
            bool is_text = (item_type == LMD_TYPE_STRING);
            bool is_inline = (item_type == LMD_TYPE_ELEMENT && !is_block);
            
            // Check if this is a paragraph break element or textblock
            bool is_par_break = false;
            bool is_textblock = false;
            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem_ptr = (Element*)content_item.pointer;
                if (elem_ptr && elem_ptr->type) {
                    StrView elem_name = ((TypeElmt*)elem_ptr->type)->name;
                    printf("DEBUG: Element name: '%.*s' (length: %zu)\n", (int)elem_name.length, elem_name.str, elem_name.length);
                    if ((elem_name.length == 3 && strncmp(elem_name.str, "par", 3) == 0) ||
                        (elem_name.length == 8 && strncmp(elem_name.str, "parbreak", 8) == 0)) {
                        is_par_break = true;
                        printf("DEBUG: Detected par break element\n");
                    } else if (elem_name.length == 9 && strncmp(elem_name.str, "textblock", 9) == 0) {
                        is_textblock = true;
                        printf("DEBUG: Detected textblock element\n");
                    }
                }
            }
            
            // Handle paragraph wrapping logic
            if (is_textblock) {
                // Process textblock: text + parbreak
                Element* textblock_elem = (Element*)content_item.pointer;
                printf("DEBUG: Processing textblock with length: %lld\n", textblock_elem ? textblock_elem->length : -1);
                if (textblock_elem && textblock_elem->items && textblock_elem->length >= 1) {
                    // Process the text part
                    Item text_item = textblock_elem->items[0];
                    printf("DEBUG: First item type: %d (LMD_TYPE_STRING=%d)\n", get_type_id(text_item), LMD_TYPE_STRING);
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        printf("DEBUG: Processing text item in textblock\n");
                    } else {
                        printf("DEBUG: First item is not a string, processing as element\n");
                        // Process as element instead
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                stringbuf_append_str(html_buf, "</p>\n");
                            }
                            stringbuf_append_str(html_buf, "<p>");
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }
                        process_latex_element(html_buf, text_item, pool, depth);
                    }
                    
                    // Original string processing
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                stringbuf_append_str(html_buf, "</p>\n");
                            }
                            stringbuf_append_str(html_buf, "<p>");
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }
                        process_latex_element(html_buf, text_item, pool, depth);
                    }
                    
                    // Check if there's a parbreak (should be second element)
                    if (textblock_elem->length >= 2) {
                        Item parbreak_item = textblock_elem->items[1];
                        if (get_type_id(parbreak_item) == LMD_TYPE_ELEMENT) {
                            Element* parbreak_elem = (Element*)parbreak_item.pointer;
                            if (parbreak_elem && parbreak_elem->type) {
                                StrView parbreak_name = ((TypeElmt*)parbreak_elem->type)->name;
                                if (parbreak_name.length == 8 && strncmp(parbreak_name.str, "parbreak", 8) == 0) {
                                    // Close current paragraph and force new paragraph for next content
                                    if (in_paragraph) {
                                        stringbuf_append_str(html_buf, "</p>\n");
                                        in_paragraph = false;
                                    }
                                    need_new_paragraph = true;
                                }
                            }
                        }
                    }
                }
            } else if (is_par_break) {
                // Close current paragraph and force new paragraph for next content
                if (in_paragraph) {
                    stringbuf_append_str(html_buf, "</p>\n");
                    in_paragraph = false;
                }
                need_new_paragraph = true;
                // Don't process the par element itself, just use it as a break marker
            } else if (is_block) {
                // Close any open paragraph before block element
                if (in_paragraph) {
                    stringbuf_append_str(html_buf, "</p>\n");
                    in_paragraph = false;
                }
                // Process block element directly
                process_latex_element(html_buf, content_item, pool, depth);
            } else if (is_text || is_inline) {
                // Handle paragraph creation based on context
                if (!in_paragraph || need_new_paragraph) {
                    if (need_new_paragraph && in_paragraph) {
                        // This shouldn't happen since par breaks close paragraphs
                        stringbuf_append_str(html_buf, "</p>\n");
                    }
                    stringbuf_append_str(html_buf, "<p>");
                    in_paragraph = true;
                    need_new_paragraph = false;
                }
                // Process inline content (both text and inline elements)
                process_latex_element(html_buf, content_item, pool, depth);
            } else {
                // Unknown content type - treat as inline if we're in a paragraph context
                if (!in_paragraph) {
                    stringbuf_append_str(html_buf, "<p>");
                    in_paragraph = true;
                }
                process_latex_element(html_buf, content_item, pool, depth);
            }

            printf("DEBUG: Finished processing element content item %d\n", i);
        }

        // Close any remaining open paragraph
        if (in_paragraph) {
            stringbuf_append_str(html_buf, "</p>\n");
        }
    }
    printf("DEBUG: process_element_content completed\n");
}

// Helper function for recursive text extraction from elements
static void extract_text_recursive(StringBuf* buf, Element* elem, Pool* pool) {
    if (!elem || !elem->items) return;
    
    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child.pointer;
            if (str && str->chars) {
                stringbuf_append_str(buf, str->chars);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child.pointer;
            extract_text_recursive(buf, child_elem, pool);
        }
    }
}

// Process title command
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);
    
    String* title_str = stringbuf_to_string(temp_buf);
    if (title_str && title_str->len > 0) {
        doc_state.title = strdup(title_str->chars);
    }
    
    stringbuf_free(temp_buf);
}

// Process author command
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);
    
    String* author_str = stringbuf_to_string(temp_buf);
    if (author_str && author_str->len > 0) {
        doc_state.author = strdup(author_str->chars);
    }
    
    stringbuf_free(temp_buf);
}

// Process date command
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);
    
    String* date_str = stringbuf_to_string(temp_buf);
    if (date_str && date_str->len > 0) {
        doc_state.date = strdup(date_str->chars);
    }
    
    stringbuf_free(temp_buf);
}

// Process maketitle command
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth) {
    if (doc_state.title) {
        stringbuf_append_str(html_buf, "<div class=\"latex-title\">");
        append_escaped_text(html_buf, doc_state.title);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.author) {
        stringbuf_append_str(html_buf, "<div class=\"latex-author\">");
        append_escaped_text(html_buf, doc_state.author);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.date) {
        stringbuf_append_str(html_buf, "<div class=\"latex-date\">");
        append_escaped_text(html_buf, doc_state.date);
        stringbuf_append_str(html_buf, "</div>\n");
    }
}

// Process section commands
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class) {
    if (!elem) return;

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Process section title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process environments (begin/end blocks)
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Get environment name from first child
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* env_name = (String*)elem->items[0].pointer;
            if (env_name && env_name->chars && env_name->len > 0) {
                if (strcmp(env_name->chars, "document") == 0) {
                    doc_state.in_document = true;
                    // Process document content - find the matching content
                    // This is a simplified approach
                    return;
                }
                else if (strcmp(env_name->chars, "itemize") == 0) {
                    process_itemize(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "enumerate") == 0) {
                    process_enumerate(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "quote") == 0) {
                    process_quote(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "verbatim") == 0) {
                    process_verbatim(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "center") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-center");
                }
                else if (strcmp(env_name->chars, "flushleft") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-flushleft");
                }
                else if (strcmp(env_name->chars, "flushright") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-flushright");
                }
            }
        }
    }
}

// Process itemize environment
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<ul class=\"latex-itemize\">\n");

    // Process items without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</ul>\n");
}

// Process enumerate environment
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<ol class=\"latex-enumerate\">\n");

    // Process items without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</ol>\n");
}

// Process quote environment
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"latex-quote\">\n");

    process_element_content(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</div>\n");
}

// Process verbatim environment
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<pre class=\"latex-verbatim\">");

    process_element_content(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</pre>\n");
}

// Process alignment environments (center, flushleft, flushright)
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">\n");

    process_element_content(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</div>\n");
}

// Process text formatting commands
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag) {
    printf("DEBUG: process_text_command starting - css_class='%s', tag='%s'\n", css_class, tag);

    stringbuf_append_str(html_buf, "<");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, " class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");
    printf("DEBUG: Opening tag appended successfully\n");

    printf("DEBUG: About to process element content\n");
    process_element_content_simple(html_buf, elem, pool, depth);
    printf("DEBUG: Element content processed successfully\n");

    printf("DEBUG: About to append closing tag\n");
    stringbuf_append_str(html_buf, "</");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, ">");
    printf("DEBUG: Closing tag appended successfully\n");
    printf("DEBUG: process_text_command completed\n");
}

// Process item command
static void process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<li>");

    process_element_content_simple(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</li>\n");
}


// Helper function to append escaped text with dash conversion
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    if (!text) return;

    for (const char* p = text; *p; p++) {
        // Check for em-dash (---)
        if (*p == '-' && *(p+1) == '-' && *(p+2) == '-') {
            stringbuf_append_str(html_buf, "—"); // U+2014 em-dash
            p += 2; // Skip next two dashes
        }
        // Check for en-dash (--)
        else if (*p == '-' && *(p+1) == '-') {
            stringbuf_append_str(html_buf, "–"); // U+2013 en-dash
            p += 1; // Skip next dash
        }
        // HTML entity escaping
        else if (*p == '<') {
            stringbuf_append_str(html_buf, "&lt;");
        }
        else if (*p == '>') {
            stringbuf_append_str(html_buf, "&gt;");
        }
        else if (*p == '&') {
            stringbuf_append_str(html_buf, "&amp;");
        }
        else if (*p == '"') {
            stringbuf_append_str(html_buf, "&quot;");
        }
        else {
            stringbuf_append_char(html_buf, *p);
        }
    }
}

// Helper function to append indentation
static void append_indent(StringBuf* html_buf, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(html_buf, "  ");
    }
}

// ===== MarkReader-based implementations =====

// process element content using reader API (simple version)
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        process_latex_element_reader(html_buf, child, pool, depth);
    }
}

// process element content using reader API (with paragraph wrapping)
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth) {
    // for now, just use simple version - can add paragraph logic later if needed
    process_element_content_simple_reader(html_buf, elem, pool, depth);
}

// main LaTeX element processor using reader API
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth) {
    if (item.isNull()) {
        return;
    }

    if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars) {
            append_escaped_text(html_buf, str->chars);
        }
        return;
    }

    if (!item.isElement()) {
        return;
    }

    ElementReader elem = item.asElement();
    const char* cmd_name = elem.tagName();
    if (!cmd_name) return;

    // handle different LaTeX commands - delegate to existing handlers for now
    // convert reader back to Item/Element for compatibility with existing code
    if (elem.element()) {
        Element* raw_elem = (Element*)elem.element();
        Item raw_item;
        raw_item.element = raw_elem;
        raw_item._type_id = LMD_TYPE_ELEMENT;
        
        process_latex_element(html_buf, raw_item, pool, depth);
    }
}
