#include "input.h"
#include <ctype.h>

// JSX JavaScript expression parsing state
typedef struct {
    int brace_depth;
    bool in_string;
    bool in_template_literal;
    char string_delimiter;
    bool escaped;
} JSXExpressionState;

// Forward declarations
static Element* parse_jsx_element(Input* input, const char** jsx, const char* end);
static Element* parse_jsx_fragment(Input* input, const char** jsx, const char* end);
static void parse_jsx_attributes(Input* input, Element* element, const char** jsx, const char* end);
static String* parse_jsx_attribute_value(Input* input, const char** jsx, const char* end);
static Element* parse_jsx_expression(Input* input, const char** jsx, const char* end);
static String* parse_jsx_text_content(Input* input, const char** jsx, const char* end);

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
static String* parse_jsx_expression_content(Input* input, const char** js_expr, const char* end) {
    StringBuf* sb = input->sb;
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
                    return stringbuf_to_string(sb);
                }
                break;
        }
        
        stringbuf_append_char(sb, c);
        (*js_expr)++;
    }
    
    return stringbuf_to_string(sb);
}

static Element* create_jsx_js_expression_element(Input* input, const char* js_content) {
    Element* js_elem = input_create_element(input, "js");
    String* content = input_create_string(input, js_content);
    Item content_item = {.item = s2it(content)};
    list_push((List*)js_elem, content_item);
    return js_elem;
}

// Parse JSX expression: {expression}
static Element* parse_jsx_expression(Input* input, const char** jsx, const char* end) {
    if (*jsx >= end || **jsx != '{') {
        return NULL;
    }
    
    (*jsx)++; // Skip opening {
    
    String* expr_content = parse_jsx_expression_content(input, jsx, end);
    if (!expr_content) {
        return NULL;
    }
    
    // jsx pointer should now be at closing }
    if (*jsx < end && **jsx == '}') {
        (*jsx)++; // Skip closing }
    }
    
    return create_jsx_js_expression_element(input, expr_content->chars);
}

// Parse JSX text content until < or {
static String* parse_jsx_text_content(Input* input, const char** jsx, const char* end) {
    StringBuf* sb = input->sb;
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
    
    return stringbuf_to_string(sb);
}

// Parse JSX tag name
static String* parse_jsx_tag_name(Input* input, const char** jsx, const char* end) {
    StringBuf* sb = input->sb;
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
    
    return stringbuf_to_string(sb);
}

// Parse JSX attribute value
static String* parse_jsx_attribute_value(Input* input, const char** jsx, const char* end) {
    skip_jsx_whitespace(jsx, end);
    
    if (*jsx >= end) return NULL;
    
    if (**jsx == '"' || **jsx == '\'') {
        char quote = **jsx;
        (*jsx)++; // Skip opening quote
        
        StringBuf* sb = input->sb;
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
        StringBuf* sb = input->sb;
        stringbuf_reset(sb);
        
        while (*jsx < end && !is_jsx_whitespace(**jsx) && **jsx != '>' && **jsx != '/') {
            stringbuf_append_char(sb, **jsx);
            (*jsx)++;
        }
        
        return stringbuf_to_string(sb);
    }
}

// Parse JSX attributes
static void parse_jsx_attributes(Input* input, Element* element, const char** jsx, const char* end) {
    while (*jsx < end) {
        skip_jsx_whitespace(jsx, end);
        
        if (*jsx >= end || **jsx == '>' || **jsx == '/') {
            break;
        }
        
        // Check for JSX expression as attribute
        if (**jsx == '{') {
            // Spread attributes or expression attributes - skip for now
            Element* expr = parse_jsx_expression(input, jsx, end);
            if (expr) {
                // For spread attributes, we'd need special handling
                // For now, just skip
            }
            continue;
        }
        
        // Parse attribute name
        String* attr_name = parse_jsx_tag_name(input, jsx, end);
        if (!attr_name) break;
        
        skip_jsx_whitespace(jsx, end);
        
        if (*jsx < end && **jsx == '=') {
            (*jsx)++; // Skip =
            skip_jsx_whitespace(jsx, end);
            
            if (*jsx < end && **jsx == '{') {
                // JSX expression attribute value
                Element* expr = parse_jsx_expression(input, jsx, end);
                if (expr) {
                    Item expr_item = {.item = (uint64_t)expr};
                    input_add_attribute_item_to_element(input, element, attr_name->chars, expr_item);
                }
            } else {
                // String attribute value
                String* attr_value = parse_jsx_attribute_value(input, jsx, end);
                if (attr_value) {
                    input_add_attribute_to_element(input, element, attr_name->chars, attr_value->chars);
                }
            }
        } else {
            // Boolean attribute (no value)
            input_add_attribute_to_element(input, element, attr_name->chars, "true");
        }
    }
}

