#include "format.h"
#include "format-markup.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"

// Create a Lambda Item from raw field data with proper type tagging
Item create_item_from_field_data(void* field_data, TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_BOOL:
            return {.item = b2it(*(bool*)field_data)};
        case LMD_TYPE_INT:
            return {.item = i2it(*(int64_t*)field_data)};  // read full int64 to preserve 56-bit value
        case LMD_TYPE_INT64:
            return {.item = l2it((int64_t*)field_data)};
        case LMD_TYPE_FLOAT:
            return {.item = d2it((double*)field_data)};
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_DTIME:
        case LMD_TYPE_BINARY:
            return {.item = s2it((String*)*(void**)field_data)};
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST:
            return {.item = (uint64_t)*(void**)field_data};
        case LMD_TYPE_MAP:
            return {.item = (uint64_t)*(void**)field_data};
        case LMD_TYPE_ELEMENT: {
            Element* element = (Element*)*(void**)field_data;
            return {.item = element ? ((((uint64_t)LMD_TYPE_ELEMENT)<<56) | (uint64_t)element) : ITEM_ERROR};
        }
        case LMD_TYPE_NULL:
            return {.item = ITEM_NULL};
        default:
            // fallback for unknown types
            return {.item = (uint64_t)field_data};
    }
}

// Common number formatting function
void format_number(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_INT) {
        int64_t val = item.get_int56();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, val);
        stringbuf_append_str(sb, num_buf);
    } else if (type == LMD_TYPE_INT64) {
        int64_t* lptr = (int64_t*)item.int64_ptr;
        if (lptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%" PRId64, *lptr);
            stringbuf_append_str(sb, num_buf);
        } else {
            stringbuf_append_str(sb, "0");
        }
    } else if (type == LMD_TYPE_FLOAT) {
        // Double stored as pointer
        double* dptr = (double*)item.double_ptr;
        if (dptr) {
            char num_buf[32];
            // Check for special values
            if (isnan(*dptr)) {
                stringbuf_append_str(sb, "null");
            } else if (isinf(*dptr)) {
                stringbuf_append_str(sb, "null");
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.15g", *dptr);
                stringbuf_append_str(sb, num_buf);
            }
        } else {
            stringbuf_append_str(sb, "null");
        }
    } else {
        // fallback for unknown numeric types
        stringbuf_append_str(sb, "0");
    }
}

