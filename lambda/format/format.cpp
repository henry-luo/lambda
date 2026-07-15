#include "format.h"
#include "../ast.hpp"
#include "../../lib/base64.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"
#include "../../lib/str.h"
#include "format-markup.h"
#include "../lambda-decimal.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"

static void format_number_impl(StringBuf* sb, Item item, bool compact_float) {
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
    } else if (is_float_type_id(type)) {
        double d = item.get_double();
        // ItemReader can pass inline floats; get_double() handles both inline and heap encodings.
        if (!isnan(d) && !isinf(d)) {
            char num_buf[32];
            // XML/SVG golden output historically uses concise numeric attributes;
            // keep round-trip dtoa for data formats and %.6g for markup geometry.
            if (compact_float) snprintf(num_buf, sizeof(num_buf), "%.6g", d);
            else lambda_double_to_shortest(d, num_buf, sizeof(num_buf));
            stringbuf_append_str(sb, num_buf);
        } else {
            stringbuf_append_str(sb, "null");
        }
    } else if (type == LMD_TYPE_DECIMAL) {
        // Parsers use Decimal as the exact carrier for oversized integer tokens.
        char* decimal = decimal_to_string(item);
        if (decimal) {
            stringbuf_append_str(sb, decimal);
            decimal_free_string(decimal);
        } else {
            stringbuf_append_str(sb, "null");
        }
    } else if (type == LMD_TYPE_NUM_SIZED) {
        // inline sized numerics (i8..u32 packed as int; f16/f32 as float)
        NumSizedType st = item.get_num_type();
        char num_buf[32];
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            double d = item.get_num_sized_as_double();
            if (isnan(d) || isinf(d)) {
                stringbuf_append_str(sb, "null");
            } else {
                if (compact_float) snprintf(num_buf, sizeof(num_buf), "%.6g", d);
                else lambda_double_to_shortest(d, num_buf, sizeof(num_buf));
                stringbuf_append_str(sb, num_buf);
            }
        } else {
            snprintf(num_buf, sizeof(num_buf), "%" PRId64, item.get_num_sized_as_int64());
            stringbuf_append_str(sb, num_buf);
        }
    } else {
        // fallback for unknown numeric types
        stringbuf_append_str(sb, "0");
    }
}

// Common number formatting function
void format_number(StringBuf* sb, Item item) {
    format_number_impl(sb, item, false);
}

void format_number_compact(StringBuf* sb, Item item) {
    format_number_impl(sb, item, true);
}

void format_binary_base64_string(StringBuf* sb, Binary* bin) {
    if (!sb || !bin) return;
    uint32_t length = binary_length(bin);
    size_t encoded_len = base64_encoded_len(length, BASE64_STD);
    char* encoded = (char*)mem_alloc(encoded_len + 1, MEM_CAT_TEMP);
    if (!encoded) return;
    base64_encode(binary_data(bin), length, encoded, BASE64_STD);
    stringbuf_append_str_n(sb, encoded, encoded_len);
    mem_free(encoded);
}

String* format_mark(Pool* pool, Item root_item) {
    if (!pool) return NULL;
    StrBuf* sb = strbuf_new();
    if (!sb) return NULL;
    print_item(sb, root_item, 0, NULL);
    String* result = (String*)pool_alloc(pool, sizeof(String) + sb->length + 1);
    if (result) {
        result->len = (uint32_t)sb->length;
        result->is_ascii = str_is_ascii(sb->str, sb->length) ? 1 : 0;
        memcpy(result->chars, sb->str, sb->length + 1);
    }
    strbuf_free(sb);
    return result;
}

extern "C" String* format_data(Item item, String* type, String* flavor, Pool* pool) {
    // format(x) documents a default serializer; null type must not drop scalars.
    const char* t = type ? type->chars : "text";
    const char* f = (flavor && flavor->len > 0) ? flavor->chars : NULL;

    log_debug("Formatting with type: %s%s%s", t, f ? "-" : "", f ? f : "");

    // ---- Static dispatch table: type → format function (no flavor) ----
    struct FormatEntry {
        const char* type_name;
        String* (*format_fn)(Pool*, Item);
    };
    static const FormatEntry SIMPLE_FORMATS[] = {
        { "mark",       format_mark },
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