// Parse JSX fragment: <>...</>
static Element* parse_jsx_fragment(Input* input, const char** jsx, const char* end) {
    if (*jsx + 1 >= end || **jsx != '<' || *(*jsx + 1) != '>') {
        return NULL;
    }
    
    *jsx += 2; // Skip <>
    
    Element* fragment = input_create_element(input, "jsx_fragment");
    input_add_attribute_to_element(input, fragment, "type", "jsx_fragment");
    
    // Parse children until </>
    while (*jsx < end) {
        skip_jsx_whitespace(jsx, end);
        
        if (*jsx + 2 < end && strncmp(*jsx, "</>", 3) == 0) {
            *jsx += 3; // Skip </>
            break;
        }
        
        if (*jsx >= end) break;
        
        if (**jsx == '<') {
            Element* child = parse_jsx_element(input, jsx, end);
            if (child) {
                Item child_item = {.item = (uint64_t)child};
                list_push((List*)fragment, child_item);
            }
        } else if (**jsx == '{') {
            Element* expr = parse_jsx_expression(input, jsx, end);
            if (expr) {
                Item expr_item = {.item = (uint64_t)expr};
                list_push((List*)fragment, expr_item);
            }
        } else {
            String* text = parse_jsx_text_content(input, jsx, end);
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
                    list_push((List*)fragment, text_item);
                }
            }
        }
    }
    
    return fragment;
}

// Parse JSX element: <tag>...</tag> or <tag />
static Element* parse_jsx_element(Input* input, const char** jsx, const char* end) {
    if (*jsx >= end || **jsx != '<') {
        return NULL;
    }
    
    // Check for fragment
    if (*jsx + 1 < end && *(*jsx + 1) == '>') {
        return parse_jsx_fragment(input, jsx, end);
    }
    
    (*jsx)++; // Skip <
    
    // Parse tag name
    String* tag_name = parse_jsx_tag_name(input, jsx, end);
    if (!tag_name) {
        return NULL;
    }
    
    Element* element = input_create_element(input, tag_name->chars);
    input_add_attribute_to_element(input, element, "type", "jsx_element");
    
    // Mark as component if starts with uppercase
    if (is_jsx_component_name(tag_name->chars)) {
        input_add_attribute_to_element(input, element, "is_component", "true");
    }
    
    // Parse attributes
    parse_jsx_attributes(input, element, jsx, end);
    
    skip_jsx_whitespace(jsx, end);
    
    // Check for self-closing tag
    if (*jsx < end && **jsx == '/') {
        (*jsx)++; // Skip /
        skip_jsx_whitespace(jsx, end);
        if (*jsx < end && **jsx == '>') {
            (*jsx)++; // Skip >
            input_add_attribute_to_element(input, element, "self_closing", "true");
            return element;
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
            Element* child = parse_jsx_element(input, jsx, end);
            if (child) {
                Item child_item = {.item = (uint64_t)child};
                list_push((List*)element, child_item);
            }
        } else if (**jsx == '{') {
            Element* expr = parse_jsx_expression(input, jsx, end);
            if (expr) {
                Item expr_item = {.item = (uint64_t)expr};
                list_push((List*)element, expr_item);
            }
        } else {
            String* text = parse_jsx_text_content(input, jsx, end);
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
                    list_push((List*)element, text_item);
                }
            }
        }
    }
    
    return element;
}

// Main JSX parser entry point
Item input_jsx(Input* input, const char* jsx_string) {
    if (!input || !jsx_string) return {.item = ITEM_NULL};
    
    const char* jsx = jsx_string;
    const char* end = jsx_string + strlen(jsx_string);
    
    // Skip any leading whitespace
    skip_jsx_whitespace(&jsx, end);
    
    // Parse the root JSX element
    if (jsx < end) {
        Element* root = parse_jsx_element(input, &jsx, end);
        if (root) {
            return {.item = (uint64_t)root};
        }
    }
    
    return {.item = ITEM_NULL};
}

// Main entry point for JSX parsing (called from input.cpp)
void parse_jsx(Input* input, const char* jsx_string) {
    if (!input || !jsx_string) return;
    
    input->root = input_jsx(input, jsx_string);
}
