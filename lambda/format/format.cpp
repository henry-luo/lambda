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

    const char* t = type->chars;
    const char* f = (flavor && flavor->chars && strlen(flavor->chars) > 0) ? flavor->chars : NULL;

    log_debug("Formatting with type: %s%s%s", t, f ? "-" : "", f ? f : "");

    // ---- Static dispatch table: type → format function (no flavor) ----
    struct FormatEntry {
        const char* type_name;
        String* (*format_fn)(Pool*, Item);
    };
    static const FormatEntry SIMPLE_FORMATS[] = {
        { "json",       format_json },
        { "xml",        format_xml },
        { "html",       format_html },
        { "yaml",       format_yaml },
        { "toml",       format_toml },
        { "ini",        format_ini },
        { "properties", format_properties },
        { "css",        format_css },
        { "jsx",        format_jsx },
        { "mdx",        format_mdx },
        { "latex",      format_latex },
        { "text",       format_text_string },
        { NULL,         NULL }
    };

    // ---- Markup dispatch table: type → MarkupOutputRules ----
    struct MarkupEntry {
        const char* type_name;
        const MarkupOutputRules* rules;
    };
    static const MarkupEntry MARKUP_FORMATS[] = {
        { "markdown",  &MARKDOWN_RULES },
        { "md",        &MARKDOWN_RULES },
        { "rst",       &RST_RULES },
        { "org",       &ORG_RULES },
        { "wiki",      &WIKI_RULES },
        { "textile",   &TEXTILE_RULES },
        { NULL,        NULL }
    };

    // 1. Check simple format table
    for (const FormatEntry* e = SIMPLE_FORMATS; e->type_name; e++) {
        if (strcmp(t, e->type_name) == 0) return e->format_fn(pool, item);
    }

    // 2. Check markup format table
    for (const MarkupEntry* e = MARKUP_FORMATS; e->type_name; e++) {
        if (strcmp(t, e->type_name) == 0) return format_markup_string(pool, item, e->rules);
    }

    // 3. Flavor-based dispatch for compound types
    if (strcmp(t, "graph") == 0) {
        const char* graph_flavor = f ? f : "dot";
        return format_graph_with_flavor(pool, item, graph_flavor);
    }

    if (strcmp(t, "markup") == 0) {
        const char* markup_flavor = (!f || strcmp(f, "standard") == 0) ? "markdown" : f;
        const MarkupOutputRules* rules = get_markup_rules(markup_flavor);
        if (!rules) {
            log_debug("format: unsupported markup flavor: %s, defaulting to markdown", markup_flavor);
            rules = &MARKDOWN_RULES;
        }
        return format_markup_string(pool, item, rules);
    }

    if (strcmp(t, "math") == 0) {
        // Math flavor dispatch table
        struct MathEntry { const char* flavor; String* (*fn)(Pool*, Item); };
        static const MathEntry MATH_FLAVORS[] = {
            { "latex",  format_math_latex },
            { "typst",  format_math_typst },
            { "ascii",  format_math_ascii },
            { "mathml", format_math_mathml },
            { NULL,     NULL }
        };
        const char* mf = f ? f : "latex";
        for (const MathEntry* e = MATH_FLAVORS; e->flavor; e++) {
            if (strcmp(mf, e->flavor) == 0) return e->fn(pool, item);
        }
        log_debug("format: unsupported math flavor: %s, defaulting to latex", mf);
        return format_math_latex(pool, item);
    }

    // 4. Legacy "type-flavor" combined strings (backward compatibility)
    if (f) {
        char combined[256];
        snprintf(combined, sizeof(combined), "%s-%s", t, f);

        struct MathEntry { const char* name; String* (*fn)(Pool*, Item); };
        static const MathEntry LEGACY_MATH[] = {
            { "math-latex",  format_math_latex },
            { "math-typst",  format_math_typst },
            { "math-ascii",  format_math_ascii },
            { "math-mathml", format_math_mathml },
            { NULL,          NULL }
        };
        for (const MathEntry* e = LEGACY_MATH; e->name; e++) {
            if (strcmp(combined, e->name) == 0) return e->fn(pool, item);
        }
    }

    log_error("format: unsupported format type: %s%s%s", t, f ? "-" : "", f ? f : "");
    return NULL;
}
