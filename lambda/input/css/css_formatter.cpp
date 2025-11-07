#include "css_formatter.hpp"
#include "css_parser.hpp"
#include "css_style.hpp"
#include "../../../lib/stringbuf.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

// ============================================================================
// Helper Functions
// ============================================================================

static void append_indent(CssFormatter* formatter) {
    if (formatter->options.style == CSS_FORMAT_COMPRESSED) {
        return; // No indentation in compressed mode
    }

    for (int i = 0; i < formatter->current_indent; i++) {
        if (formatter->options.use_tabs) {
            stringbuf_append_str(formatter->output, "\t");
        } else {
            for (int j = 0; j < formatter->options.indent_size; j++) {
                stringbuf_append_str(formatter->output, " ");
            }
        }
    }
}

static void append_newline(CssFormatter* formatter) {
    if (formatter->options.style != CSS_FORMAT_COMPRESSED) {
        stringbuf_append_str(formatter->output, "\n");
    }
}

static void append_space(CssFormatter* formatter) {
    if (formatter->options.style != CSS_FORMAT_COMPRESSED) {
        stringbuf_append_str(formatter->output, " ");
    }
}

static const char* unit_to_string(CssUnit unit) {
    switch (unit) {
        case CSS_UNIT_PX: return "px";
        case CSS_UNIT_EM: return "em";
        case CSS_UNIT_REM: return "rem";
        case CSS_UNIT_PERCENT: return "%";
        case CSS_UNIT_VW: return "vw";
        case CSS_UNIT_VH: return "vh";
        case CSS_UNIT_CM: return "cm";
        case CSS_UNIT_MM: return "mm";
        case CSS_UNIT_IN: return "in";
        case CSS_UNIT_PT: return "pt";
        case CSS_UNIT_PC: return "pc";
        case CSS_UNIT_EX: return "ex";
        case CSS_UNIT_CH: return "ch";
        case CSS_UNIT_VMIN: return "vmin";
        case CSS_UNIT_VMAX: return "vmax";
        case CSS_UNIT_DEG: return "deg";
        case CSS_UNIT_RAD: return "rad";
        case CSS_UNIT_GRAD: return "grad";
        case CSS_UNIT_TURN: return "turn";
        case CSS_UNIT_S: return "s";
        case CSS_UNIT_MS: return "ms";
        case CSS_UNIT_FR: return "fr";
        default: return "";
    }
}

// ============================================================================
// Value Formatting
// ============================================================================

