// LaTeX Formatter - Simple implementation for LaTeX output
#include "format.h"
#include <string.h>

// Forward declarations
static void format_latex_document(StrBuf* sb, Element* document);
static void format_latex_element(StrBuf* sb, Element* element, int depth);
static void format_latex_value(StrBuf* sb, Item value);

// Helper function to add indentation
static void add_latex_indent(StrBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        strbuf_append_str(sb, "  ");
    }
}

// Format LaTeX value item
static void format_latex_value(StrBuf* sb, Item value) {
    TypeId type = get_type_id(value);
    
    if (type == LMD_TYPE_ELEMENT) {
        format_latex_element(sb, (Element*)value.pointer, 0);
    } else if (type == LMD_TYPE_STRING) {
        // Direct text content
        String* str = (String*)value.pointer;
        if (str && str->len > 0) {
            strbuf_append_str_n(sb, str->chars, str->len);
        }
    } else if (type == LMD_TYPE_ARRAY) {
        // Array of elements/values
        Array* arr = (Array*)value.pointer;
        if (arr && arr->length > 0) {
            for (int i = 0; i < arr->length; i++) {
                format_latex_value(sb, arr->items[i]);
                if (i < arr->length - 1) {
                    strbuf_append_char(sb, ' ');
                }
            }
        }
    } else if (type == LMD_TYPE_INT) {
        // Integer values
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%d", value.int_val);
        strbuf_append_str(sb, num_buf);
    } else if (type == LMD_TYPE_FLOAT) {
        // Float values
        double* dptr = (double*)value.pointer;
        if (dptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%.6g", *dptr);
            strbuf_append_str(sb, num_buf);
        }
    } else {
        // Fallback for unknown types
        strbuf_append_str(sb, "[unknown]");
    }
}

// Format LaTeX element
static void format_latex_element(StrBuf* sb, Element* element, int depth) {
    if (!element || !element->type) {
        return;
    }
    
    TypeElmt* elmt_type = (TypeElmt*)element->type;
    if (!elmt_type) return;
    
    StrView name = elmt_type->name;
    if (!name.str || name.length == 0) return;
    
    // Handle different LaTeX element types
    if (strncmp(name.str, "command", 7) == 0 || 
        strncmp(name.str, "cmd", 3) == 0) {
        // LaTeX command (e.g., \section{}, \textbf{}, etc.)
        strbuf_append_char(sb, '\\');
        strbuf_append_str(sb, "command");
        
    } else if (strncmp(name.str, "environment", 11) == 0 || 
               strncmp(name.str, "env", 3) == 0) {
        // LaTeX environment (e.g., \begin{document}...\end{document})
        strbuf_append_str(sb, "\\begin{environment}");
        strbuf_append_str(sb, "\\end{environment}");
        
    } else if (strncmp(name.str, "text", 4) == 0) {
        // Text content - just output the text
        strbuf_append_str(sb, "text");
    } else if (strncmp(name.str, "math", 4) == 0) {
        // Math content (inline or display)
        strbuf_append_char(sb, '$');
        strbuf_append_str(sb, "math");
        strbuf_append_char(sb, '$');
    } else if (strncmp(name.str, "comment", 7) == 0) {
        // LaTeX comment
        strbuf_append_str(sb, "% comment");
        strbuf_append_char(sb, '\n');
    } else {
        // Generic element - treat as command
        strbuf_append_char(sb, '\\');
        strbuf_append_str_n(sb, name.str, name.length);
    }
}

// Format LaTeX document (top-level)
static void format_latex_document(StrBuf* sb, Element* document) {
    if (!document) return;
    
    // For simple documents, just format as array of elements
    if (document->length > 0) {
        for (int i = 0; i < document->length; i++) {
            format_latex_value(sb, document->items[i]);
            strbuf_append_char(sb, '\n');
        }
    } else {
        // Fallback - just add basic LaTeX structure
        strbuf_append_str(sb, "\\documentclass{article}\n");
        strbuf_append_str(sb, "\\begin{document}\n");
        strbuf_append_str(sb, "\\end{document}\n");
    }
}

// Main LaTeX formatting function
String* format_latex(VariableMemPool *pool, Item item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY) {
        // Handle array of LaTeX elements (root document)
        Array* elements = (Array*)item.pointer;
        if (elements && elements->length > 0) {
            for (int i = 0; i < elements->length; i++) {
                Item element_item = elements->items[i];
                format_latex_value(sb, element_item);
                if (i < elements->length - 1) {
                    strbuf_append_char(sb, '\n');
                }
            }
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        Element* element = (Element*)item.pointer;
        if (element && element->type) {
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            
            // Check if this is a document or article-level element
            if ((elmt_type->name.length >= 8 && strncmp(elmt_type->name.str, "document", 8) == 0) ||
                (elmt_type->name.length >= 7 && strncmp(elmt_type->name.str, "article", 7) == 0) ||
                (elmt_type->name.length >= 4 && strncmp(elmt_type->name.str, "book", 4) == 0) ||
                (elmt_type->name.length >= 14 && strncmp(elmt_type->name.str, "latex_document", 14) == 0)) {
                format_latex_document(sb, element);
            } else {
                format_latex_element(sb, element, 0);
            }
        } else {
            format_latex_element(sb, (Element*)item.pointer, 0);
        }
    } else {
        // Fallback - format as value
        format_latex_value(sb, item);
    }
    
    String* result = strbuf_to_string(sb);
    strbuf_free(sb);
    
    return result;
}
