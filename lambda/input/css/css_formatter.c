#include "css_formatter.h"
#include "css_parser.h"
#include "css_selector_parser.h"
#include "css_style.h"
#include "../../../lib/stringbuf.h"
#include <stdio.h>
#include <string.h>

// Create a CSS formatter with default style
CssFormatter* css_formatter_create(Pool* pool, CssFormatStyle style) {
    if (!pool) return NULL;

    CssFormatter* formatter = (CssFormatter*)pool_alloc(pool, sizeof(CssFormatter));
    if (!formatter) return NULL;

    formatter->pool = pool;
    formatter->output = stringbuf_new(pool);
    formatter->current_indent = 0;

    // Set default options based on style
    formatter->options.style = style;
    formatter->options.indent_size = (style == CSS_FORMAT_COMPACT) ? 2 : 4;
    formatter->options.use_tabs = false;
    formatter->options.trailing_semicolon = true;
    formatter->options.space_before_brace = true;
    formatter->options.newline_after_brace = (style != CSS_FORMAT_COMPRESSED);
    formatter->options.lowercase_hex = true;
    formatter->options.quote_urls = false;
    formatter->options.sort_properties = false;

    return formatter;
}

// Create with custom options
CssFormatter* css_formatter_create_with_options(Pool* pool, const CssFormatOptions* options) {
    if (!pool || !options) return NULL;

    CssFormatter* formatter = (CssFormatter*)pool_alloc(pool, sizeof(CssFormatter));
    if (!formatter) return NULL;

    formatter->pool = pool;
    formatter->output = stringbuf_new(pool);
    formatter->current_indent = 0;
    formatter->options = *options;

    return formatter;
}

// Destroy formatter
void css_formatter_destroy(CssFormatter* formatter) {
    // Memory managed by pool
    (void)formatter;
}

// Format stylesheet (stub implementation)
const char* css_format_stylesheet(CssFormatter* formatter, CssStylesheet* stylesheet) {
    if (!formatter || !stylesheet) return NULL;

    stringbuf_reset(formatter->output);

    // Simple stub: just output rule count
    stringbuf_append_format(formatter->output, "/* Stylesheet with %zu rules */\n", stylesheet->rule_count);

    // Format each rule
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        if (stylesheet->rules[i]->property_count > 0) {
            stringbuf_append_str(formatter->output, "/* Rule ");
            stringbuf_append_ulong(formatter->output, i);
            stringbuf_append_str(formatter->output, " */ {\n");

            // Format properties
            for (size_t j = 0; j < stylesheet->rules[i]->property_count; j++) {
                stringbuf_append_str(formatter->output, "    ");
                stringbuf_append_str(formatter->output, stylesheet->rules[i]->property_names[j]);
                stringbuf_append_str(formatter->output, ": <value>;\n");
            }

            stringbuf_append_str(formatter->output, "}\n\n");
        }
    }

    return stringbuf_to_string(formatter->output)->chars;
}

// Format rule (stub)
const char* css_format_rule(CssFormatter* formatter, CssRule* rule) {
    if (!formatter || !rule) return NULL;

    stringbuf_reset(formatter->output);
    stringbuf_append_str(formatter->output, "/* CSS Rule */\n");

    return stringbuf_to_string(formatter->output)->chars;
}