// Format value (complete implementation)
void css_format_value(CssFormatter* formatter, CssValue* value) {
    if (!formatter || !value) return;

    // Don't reset buffer - append to existing content

    switch (value->type) {
        case CSS_VALUE_KEYWORD:
            if (value->data.keyword) {
                stringbuf_append_str(formatter->output, value->data.keyword);
            }
            break;

        case CSS_VALUE_LENGTH:
            // Format number with unit
            stringbuf_append_format(formatter->output, "%.2f", value->data.length.value);
            stringbuf_append_str(formatter->output, unit_to_string(value->data.length.unit));
            break;

        case CSS_VALUE_NUMBER:
            stringbuf_append_format(formatter->output, "%.2f", value->data.number.value);
            break;

        case CSS_VALUE_PERCENTAGE:
            stringbuf_append_format(formatter->output, "%.2f%%", value->data.percentage.value);
            break;

        case CSS_VALUE_COLOR:
            // Format color based on type
            if (value->data.color.type == CSS_COLOR_KEYWORD && value->data.color.data.keyword) {
                stringbuf_append_str(formatter->output, value->data.color.data.keyword);
            } else if (value->data.color.type == CSS_COLOR_HEX || value->data.color.type == CSS_COLOR_RGB) {
                // Format as hex or rgb
                uint8_t r = value->data.color.data.rgba.r;
                uint8_t g = value->data.color.data.rgba.g;
                uint8_t b = value->data.color.data.rgba.b;
                uint8_t a = value->data.color.data.rgba.a;

                if (a == 255) {
                    if (formatter->options.lowercase_hex) {
                        stringbuf_append_format(formatter->output, "#%02x%02x%02x", r, g, b);
                    } else {
                        stringbuf_append_format(formatter->output, "#%02X%02X%02X", r, g, b);
                    }
                } else {
                    stringbuf_append_format(formatter->output, "rgba(%d, %d, %d, %.2f)",
                        r, g, b, a / 255.0);
                }
            } else if (value->data.color.type == CSS_COLOR_HSL) {
                double h = value->data.color.data.hsla.h;
                double s = value->data.color.data.hsla.s;
                double l = value->data.color.data.hsla.l;
                double a = value->data.color.data.hsla.a;

                if (a >= 1.0) {
                    stringbuf_append_format(formatter->output, "hsl(%.1f, %.1f%%, %.1f%%)", h, s * 100, l * 100);
                } else {
                    stringbuf_append_format(formatter->output, "hsla(%.1f, %.1f%%, %.1f%%, %.2f)", h, s * 100, l * 100, a);
                }
            } else {
                // Fallback to black
                stringbuf_append_str(formatter->output, "#000000");
            }
            break;

        case CSS_VALUE_STRING:
            if (value->data.string) {
                stringbuf_append_str(formatter->output, "\"");
                stringbuf_append_str(formatter->output, value->data.string);
                stringbuf_append_str(formatter->output, "\"");
            }
            break;

        case CSS_VALUE_URL:
            stringbuf_append_str(formatter->output, "url(");
            if (formatter->options.quote_urls) {
                stringbuf_append_str(formatter->output, "\"");
            }
            if (value->data.url) {
                stringbuf_append_str(formatter->output, value->data.url);
            }
            if (formatter->options.quote_urls) {
                stringbuf_append_str(formatter->output, "\"");
            }
            stringbuf_append_str(formatter->output, ")");
            break;

        case CSS_VALUE_FUNCTION:
            if (value->data.function.name) {
                stringbuf_append_str(formatter->output, value->data.function.name);
                stringbuf_append_str(formatter->output, "(");
                // Format function arguments
                for (size_t i = 0; i < value->data.function.arg_count; i++) {
                    if (i > 0) {
                        stringbuf_append_str(formatter->output, ", ");
                    }
                    if (value->data.function.args && value->data.function.args[i]) {
                        // Save current output, format arg value, restore
                        StringBuf* temp = formatter->output;
                        formatter->output = stringbuf_new(formatter->pool);
                        css_format_value(formatter, value->data.function.args[i]);
                        String* formatted_str = stringbuf_to_string(formatter->output);
                        formatter->output = temp;
                        if (formatted_str && formatted_str->chars) {
                            stringbuf_append_str(formatter->output, formatted_str->chars);
                        }
                    }
                }
                stringbuf_append_str(formatter->output, ")");
            }
            break;

        case CSS_VALUE_LIST:
            // Format value list (space-separated or comma-separated)
            if (value->data.list.values) {
                for (size_t i = 0; i < value->data.list.count; i++) {
                    if (i > 0) {
                        if (value->data.list.comma_separated) {
                            stringbuf_append_str(formatter->output, ", ");
                        } else {
                            stringbuf_append_str(formatter->output, " ");
                        }
                    }
                    if (value->data.list.values[i]) {
                        // Recursively format list values
                        StringBuf* temp = formatter->output;
                        formatter->output = stringbuf_new(formatter->pool);
                        css_format_value(formatter, value->data.list.values[i]);
                        String* formatted_str = stringbuf_to_string(formatter->output);
                        formatter->output = temp;
                        if (formatted_str && formatted_str->chars) {
                            stringbuf_append_str(formatter->output, formatted_str->chars);
                        }
                    }
                }
            }
            break;

        default:
            stringbuf_append_str(formatter->output, "<unknown-value>");
            break;
    }

    // Don't call stringbuf_to_string here - let the caller handle it
    // This function just appends to the formatter's output buffer
}

// ============================================================================
// Declaration Formatting
// ============================================================================

