// LaTeX Formatter - Simple implementation for LaTeX output
#include "format.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <string.h>

// Forward declarations - old Element* API
static void format_latex_document(StringBuf* sb, Element* document);
static void format_latex_element(StringBuf* sb, Element* element, int depth);
static void format_latex_command_with_args(StringBuf* sb, Element* element, const char* cmd_name);
static void format_latex_environment_with_content(StringBuf* sb, Element* element, const char* env_name, int depth);
static void format_latex_element_content(StringBuf* sb, Element* element);
static void format_latex_value(StringBuf* sb, Item value);

// MarkReader-based forward declarations using LaTeXContext
static void format_latex_value_reader(LaTeXContext& ctx, const ItemReader& value);
static void format_latex_element_reader(LaTeXContext& ctx, const ElementReader& element, int depth);

// Helper function to add indentation
static void add_latex_indent(StringBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

// Format LaTeX value item
static void format_latex_value(StringBuf* sb, Item value) {
    TypeId type = get_type_id(value);

    if (type == LMD_TYPE_ELEMENT) {
        format_latex_element(sb, value.element, 0);
    }
    else if (type == LMD_TYPE_STRING) {
        // Direct text content with safety checks
        String* str = (String*)value.string_ptr;
        if (str && str->chars && str->len > 0 && str->len < 65536) { // Reasonable size limit
            stringbuf_append_str_n(sb, str->chars, str->len);
        }
    }
    else if (type == LMD_TYPE_ARRAY) {
        // Array of elements/values with safety checks
        Array* arr = value.array;
        if (arr && arr->items && arr->length > 0 && arr->length < 10000) { // Reasonable size limit
            for (int i = 0; i < arr->length; i++) {
                format_latex_value(sb, arr->items[i]);
                if (i < arr->length - 1) {
                    stringbuf_append_char(sb, ' ');
                }
            }
        }
    }
    else if (type == LMD_TYPE_INT) {
        // Integer values
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, value.get_int56());
        stringbuf_append_str(sb, num_buf);
    }
    else if (type == LMD_TYPE_FLOAT) {
        // Float values with safety check
        double* dptr = (double*)value.double_ptr;
        if (dptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%.6g", *dptr);
            stringbuf_append_str(sb, num_buf);
        }
    } else if (type == LMD_TYPE_SYMBOL) {
        // Symbol values (like strings but handled as symbols) with safety check
        String* str = (String*)value.string_ptr;
        if (str && str->chars && str->len > 0 && str->len < 65536) {
            stringbuf_append_str_n(sb, str->chars, str->len);
        }
    } else {
        // Fallback for unknown types
        stringbuf_append_str(sb, "[unknown]");
    }
}

// Format LaTeX command with arguments
static void format_latex_command_with_args(StringBuf* sb, Element* element, const char* cmd_name) {
    stringbuf_append_char(sb, '\\');
    stringbuf_append_str(sb, cmd_name);

    // Format command arguments from element content
    format_latex_element_content(sb, element);
}

// Format LaTeX environment with content
static void format_latex_environment_with_content(StringBuf* sb, Element* element, const char* env_name, int depth) {
    add_latex_indent(sb, depth);
    stringbuf_append_str(sb, "\\begin{");
    stringbuf_append_str(sb, env_name);
    stringbuf_append_str(sb, "}");

    // Add arguments if any
    format_latex_element_content(sb, element);

    stringbuf_append_char(sb, '\n');

    add_latex_indent(sb, depth);
    stringbuf_append_str(sb, "\\end{");
    stringbuf_append_str(sb, env_name);
    stringbuf_append_char(sb, '}');
}

// Format element content (arguments and body)
static void format_latex_element_content(StringBuf* sb, Element* element) {
    if (!element || !element->items) return;

    // Format element items as arguments/content with safety checks
    if (element->length > 0 && element->length < 1000) { // Reasonable limit
        for (int i = 0; i < element->length; i++) {
            Item content_item = element->items[i];
            TypeId content_type = get_type_id(content_item);

            if (content_type == LMD_TYPE_STRING) {
                // Text argument - wrap in braces
                stringbuf_append_char(sb, '{');
                format_latex_value(sb, content_item);
                stringbuf_append_char(sb, '}');
            } else {
                // Other content - format directly
                format_latex_value(sb, content_item);
            }
        }
    }
}