extern "C" String* format_data(Item item, String* type, String* flavor, Pool* pool) {
    if (!type) return NULL;

    // If type is null, try to auto-detect from item type
    if (!type) {
        log_debug("format: Format type is null, using default");
        return NULL;
    }

    // Helper function to build format type with flavor
    char format_type_with_flavor[256];
    if (flavor && flavor->chars && strlen(flavor->chars) > 0) {
        snprintf(format_type_with_flavor, sizeof(format_type_with_flavor), "%s-%s", type->chars, flavor->chars);
    } else {
        strncpy(format_type_with_flavor, type->chars, sizeof(format_type_with_flavor) - 1);
        format_type_with_flavor[sizeof(format_type_with_flavor) - 1] = '\0';
    }

    log_debug("Formatting with type: %s", format_type_with_flavor);

    String* result = NULL;

    if (strcmp(type->chars, "json") == 0) {
        result = format_json(pool, item);
    }
    else if (strcmp(type->chars, "markdown") == 0) {
        result = format_markup_string(pool, item, &MARKDOWN_RULES);
    }
    else if (strcmp(type->chars, "rst") == 0) {
        result = format_markup_string(pool, item, &RST_RULES);
    }
    else if (strcmp(type->chars, "xml") == 0) {
        result = format_xml(pool, item);
    }
    else if (strcmp(type->chars, "html") == 0) {
        result = format_html(pool, item);
    }
    else if (strcmp(type->chars, "yaml") == 0) {
        result = format_yaml(pool, item);
    }
    else if (strcmp(type->chars, "toml") == 0) {
        result = format_toml(pool, item);
    }
    else if (strcmp(type->chars, "ini") == 0) {
        result = format_ini(pool, item);
    }
    else if (strcmp(type->chars, "properties") == 0) {
        result = format_properties(pool, item);
    }
    else if (strcmp(type->chars, "css") == 0) {
        result = format_css(pool, item);
    }
    else if (strcmp(type->chars, "jsx") == 0) {
        result = format_jsx(pool, item);
    }
    else if (strcmp(type->chars, "latex") == 0) {
        result = format_latex(pool, item);
    }
    else if (strcmp(type->chars, "org") == 0) {
        result = format_markup_string(pool, item, &ORG_RULES);
    }
    else if (strcmp(type->chars, "wiki") == 0) {
        result = format_markup_string(pool, item, &WIKI_RULES);
    }
    else if (strcmp(type->chars, "textile") == 0) {
        result = format_markup_string(pool, item, &TEXTILE_RULES);
    }
    else if (strcmp(type->chars, "text") == 0) {
        result = format_text_string(pool, item);
    }
    else if (strcmp(type->chars, "graph") == 0) {
        // Graph type with flavor support for DOT, Mermaid, D2
        if (!flavor || strcmp(flavor->chars, "dot") == 0) {
            result = format_graph_with_flavor(pool, item, "dot");
        }
        else if (strcmp(flavor->chars, "mermaid") == 0) {
            result = format_graph_with_flavor(pool, item, "mermaid");
        }
        else if (strcmp(flavor->chars, "d2") == 0) {
            result = format_graph_with_flavor(pool, item, "d2");
        }
        else {
            log_debug("format: Unsupported graph flavor: %s, defaulting to dot", flavor->chars);
            result = format_graph_with_flavor(pool, item, "dot");
        }
    }
    else if (strcmp(type->chars, "markup") == 0) {
        // Markup type with flavor-based format selection
        // Use unified markup emitter with flavor-based rule lookup
        const char* markup_flavor = (!flavor || strcmp(flavor->chars, "standard") == 0)
            ? "markdown" : flavor->chars;
        const MarkupOutputRules* markup_rules = get_markup_rules(markup_flavor);
        if (!markup_rules) {
            log_debug("format: Unsupported markup flavor: %s, defaulting to markdown", markup_flavor);
            markup_rules = &MARKDOWN_RULES;
        }
        result = format_markup_string(pool, item, markup_rules);
    }
    else if (strcmp(type->chars, "math") == 0) {
        // Math type with flavor support
        if (!flavor || strcmp(flavor->chars, "latex") == 0) {
            result = format_math_latex(pool, item);
        }
        else if (strcmp(flavor->chars, "typst") == 0) {
            result = format_math_typst(pool, item);
        }
        else if (strcmp(flavor->chars, "ascii") == 0) {
            log_debug("format: calling format_math_ascii");
            result = format_math_ascii(pool, item);
        }
        else if (strcmp(flavor->chars, "mathml") == 0) {
            result = format_math_mathml(pool, item);
        }
        else {
            log_debug("format: Unsupported math flavor: %s, defaulting to latex", flavor->chars);
            result = format_math_latex(pool, item);
        }
    }
    // Legacy format type strings (for backwards compatibility)
    else if (strcmp(format_type_with_flavor, "math-latex") == 0) {
        result = format_math_latex(pool, item);
    }
    else if (strcmp(format_type_with_flavor, "math-typst") == 0) {
        result = format_math_typst(pool, item);
    }
    else if (strcmp(format_type_with_flavor, "math-ascii") == 0) {
        log_debug("format: calling format_math_ascii via legacy path");
        result = format_math_ascii(pool, item);
    }
    else if (strcmp(format_type_with_flavor, "math-mathml") == 0) {
        result = format_math_mathml(pool, item);
    }
    else {
        log_error("format: Unsupported format type: %s", format_type_with_flavor);
    }
    return result;
}
