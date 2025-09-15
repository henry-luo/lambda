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

// Global array to collect all rules including nested ones
static Array* g_all_rules = NULL;

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
    if (!stylesheet) return {.item = ITEM_ERROR};
    
    skip_css_comments(css);
    
    // Create separate collections for different rule types
    Array* rules = array_pooled(input->pool);          // Regular CSS rules
    Array* keyframes = array_pooled(input->pool);      // @keyframes rules
    Array* media_queries = array_pooled(input->pool);  // @media rules
    Array* supports_queries = array_pooled(input->pool); // @supports rules
    Array* font_faces = array_pooled(input->pool);     // @font-face rules
    Array* other_at_rules = array_pooled(input->pool); // Other at-rules
    
    // Initialize global array to collect ALL rules including nested ones
    g_all_rules = rules;
    
    if (!rules || !keyframes || !media_queries || !supports_queries || !font_faces || !other_at_rules) {
        return {.item = ITEM_ERROR};
    }
    
    // Parse all rules and categorize them
    while (**css) {
        skip_css_comments(css);
        if (!**css) break;
        
        printf("Parsing CSS rule\n");
        
        // Check if this is an at-rule before parsing
        const char* rule_start = *css;
        bool is_at_rule = (**css == '@');
        const char* at_rule_name = NULL;
        
        if (is_at_rule) {
            // Extract at-rule name for categorization
            const char* name_start = *css + 1; // Skip @
            const char* name_end = name_start;
            while (*name_end && is_css_identifier_char(*name_end)) {
                name_end++;
            }
            
            if (name_end > name_start) {
                size_t name_len = name_end - name_start;
                char* name_buf = (char*)malloc(name_len + 1);
                if (name_buf) {
                    strncpy(name_buf, name_start, name_len);
                    name_buf[name_len] = '\0';
                    at_rule_name = name_buf;
                }
            }
        }
        
        Item rule = parse_css_rule(input, css);
        if (rule .item != ITEM_ERROR) {
            
            if (is_at_rule && at_rule_name) {
                // Categorize at-rules
                if (strcmp(at_rule_name, "keyframes") == 0) {
                    array_append(keyframes, rule, input->pool);
                } else if (strcmp(at_rule_name, "media") == 0) {
                    array_append(media_queries, rule, input->pool);
                } else if (strcmp(at_rule_name, "supports") == 0) {
                    array_append(supports_queries, rule, input->pool);
                } else if (strcmp(at_rule_name, "font-face") == 0) {
                    array_append(font_faces, rule, input->pool);
                } else {
                    array_append(other_at_rules, rule, input->pool);
                }
                free((void*)at_rule_name); // Free the allocated name buffer
            } else {
                // Regular CSS rule
                array_append(rules, rule, input->pool);
            }
        } else {
            // Skip invalid rule content until next rule or end
            while (**css && **css != '}' && **css != '@') {
                (*css)++;
            }
            if (**css == '}') {
                (*css)++;
            }
            
            if (at_rule_name) {
                free((void*)at_rule_name);
            }
        }
        
        skip_css_comments(css);
    }
    
    // Add all collections to the stylesheet
    input_add_attribute_item_to_element(input, stylesheet, "rules", {.item = (uint64_t)rules});
    if (keyframes->length > 0) {
        input_add_attribute_item_to_element(input, stylesheet, "keyframes", {.item = (uint64_t)keyframes});
    }
    if (media_queries->length > 0) {
        input_add_attribute_item_to_element(input, stylesheet, "media", {.item = (uint64_t)media_queries});
    }
    if (supports_queries->length > 0) {
        input_add_attribute_item_to_element(input, stylesheet, "supports", {.item = (uint64_t)supports_queries});
    }
    if (font_faces->length > 0) {
        input_add_attribute_item_to_element(input, stylesheet, "font_faces", {.item = (uint64_t)font_faces});
    }
    if (other_at_rules->length > 0) {
        input_add_attribute_item_to_element(input, stylesheet, "at_rules", {.item = (uint64_t)other_at_rules});
    }
    
    return {.item = (uint64_t)stylesheet};
}