// Format LaTeX element
static void format_latex_element(StringBuf* sb, Element* element, int depth) {
    if (!element || !element->type) {
        return;
    }

    TypeElmt* elmt_type = (TypeElmt*)element->type;
    if (!elmt_type) return;

    StrView name = elmt_type->name;
    if (!name.str || name.length == 0 || name.length > 100) return; // Safety check

    // Convert name to null-terminated string for easier comparison
    char cmd_name[64];
    int name_len = name.length < 63 ? name.length : 63;
    strncpy(cmd_name, name.str, name_len);
    cmd_name[name_len] = '\0';

    // Handle specific LaTeX commands and environments
    if (strcmp(cmd_name, "documentclass") == 0) {
        format_latex_command_with_args(sb, element, "documentclass");
    } else if (strcmp(cmd_name, "usepackage") == 0) {
        format_latex_command_with_args(sb, element, "usepackage");
    } else if (strcmp(cmd_name, "title") == 0) {
        format_latex_command_with_args(sb, element, "title");
    } else if (strcmp(cmd_name, "author") == 0) {
        format_latex_command_with_args(sb, element, "author");
    } else if (strcmp(cmd_name, "date") == 0) {
        format_latex_command_with_args(sb, element, "date");
    } else if (strcmp(cmd_name, "section") == 0) {
        format_latex_command_with_args(sb, element, "section");
    } else if (strcmp(cmd_name, "subsection") == 0) {
        format_latex_command_with_args(sb, element, "subsection");
    } else if (strcmp(cmd_name, "subsubsection") == 0) {
        format_latex_command_with_args(sb, element, "subsubsection");
    } else if (strcmp(cmd_name, "textbf") == 0) {
        format_latex_command_with_args(sb, element, "textbf");
    } else if (strcmp(cmd_name, "textit") == 0) {
        format_latex_command_with_args(sb, element, "textit");
    } else if (strcmp(cmd_name, "texttt") == 0) {
        format_latex_command_with_args(sb, element, "texttt");
    } else if (strcmp(cmd_name, "emph") == 0) {
        format_latex_command_with_args(sb, element, "emph");
    } else if (strcmp(cmd_name, "underline") == 0) {
        format_latex_command_with_args(sb, element, "underline");

    // Environments
    } else if (strcmp(cmd_name, "document") == 0) {
        format_latex_environment_with_content(sb, element, "document", depth);
    } else if (strcmp(cmd_name, "abstract") == 0) {
        format_latex_environment_with_content(sb, element, "abstract", depth);
    } else if (strcmp(cmd_name, "itemize") == 0) {
        format_latex_environment_with_content(sb, element, "itemize", depth);
    } else if (strcmp(cmd_name, "enumerate") == 0) {
        format_latex_environment_with_content(sb, element, "enumerate", depth);
    } else if (strcmp(cmd_name, "description") == 0) {
        format_latex_environment_with_content(sb, element, "description", depth);
    } else if (strcmp(cmd_name, "quote") == 0) {
        format_latex_environment_with_content(sb, element, "quote", depth);
    } else if (strcmp(cmd_name, "center") == 0) {
        format_latex_environment_with_content(sb, element, "center", depth);
    } else if (strcmp(cmd_name, "verbatim") == 0) {
        format_latex_environment_with_content(sb, element, "verbatim", depth);

    // Simple commands without arguments
    } else if (strcmp(cmd_name, "maketitle") == 0) {
        stringbuf_append_str(sb, "\\maketitle");
    } else if (strcmp(cmd_name, "tableofcontents") == 0) {
        stringbuf_append_str(sb, "\\tableofcontents");
    } else if (strcmp(cmd_name, "item") == 0) {
        add_latex_indent(sb, depth + 1);
        stringbuf_append_str(sb, "\\item ");
        format_latex_element_content(sb, element);

    // Math and special commands
    } else if (strncmp(cmd_name, "math", 4) == 0) {
        // Math content (inline or display)
        stringbuf_append_char(sb, '$');
        format_latex_element_content(sb, element);
        stringbuf_append_char(sb, '$');
    } else if (strncmp(cmd_name, "comment", 7) == 0) {
        // LaTeX comment
        stringbuf_append_str(sb, "% ");
        format_latex_element_content(sb, element);
    } else {
        // Generic element - treat as command
        stringbuf_append_char(sb, '\\');
        stringbuf_append_str_n(sb, name.str, name.length);
        format_latex_element_content(sb, element);
    }
}

// Format LaTeX document (top-level)
static void format_latex_document(StringBuf* sb, Element* document) {
    if (!document) return;

    // Format document elements with proper spacing
    if (document->length > 0) {
        for (int i = 0; i < document->length; i++) {
            Item element_item = document->items[i];
            TypeId item_type = get_type_id(element_item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = element_item.element;
                if (elem && elem->type) {
                    TypeElmt* elmt_type = (TypeElmt*)elem->type;

                    // Add spacing based on element type
                    if (i > 0) {
                        // Add appropriate spacing between elements
                        if (elmt_type->name.length >= 7 &&
                            strncmp(elmt_type->name.str, "section", 7) == 0) {
                            stringbuf_append_str(sb, "\n\n");
                        } else if (elmt_type->name.length >= 8 &&
                                   strncmp(elmt_type->name.str, "document", 8) == 0) {
                            stringbuf_append_str(sb, "\n\n");
                        } else {
                            stringbuf_append_char(sb, '\n');
                        }
                    }
                }
            }

            format_latex_value(sb, element_item);
        }
        stringbuf_append_char(sb, '\n');
    } else {
        // Fallback - just add basic LaTeX structure
        stringbuf_append_str(sb, "\\documentclass{article}\n");
        stringbuf_append_str(sb, "\\begin{document}\n");
        stringbuf_append_str(sb, "\\end{document}\n");
    }
}

