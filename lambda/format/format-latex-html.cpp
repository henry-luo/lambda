#include "format-latex-html.h"
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../../lib/mem-pool/include/mem_pool.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
static void generate_latex_css(StringBuf* css_buf);
static void process_latex_element(StringBuf* html_buf, Item item, VariableMemPool* pool, int depth);
static void process_element_content(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_title(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_author(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_date(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_maketitle(StringBuf* html_buf, VariableMemPool* pool, int depth);
static void process_section(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth, const char* css_class);
static void process_environment(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_itemize(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_enumerate(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_quote(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_verbatim(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void process_text_command(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth, const char* css_class, const char* tag);
static void process_item(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth);
static void append_escaped_text(StringBuf* html_buf, const char* text);
static void append_indent(StringBuf* html_buf, int depth);

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
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, VariableMemPool* pool) {
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
        stringbuf_append_str(html_buf, "<p>No content</p>\n");
    } else {
        // Add paragraph wrapper for inline content
        stringbuf_append_str(html_buf, "<p>");
        
        // Process the LaTeX AST
        printf("DEBUG: About to process LaTeX AST\n");
        process_latex_element(html_buf, latex_ast, pool, 1);
        printf("DEBUG: Finished processing LaTeX AST\n");
        
        // Close paragraph wrapper
        stringbuf_append_str(html_buf, "</p>");
    }
    
    // Close document container
    stringbuf_append_str(html_buf, "</div>\n");
    printf("DEBUG: Closed HTML container\n");
    
    // Generate CSS
    printf("DEBUG: About to generate CSS to css_buf=%p\n", css_buf);
    generate_latex_css(css_buf);
    printf("DEBUG: CSS generation complete\n");
    
    printf("DEBUG: HTML and CSS generation completed\n");
}

// Generate comprehensive CSS for LaTeX documents
static void generate_latex_css(StringBuf* css_buf) {
    if (!css_buf) {
        return;
    }

    // Generate basic CSS for LaTeX documents
    stringbuf_append_str(css_buf,
        ".latex-document {\n"
        "  font-family: 'Computer Modern', 'Latin Modern', serif;\n"
        "  max-width: 800px;\n"
        "  margin: 0 auto;\n"
        "  padding: 2rem;\n"
        "  line-height: 1.6;\n"
        "  color: #333;\n"
        "}\n"

        ".latex-title {\n"
        "  text-align: center;\n"
        "  font-size: 2.5em;\n"
        "  font-weight: bold;\n"
        "  margin: 2rem 0;\n"
        "}\n"

        ".latex-author {\n"
        "  text-align: center;\n"
        "  font-size: 1.2em;\n"
        "  margin: 1rem 0;\n"
        "}\n"

        ".latex-date {\n"
        "  text-align: center;\n"
        "  font-style: italic;\n"
        "  margin: 1rem 0 2rem 0;\n"
        "}\n"

        ".latex-section {\n"
        "  font-size: 1.8em;\n"
        "  font-weight: bold;\n"
        "  margin: 2rem 0 1rem 0;\n"
        "  border-bottom: 1px solid #ccc;\n"
        "  padding-bottom: 0.5rem;\n"
        "}\n"

        ".latex-subsection {\n"
        "  font-size: 1.4em;\n"
        "  font-weight: bold;\n"
        "  margin: 1.5rem 0 1rem 0;\n"
        "}\n"

        ".latex-subsubsection {\n"
        "  font-size: 1.2em;\n"
        "  font-weight: bold;\n"
        "  margin: 1rem 0 0.5rem 0;\n"
        "}\n"

        ".latex-paragraph {\n"
        "  margin: 1rem 0;\n"
        "  text-align: justify;\n"
        "}\n"

        ".latex-itemize, .latex-enumerate {\n"
        "  margin: 1rem 0;\n"
        "  padding-left: 2rem;\n"
        "}\n"

        ".latex-item {\n"
        "  margin: 0.5rem 0;\n"
        "}\n"

        ".latex-quote {\n"
        "  margin: 1rem 2rem;\n"
        "  padding: 1rem;\n"
        "  border-left: 4px solid #ccc;\n"
        "  background-color: #f9f9f9;\n"
        "  font-style: italic;\n"
        "}\n"

        ".latex-verbatim {\n"
        "  font-family: 'Courier New', monospace;\n"
        "  background-color: #f5f5f5;\n"
        "  border: 1px solid #ddd;\n"
        "  padding: 1rem;\n"
        "  margin: 1rem 0;\n"
        "  white-space: pre;\n"
        "  overflow-x: auto;\n"
        "}\n"

        ".latex-textbf {\n"
        "  font-weight: bold;\n"
        "}\n"

        ".latex-textit {\n"
        "  font-style: italic;\n"
        "}\n"

        ".latex-emph {\n"
        "  font-style: italic;\n"
        "}\n"
    );
}

// Process a LaTeX element and convert to HTML
static void process_latex_element(StringBuf* html_buf, Item item, VariableMemPool* pool, int depth) {
    if (item.item == ITEM_NULL) {
        return;
    }
    
    TypeId type = get_type_id(item);

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


        // Handle different LaTeX commands
        if (strcmp(cmd_name, "documentclass") == 0) {
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
        else if (strcmp(cmd_name, "textbf") == 0) {
            printf("DEBUG: Processing textbf command\n");
            process_text_command(html_buf, elem, pool, depth, "latex-textbf", "span");
            printf("DEBUG: Finished processing textbf command\n");
        }
        else if (strcmp(cmd_name, "textit") == 0) {
            printf("DEBUG: Processing textit command\n");
            process_text_command(html_buf, elem, pool, depth, "latex-textit", "span");
            printf("DEBUG: Finished processing textit command\n");
        }
        else if (strcmp(cmd_name, "emph") == 0) {
            process_text_command(html_buf, elem, pool, depth, "latex-emph", "span");
        }
        else if (strcmp(cmd_name, "item") == 0) {
            process_item(html_buf, elem, pool, depth);
        }
        else {
            // Generic element - process children
            process_element_content(html_buf, elem, pool, depth);
        }
    }
    else if (type == LMD_TYPE_STRING) {
        // Handle text content
        String* str = (String*)item.pointer;
        printf("DEBUG: LMD_TYPE_STRING - str=%p\n", str);
        if (str) {
            printf("DEBUG: String length=%u, chars=%p\n", str->len, str->chars);
            if (str->len > 0 && str->len < 1000) { // Safety check
                printf("DEBUG: Processing string of length %u: '%.20s'\n", str->len, str->chars);
                // Use a simpler approach - create a temporary buffer on stack for small strings
                if (str->len < 256) {
                    char temp_buf[256];
                    memcpy(temp_buf, str->chars, str->len);
                    temp_buf[str->len] = '\0';
                    printf("DEBUG: About to append text: '%s'\n", temp_buf);
                    // Use direct stringbuf_append_str instead of append_escaped_text to avoid corruption
                    stringbuf_append_str(html_buf, temp_buf);
                    printf("DEBUG: Text appended successfully\n");
                } else {
                    printf("DEBUG: String too long (%u chars), skipping\n", str->len);
                }
            } else {
                printf("DEBUG: String length %u is invalid (empty or too long), skipping\n", str->len);
            }
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

// Process element content (based on format-latex.cpp pattern)
static void process_element_content(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    if (!elem || !elem->items) {
        printf("DEBUG: process_element_content - elem or items is null\n");
        return;
    }

    printf("DEBUG: process_element_content - elem->length = %d\n", elem->length);
    
    // Process element items with safety checks
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        for (int i = 0; i < elem->length; i++) {
            printf("DEBUG: Processing element content item %d\n", i);
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);
            printf("DEBUG: Content item %d has type %d\n", i, item_type);
            process_latex_element(html_buf, content_item, pool, depth);
            printf("DEBUG: Finished processing element content item %d\n", i);
        }
    }
    printf("DEBUG: process_element_content completed\n");
}

// Process title command
static void process_title(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    if (!elem) return;

    // Store title for later use in maketitle
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* title_str = (String*)elem->items[0].pointer;
            if (title_str && title_str->chars && title_str->len > 0) {
                doc_state.title = strdup(title_str->chars);
            }
        }
    }
}

// Process author command
static void process_author(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    if (!elem) return;

    // Store author for later use in maketitle
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* author_str = (String*)elem->items[0].pointer;
            if (author_str && author_str->chars && author_str->len > 0) {
                doc_state.author = strdup(author_str->chars);
            }
        }
    }
}

// Process date command
static void process_date(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    if (!elem) return;

    // Store date for later use in maketitle
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* date_str = (String*)elem->items[0].pointer;
            if (date_str && date_str->chars && date_str->len > 0) {
                doc_state.date = strdup(date_str->chars);
            }
        }
    }
}

// Process maketitle command
static void process_maketitle(StringBuf* html_buf, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);

    if (doc_state.title) {
        stringbuf_append_str(html_buf, "<div class=\"latex-title\">");
        append_escaped_text(html_buf, doc_state.title);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.author) {
        append_indent(html_buf, depth);
        stringbuf_append_str(html_buf, "<div class=\"latex-author\">");
        append_escaped_text(html_buf, doc_state.author);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.date) {
        append_indent(html_buf, depth);
        stringbuf_append_str(html_buf, "<div class=\"latex-date\">");
        append_escaped_text(html_buf, doc_state.date);
        stringbuf_append_str(html_buf, "</div>\n");
    }
}

// Process section commands
static void process_section(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth, const char* css_class) {
    if (!elem) return;

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Process section title
    process_element_content(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process environments (begin/end blocks)
static void process_environment(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
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
            }
        }
    }
}

