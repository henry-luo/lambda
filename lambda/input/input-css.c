#include "input.h"

// Forward declarations for CSS stylesheet parsing
static Item parse_css_stylesheet(Input *input, const char **css);
static Array* parse_css_rules(Input *input, const char **css);
static Item parse_css_rule(Input *input, const char **css);
static Item parse_css_at_rule(Input *input, const char **css);
static Item parse_css_qualified_rule(Input *input, const char **css);
static Array* parse_css_selectors(Input *input, const char **css);
static Item parse_css_selector(Input *input, const char **css);
static Array* parse_css_declarations(Input *input, const char **css);
static Item parse_css_declaration(Input *input, const char **css);
static Item parse_css_value(Input *input, const char **css);
static Item parse_css_function(Input *input, const char **css);
static Item parse_css_measure(Input *input, const char **css);
static Item parse_css_color(Input *input, const char **css);
static Item parse_css_string(Input *input, const char **css);
static Item parse_css_url(Input *input, const char **css);
static Item parse_css_number(Input *input, const char **css);
static Item parse_css_identifier(Input *input, const char **css);
static Array* parse_css_value_list(Input *input, const char **css);
static Array* parse_css_function_params(Input *input, const char **css);
static Item flatten_single_array(Array* arr);

static void skip_css_whitespace(const char **css) {
    while (**css && (**css == ' ' || **css == '\n' || **css == '\r' || **css == '\t')) {
        (*css)++;
    }
}

static void skip_css_comments(const char **css) {
    skip_css_whitespace(css);
    while (**css == '/' && *(*css + 1) == '*') {
        *css += 2; // Skip /*
        while (**css && !(**css == '*' && *(*css + 1) == '/')) {
            (*css)++;
        }
        if (**css == '*' && *(*css + 1) == '/') {
            *css += 2; // Skip */
        }
        skip_css_whitespace(css);
    }
}

static bool is_css_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static bool is_css_identifier_char(char c) {
    return is_css_identifier_start(c) || (c >= '0' && c <= '9');
}

