#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <ctype.h>

using namespace lambda;

// JSX JavaScript expression parsing state
typedef struct {
    int brace_depth;
    bool in_string;
    bool in_template_literal;
    char string_delimiter;
    bool escaped;
} JSXExpressionState;

static const int JSX_MAX_DEPTH = 512;

// Forward declarations
static Element* parse_jsx_element(InputContext& ctx, const char** jsx, const char* end, int depth = 0);
static Element* parse_jsx_fragment(InputContext& ctx, const char** jsx, const char* end, int depth = 0);
static void parse_jsx_attributes(InputContext& ctx, Element* element, const char** jsx, const char* end);
static String* parse_jsx_attribute_value(InputContext& ctx, const char** jsx, const char* end);
static Element* parse_jsx_expression(InputContext& ctx, const char** jsx, const char* end);
static String* parse_jsx_text_content(InputContext& ctx, const char** jsx, const char* end);

// Utility functions
static bool is_jsx_identifier_char(char c) {
    return isalnum(c) || c == '_' || c == '$';
}

static bool is_jsx_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_jsx_whitespace(const char** jsx, const char* end) {
    while (*jsx < end && is_jsx_whitespace(**jsx)) {
        (*jsx)++;
    }
}

static bool is_jsx_component_name(const char* name) {
    return name && name[0] >= 'A' && name[0] <= 'Z';
}

// JSX expression parsing functions
static String* parse_jsx_expression_content(InputContext& ctx, const char** js_expr, const char* end) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    JSXExpressionState state = {0};
    state.brace_depth = 1; // Start with 1 since we've already seen opening {

    while (*js_expr < end && state.brace_depth > 0) {
        char c = **js_expr;

        if (state.escaped) {
            stringbuf_append_char(sb, c);
            state.escaped = false;
            (*js_expr)++;
            continue;
        }

        if (c == '\\' && (state.in_string || state.in_template_literal)) {
            state.escaped = true;
            stringbuf_append_char(sb, c);
            (*js_expr)++;
            continue;
        }

        if (state.in_template_literal) {
            if (c == '`') {
                state.in_template_literal = false;
            }
            stringbuf_append_char(sb, c);
            (*js_expr)++;
            continue;
        }

        if (state.in_string) {
            if (c == state.string_delimiter) {
                state.in_string = false;
                state.string_delimiter = 0;
            }
            stringbuf_append_char(sb, c);
            (*js_expr)++;
            continue;
        }

        // Not in string or template literal
        switch (c) {
            case '"':
            case '\'':
                state.in_string = true;
                state.string_delimiter = c;
                break;
            case '`':
                state.in_template_literal = true;
                break;
            case '{':
                state.brace_depth++;
                break;
            case '}':
                state.brace_depth--;
                if (state.brace_depth == 0) {
                    // Don't include the closing brace
                    return builder.createString(sb->str->chars, sb->length);
                }
                break;
        }

        stringbuf_append_char(sb, c);
        (*js_expr)++;
    }

    return builder.createString(sb->str->chars, sb->length);
}

static Element* create_jsx_js_expression_element(InputContext& ctx, const char* js_content) {
    MarkBuilder& builder = ctx.builder;

    ElementBuilder js_elem = builder.element("js");
    String* content = builder.createString(js_content);
    Item content_item = {.item = s2it(content)};
    js_elem.child(content_item);

    // Return raw Element* for compatibility with existing list_push code
    return js_elem.final().element;
}

// Parse JSX expression: {expression}
static Element* parse_jsx_expression(InputContext& ctx, const char** jsx, const char* end) {
    if (*jsx >= end || **jsx != '{') {
        return NULL;
    }

    (*jsx)++; // Skip opening {

    String* expr_content = parse_jsx_expression_content(ctx, jsx, end);
    if (!expr_content) {
        return NULL;
    }

    // jsx pointer should now be at closing }
    if (*jsx < end && **jsx == '}') {
        (*jsx)++; // Skip closing }
    }

    return create_jsx_js_expression_element(ctx, expr_content->chars);
}

// Parse JSX text content until < or {
static String* parse_jsx_text_content(InputContext& ctx, const char** jsx, const char* end) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (*jsx < end && **jsx != '<' && **jsx != '{') {
        char c = **jsx;

        // Handle HTML entities in JSX text
        if (c == '&') {
            const char* entity_start = *jsx + 1;
            const char* entity_end = entity_start;

            while (entity_end < end && *entity_end != ';' && *entity_end != ' ' &&
                   *entity_end != '<' && *entity_end != '&') {
                entity_end++;
            }

            if (entity_end < end && *entity_end == ';') {
                // Simple entity handling - just preserve as-is for now
                while (*jsx <= entity_end) {
                    stringbuf_append_char(sb, **jsx);
                    (*jsx)++;
                }
                continue;
            }
        }

        stringbuf_append_char(sb, c);
        (*jsx)++;
    }

    return builder.createString(sb->str->chars, sb->length);
}