// Process itemize environment
static void process_itemize(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<ul class=\"latex-itemize\">\n");

    // Process items
    process_element_content(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</ul>\n");
}

// Process enumerate environment
static void process_enumerate(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<ol class=\"latex-enumerate\">\n");

    // Process items
    process_element_content(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</ol>\n");
}

// Process quote environment
static void process_quote(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"latex-quote\">\n");

    process_element_content(html_buf, elem, pool, depth + 1);

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "</div>\n");
}

// Process verbatim environment
static void process_verbatim(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<pre class=\"latex-verbatim\">");

    process_element_content(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</pre>\n");
}

// Process text formatting commands
static void process_text_command(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth, const char* css_class, const char* tag) {
    printf("DEBUG: process_text_command starting - css_class='%s', tag='%s'\n", css_class, tag);
    
    stringbuf_append_str(html_buf, "<");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, " class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");
    printf("DEBUG: Opening tag appended successfully\n");

    printf("DEBUG: About to process element content\n");
    process_element_content(html_buf, elem, pool, depth);
    printf("DEBUG: Element content processed successfully\n");

    printf("DEBUG: About to append closing tag\n");
    stringbuf_append_str(html_buf, "</");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, ">");
    printf("DEBUG: Closing tag appended successfully\n");
    printf("DEBUG: process_text_command completed\n");
}

// Process item command
static void process_item(StringBuf* html_buf, Element* elem, VariableMemPool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<li class=\"latex-item\">");

    process_element_content(html_buf, elem, pool, depth);

    stringbuf_append_str(html_buf, "</li>\n");
}


// Helper function to append escaped text
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    if (!text) return;

    for (const char* p = text; *p; p++) {
        switch (*p) {
            case '<':
                stringbuf_append_str(html_buf, "&lt;");
                break;
            case '>':
                stringbuf_append_str(html_buf, "&gt;");
                break;
            case '&':
                stringbuf_append_str(html_buf, "&amp;");
                break;
            case '"':
                stringbuf_append_str(html_buf, "&quot;");
                break;
            default:
                stringbuf_append_char(html_buf, *p);
                break;
        }
    }
}

// Helper function to append indentation
static void append_indent(StringBuf* html_buf, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(html_buf, "  ");
    }
}