static bool is_css_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_css_hex_digit(char c) {
    return is_css_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// CSS Stylesheet parsing functions
static Item parse_css_stylesheet(Input *input, const char **css) {
    Element* stylesheet = input_create_element(input, "stylesheet");
    if (!stylesheet) return ITEM_ERROR;
    
    skip_css_comments(css);
    
    Array* rules = parse_css_rules(input, css);
    if (rules) {
        input_add_attribute_item_to_element(input, stylesheet, "rules", (Item)rules);
    }
    
    return (Item)stylesheet;
}

static Array* parse_css_rules(Input *input, const char **css) {
    Array* rules = array_pooled(input->pool);
    if (!rules) return NULL;
    
    while (**css) {
        skip_css_comments(css);
        if (!**css) break;
        
        printf("Parsing CSS rule\n");
        Item rule = parse_css_rule(input, css);
        if (rule != ITEM_ERROR) {
            LambdaItem item = {.item = rule};
            array_append(rules, item, input->pool);
        } else {
            // Skip invalid rule content until next rule or end
            while (**css && **css != '}' && **css != '@') {
                (*css)++;
            }
            if (**css == '}') {
                (*css)++;
            }
        }
        
        skip_css_comments(css);
    }
    
    return rules;
}

static Item parse_css_rule(Input *input, const char **css) {
    skip_css_comments(css);
    
    if (**css == '@') {
        return parse_css_at_rule(input, css);
    } else {
        return parse_css_qualified_rule(input, css);
    }
}

static Item parse_css_at_rule(Input *input, const char **css) {
    if (**css != '@') return ITEM_ERROR;
    
    (*css)++; // Skip @
    
    // Parse at-rule name
    StrBuf* sb = input->sb;
    while (is_css_identifier_char(**css)) {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    String* at_rule_name = strbuf_to_string(sb);
    if (!at_rule_name) return ITEM_ERROR;
    
    Element* at_rule = input_create_element(input, "at-rule");
    if (!at_rule) return ITEM_ERROR;
    
    input_add_attribute_to_element(input, at_rule, "name", at_rule_name->chars);
    
    skip_css_comments(css);
    
    // Parse at-rule prelude (everything before { or ;)
    int brace_depth = 0;
    
    while (**css && (**css != '{' && **css != ';')) {
        if (**css == '(') brace_depth++;
        else if (**css == ')') brace_depth--;
        
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    String* prelude_str = strbuf_to_string(sb);
    if (prelude_str && prelude_str->len > 0) {
        char* trimmed = input_trim_whitespace(prelude_str->chars);
        if (trimmed && strlen(trimmed) > 0) {
            input_add_attribute_to_element(input, at_rule, "prelude", trimmed);
        }
        if (trimmed) free(trimmed);
    }
    
    skip_css_comments(css);
    
    if (**css == '{') {
        (*css)++; // Skip opening brace
        
        // Parse nested rules or declarations
        if (strcmp(at_rule_name->chars, "media") == 0 || 
            strcmp(at_rule_name->chars, "supports") == 0 ||
            strcmp(at_rule_name->chars, "document") == 0) {
            // These at-rules contain nested rules
            Array* nested_rules = parse_css_rules(input, css);
            if (nested_rules) {
                input_add_attribute_item_to_element(input, at_rule, "rules", (Item)nested_rules);
            }
        } else {
            // Other at-rules contain declarations - parse them directly as properties
            while (**css && **css != '}') {
                skip_css_comments(css);
                if (**css == '}') break;
                
                // Parse property name
                StrBuf* sb = input->sb;
                while (**css && **css != ':' && **css != ';' && **css != '}' && !isspace(**css)) {
                    strbuf_append_char(sb, **css);
                    (*css)++;
                }
                String* property_str = strbuf_to_string(sb);
                if (!property_str) {
                    // Skip to next declaration
                    while (**css && **css != ';' && **css != '}') (*css)++;
                    if (**css == ';') (*css)++;
                    continue;
                }
                
                skip_css_comments(css);
                
                if (**css == ':') {
                    (*css)++; // Skip colon
                    skip_css_comments(css);
                    
                    // Parse value list
                    Array* values = parse_css_value_list(input, css);
                    if (values) {
                        // Flatten single property value array
                        Item values_item = flatten_single_array(values);
                        input_add_attribute_item_to_element(input, at_rule, property_str->chars, values_item);
                    }                    
                    // Check for !important
                    skip_css_comments(css);
                    if (**css == '!' && strncmp(*css, "!important", 10) == 0) {
                        *css += 10;
                        // Could add importance as property_name + "_important" if needed
                    }
                }
                
                skip_css_comments(css);
                if (**css == ';') {
                    (*css)++; // Skip semicolon
                    skip_css_comments(css);
                }
            }
        }
        
        skip_css_comments(css);
        if (**css == '}') {
            (*css)++; // Skip closing brace
        }
    } else if (**css == ';') {
        (*css)++; // Skip semicolon
    }
    
    return (Item)at_rule;
}

static Item parse_css_qualified_rule(Input *input, const char **css) {
    Element* rule = input_create_element(input, "rule");
    if (!rule) return ITEM_ERROR;
    
    // Parse selectors
    printf("Parsing CSS qualified rule\n");
    Array* selectors = parse_css_selectors(input, css);
    if (selectors) {
        // Flatten single selector array
        Item selectors_item = flatten_single_array(selectors);
        input_add_attribute_item_to_element(input, rule, "_", selectors_item);
    }
    
    skip_css_comments(css);
    
    if (**css == '{') {
        (*css)++; // Skip opening brace
        
        // Parse declarations and add them directly as properties of the rule
        while (**css && **css != '}') {
            skip_css_comments(css);
            if (**css == '}') break;
            
            // Parse property name
            StrBuf* sb = input->sb;
            while (**css && **css != ':' && **css != ';' && **css != '}' && !isspace(**css)) {
                strbuf_append_char(sb, **css);
                (*css)++;
            }
            String* property_str = strbuf_to_string(sb);
            printf("got CSS property: %s\n", property_str ? property_str->chars : "NULL");
            if (!property_str) {
                // Skip to next declaration
                while (**css && **css != ';' && **css != '}') (*css)++;
                if (**css == ';') (*css)++;
                continue;
            }
            
            skip_css_comments(css);
            if (**css == ':') {
                (*css)++; // Skip colon
                skip_css_comments(css);
                
                // Parse value list
                Array* values = parse_css_value_list(input, css);
                if (values) {
                    // Flatten single property value array
                    Item values_item = flatten_single_array(values);
                    printf("Adding property %s with values %p\n", property_str->chars, (void*)values_item);
                    input_add_attribute_item_to_element(input, rule, property_str->chars, values_item);
                }
                
                // Check for !important (for now, we'll ignore it in the flattened structure)
                skip_css_comments(css);
                if (**css == '!' && strncmp(*css, "!important", 10) == 0) {
                    *css += 10;
                    // Could add importance as property_name + "_important" if needed
                }
            }
            
            skip_css_comments(css);
            if (**css == ';') {
                (*css)++; // Skip semicolon
                skip_css_comments(css);
            }
        }
        
        skip_css_comments(css);
        if (**css == '}') {
            (*css)++; // Skip closing brace
        }
    }
    
    return (Item)rule;
}

static Array* parse_css_selectors(Input *input, const char **css) {
    Array* selectors = array_pooled(input->pool);
    if (!selectors) return NULL;
    
    while (**css && **css != '{') {
        skip_css_comments(css);
        if (**css == '{') break;
        
        Item selector = parse_css_selector(input, css);
        if (selector != ITEM_ERROR) {
            LambdaItem item = {.item = selector};
            array_append(selectors, item, input->pool);
        }
        
        skip_css_comments(css);
        if (**css == ',') {
            (*css)++; // Skip comma separator
            skip_css_comments(css);
        } else if (**css != '{') {
            break;
        }
    }
    
    return selectors;
}

static Item parse_css_selector(Input *input, const char **css) {
    StrBuf* sb = input->sb;
    
    // Parse selector text until comma or opening brace
    while (**css && **css != ',' && **css != '{') {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    String* selector_str = strbuf_to_string(sb);
    if (selector_str) {
        char* trimmed = input_trim_whitespace(selector_str->chars);
        if (trimmed) {
            String* trimmed_str = input_create_string(input, trimmed);
            free(trimmed);
            return trimmed_str ? s2it(trimmed_str) : ITEM_ERROR;
        }
    }
    
    return ITEM_ERROR;
}

static Array* parse_css_declarations(Input *input, const char **css) {
    Array* declarations = array_pooled(input->pool);
    if (!declarations) return NULL;
    
    while (**css && **css != '}') {
        skip_css_comments(css);
        if (**css == '}') break;
        
        Item declaration = parse_css_declaration(input, css);
        if (declaration != ITEM_ERROR) {
            LambdaItem item = {.item = declaration};
            array_append(declarations, item, input->pool);
        }
        
        skip_css_comments(css);
        if (**css == ';') {
            (*css)++; // Skip semicolon
            skip_css_comments(css);
        }
    }
    
    return declarations;
}

static Item parse_css_declaration(Input *input, const char **css) {
    printf("Parsing CSS declaration\n");
    skip_css_comments(css);
    
    // Parse property name
    StrBuf* sb = input->sb;
    while (**css && **css != ':' && **css != ';' && **css != '}') {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    String* property_str = strbuf_to_string(sb);
    if (!property_str) return ITEM_ERROR;
    printf("Parsing CSS property: %s\n", property_str->chars);
    
    char* property_trimmed = input_trim_whitespace(property_str->chars);
    if (!property_trimmed || strlen(property_trimmed) == 0) {
        if (property_trimmed) free(property_trimmed);
        return ITEM_ERROR;
    }
    
    Element* declaration = input_create_element(input, "declaration");
    if (!declaration) {
        free(property_trimmed);
        return ITEM_ERROR;
    }
    
    input_add_attribute_to_element(input, declaration, "property", property_trimmed);
    free(property_trimmed);
    
    skip_css_comments(css);
    
    if (**css == ':') {
        (*css)++; // Skip colon
        skip_css_comments(css);
        
        // Parse value
        Array* values = parse_css_value_list(input, css);
        if (values) {
            input_add_attribute_item_to_element(input, declaration, "values", (Item)values);
        }
        
        // Check for !important
        skip_css_comments(css);
        if (**css == '!' && strncmp(*css, "!important", 10) == 0) {
            *css += 10;
            input_add_attribute_to_element(input, declaration, "important", "true");
        }
    }
    
    return (Item)declaration;
}

static Item parse_css_string(Input *input, const char **css) {
    printf("Parsing CSS string\n");
    char quote = **css;
    if (quote != '"' && quote != '\'') return ITEM_ERROR;
    
    StrBuf* sb = input->sb;
    (*css)++; // Skip opening quote
    
    while (**css && **css != quote) {
        if (**css == '\\') {
            (*css)++;
            switch (**css) {
                case '"': strbuf_append_char(sb, '"'); break;
                case '\'': strbuf_append_char(sb, '\''); break;
                case '\\': strbuf_append_char(sb, '\\'); break;
                case '/': strbuf_append_char(sb, '/'); break;
                case 'n': strbuf_append_char(sb, '\n'); break;
                case 'r': strbuf_append_char(sb, '\r'); break;
                case 't': strbuf_append_char(sb, '\t'); break;
                case 'f': strbuf_append_char(sb, '\f'); break;
                default: 
                    // Handle hex escapes like \A0
                    if (is_css_hex_digit(**css)) {
                        char hex[7] = {0};
                        int hex_len = 0;
                        while (hex_len < 6 && is_css_hex_digit(**css)) {
                            hex[hex_len++] = **css;
                            (*css)++;
                        }
                        (*css)--; // Back up one since we'll advance at end of loop
                        int codepoint = (int)strtol(hex, NULL, 16);
                        // Simple UTF-8 encoding for basic cases
                        if (codepoint < 0x80) {
                            strbuf_append_char(sb, (char)codepoint);
                        } else if (codepoint < 0x800) {
                            strbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                            strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        } else {
                            strbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                            strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                            strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        }
                    } else {
                        strbuf_append_char(sb, **css);
                    }
                    break;
            }
        } else {
            strbuf_append_char(sb, **css);
        }
        (*css)++;
    }
    
    if (**css == quote) {
        (*css)++; // Skip closing quote
    }
    
    String* str = strbuf_to_string(sb);
    printf("Parsed CSS string: %s\n", str ? str->chars : "NULL");
    return str ? s2it(str) : ITEM_ERROR;
}

static Item parse_css_url(Input *input, const char **css) {
    if (strncmp(*css, "url(", 4) != 0) return ITEM_ERROR;
    
    *css += 4; // Skip "url("
    skip_css_whitespace(css);
    
    Item url_value = ITEM_ERROR;
    
    // Parse URL - can be quoted or unquoted
    if (**css == '"' || **css == '\'') {
        url_value = parse_css_string(input, css);
    } else {
        // Unquoted URL
        StrBuf* sb = input->sb;
        while (**css && **css != ')' && !(**css == ' ' || **css == '\t' || **css == '\n' || **css == '\r')) {
            if (**css == '\\') {
                (*css)++;
                if (**css) {
                    strbuf_append_char(sb, **css);
                }
            } else {
                strbuf_append_char(sb, **css);
            }
            (*css)++;
        }
        String* str = strbuf_to_string(sb);
        url_value = str ? s2it(str) : ITEM_ERROR;
    }
    
    skip_css_whitespace(css);
    if (**css == ')') {
        (*css)++; // Skip closing parenthesis
    }
    
    // Create url element with the URL as content
    Element* url_element = input_create_element(input, "url");
    if (url_element && url_value != ITEM_ERROR) {
        // Add URL as content - for now just store as attribute
        input_add_attribute_item_to_element(input, url_element, "href", url_value);
    }
    
    return url_element ? (Item)url_element : ITEM_ERROR;
}

static Item parse_css_color(Input *input, const char **css) {
    StrBuf* sb = input->sb;
    
    if (**css == '#') {
        // Hex color
        strbuf_append_char(sb, **css);
        (*css)++;
        
        int hex_count = 0;
        while (**css && is_css_hex_digit(**css) && hex_count < 8) {
            strbuf_append_char(sb, **css);
            (*css)++;
            hex_count++;
        }
        
        // Valid hex colors are 3, 4, 6, or 8 digits
        if (hex_count == 3 || hex_count == 4 || hex_count == 6 || hex_count == 8) {
            String* color_str = strbuf_to_string(sb);
            return color_str ? s2it(color_str) : ITEM_ERROR;
        }
        // Clear buffer even on error path
        strbuf_to_string(sb);
        return ITEM_ERROR;
    }
    
    return ITEM_ERROR;
}

static Item parse_css_number(Input *input, const char **css) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
    
    char* end;
    *dval = strtod(*css, &end);
    *css = end;
    
    return d2it(dval);
}

static Item parse_css_measure(Input *input, const char **css) {
    StrBuf* sb = input->sb;
    const char* start = *css;
    
    // Parse number part
    if (**css == '+' || **css == '-') {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    bool has_digits = false;
    while (is_css_digit(**css)) {
        strbuf_append_char(sb, **css);
        (*css)++;
        has_digits = true;
    }
    
    if (**css == '.') {
        strbuf_append_char(sb, **css);
        (*css)++;
        while (is_css_digit(**css)) {
            strbuf_append_char(sb, **css);
            (*css)++;
            has_digits = true;
        }
    }
    
    if (!has_digits) {
        // Clear buffer before returning error
        strbuf_to_string(sb);
        *css = start; // Reset
        return ITEM_ERROR;
    }
    
    // Parse unit part
    const char* unit_start = *css;
    if (**css == '%') {
        strbuf_append_char(sb, **css);
        (*css)++;
    } else if (is_css_identifier_start(**css)) {
        while (is_css_identifier_char(**css)) {
            strbuf_append_char(sb, **css);
            (*css)++;
        }
    }
    
    // If we have a unit, treat as symbol (measure), otherwise as number
    String* measure_str = strbuf_to_string(sb);
    if (*css > unit_start) {
        return measure_str ? s2it(measure_str) : ITEM_ERROR;
    } else {
        // Reset and parse as number only
        *css = start;
        return parse_css_number(input, css);
    }
}

static Item parse_css_identifier(Input *input, const char **css) {
    if (!is_css_identifier_start(**css)) return ITEM_ERROR;
    
    StrBuf* sb = input->sb;
    while (is_css_identifier_char(**css)) {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    String* id_str = strbuf_to_string(sb);
    if (!id_str) {
        return ITEM_ERROR;
    }
    
    // Convert CSS keyword values to Lambda symbols using y2it()
    // CSS identifiers like 'flex', 'red', etc. should be symbols, not strings
    return y2it(id_str);
}

static Array* parse_css_function_params(Input *input, const char **css) {
    Array* params = array_pooled(input->pool);
    if (!params) return NULL;
    
    skip_css_comments(css);
    
    if (**css == ')') {
        return params; // Empty parameter list
    }
    
    while (**css && **css != ')') {
        skip_css_comments(css);
        if (**css == ')') break;
        
        const char* start_pos = *css; // Track position to detect infinite loops
        
        Item param = parse_css_value(input, css);
        if (param != ITEM_ERROR) {
            LambdaItem item = {.item = param};
            array_append(params, item, input->pool);
        } else {
            // If we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (*css == start_pos) {
                (*css)++;
            }
        }
        
        skip_css_comments(css);
        
        // Handle parameter separators
        if (**css == ',') {
            (*css)++; // Skip comma
            skip_css_comments(css);
        } else if (**css == ')') {
            // End of parameters
            break;
        } else if (**css != ')') {
            // Skip any whitespace between parameters (space-separated values)
            skip_css_whitespace(css);
            
            // If we're still not at a separator or end, this might be a parsing error
            // Skip to next comma or closing paren to recover
            if (**css && **css != ',' && **css != ')') {
                while (**css && **css != ',' && **css != ')') {
                    (*css)++;
                }
            }
        }
    }
    
    return params;
}

// Helper function to flatten single-element arrays
static Item flatten_single_array(Array* arr) {
    if (!arr) {
        return ITEM_ERROR;
    }
    
    if (arr->length != 1) {
        // Return array as-is if not single element
        return (Item)arr;
    }
    
    // For single-element arrays, return the single item directly
    // Use array_get to safely access the item
    return list_get((List*)arr, 0);
}

static Item parse_css_function(Input *input, const char **css) {
    // Parse function name
    if (!is_css_identifier_start(**css)) return ITEM_ERROR;
    
    StrBuf* sb = input->sb;
    while (is_css_identifier_char(**css)) {
        strbuf_append_char(sb, **css);
        (*css)++;
    }
    
    skip_css_comments(css);
    if (**css != '(') {
        // Not a function, treat as identifier (symbol)
        String* id_str = strbuf_to_string(sb);
        return id_str ? y2it(id_str) : ITEM_ERROR;
    }
    
    String* func_name = strbuf_to_string(sb);
    if (!func_name) return ITEM_ERROR;
    
    printf("Parsing CSS function: %s\n", func_name->chars);
    
    (*css)++; // Skip opening parenthesis
    
    // Parse function parameters
    Array* params = parse_css_function_params(input, css);
    
    if (**css == ')') {
        (*css)++; // Skip closing parenthesis
    }
    
    // Create Lambda element with function name as element name
    Element* func_element = input_create_element(input, func_name->chars);
    if (!func_element) return ITEM_ERROR;
    
    // Add parameters as child content using list_push
    if (params && params->length > 0) {
        for (long i = 0; i < params->length; i++) {
            Item param = list_get((List*)params, i);
            list_push((List*)func_element, param);
        }
    }
    
    printf("Created function element '%s' with %ld parameters\n", func_name->chars, params ? params->length : 0);
    return (Item)func_element;
}

static Array* parse_css_value_list(Input *input, const char **css) {
    Array* values = array_pooled(input->pool);
    if (!values) return NULL;
    
    while (**css && **css != ';' && **css != '}' && **css != '!') {
        skip_css_comments(css);
        if (!**css || **css == ';' || **css == '}' || **css == '!') break;
        
        const char* start_pos = *css; // Track position to detect infinite loops
        
        Item value = parse_css_value(input, css);
        if (value != ITEM_ERROR) {
            LambdaItem item = {.item = value};
            array_append(values, item, input->pool);
        } else {
            // If we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (*css == start_pos) {
                (*css)++;
                continue;
            }
        }
        
        skip_css_comments(css);
        
        // Check for value separators
        if (**css == ',') {
            (*css)++;
            skip_css_comments(css);
        } else if (**css == ' ' || **css == '\t') {
            skip_css_whitespace(css);
            // Space-separated values continue
        } else {
            break; // End of value list
        }
    }
    
    return values;
}

static Item parse_css_value(Input *input, const char **css) {
    skip_css_comments(css);
    
    if (!**css) return ITEM_ERROR;
    
    printf("Parsing CSS value starting with: %.10s\n", *css);
    
    // Try to parse different CSS value types
    switch (**css) {
        case '"':
        case '\'':
            printf("Parsing string value\n");
            return parse_css_string(input, css);
            
        case '#':
            printf("Parsing color value\n");
            return parse_css_color(input, css);
            
        case '+':
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '.':
            printf("Parsing number/measure value\n");
            return parse_css_measure(input, css);
            
        default:
            if (**css == 'u' && strncmp(*css, "url(", 4) == 0) {
                printf("Parsing URL value\n");
                return parse_css_url(input, css);
            } else if (is_css_identifier_start(**css)) {
                // Look ahead to see if this is a function
                const char* lookahead = *css;
                while (is_css_identifier_char(*lookahead)) {
                    lookahead++;
                }
                skip_css_whitespace(&lookahead);
                if (*lookahead == '(') {
                    printf("Parsing function value\n");
                    return parse_css_function(input, css);
                } else {
                    printf("Parsing identifier value\n");
                    return parse_css_identifier(input, css);
                }
            }
            printf("Unknown value type, returning error\n");
            return ITEM_ERROR;
    }
}

void parse_css(Input* input, const char* css_string) {
    printf("css_parse (stylesheet)\n");
    input->sb = strbuf_new_pooled(input->pool);
    
    const char* css = css_string;
    skip_css_comments(&css);
    
    // Parse as complete CSS stylesheet
    if (*css) {
        input->root = parse_css_stylesheet(input, &css);
    } else {
        // Empty stylesheet
        Element* empty_stylesheet = input_create_element(input, "stylesheet");
        Array* empty_rules = array_pooled(input->pool);
        if (empty_stylesheet && empty_rules) {
            input_add_attribute_item_to_element(input, empty_stylesheet, "rules", (Item)empty_rules);
            input->root = (Item)empty_stylesheet;
        } else {
            input->root = ITEM_ERROR;
        }
    }
}