const char* css_format_declaration(CssFormatter* formatter, CssPropertyId property_id, CssValue* value) {
    if (!formatter || !value) return NULL;

    // Don't reset buffer - append to existing content

    // Get property name
    const char* property_name = css_property_get_name(property_id);
    if (!property_name) {
        property_name = "<unknown-property>";
    }

    stringbuf_append_str(formatter->output, property_name);
    stringbuf_append_str(formatter->output, ":");
    append_space(formatter);

    // Format value - use temporary buffer
    StringBuf* temp = formatter->output;
    formatter->output = stringbuf_new(formatter->pool);
    css_format_value(formatter, value);
    String* value_str = stringbuf_to_string(formatter->output);
    formatter->output = temp;

    if (value_str && value_str->chars) {
        stringbuf_append_str(formatter->output, value_str->chars);
    }    String* result = stringbuf_to_string(formatter->output);
    return (result && result->chars) ? result->chars : "";
}

// ============================================================================
// Selector Formatting
// ============================================================================

const char* css_format_selector_group(CssFormatter* formatter, CssSelectorGroup* selector_group) {
    if (!formatter || !selector_group) return NULL;

    // Don't reset buffer - append to existing content

    // Format each selector in the group separated by commas
    for (size_t i = 0; i < selector_group->selector_count; i++) {
        if (i > 0) {
            stringbuf_append_str(formatter->output, ",");
            append_space(formatter);
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
                        append_space(formatter);
                        break;
                    case CSS_COMBINATOR_CHILD:
                        append_space(formatter);
                        stringbuf_append_str(formatter->output, ">");
                        append_space(formatter);
                        break;
                    case CSS_COMBINATOR_NEXT_SIBLING:
                        append_space(formatter);
                        stringbuf_append_str(formatter->output, "+");
                        append_space(formatter);
                        break;
                    case CSS_COMBINATOR_SUBSEQUENT_SIBLING:
                        append_space(formatter);
                        stringbuf_append_str(formatter->output, "~");
                        append_space(formatter);
                        break;
                    case CSS_COMBINATOR_COLUMN:
                        append_space(formatter);
                        stringbuf_append_str(formatter->output, "||");
                        append_space(formatter);
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
                    case CSS_SELECTOR_PSEUDO_NTH_CHILD:
                        stringbuf_append_str(formatter->output, ":nth-child");
                        if (simple->value) {
                            stringbuf_append_str(formatter->output, "(");
                            stringbuf_append_str(formatter->output, simple->value);
                            stringbuf_append_str(formatter->output, ")");
                        }
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

    String* result = stringbuf_to_string(formatter->output);
    return (result && result->chars) ? result->chars : "";
}

// ============================================================================
// Rule Formatting
// ============================================================================

const char* css_format_rule(CssFormatter* formatter, CssRule* rule) {
    if (!formatter || !rule) return NULL;

    stringbuf_reset(formatter->output);

    // Handle different rule types
    if (rule->type == CSS_RULE_STYLE) {
        // Format selector group
        if (rule->data.style_rule.selector_group) {
            const char* selector_str = css_format_selector_group(formatter, rule->data.style_rule.selector_group);
            if (selector_str) {
                stringbuf_append_str(formatter->output, selector_str);
            }
        }

        // Opening brace
        if (formatter->options.space_before_brace) {
            append_space(formatter);
        }
        stringbuf_append_str(formatter->output, "{");

        if (formatter->options.newline_after_brace) {
            append_newline(formatter);
        }

        // Format declarations
        formatter->current_indent++;

        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (!decl) continue;

            if (formatter->options.newline_after_brace) {
                append_indent(formatter);
            } else if (i > 0) {
                append_space(formatter);
            }

            // Format declaration
            const char* decl_str = css_format_declaration(formatter, decl->property_id, decl->value);
            if (decl_str) {
                stringbuf_append_str(formatter->output, decl_str);
            }

            // Add !important flag
            if (decl->important) {
                append_space(formatter);
                stringbuf_append_str(formatter->output, "!important");
            }

            // Semicolon
            if (i < rule->data.style_rule.declaration_count - 1 || formatter->options.trailing_semicolon) {
                stringbuf_append_str(formatter->output, ";");
            }

            if (formatter->options.newline_after_brace) {
                append_newline(formatter);
            }
        }

        formatter->current_indent--;

        // Closing brace
        if (formatter->options.newline_after_brace && rule->data.style_rule.declaration_count > 0) {
            append_indent(formatter);
        }
        stringbuf_append_str(formatter->output, "}");

    } else if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
               rule->type == CSS_RULE_CONTAINER) {
        // Format conditional at-rules (@media, @supports, @container, etc.)
        const char* rule_name = (rule->type == CSS_RULE_MEDIA) ? "media" :
                               (rule->type == CSS_RULE_SUPPORTS) ? "supports" : "container";

        fprintf(stderr, "[CSS Formatter] Formatting conditional @%s with %zu nested rules\n",
                rule_name, rule->data.conditional_rule.rule_count);

        stringbuf_append_str(formatter->output, "@");
        stringbuf_append_str(formatter->output, rule_name);

        if (rule->data.conditional_rule.condition) {
            append_space(formatter);
            stringbuf_append_str(formatter->output, rule->data.conditional_rule.condition);
        }

        if (formatter->options.space_before_brace) {
            append_space(formatter);
        }
        stringbuf_append_str(formatter->output, "{");

        if (formatter->options.newline_after_brace) {
            append_newline(formatter);
        }

        // Format nested rules
        formatter->current_indent++;
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            if (formatter->options.newline_after_brace) {
                append_indent(formatter);
            }

            // Format nested rule using temporary buffer (same approach as stylesheet formatter)
            StringBuf* saved_output = formatter->output;
            formatter->output = stringbuf_new(formatter->pool);

            const char* nested_rule_str = css_format_rule(formatter, rule->data.conditional_rule.rules[i]);

            // Restore main buffer and append nested rule
            formatter->output = saved_output;
            if (nested_rule_str) {
                stringbuf_append_str(formatter->output, nested_rule_str);
            }

            if (formatter->options.newline_after_brace) {
                append_newline(formatter);
            }
        }
        formatter->current_indent--;

        if (formatter->options.newline_after_brace) {
            append_indent(formatter);
        }
        stringbuf_append_str(formatter->output, "}");

    } else if (rule->type == CSS_RULE_IMPORT) {
        // Format @import rule
        stringbuf_append_str(formatter->output, "@import url(");
        if (rule->data.import_rule.url) {
            stringbuf_append_str(formatter->output, rule->data.import_rule.url);
        }
        stringbuf_append_str(formatter->output, ")");

        if (rule->data.import_rule.media) {
            append_space(formatter);
            stringbuf_append_str(formatter->output, rule->data.import_rule.media);
        }
        stringbuf_append_str(formatter->output, ";");

    } else if (rule->type == CSS_RULE_CHARSET) {
        // Format @charset rule
        stringbuf_append_str(formatter->output, "@charset ");
        if (rule->data.charset_rule.charset) {
            stringbuf_append_str(formatter->output, "\"");
            stringbuf_append_str(formatter->output, rule->data.charset_rule.charset);
            stringbuf_append_str(formatter->output, "\"");
        }
        stringbuf_append_str(formatter->output, ";");

    } else if (rule->type == CSS_RULE_NAMESPACE) {
        // Format @namespace rule
        stringbuf_append_str(formatter->output, "@namespace ");
        if (rule->data.namespace_rule.prefix) {
            stringbuf_append_str(formatter->output, rule->data.namespace_rule.prefix);
            append_space(formatter);
        }
        if (rule->data.namespace_rule.namespace_url) {
            stringbuf_append_str(formatter->output, "url(");
            stringbuf_append_str(formatter->output, rule->data.namespace_rule.namespace_url);
            stringbuf_append_str(formatter->output, ")");
        }
        stringbuf_append_str(formatter->output, ";");

    } else if (rule->type == CSS_RULE_FONT_FACE || rule->type == CSS_RULE_KEYFRAMES) {
        // Format generic at-rules (@font-face, @keyframes, etc.)
        fprintf(stderr, "[CSS Formatter] Formatting generic @-rule: %s\n",
                rule->data.generic_rule.name ? rule->data.generic_rule.name : "null");

        stringbuf_append_str(formatter->output, "@");
        if (rule->data.generic_rule.name) {
            stringbuf_append_str(formatter->output, rule->data.generic_rule.name);
        }

        if (rule->data.generic_rule.content) {
            fprintf(stderr, "[CSS Formatter] Content: '%s'\n", rule->data.generic_rule.content);
            append_space(formatter);
            stringbuf_append_str(formatter->output, rule->data.generic_rule.content);
        }

        if (formatter->options.newline_after_brace) {
            append_newline(formatter);
        }
    }

    String* result = stringbuf_to_string(formatter->output);
    fprintf(stderr, "[CSS Formatter] Returning from css_format_rule (rule type %d), buffer='%.80s...'\n",
            rule ? rule->type : -1, result->chars ? result->chars : "(null)");
    return (result && result->chars) ? result->chars : "";
}