// Parse JSX tag name
static String* parse_jsx_tag_name(InputContext& ctx, const char** jsx, const char* end) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // First character
    if (*jsx < end && (isalpha(**jsx) || **jsx == '_')) {
        stringbuf_append_char(sb, **jsx);
        (*jsx)++;
    } else {
        return NULL;
    }

    // Remaining characters
    while (*jsx < end && is_jsx_identifier_char(**jsx)) {
        stringbuf_append_char(sb, **jsx);
        (*jsx)++;
    }

    // Handle dot notation for namespaced components (e.g., React.Component)
    while (*jsx < end && **jsx == '.') {
        stringbuf_append_char(sb, **jsx);
        (*jsx)++;

        while (*jsx < end && is_jsx_identifier_char(**jsx)) {
            stringbuf_append_char(sb, **jsx);
            (*jsx)++;
        }
    }

    return builder.createString(sb->str->chars, sb->length);
}

// Parse JSX attribute value
static String* parse_jsx_attribute_value(InputContext& ctx, const char** jsx, const char* end) {
    Input* input = ctx.input();

    skip_jsx_whitespace(jsx, end);

    if (*jsx >= end) return NULL;

    if (**jsx == '"' || **jsx == '\'') {
        char quote = **jsx;
        (*jsx)++; // Skip opening quote

        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        while (*jsx < end && **jsx != quote) {
            if (**jsx == '\\' && *jsx + 1 < end) {
                // Handle escaped characters
                (*jsx)++; // Skip backslash
                if (*jsx < end) {
                    stringbuf_append_char(sb, **jsx);
                    (*jsx)++;
                }
            } else {
                stringbuf_append_char(sb, **jsx);
                (*jsx)++;
            }
        }

        if (*jsx < end && **jsx == quote) {
            (*jsx)++; // Skip closing quote
        }

        return stringbuf_to_string(sb);
    } else {
        // Unquoted attribute value (shouldn't happen in JSX but handle it)
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        while (*jsx < end && !is_jsx_whitespace(**jsx) && **jsx != '>' && **jsx != '/') {
            stringbuf_append_char(sb, **jsx);
            (*jsx)++;
        }

        return stringbuf_to_string(sb);
    }
}

// Parse JSX attributes (mutates ElementBuilder)
static void parse_jsx_attributes(InputContext& ctx, ElementBuilder& element, const char** jsx, const char* end) {
    while (*jsx < end) {
        skip_jsx_whitespace(jsx, end);

        if (*jsx >= end || **jsx == '>' || **jsx == '/') {
            break;
        }

        // Check for JSX expression as attribute
        if (**jsx == '{') {
            // Spread attributes or expression attributes - skip for now
            Element* expr = parse_jsx_expression(ctx, jsx, end);
            if (expr) {
                // For spread attributes, we'd need special handling
                // For now, just skip
            }
            continue;
        }

        // Parse attribute name
        String* attr_name = parse_jsx_tag_name(ctx, jsx, end);
        if (!attr_name) break;

        skip_jsx_whitespace(jsx, end);

        if (*jsx < end && **jsx == '=') {
            (*jsx)++; // Skip =
            skip_jsx_whitespace(jsx, end);

            if (*jsx < end && **jsx == '{') {
                // JSX expression attribute value
                Element* expr = parse_jsx_expression(ctx, jsx, end);
                if (expr) {
                    Item expr_item = {.item = (uint64_t)expr};
                    element.attr(attr_name->chars, expr_item);
                }
            } else {
                // String attribute value
                String* attr_value = parse_jsx_attribute_value(ctx, jsx, end);
                if (attr_value) {
                    element.attr(attr_name->chars, attr_value->chars);
                }
            }
        } else {
            // Boolean attribute (no value)
            element.attr(attr_name->chars, "true");
        }
    }
}

// Parse JSX fragment: <>...</>
static Element* parse_jsx_fragment(InputContext& ctx, const char** jsx, const char* end, int depth) {
    if (*jsx + 1 >= end || **jsx != '<' || *(*jsx + 1) != '>') {
        return NULL;
    }
    if (depth >= JSX_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum JSX nesting depth (%d) exceeded", JSX_MAX_DEPTH);
        return NULL;
    }

    *jsx += 2; // Skip <>

    ElementBuilder fragment = ctx.builder.element("jsx_fragment");
    fragment.attr("type", "jsx_fragment");

    // Parse children until </>
    while (*jsx < end) {
        skip_jsx_whitespace(jsx, end);

        if (*jsx + 2 < end && strncmp(*jsx, "</>", 3) == 0) {
            *jsx += 3; // Skip </>
            break;
        }

        if (*jsx >= end) break;

        if (**jsx == '<') {
            Element* child = parse_jsx_element(ctx, jsx, end, depth + 1);
            if (child) {
                Item child_item = {.item = (uint64_t)child};
                fragment.child(child_item);
            }
        } else if (**jsx == '{') {
            Element* expr = parse_jsx_expression(ctx, jsx, end);
            if (expr) {
                Item expr_item = {.item = (uint64_t)expr};
                fragment.child(expr_item);
            }
        } else {
            String* text = parse_jsx_text_content(ctx, jsx, end);
            if (text && text->len > 0) {
                // Only add non-empty text
                bool has_non_whitespace = false;
                for (int i = 0; i < text->len; i++) {
                    if (!is_jsx_whitespace(text->chars[i])) {
                        has_non_whitespace = true;
                        break;
                    }
                }
                if (has_non_whitespace) {
                    Item text_item = {.item = s2it(text)};
                    fragment.child(text_item);
                }
            }
        }
    }

    // Return raw Element* for compatibility with existing list_push code
    return fragment.final().element;
}