static Array* parse_css_rules(Input *input, const char **css) {
    Array* rules = array_pooled(input->pool);
    if (!rules) return NULL;
    
    while (**css) {
        skip_css_comments(css);
        if (!**css) break;
        
        printf("Parsing CSS rule\n");
        Item rule = parse_css_rule(input, css);
        if (rule .item != ITEM_ERROR) {
            array_append(rules, rule, input->pool);
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
    if (**css != '@') return {.item = ITEM_ERROR};
    
    (*css)++; // Skip @
    
    // Parse at-rule name
    StringBuf* sb = input->sb;
    while (is_css_identifier_char(**css)) {
        stringbuf_append_char(sb, **css);
        (*css)++;
    }
    
    String* at_rule_name = stringbuf_to_string(sb);
    if (!at_rule_name) return {.item = ITEM_ERROR};
    
    Element* at_rule = input_create_element(input, "at-rule");
    if (!at_rule) return {.item = ITEM_ERROR};
    
    input_add_attribute_to_element(input, at_rule, "name", at_rule_name->chars);
    
    skip_css_comments(css);
    
    // Parse at-rule prelude (everything before { or ;)
    StringBuf* prelude_sb = stringbuf_new(input->pool);
    int paren_depth = 0;
    
    while (**css && (**css != '{' && **css != ';')) {
        char c = **css;
        if (c == '(') paren_depth++;
        else if (c == ')') paren_depth--;
        
        // Don't break on braces inside parentheses
        if (c == '{' && paren_depth > 0) {
            stringbuf_append_char(prelude_sb, c);
            (*css)++;
            continue;
        }
        
        if (c == '{' || c == ';') break;
        
        stringbuf_append_char(prelude_sb, c);
        (*css)++;
    }
    
    String* prelude_str = stringbuf_to_string(prelude_sb);
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
            strcmp(at_rule_name->chars, "document") == 0 ||
            strcmp(at_rule_name->chars, "container") == 0) {
            // These at-rules contain nested rules
            Array* nested_rules = array_pooled(input->pool);
            if (nested_rules) {
                // Parse nested CSS rules until we hit the closing brace
                while (**css && **css != '}') {
                    skip_css_comments(css);
                    if (**css == '}') break;
                    
                    Item nested_rule = parse_css_rule(input, css);
                    if (nested_rule .item != ITEM_ERROR) {
                        array_append(nested_rules, nested_rule, input->pool);
                        
                        // Also add nested rule to global rules array
                        if (g_all_rules) {
                            array_append(g_all_rules, nested_rule, input->pool);
                            printf("DEBUG: Added nested rule to global rules array\n");
                        }
                    } else {
                        // Skip malformed rule
                        while (**css && **css != '}' && **css != '{') {
                            (*css)++;
                        }
                        if (**css == '{') {
                            // Skip entire block
                            int brace_count = 1;
                            (*css)++;
                            while (**css && brace_count > 0) {
                                if (**css == '{') brace_count++;
                                else if (**css == '}') brace_count--;
                                (*css)++;
                            }
                        }
                    }
                    skip_css_comments(css);
                }
                
                input_add_attribute_item_to_element(input, at_rule, "rules", {.item = (uint64_t)nested_rules});
            }
        } else if (strcmp(at_rule_name->chars, "keyframes") == 0) {
            // @keyframes contains keyframe rules
            Array* keyframe_rules = array_pooled(input->pool);
            if (keyframe_rules) {
                while (**css && **css != '}') {
                    skip_css_comments(css);
                    if (**css == '}') break;
                    
                    // Parse keyframe selector (0%, 50%, from, to, etc.)
                    StringBuf* keyframe_sb = stringbuf_new(input->pool);
                    while (**css && **css != '{' && **css != '}') {
                        stringbuf_append_char(keyframe_sb, **css);
                        (*css)++;
                    }
                    String* keyframe_selector = stringbuf_to_string(keyframe_sb);
                    
                    if (keyframe_selector && **css == '{') {
                        (*css)++; // Skip opening brace
                        
                        Element* keyframe_rule = input_create_element(input, "keyframe");
                        if (keyframe_rule) {
                            char* trimmed = input_trim_whitespace(keyframe_selector->chars);
                            if (trimmed) {
                                input_add_attribute_to_element(input, keyframe_rule, "selector", trimmed);
                                free(trimmed);
                            }
                            
                            // Parse declarations within keyframe
                            while (**css && **css != '}') {
                                skip_css_comments(css);
                                if (**css == '}') break;
                                
                                // Parse property name
                                StringBuf* prop_sb = stringbuf_new(input->pool);
                                while (**css && **css != ':' && **css != ';' && **css != '}' && !isspace(**css)) {
                                    stringbuf_append_char(prop_sb, **css);
                                    (*css)++;
                                }
                                String* property_str = stringbuf_to_string(prop_sb);
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
                                        Item values_item = flatten_single_array(values);
                                        input_add_attribute_item_to_element(input, keyframe_rule, property_str->chars, values_item);
                                    }
                                    
                                    // Check for !important
                                    skip_css_comments(css);
                                    if (**css == '!' && strncmp(*css, "!important", 10) == 0) {
                                        *css += 10;
                                    }
                                }
                                
                                skip_css_comments(css);
                                if (**css == ';') {
                                    (*css)++; // Skip semicolon
                                    skip_css_comments(css);
                                }
                            }
                            
                            if (**css == '}') {
                                (*css)++; // Skip closing brace
                            }
                            
                            array_append(keyframe_rules, {.item = (uint64_t)keyframe_rule}, input->pool);
                        }
                    }
                    
                    skip_css_comments(css);
                }
                
                input_add_attribute_item_to_element(input, at_rule, "keyframes", {.item = (uint64_t)keyframe_rules});
            }
        } else {
            // Other at-rules contain declarations - parse them directly as properties
            while (**css && **css != '}') {
                skip_css_comments(css);
                if (**css == '}') break;
                
                // Parse property name
                StringBuf* prop_sb = stringbuf_new(input->pool);
                while (**css && **css != ':' && **css != ';' && **css != '}' && !isspace(**css)) {
                    stringbuf_append_char(prop_sb, **css);
                    (*css)++;
                }
                String* property_str = stringbuf_to_string(prop_sb);
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
    
    return {.item = (uint64_t)at_rule};
}

static Item parse_css_qualified_rule(Input *input, const char **css) {
    Element* rule = input_create_element(input, "rule");
    if (!rule) return {.item = ITEM_ERROR};
    
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
            StringBuf* sb = input->sb;
            while (**css && **css != ':' && **css != ';' && **css != '}' && !isspace(**css)) {
                stringbuf_append_char(sb, **css);
                (*css)++;
            }
            String* property_str = stringbuf_to_string(sb);
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
                    printf("Adding property %s with values %p\n", property_str->chars, (void*)values_item.item);
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
    
    return {.item = (uint64_t)rule};
}

static Array* parse_css_selectors(Input *input, const char **css) {
    Array* selectors = array_pooled(input->pool);
    if (!selectors) return NULL;
    
    while (**css && **css != '{') {
        skip_css_comments(css);
        if (**css == '{') break;
        
        Item selector = parse_css_selector(input, css);
        if (selector .item != ITEM_ERROR) {
            array_append(selectors, selector, input->pool);
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

static Item parse_css_selector(Input* input, const char** css) {
    StringBuf* sb = input->sb;
    
    // Parse selector text until comma or opening brace
    // Handle complex selectors including attribute selectors, pseudo-classes, pseudo-elements
    int bracket_depth = 0;
    int paren_depth = 0;
    
    while (**css && **css != ',' && **css != '{') {
        char c = **css;
        
        // Track bracket and parenthesis depth for complex selectors
        if (c == '[') {
            bracket_depth++;
        } else if (c == ']') {
            bracket_depth--;
        } else if (c == '(') {
            paren_depth++;
        } else if (c == ')') {
            paren_depth--;
        }
        
        // Handle escaped characters in selectors
        if (c == '\\' && *(*css + 1)) {
            stringbuf_append_char(sb, c);
            (*css)++;
            stringbuf_append_char(sb, **css);
            (*css)++;
            continue;
        }
        
        // Don't break on comma or brace inside brackets or parentheses
        if ((c == ',' || c == '{') && (bracket_depth > 0 || paren_depth > 0)) {
            stringbuf_append_char(sb, c);
            (*css)++;
            continue;
        }
        
        if (c == ',' || c == '{') {
            break;
        }
        
        stringbuf_append_char(sb, c);
        (*css)++;
    }
    
    String* selector_str = stringbuf_to_string(sb);
    if (selector_str) {
        char* trimmed = input_trim_whitespace(selector_str->chars);
        if (trimmed) {
            String* trimmed_str = input_create_string(input, trimmed);
            free(trimmed);
            return trimmed_str ? (Item){.item = s2it(trimmed_str)} : (Item){.item = ITEM_ERROR};
        }
    }
    
    return {.item = ITEM_ERROR};
}

static Array* parse_css_declarations(Input *input, const char **css) {
    Array* declarations = array_pooled(input->pool);
    if (!declarations) return NULL;
    
    while (**css && **css != '}') {
        skip_css_comments(css);
        if (**css == '}') break;
        
        Item declaration = parse_css_declaration(input, css);
        if (declaration .item != ITEM_ERROR) {
            array_append(declarations, declaration, input->pool);
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
    StringBuf* sb = input->sb;
    while (**css && **css != ':' && **css != ';' && **css != '}') {
        stringbuf_append_char(sb, **css);
        (*css)++;
    }
    String* property_str = stringbuf_to_string(sb);
    if (!property_str) return {.item = ITEM_ERROR};
    printf("Parsing CSS property: %s\n", property_str->chars);
    
    char* property_trimmed = input_trim_whitespace(property_str->chars);
    if (!property_trimmed || strlen(property_trimmed) == 0) {
        if (property_trimmed) free(property_trimmed);
        return {.item = ITEM_ERROR};
    }
    
    Element* declaration = input_create_element(input, "declaration");
    if (!declaration) {
        free(property_trimmed);
        return {.item = ITEM_ERROR};
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
            input_add_attribute_item_to_element(input, declaration, "values", {.item = (uint64_t)values});
        }
        
        // Check for !important
        skip_css_comments(css);
        if (**css == '!' && strncmp(*css, "!important", 10) == 0) {
            *css += 10;
            input_add_attribute_to_element(input, declaration, "important", "true");
        }
    }
    
    return {.item = (uint64_t)declaration};
}

static Item parse_css_string(Input *input, const char **css) {
    printf("Parsing CSS string\n");
    char quote = **css;
    if (quote != '"' && quote != '\'') return {.item = ITEM_ERROR};
    
    StringBuf* sb = input->sb;
    (*css)++; // Skip opening quote
    
    while (**css && **css != quote) {
        if (**css == '\\') {
            (*css)++;
            switch (**css) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\'': stringbuf_append_char(sb, '\''); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case '/': stringbuf_append_char(sb, '/'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'f': stringbuf_append_char(sb, '\f'); break;
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
                            stringbuf_append_char(sb, (char)codepoint);
                        } else if (codepoint < 0x800) {
                            stringbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                            stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        } else {
                            stringbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                            stringbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                            stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        }
                    } else {
                        stringbuf_append_char(sb, **css);
                    }
                    break;
            }
        } else {
            stringbuf_append_char(sb, **css);
        }
        (*css)++;
    }
    
    if (**css == quote) {
        (*css)++; // Skip closing quote
    }
    
    String* str = stringbuf_to_string(sb);
    printf("Parsed CSS string: %s\n", str ? str->chars : "NULL");
    return str ? (Item){.item = s2it(str)} : (Item){.item = ITEM_ERROR};
}

static Item parse_css_url(Input *input, const char **css) {
    if (strncmp(*css, "url(", 4) != 0) return {.item = ITEM_ERROR};
    
    *css += 4; // Skip "url("
    skip_css_whitespace(css);
    
    Item url_value = {.item = ITEM_ERROR};
    
    // Parse URL - can be quoted or unquoted
    if (**css == '"' || **css == '\'') {
        url_value = parse_css_string(input, css);
    } else {
        // Unquoted URL
        StringBuf* sb = input->sb;
        while (**css && **css != ')' && !(**css == ' ' || **css == '\t' || **css == '\n' || **css == '\r')) {
            if (**css == '\\') {
                (*css)++;
                if (**css) {
                    stringbuf_append_char(sb, **css);
                }
            } else {
                stringbuf_append_char(sb, **css);
            }
            (*css)++;
        }
        String* str = stringbuf_to_string(sb);
        url_value = str ? (Item){.item = s2it(str)} : (Item){.item = ITEM_ERROR};
    }
    
    skip_css_whitespace(css);
    if (**css == ')') {
        (*css)++; // Skip closing parenthesis
    }
    
    // Create url element with the URL as content
    Element* url_element = input_create_element(input, "url");
    if (url_element && url_value .item != ITEM_ERROR) {
        // Add URL as content - for now just store as attribute
        input_add_attribute_item_to_element(input, url_element, "href", url_value);
    }
    
    return url_element ? (Item){.item = (uint64_t)url_element} : (Item){.item = ITEM_ERROR};
}

static Item parse_css_color(Input *input, const char **css) {
    StringBuf* sb = input->sb;
    
    if (**css == '#') {
        // Hex color
        stringbuf_append_char(sb, **css);
        (*css)++;
        
        int hex_count = 0;
        while (**css && is_css_hex_digit(**css) && hex_count < 8) {
            stringbuf_append_char(sb, **css);
            (*css)++;
            hex_count++;
        }
        
        // Valid hex colors are 3, 4, 6, or 8 digits
        if (hex_count == 3 || hex_count == 4 || hex_count == 6 || hex_count == 8) {
            String* color_str = stringbuf_to_string(sb);
            return color_str ? (Item){.item = s2it(color_str)} : (Item){.item = ITEM_ERROR};
        }
        // Clear buffer even on error path
        stringbuf_to_string(sb);
        return {.item = ITEM_ERROR};
    }
    
    // Check for CSS3 color functions (rgba, hsla, etc.)
    if (is_css_identifier_start(**css)) {
        const char* start = *css;
        
        // Check for color function names
        if (strncmp(*css, "rgba(", 5) == 0 ||
            strncmp(*css, "hsla(", 5) == 0 ||
            strncmp(*css, "rgb(", 4) == 0 ||
            strncmp(*css, "hsl(", 4) == 0) {
            // Parse as function
            return parse_css_function(input, css);
        }
        
        // Check for named colors
        while (is_css_identifier_char(**css)) {
            stringbuf_append_char(sb, **css);
            (*css)++;
        }
        
        String* color_name = stringbuf_to_string(sb);
        if (color_name) {
            // Common CSS color names - return as symbol
            const char* name = color_name->chars;
            if (strcmp(name, "red") == 0 || strcmp(name, "blue") == 0 || 
                strcmp(name, "green") == 0 || strcmp(name, "white") == 0 ||
                strcmp(name, "black") == 0 || strcmp(name, "yellow") == 0 ||
                strcmp(name, "transparent") == 0 || strcmp(name, "currentColor") == 0) {
                return (Item){.item = y2it(color_name)};
            }
        }
        
        // Reset if not a color
        *css = start;
        stringbuf_to_string(sb); // Clear buffer
    }
    
    return {.item = ITEM_ERROR};
}

static Item parse_css_number(Input *input, const char **css) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return {.item = ITEM_ERROR};
    
    char* end;
    *dval = strtod(*css, &end);
    *css = end;
    
    return {.item = d2it(dval)};
}

static Item parse_css_measure(Input *input, const char **css) {
    StringBuf* sb = input->sb;
    const char* start = *css;
    
    // Parse number part
    if (**css == '+' || **css == '-') {
        stringbuf_append_char(sb, **css);
        (*css)++;
    }
    
    bool has_digits = false;
    while (is_css_digit(**css)) {
        stringbuf_append_char(sb, **css);
        (*css)++;
        has_digits = true;
    }
    
    if (**css == '.') {
        stringbuf_append_char(sb, **css);
        (*css)++;
        while (is_css_digit(**css)) {
            stringbuf_append_char(sb, **css);
            (*css)++;
            has_digits = true;
        }
    }
    
    if (!has_digits) {
        // Clear buffer before returning error
        stringbuf_to_string(sb);
        *css = start; // Reset
        return {.item = ITEM_ERROR};
    }
    
    // Parse unit part
    const char* unit_start = *css;
    if (**css == '%') {
        stringbuf_append_char(sb, **css);
        (*css)++;
    } else if (is_css_identifier_start(**css)) {
        // Handle CSS3 units
        while (is_css_identifier_char(**css)) {
            stringbuf_append_char(sb, **css);
            (*css)++;
        }
    }
    
    // If we have a unit, return the complete dimension token as a single string
    if (*css > unit_start) {
        // Create the complete dimension string (e.g., "10px")
        String* dimension_str = stringbuf_to_string(sb);
        return (Item){.item = s2it(dimension_str)};
    } else {
        // Reset and parse as number only
        *css = start;
        return parse_css_number(input, css);
    }
}

static Item parse_css_identifier(Input *input, const char **css) {
    if (!is_css_identifier_start(**css)) return {.item = ITEM_ERROR};
    
    StringBuf* sb = input->sb;
    
    // Handle CSS pseudo-elements (::) and pseudo-classes (:)
    if (**css == ':') {
        stringbuf_append_char(sb, **css);
        (*css)++;
        
        // Check for double colon (pseudo-elements)
        if (**css == ':') {
            stringbuf_append_char(sb, **css);
            (*css)++;
        }
    }
    
    while (is_css_identifier_char(**css) || **css == '-') {
        stringbuf_append_char(sb, **css);
        (*css)++;
    }
    
    // Handle CSS3 pseudo-class functions like :nth-child(2n+1)
    if (**css == '(') {
        stringbuf_append_char(sb, **css);
        (*css)++;
        
        int paren_depth = 1;
        while (**css && paren_depth > 0) {
            if (**css == '(') paren_depth++;
            else if (**css == ')') paren_depth--;
            
            stringbuf_append_char(sb, **css);
            (*css)++;
        }
    }
    
    String* id_str = stringbuf_to_string(sb);
    if (!id_str) {
        return {.item = ITEM_ERROR};
    }
    
    // Convert CSS keyword values to Lambda symbols using y2it()
    // CSS identifiers like 'flex', 'red', etc. should be symbols, not strings
    return (Item){.item = y2it(id_str)};
}

static Array* parse_css_function_params(Input *input, const char **css) {
    Array* params = array_pooled(input->pool);
    if (!params) return NULL;
    
    skip_css_comments(css);
    
    if (**css == ')') {
        return params; // empty parameter list
    }
    
    while (**css && **css != ')') {
        skip_css_comments(css);
        if (**css == ')') break;
        
        const char* start_pos = *css; // track position to detect infinite loops
        
        Item param = parse_css_value(input, css);
        if (param .item != ITEM_ERROR) {
            array_append(params, param, input->pool);
        } else {
            // if we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (*css == start_pos) {
                (*css)++;
            }
        }
        
        skip_css_comments(css);
        
        // handle parameter separators
        if (**css == ',') {
            (*css)++; // skip comma
            skip_css_comments(css);
        } else if (**css == '/') {
            // Handle slash separator (e.g., rgba(255 0 0 / 0.5))
            (*css)++; // skip slash
            skip_css_comments(css);
        } else if (**css == ')') {
            // end of parameters
            break;
        } else if (**css != ')') {
            // check for space-separated values within a function parameter
            // some CSS functions use space separation (e.g., rgba(255 0 0 / 0.5))
            skip_css_whitespace(css);
            
            if (**css && **css != ',' && **css != ')' && **css != '/') {
                // continue parsing more values in this parameter context
                continue;
            }
        }
    }
    
    return params;
}

// Helper function to flatten single-element arrays  
static Item flatten_single_array(Array* arr) {
    if (!arr) {
        return {.item = ITEM_ERROR};
    }
    
    if (arr->length != 1) {
        // Return array as-is if not single element
        return {.item = (uint64_t)arr};
    }
    
    // For single-element arrays, return the single item directly
    // Use array_get to safely access the item
    Item single_item = arr->items[0];
    
    // Debug: check what type we're flattening
    if (single_item .item != ITEM_ERROR) {
        printf("Flattening single array item, type: %lu\n", single_item.item >> 56);
        
        // For container types (like Elements), the type is determined by the container's type_id field
        // Check if this is a direct pointer to a container
        if ((single_item.item >> 56) == 0) {
            Container* container = (Container*)single_item.item;
            if (container && container->type_id == LMD_TYPE_ELEMENT) {
                printf("Single item is element container, keeping as-is\n");
                return single_item;
            }
        }
    }
    
    return single_item;
}

static Item parse_css_function(Input *input, const char **css) {
    // Parse function name
    if (!is_css_identifier_start(**css)) return {.item = ITEM_ERROR};
    
    StringBuf* sb = input->sb;
    while (is_css_identifier_char(**css)) {
        stringbuf_append_char(sb, **css);
        (*css)++;
    }
    
    skip_css_comments(css);
    if (**css != '(') {
        // Not a function, treat as identifier (symbol)
        String* id_str = stringbuf_to_string(sb);
        return id_str ? (Item){.item = y2it(id_str)} : (Item){.item = ITEM_ERROR};
    }
    
    String* func_name = stringbuf_to_string(sb);
    if (!func_name) return {.item = ITEM_ERROR};
    
    printf("Parsing CSS function: %s\n", func_name->chars);
    
    (*css)++; // Skip opening parenthesis
    
    // Parse function parameters
    Array* params = parse_css_function_params(input, css);
    
    if (**css == ')') {
        (*css)++; // Skip closing parenthesis
    }
    
    // Create Lambda element with function name as element name
    Element* func_element = input_create_element(input, func_name->chars);
    if (!func_element) return {.item = ITEM_ERROR};
    
    // Add parameters as child content using list_push
    if (params && params->length > 0) {
        for (long i = 0; i < params->length; i++) {
            Item param = params->items[i];
            list_push((List*)func_element, param);
        }
    }
    
    printf("Created function element '%s' with %ld parameters\n", func_name->chars, params ? params->length : 0);
    
    // For container types like Element, return direct pointer (not tagged)
    // The container's type_id field indicates it's an element
    return {.item = (uint64_t)func_element};
}

static Array* parse_css_value_list(Input *input, const char **css) {
    Array* values = array_pooled(input->pool);
    if (!values) return NULL;
    
    while (**css && **css != ';' && **css != '}' && **css != '!' && **css != ')') {
        skip_css_comments(css);
        if (!**css || **css == ';' || **css == '}' || **css == '!' || **css == ')') break;
        
        const char* start_pos = *css; // Track position to detect infinite loops
        
        Item value = parse_css_value(input, css);
        if (value .item != ITEM_ERROR) {
            array_append(values, value, input->pool);
        } else {
            // If we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (*css == start_pos) {
                (*css)++;
                continue;
            }
        }
        
        // Check for value separators BEFORE calling skip_css_comments
        // because skip_css_comments would consume the space we want to detect
        if (**css == ',') {
            (*css)++;
            skip_css_comments(css);
        } else if (**css == '/') {
            // Handle slash separator (e.g., rgba(255 0 0 / 0.5))
            (*css)++;
            skip_css_comments(css);
        } else if (**css == ' ' || **css == '\t' || **css == '\n' || **css == '\r') {
            skip_css_whitespace(css);
            // Skip any comments after whitespace
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
            // Space-separated values continue
        } else {
            break; // End of value list
        }
    }
    
    return values;
}