// ============================================================================
// Stylesheet Formatting
// ============================================================================

const char* css_format_stylesheet(CssFormatter* formatter, CssStylesheet* stylesheet) {
    if (!formatter || !stylesheet) return NULL;

    stringbuf_reset(formatter->output);
    formatter->current_indent = 0;

    // Format each rule
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        if (i > 0) {
            append_newline(formatter);
            if (formatter->options.style == CSS_FORMAT_PRETTY ||
                formatter->options.style == CSS_FORMAT_EXPANDED) {
                append_newline(formatter); // Extra blank line between rules
            }
        }

        // Format this rule into a temporary buffer
        // We need to save the current output and restore it after formatting the rule
        StringBuf* saved_output = formatter->output;
        formatter->output = stringbuf_new(formatter->pool);

        const char* rule_str = css_format_rule(formatter, stylesheet->rules[i]);

        // Restore the main output buffer and append the rule
        formatter->output = saved_output;
        if (rule_str) {
            fprintf(stderr, "[CSS Formatter Stylesheet] Appending rule %zu (type %d), length=%zu: '%.80s...'\n",
                    i, stylesheet->rules[i]->type, strlen(rule_str), rule_str);
            stringbuf_append_str(formatter->output, rule_str);
        }
    }

    // Final newline
    if (stylesheet->rule_count > 0 && formatter->options.style != CSS_FORMAT_COMPRESSED) {
        append_newline(formatter);
    }

    String* result = stringbuf_to_string(formatter->output);
    return (result && result->chars) ? result->chars : "";
}
// ============================================================================
// Convenience functions
// ============================================================================

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

    switch (style) {
        case CSS_FORMAT_COMPACT:
            options.indent_size = 2;
            options.use_tabs = false;
            options.trailing_semicolon = true;
            options.space_before_brace = true;
            options.newline_after_brace = false;
            options.lowercase_hex = true;
            options.quote_urls = false;
            options.sort_properties = false;
            break;

        case CSS_FORMAT_EXPANDED:
            options.indent_size = 4;
            options.use_tabs = false;
            options.trailing_semicolon = true;
            options.space_before_brace = true;
            options.newline_after_brace = true;
            options.lowercase_hex = true;
            options.quote_urls = false;
            options.sort_properties = false;
            break;

        case CSS_FORMAT_COMPRESSED:
            options.indent_size = 0;
            options.use_tabs = false;
            options.trailing_semicolon = false;
            options.space_before_brace = false;
            options.newline_after_brace = false;
            options.lowercase_hex = true;
            options.quote_urls = false;
            options.sort_properties = false;
            break;

        case CSS_FORMAT_PRETTY:
            options.indent_size = 2;
            options.use_tabs = false;
            options.trailing_semicolon = true;
            options.space_before_brace = true;
            options.newline_after_brace = true;
            options.lowercase_hex = true;
            options.quote_urls = true;
            options.sort_properties = false;
            break;

        default:
            // Default to compact
            options = css_get_default_format_options(CSS_FORMAT_COMPACT);
            break;
    }

    return options;
}