// MarkReader-based version: format LaTeX value item
static void format_latex_value_reader(LaTeXContext& ctx, const ItemReader& value) {
    if (value.isNull()) {
        return;
    } else if (value.isElement()) {
        ElementReader elem = value.asElement();
        format_latex_element_reader(ctx, elem, 0);
    } else if (value.isString()) {
        String* str = value.asString();
        if (str && str->chars && str->len > 0 && str->len < 65536) {
            stringbuf_append_str_n(ctx.output(), str->chars, str->len);
        }
    } else if (value.isArray()) {
        ArrayReader arr = value.asArray();
        auto items = arr.items();
        ItemReader item;
        bool first = true;
        while (items.next(&item)) {
            if (!first) stringbuf_append_char(ctx.output(), ' ');
            first = false;
            format_latex_value_reader(ctx, item);
        }
    } else if (value.isInt() || value.isFloat()) {
        format_number(ctx.output(), value.item());
    } else {
        stringbuf_append_str(ctx.output(), "[unknown]");
    }
}

// MarkReader-based version: format LaTeX element
static void format_latex_element_reader(LaTeXContext& ctx, const ElementReader& element, int depth) {
    const char* cmd_name = element.tagName();
    if (!cmd_name) return;

    // get element for content access
    Element* elem = (Element*)element.element();

    // handle specific LaTeX commands and environments
    if (strcmp(cmd_name, "documentclass") == 0 || strcmp(cmd_name, "usepackage") == 0 ||
        strcmp(cmd_name, "title") == 0 || strcmp(cmd_name, "author") == 0 || strcmp(cmd_name, "date") == 0 ||
        strcmp(cmd_name, "section") == 0 || strcmp(cmd_name, "subsection") == 0 || strcmp(cmd_name, "subsubsection") == 0 ||
        strcmp(cmd_name, "textbf") == 0 || strcmp(cmd_name, "textit") == 0 || strcmp(cmd_name, "texttt") == 0 ||
        strcmp(cmd_name, "emph") == 0 || strcmp(cmd_name, "underline") == 0) {
        format_latex_command_with_args(ctx.output(), elem, cmd_name);
    } else if (strcmp(cmd_name, "document") == 0 || strcmp(cmd_name, "abstract") == 0 ||
               strcmp(cmd_name, "itemize") == 0 || strcmp(cmd_name, "enumerate") == 0 ||
               strcmp(cmd_name, "description") == 0 || strcmp(cmd_name, "quote") == 0 ||
               strcmp(cmd_name, "center") == 0 || strcmp(cmd_name, "verbatim") == 0) {
        format_latex_environment_with_content(ctx.output(), elem, cmd_name, depth);
    } else if (strcmp(cmd_name, "maketitle") == 0) {
        stringbuf_append_str(ctx.output(), "\\maketitle");
    } else if (strcmp(cmd_name, "tableofcontents") == 0) {
        stringbuf_append_str(ctx.output(), "\\tableofcontents");
    } else if (strcmp(cmd_name, "item") == 0) {
        add_latex_indent(ctx.output(), depth + 1);
        stringbuf_append_str(ctx.output(), "\\item ");
        format_latex_element_content(ctx.output(), elem);
    } else if (strncmp(cmd_name, "math", 4) == 0) {
        stringbuf_append_char(ctx.output(), '$');
        format_latex_element_content(ctx.output(), elem);
        stringbuf_append_char(ctx.output(), '$');
    } else if (strncmp(cmd_name, "comment", 7) == 0) {
        stringbuf_append_str(ctx.output(), "% ");
        format_latex_element_content(ctx.output(), elem);
    } else {
        stringbuf_append_char(ctx.output(), '\\');
        stringbuf_append_str(ctx.output(), cmd_name);
        format_latex_element_content(ctx.output(), elem);
    }
}

// Main LaTeX formatting function
String* format_latex(Pool *pool, Item item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Create LaTeX context
    Pool* ctx_pool = pool_create();
    LaTeXContext ctx(ctx_pool, sb);

    // use MarkReader API
    ItemReader root(item.to_const());

    if (root.isArray()) {
        ArrayReader arr = root.asArray();
        auto items = arr.items();
        ItemReader elem;
        bool first = true;
        while (items.next(&elem)) {
            if (!first) stringbuf_append_char(ctx.output(), '\n');
            first = false;
            format_latex_value_reader(ctx, elem);
        }
    } else if (root.isElement()) {
        ElementReader element = root.asElement();
        const char* tag = element.tagName();

        if (tag && (strcmp(tag, "document") == 0 || strcmp(tag, "article") == 0 ||
                    strcmp(tag, "book") == 0 || strcmp(tag, "latex_document") == 0)) {
            format_latex_document(sb, (Element*)element.element());
        } else {
            format_latex_element_reader(ctx, element, 0);
        }
    } else {
        format_latex_value_reader(ctx, root);
    }

    pool_destroy(ctx_pool);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}