static Item parse_css_value(Input *input, const char **css) {
    skip_css_comments(css);
    
    if (!**css) return {.item = ITEM_ERROR};
    
    // Try to parse different CSS value types
    switch (**css) {
        case '"':
        case '\'':
            return parse_css_string(input, css);
            
        case '#':
            return parse_css_color(input, css);
            
        case '+':
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '.':
            return parse_css_measure(input, css);
            
        default:
            if (**css == 'u' && strncmp(*css, "url(", 4) == 0) {
                return parse_css_url(input, css);
            } else if (is_css_identifier_start(**css)) {
                // Look ahead to see if this is a function
                const char* lookahead = *css;
                while (is_css_identifier_char(*lookahead)) {
                    lookahead++;
                }
                skip_css_whitespace(&lookahead);
                if (*lookahead == '(') {
                    // Check for CSS3 functions
                    const char* start = *css;
                    if (strncmp(start, "calc(", 5) == 0 ||
                        strncmp(start, "var(", 4) == 0 ||
                        strncmp(start, "linear-gradient(", 16) == 0 ||
                        strncmp(start, "radial-gradient(", 16) == 0 ||
                        strncmp(start, "repeating-linear-gradient(", 26) == 0 ||
                        strncmp(start, "repeating-radial-gradient(", 26) == 0 ||
                        strncmp(start, "rgba(", 5) == 0 ||
                        strncmp(start, "hsla(", 5) == 0 ||
                        strncmp(start, "rgb(", 4) == 0 ||
                        strncmp(start, "hsl(", 4) == 0 ||
                        strncmp(start, "cubic-bezier(", 13) == 0 ||
                        strncmp(start, "steps(", 6) == 0 ||
                        strncmp(start, "rotate(", 7) == 0 ||
                        strncmp(start, "rotateX(", 8) == 0 ||
                        strncmp(start, "rotateY(", 8) == 0 ||
                        strncmp(start, "rotateZ(", 8) == 0 ||
                        strncmp(start, "rotate3d(", 9) == 0 ||
                        strncmp(start, "scale(", 6) == 0 ||
                        strncmp(start, "scaleX(", 7) == 0 ||
                        strncmp(start, "scaleY(", 7) == 0 ||
                        strncmp(start, "scaleZ(", 7) == 0 ||
                        strncmp(start, "scale3d(", 8) == 0 ||
                        strncmp(start, "translate(", 10) == 0 ||
                        strncmp(start, "translateX(", 11) == 0 ||
                        strncmp(start, "translateY(", 11) == 0 ||
                        strncmp(start, "translateZ(", 11) == 0 ||
                        strncmp(start, "translate3d(", 12) == 0 ||
                        strncmp(start, "skew(", 5) == 0 ||
                        strncmp(start, "skewX(", 6) == 0 ||
                        strncmp(start, "skewY(", 6) == 0 ||
                        strncmp(start, "matrix(", 7) == 0 ||
                        strncmp(start, "matrix3d(", 9) == 0 ||
                        strncmp(start, "perspective(", 12) == 0 ||
                        strncmp(start, "blur(", 5) == 0 ||
                        strncmp(start, "brightness(", 11) == 0 ||
                        strncmp(start, "contrast(", 9) == 0 ||
                        strncmp(start, "drop-shadow(", 12) == 0 ||
                        strncmp(start, "grayscale(", 10) == 0 ||
                        strncmp(start, "hue-rotate(", 11) == 0 ||
                        strncmp(start, "invert(", 7) == 0 ||
                        strncmp(start, "opacity(", 8) == 0 ||
                        strncmp(start, "saturate(", 9) == 0 ||
                        strncmp(start, "sepia(", 6) == 0 ||
                        strncmp(start, "minmax(", 7) == 0 ||
                        strncmp(start, "repeat(", 7) == 0 ||
                        strncmp(start, "fit-content(", 12) == 0) {
                        return parse_css_function(input, css);
                    } else {
                        return parse_css_function(input, css);
                    }
                } else {
                    // Check if it's a color first
                    Item color_result = parse_css_color(input, css);
                    if (color_result .item != ITEM_ERROR) {
                        return color_result;
                    }
                    // Otherwise parse as identifier
                    return parse_css_identifier(input, css);
                }
            }
            return {.item = ITEM_ERROR};
    }
}

void parse_css(Input* input, const char* css_string) {
    printf("css_parse (stylesheet)\n");
    input->sb = stringbuf_new(input->pool);
    
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
            input_add_attribute_item_to_element(input, empty_stylesheet, "rules", {.item = (uint64_t)empty_rules});
            input->root = {.item = (uint64_t)empty_stylesheet};
        } else {
            input->root = {.item = ITEM_ERROR};
        }
    }
}