// Format selector group (stub)
const char* css_format_selector_group(CssFormatter* formatter, CssSelectorGroup* selector_group) {
    if (!formatter || !selector_group) return NULL;

    stringbuf_reset(formatter->output);

    // Format each selector in the group separated by commas
    for (size_t i = 0; i < selector_group->selector_count; i++) {
        if (i > 0) {
            stringbuf_append_str(formatter->output, ", ");
        }

        CssSelector* selector = selector_group->selectors[i];
        if (!selector) continue;

        // Format each compound selector in the complex selector
        for (size_t j = 0; j < selector->compound_selector_count; j++) {
            // Add combinator (except before first compound selector)
            if (j > 0 && j <= selector->compound_selector_count) {
                CssCombinator combinator = selector->combinators[j - 1];
                switch (combinator) {
                    case CSS_COMBINATOR_DESCENDANT:
                        stringbuf_append_str(formatter->output, " ");
                        break;
                    case CSS_COMBINATOR_CHILD:
                        stringbuf_append_str(formatter->output, " > ");
                        break;
                    case CSS_COMBINATOR_NEXT_SIBLING:
                        stringbuf_append_str(formatter->output, " + ");
                        break;
                    case CSS_COMBINATOR_SUBSEQUENT_SIBLING:
                        stringbuf_append_str(formatter->output, " ~ ");
                        break;
                    case CSS_COMBINATOR_COLUMN:
                        stringbuf_append_str(formatter->output, " || ");
                        break;
                    default:
                        break;
                }
            }

            CssCompoundSelector* compound = selector->compound_selectors[j];
            if (!compound) continue;

            // Format each simple selector in the compound selector
            for (size_t k = 0; k < compound->simple_selector_count; k++) {
                CssSimpleSelector* simple = compound->simple_selectors[k];
                if (!simple) continue;

                // Format based on selector type
                switch (simple->type) {
                    case CSS_SELECTOR_TYPE_ELEMENT:
                        if (simple->value) {
                            stringbuf_append_str(formatter->output, simple->value);
                        }
                        break;
                    case CSS_SELECTOR_TYPE_CLASS:
                        stringbuf_append_str(formatter->output, ".");
                        if (simple->value) {
                            stringbuf_append_str(formatter->output, simple->value);
                        }
                        break;
                    case CSS_SELECTOR_TYPE_ID:
                        stringbuf_append_str(formatter->output, "#");
                        if (simple->value) {
                            stringbuf_append_str(formatter->output, simple->value);
                        }
                        break;
                    case CSS_SELECTOR_TYPE_UNIVERSAL:
                        stringbuf_append_str(formatter->output, "*");
                        break;
                    case CSS_SELECTOR_ATTR_EXACT:
                    case CSS_SELECTOR_ATTR_CONTAINS:
                    case CSS_SELECTOR_ATTR_BEGINS:
                    case CSS_SELECTOR_ATTR_ENDS:
                    case CSS_SELECTOR_ATTR_SUBSTRING:
                    case CSS_SELECTOR_ATTR_LANG:
                    case CSS_SELECTOR_ATTR_EXISTS:
                        stringbuf_append_str(formatter->output, "[");
                        if (simple->attribute.name) {
                            stringbuf_append_str(formatter->output, simple->attribute.name);
                            if (simple->attribute.value) {
                                stringbuf_append_str(formatter->output, "=\"");
                                stringbuf_append_str(formatter->output, simple->attribute.value);
                                stringbuf_append_str(formatter->output, "\"");
                            }
                        }
                        stringbuf_append_str(formatter->output, "]");
                        break;
                    case CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
                        stringbuf_append_str(formatter->output, "::before");
                        break;
                    case CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:
                        stringbuf_append_str(formatter->output, "::after");
                        break;
                    case CSS_SELECTOR_PSEUDO_HOVER:
                        stringbuf_append_str(formatter->output, ":hover");
                        break;
                    case CSS_SELECTOR_PSEUDO_FOCUS:
                        stringbuf_append_str(formatter->output, ":focus");
                        break;
                    case CSS_SELECTOR_PSEUDO_ACTIVE:
                        stringbuf_append_str(formatter->output, ":active");
                        break;
                    case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
                        stringbuf_append_str(formatter->output, ":first-child");
                        break;
                    case CSS_SELECTOR_PSEUDO_LAST_CHILD:
                        stringbuf_append_str(formatter->output, ":last-child");
                        break;
                    default:
                        // For other pseudo-classes/elements with values
                        if (simple->value) {
                            stringbuf_append_str(formatter->output, ":");
                            stringbuf_append_str(formatter->output, simple->value);
                        }
                        break;
                }
            }
        }
    }

    return stringbuf_to_string(formatter->output)->chars;
}// Format value (stub)
const char* css_format_value(CssFormatter* formatter, CssValue* value) {
    if (!formatter || !value) return NULL;

    stringbuf_reset(formatter->output);

    switch (value->type) {
        case CSS_VALUE_KEYWORD:
            stringbuf_append_str(formatter->output, value->data.keyword);
            break;
        case CSS_VALUE_LENGTH:
            stringbuf_append_format(formatter->output, "%.2fpx", value->data.length.value);
            break;
        case CSS_VALUE_NUMBER:
            stringbuf_append_format(formatter->output, "%.2f", value->data.number.value);
            break;
        default:
            stringbuf_append_str(formatter->output, "<value>");
            break;
    }

    return stringbuf_to_string(formatter->output)->chars;
}

// Format declaration (stub)
const char* css_format_declaration(CssFormatter* formatter, CssPropertyId property_id, CssValue* value) {
    if (!formatter || !value) return NULL;

    stringbuf_reset(formatter->output);
    stringbuf_append_format(formatter->output, "property_%d: ", property_id);
    stringbuf_append_str(formatter->output, css_format_value(formatter, value));

    return stringbuf_to_string(formatter->output)->chars;
}

// Convenience function: format stylesheet with default compact style
const char* css_stylesheet_to_string(CssStylesheet* stylesheet, Pool* pool) {
    if (!stylesheet || !pool) return NULL;

    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    return css_format_stylesheet(formatter, stylesheet);
}

// Convenience function: format stylesheet with specific style
const char* css_stylesheet_to_string_styled(CssStylesheet* stylesheet, Pool* pool, CssFormatStyle style) {
    if (!stylesheet || !pool) return NULL;

    CssFormatter* formatter = css_formatter_create(pool, style);
    return css_format_stylesheet(formatter, stylesheet);
}

// Get default format options
CssFormatOptions css_get_default_format_options(CssFormatStyle style) {
    CssFormatOptions options;
    options.style = style;
    options.indent_size = (style == CSS_FORMAT_COMPACT) ? 2 : 4;
    options.use_tabs = false;
    options.trailing_semicolon = true;
    options.space_before_brace = true;
    options.newline_after_brace = (style != CSS_FORMAT_COMPRESSED);
    options.lowercase_hex = true;
    options.quote_urls = false;
    options.sort_properties = false;
    return options;
}