// Parse JSX element: <tag>...</tag> or <tag />
static Element* parse_jsx_element(InputContext& ctx, const char** jsx, const char* end, int depth) {
    if (*jsx >= end || **jsx != '<') {
        return NULL;
    }
    if (depth >= JSX_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum JSX nesting depth (%d) exceeded", JSX_MAX_DEPTH);
        return NULL;
    }

    // Check for fragment
    if (*jsx + 1 < end && *(*jsx + 1) == '>') {
        return parse_jsx_fragment(ctx, jsx, end, depth);
    }

    (*jsx)++; // Skip <

    // Parse tag name
    String* tag_name = parse_jsx_tag_name(ctx, jsx, end);
    if (!tag_name) {
        return NULL;
    }

    ElementBuilder element = ctx.builder.element(tag_name->chars);
    element.attr("type", "jsx_element");

    // Mark as component if starts with uppercase
    if (is_jsx_component_name(tag_name->chars)) {
        element.attr("is_component", "true");
    }

    // Parse attributes
    parse_jsx_attributes(ctx, element, jsx, end);

    skip_jsx_whitespace(jsx, end);

    // Check for self-closing tag
    if (*jsx < end && **jsx == '/') {
        (*jsx)++; // Skip /
        skip_jsx_whitespace(jsx, end);
        if (*jsx < end && **jsx == '>') {
            (*jsx)++; // Skip >
            element.attr("self_closing", "true");
            // Return raw Element* for compatibility with existing list_push code
            return element.final().element;
        }
    }

    // Expect >
    if (*jsx >= end || **jsx != '>') {
        return NULL;
    }
    (*jsx)++; // Skip >

    // Parse children until closing tag
    char closing_tag[256];
    snprintf(closing_tag, sizeof(closing_tag), "</%s>", tag_name->chars);
    size_t closing_tag_len = strlen(closing_tag);

    while (*jsx < end) {
        // Check for closing tag
        if (*jsx + closing_tag_len <= end &&
            strncmp(*jsx, closing_tag, closing_tag_len) == 0) {
            *jsx += closing_tag_len;
            break;
        }

        if (**jsx == '<') {
            Element* child = parse_jsx_element(ctx, jsx, end, depth + 1);
            if (child) {
                Item child_item = {.item = (uint64_t)child};
                element.child(child_item);
            }
        } else if (**jsx == '{') {
            Element* expr = parse_jsx_expression(ctx, jsx, end);
            if (expr) {
                Item expr_item = {.item = (uint64_t)expr};
                element.child(expr_item);
            }
        } else {
            String* text = parse_jsx_text_content(ctx, jsx, end);
            if (text && text->len > 0) {
                // Only add non-empty text
                bool has_non_whitespace = false;
                for (int i = 0; i < text->len; i++) {
                    if (!is_jsx_whitespace(text->chars[i])) {
                        has_non_whitespace = true;
                        break;
                    }
                }
                if (has_non_whitespace) {
                    Item text_item = {.item = s2it(text)};
                    element.child(text_item);
                }
            }
        }
    }

    // Return raw Element* for compatibility with existing list_push code
    return element.final().element;
}

// Main JSX parser entry point
Item input_jsx(Input* input, const char* jsx_string) {
    if (!input || !jsx_string) return {.item = ITEM_NULL};

    // create unified InputContext with source tracking
    InputContext ctx(input, jsx_string, strlen(jsx_string));

    const char* jsx = jsx_string;
    const char* end = jsx_string + strlen(jsx_string);

    // Skip any leading whitespace
    skip_jsx_whitespace(&jsx, end);

    // Parse the root JSX element
    if (jsx < end) {
        Element* root = parse_jsx_element(ctx, &jsx, end);
        if (root) {
            return {.item = (uint64_t)root};
        } else {
            ctx.addError(ctx.tracker.location(), "Failed to parse JSX element");
        }
    }

    if (ctx.hasErrors()) {
        // errors occurred
    }

    return {.item = ITEM_NULL};
}

// Main entry point for JSX parsing (called from input.cpp)
void parse_jsx(Input* input, const char* jsx_string) {
    if (!input || !jsx_string) return;

    input->root = input_jsx(input, jsx_string);
}
