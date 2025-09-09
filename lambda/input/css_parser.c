#include "css_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Helper function to skip whitespace and comments
static void skip_whitespace_and_comments(css_parser_t* parser) {
    css_token_t* token;
    while ((token = css_parser_current_token(parser)) && 
           (token->type == CSS_TOKEN_WHITESPACE || token->type == CSS_TOKEN_COMMENT)) {
        if (token->type == CSS_TOKEN_COMMENT && parser->preserve_comments) {
            break; // Don't skip comments if we want to preserve them
        }
        css_parser_advance(parser);
    }
}

// Global memory tracking for safety
static size_t g_total_allocated = 0;
static const size_t MAX_PARSER_MEMORY = 5 * 1024 * 1024; // 5MB limit

// Safe memory allocation wrapper
static void* safe_pool_calloc(VariableMemPool* pool, size_t size) {
    if (g_total_allocated + size > MAX_PARSER_MEMORY) {
        return NULL; // Reject allocation to prevent memory explosion
    }
    void* ptr = pool_calloc(pool, size);
    if (ptr) {
        g_total_allocated += size;
    }
    return ptr;
}

// Create a new CSS parser
css_parser_t* css_parser_create(VariableMemPool* pool) {
    g_total_allocated = 0; // Reset counter for new parser
    css_parser_t* parser = (css_parser_t*)safe_pool_calloc(pool, sizeof(css_parser_t));
    
    parser->tokens = NULL;
    parser->property_db = css_property_db_create(pool);
    parser->pool = pool;
    parser->errors = NULL;
    parser->error_count = 0;
    parser->error_capacity = 0;
    parser->strict_mode = false;
    parser->preserve_comments = false;
    
    return parser;
}

void css_parser_destroy(css_parser_t* parser) {
    // Memory is managed by the pool, so nothing to do here
    (void)parser;
}

// Error handling functions
void css_parser_add_error(css_parser_t* parser, css_error_type_t type, 
                         const char* message, css_token_t* token) {
    if (parser->error_count >= parser->error_capacity) {
        parser->error_capacity = parser->error_capacity == 0 ? 10 : parser->error_capacity * 2;
        parser->errors = (css_error_t*)pool_calloc(parser->pool, 
                                                              sizeof(css_error_t) * parser->error_capacity);
    }
    
    css_error_t* error = &parser->errors[parser->error_count++];
    error->type = type;
    error->message = message;
    error->line = 0; // TODO: Add line tracking to tokenizer
    error->column = 0; // TODO: Add column tracking to tokenizer
    error->context = token ? token->value : NULL;
}

bool css_parser_has_errors(css_parser_t* parser) {
    return parser->error_count > 0;
}

void css_parser_clear_errors(css_parser_t* parser) {
    parser->error_count = 0;
}

// Token navigation functions
css_token_t* css_parser_current_token(css_parser_t* parser) {
    return css_token_stream_current(parser->tokens);
}

css_token_t* css_parser_peek_token(css_parser_t* parser, int offset) {
    return css_token_stream_peek(parser->tokens, offset);
}

void css_parser_advance(css_parser_t* parser) {
    css_token_stream_advance(parser->tokens);
}

bool css_parser_expect_token(css_parser_t* parser, CSSTokenType type) {
    css_token_t* token = css_parser_current_token(parser);
    return token && token->type == type;
}

bool css_parser_consume_token(css_parser_t* parser, CSSTokenType type) {
    if (css_parser_expect_token(parser, type)) {
        css_parser_advance(parser);
        return true;
    }
    return false;
}

// Configuration functions
void css_parser_set_strict_mode(css_parser_t* parser, bool strict) {
    parser->strict_mode = strict;
}

void css_parser_set_preserve_comments(css_parser_t* parser, bool preserve) {
    parser->preserve_comments = preserve;
}

// Main parsing function
css_stylesheet_t* css_parse_stylesheet(css_parser_t* parser, const char* css_text) {
    if (!parser || !css_text) return NULL;
    
    // Tokenize the CSS text
    size_t token_count = 0;
    CSSToken* tokens = css_tokenize(css_text, strlen(css_text), parser->pool, &token_count);
    if (!tokens) {
        css_parser_add_error(parser, CSS_ERROR_MEMORY_ERROR, "Failed to tokenize CSS", NULL);
        return NULL;
    }
    
    // Create token stream
    CSSTokenStream* stream = (CSSTokenStream*)pool_calloc(parser->pool, sizeof(CSSTokenStream));
    stream->tokens = tokens;
    stream->current = 0;
    stream->length = token_count;
    stream->pool = parser->pool;
    parser->tokens = stream;
    
    // Create stylesheet
    css_stylesheet_t* stylesheet = (css_stylesheet_t*)pool_calloc(parser->pool, sizeof(css_stylesheet_t));
    stylesheet->rules = NULL;
    stylesheet->rule_count = 0;
    stylesheet->errors = NULL;
    stylesheet->error_count = 0;
    stylesheet->error_capacity = 0;
    
    // Parse rules
    css_rule_t* rules_head = NULL;
    css_rule_t* rules_tail = NULL;
    int rule_count = 0;
    
    skip_whitespace_and_comments(parser);
    
    while (!css_token_stream_at_end(parser->tokens)) {
        size_t initial_position = parser->tokens->current;
        
        css_rule_t* rule = css_parse_rule(parser);
        if (rule) {
            if (!rules_head) {
                rules_head = rules_tail = rule;
            } else {
                rules_tail->next = rule;
                rules_tail = rule;
            }
            rule_count++;
        } else {
            // If parsing failed and we haven't advanced, skip the current token
            // to prevent infinite loops
            if (parser->tokens->current == initial_position) {
                css_token_t* token = css_parser_current_token(parser);
                if (token) {
                    css_parser_add_error(parser, CSS_ERROR_UNEXPECTED_TOKEN, 
                                        "Unexpected token, skipping", token);
                    css_parser_advance(parser);
                } else {
                    break; // No more tokens
                }
            }
        }
        
        skip_whitespace_and_comments(parser);
        
        // If we're in strict mode and have errors, stop parsing
        if (parser->strict_mode && css_parser_has_errors(parser)) {
            break;
        }
    }
    
    stylesheet->rules = rules_head;
    stylesheet->rule_count = rule_count;
    
    // Copy errors to stylesheet
    if (parser->error_count > 0) {
        stylesheet->errors = parser->errors;
        stylesheet->error_count = parser->error_count;
        stylesheet->error_capacity = parser->error_capacity;
    }
    
    return stylesheet;
}

// Parse a single rule (style rule, at-rule, or comment)
css_rule_t* css_parse_rule(css_parser_t* parser) {
    skip_whitespace_and_comments(parser);
    
    css_token_t* token = css_parser_current_token(parser);
    if (!token) return NULL;
    
    // Handle comments
    if (token->type == CSS_TOKEN_COMMENT && parser->preserve_comments) {
        css_rule_t* rule = css_rule_create_comment(parser, token->value);
        css_parser_advance(parser);
        return rule;
    }
    
    // Handle at-rules
    if (token->type == CSS_TOKEN_AT_KEYWORD) {
        return css_rule_create_at_rule(parser, css_parse_at_rule(parser));
    }
    
    // Handle style rules
    css_style_rule_t* style_rule = css_parse_style_rule(parser);
    if (!style_rule) return NULL;
    
    css_rule_t* rule = (css_rule_t*)pool_calloc(parser->pool, sizeof(css_rule_t));
    if (!rule) return NULL;
    
    rule->type = CSS_RULE_STYLE;
    rule->data.style_rule = style_rule;
    rule->next = NULL;
    return rule;
}

// Parse a style rule (selector { declarations })
css_style_rule_t* css_parse_style_rule(css_parser_t* parser) {
    // Parse selector list
    css_selector_t* selectors = css_parse_selector_list(parser);
    if (!selectors) {
        css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected selector", css_parser_current_token(parser));
        return NULL;
    }
    
    skip_whitespace_and_comments(parser);
    
    // Expect opening brace
    if (!css_parser_consume_token(parser, CSS_TOKEN_LEFT_BRACE)) {
        css_parser_add_error(parser, CSS_ERROR_MISSING_BRACE, "Expected '{'", css_parser_current_token(parser));
        return NULL;
    }
    
    // Create style rule
    css_style_rule_t* rule = (css_style_rule_t*)pool_calloc(parser->pool, sizeof(css_style_rule_t));
    if (!rule) return NULL;
    
    rule->selectors = selectors;
    rule->declarations = NULL;
    rule->declaration_count = 0;
    rule->declaration_capacity = 0;
    
    skip_whitespace_and_comments(parser);
    
    // Parse declarations
    while (!css_parser_expect_token(parser, CSS_TOKEN_RIGHT_BRACE) && 
           !css_token_stream_at_end(parser->tokens)) {
        
        size_t initial_position = parser->tokens->current;
        
        css_declaration_t* decl = css_parse_declaration(parser);
        if (decl) {
            css_style_rule_add_declaration(rule, decl, parser->pool);
        } else {
            // If parsing failed and we haven't advanced, skip the current token
            if (parser->tokens->current == initial_position) {
                css_token_t* token = css_parser_current_token(parser);
                if (token) {
                    css_parser_add_error(parser, CSS_ERROR_UNEXPECTED_TOKEN, 
                                        "Unexpected token in declaration, skipping", token);
                    css_parser_advance(parser);
                } else {
                    break; // No more tokens
                }
            }
        }
        
        skip_whitespace_and_comments(parser);
        
        // Optional semicolon
        css_parser_consume_token(parser, CSS_TOKEN_SEMICOLON);
        skip_whitespace_and_comments(parser);
    }
    
    // Expect closing brace
    if (!css_parser_consume_token(parser, CSS_TOKEN_RIGHT_BRACE)) {
        css_parser_add_error(parser, CSS_ERROR_MISSING_BRACE, "Expected '}'", css_parser_current_token(parser));
    }
    
    return rule;
}

// Parse selector list (selector1, selector2, ...)
css_selector_t* css_parse_selector_list(css_parser_t* parser) {
    css_selector_t* selectors_head = NULL;
    css_selector_t* selectors_tail = NULL;
    
    do {
        size_t initial_position = parser->tokens->current;
        
        css_selector_t* selector = css_parse_selector(parser);
        if (selector) {
            if (!selectors_head) {
                selectors_head = selectors_tail = selector;
            } else {
                selectors_tail->next = selector;
                selectors_tail = selector;
            }
        } else {
            // If parsing failed and we haven't advanced, break to prevent infinite loops
            if (parser->tokens->current == initial_position) {
                break;
            }
        }
        
        skip_whitespace_and_comments(parser);
        
        if (css_parser_consume_token(parser, CSS_TOKEN_COMMA)) {
            skip_whitespace_and_comments(parser);
        } else {
            break;
        }
    } while (!css_token_stream_at_end(parser->tokens));
    
    return selectors_head;
}

// Parse a single selector
css_selector_t* css_parse_selector(css_parser_t* parser) {
    css_selector_t* selector = (css_selector_t*)pool_calloc(parser->pool, sizeof(css_selector_t));
    if (!selector) return NULL;
    
    selector->components = NULL;
    selector->specificity = 0;
    selector->next = NULL;
    
    css_selector_component_t* components_head = NULL;
    css_selector_component_t* components_tail = NULL;
    
    skip_whitespace_and_comments(parser);
    
    while (!css_token_stream_at_end(parser->tokens)) {
        size_t initial_position = parser->tokens->current;
        css_token_t* token = css_parser_current_token(parser);
        if (!token) break;
        
        // Stop at tokens that end a selector
        if (token->type == CSS_TOKEN_LEFT_BRACE || token->type == CSS_TOKEN_COMMA) {
            break;
        }
        
        css_selector_component_t* component = css_parse_selector_component(parser);
        if (component) {
            if (!components_head) {
                components_head = components_tail = component;
            } else {
                components_tail->next = component;
                components_tail = component;
            }
        } else {
            // If parsing failed and we haven't advanced, skip the token to prevent infinite loops
            if (parser->tokens->current == initial_position) {
                css_parser_advance(parser);
            }
            break;
        }
        
        skip_whitespace_and_comments(parser);
    }
    
    selector->components = components_head;
    selector->specificity = css_selector_calculate_specificity(selector);
    
    return selector;
}

// Parse a selector component
css_selector_component_t* css_parse_selector_component(css_parser_t* parser) {
    css_token_t* token = css_parser_current_token(parser);
    if (!token) return NULL;
    
    css_selector_component_t* component = (css_selector_component_t*)pool_calloc(parser->pool, sizeof(css_selector_component_t));
    if (!component) return NULL;
    
    component->type = CSS_SELECTOR_TYPE;
    component->name = NULL;
    component->value = NULL;
    component->operator = NULL;
    component->next = NULL;
    
    switch (token->type) {
        case CSS_TOKEN_IDENT:
            component->type = CSS_SELECTOR_TYPE;
            component->name = token->value;
            css_parser_advance(parser);
            break;
            
        case CSS_TOKEN_HASH:
            component->type = CSS_SELECTOR_ID;
            component->name = token->value + 1; // Skip the '#'
            css_parser_advance(parser);
            break;
            
        case CSS_TOKEN_DELIM:
            if (strcmp(token->value, ".") == 0) {
                css_parser_advance(parser);
                css_token_t* class_token = css_parser_current_token(parser);
                if (class_token && class_token->type == CSS_TOKEN_IDENT) {
                    component->type = CSS_SELECTOR_CLASS;
                    component->name = class_token->value;
                    css_parser_advance(parser);
                } else {
                    css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected class name after '.'", class_token);
                    return NULL;
                }
            } else if (strcmp(token->value, "*") == 0) {
                component->type = CSS_SELECTOR_UNIVERSAL;
                component->name = "*";
                css_parser_advance(parser);
            } else if (strcmp(token->value, ">") == 0) {
                component->type = CSS_SELECTOR_CHILD;
                component->name = ">";
                css_parser_advance(parser);
            } else if (strcmp(token->value, "~") == 0) {
                component->type = CSS_SELECTOR_SIBLING;
                component->name = "~";
                css_parser_advance(parser);
            } else if (strcmp(token->value, "+") == 0) {
                component->type = CSS_SELECTOR_ADJACENT;
                component->name = "+";
                css_parser_advance(parser);
            } else {
                return NULL;
            }
            break;
            
        case CSS_TOKEN_COLON:
            css_parser_advance(parser);
            // Check for double colon (pseudo-element)
            if (css_parser_expect_token(parser, CSS_TOKEN_COLON)) {
                css_parser_advance(parser);
                css_token_t* pseudo_token = css_parser_current_token(parser);
                if (pseudo_token && pseudo_token->type == CSS_TOKEN_IDENT) {
                    component->type = CSS_SELECTOR_PSEUDO_ELEMENT;
                    component->name = pseudo_token->value;
                    css_parser_advance(parser);
                } else {
                    css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected pseudo-element name after '::'", pseudo_token);
                    return NULL;
                }
            } else {
                // Single colon (pseudo-class)
                css_token_t* pseudo_token = css_parser_current_token(parser);
                if (pseudo_token && pseudo_token->type == CSS_TOKEN_IDENT) {
                    component->type = CSS_SELECTOR_PSEUDO_CLASS;
                    component->name = pseudo_token->value;
                    css_parser_advance(parser);
                    
                    // Check for functional pseudo-classes like :nth-child(2n+1)
                    if (css_parser_expect_token(parser, CSS_TOKEN_LEFT_PAREN)) {
                        css_parser_advance(parser);
                        // Parse function parameters
                        css_token_t* param_token = css_parser_current_token(parser);
                        if (param_token && (param_token->type == CSS_TOKEN_IDENT || 
                                          param_token->type == CSS_TOKEN_NUMBER ||
                                          param_token->type == CSS_TOKEN_STRING)) {
                            component->value = param_token->value;
                            css_parser_advance(parser);
                        }
                        
                        if (!css_parser_consume_token(parser, CSS_TOKEN_RIGHT_PAREN)) {
                            css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected ')' in pseudo-class function", css_parser_current_token(parser));
                        }
                    }
                } else {
                    css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected pseudo-class name after ':'", pseudo_token);
                    return NULL;
                }
            }
            break;
            
        case CSS_TOKEN_LEFT_BRACKET:
            // Parse advanced attribute selector [attr operator value i]
            css_parser_advance(parser);
            css_token_t* attr_token = css_parser_current_token(parser);
            if (attr_token && attr_token->type == CSS_TOKEN_IDENT) {
                component->type = CSS_SELECTOR_ATTRIBUTE;
                component->name = attr_token->value;
                css_parser_advance(parser);
                
                // Check for operator and value
                css_token_t* op_token = css_parser_current_token(parser);
                if (op_token && op_token->type == CSS_TOKEN_DELIM) {
                    // Support CSS3 attribute operators: =, ~=, |=, ^=, $=, *=
                    if (strcmp(op_token->value, "=") == 0 ||
                        strcmp(op_token->value, "~") == 0 ||
                        strcmp(op_token->value, "|") == 0 ||
                        strcmp(op_token->value, "^") == 0 ||
                        strcmp(op_token->value, "$") == 0 ||
                        strcmp(op_token->value, "*") == 0) {
                        
                        component->operator = op_token->value;
                        css_parser_advance(parser);
                        
                        // Check for compound operators like ~=, |=, ^=, $=, *=
                        css_token_t* eq_token = css_parser_current_token(parser);
                        if (eq_token && eq_token->type == CSS_TOKEN_DELIM && strcmp(eq_token->value, "=") == 0) {
                            // Combine operator with =
                            char* compound_op = (char*)pool_calloc(parser->pool, 3);
                            snprintf(compound_op, 3, "%s=", component->operator);
                            component->operator = compound_op;
                            css_parser_advance(parser);
                        }
                        
                        css_token_t* value_token = css_parser_current_token(parser);
                        if (value_token && (value_token->type == CSS_TOKEN_IDENT || value_token->type == CSS_TOKEN_STRING)) {
                            component->value = value_token->value;
                            css_parser_advance(parser);
                            
                            // Check for case-insensitive flag 'i' or 's'
                            css_token_t* flag_token = css_parser_current_token(parser);
                            if (flag_token && flag_token->type == CSS_TOKEN_IDENT && 
                                (strcmp(flag_token->value, "i") == 0 || strcmp(flag_token->value, "s") == 0)) {
                                // Store flag in a separate field if needed, for now append to value
                                char* flagged_value = (char*)pool_calloc(parser->pool, strlen(component->value) + 4);
                                snprintf(flagged_value, strlen(component->value) + 4, "%s %s", component->value, flag_token->value);
                                component->value = flagged_value;
                                css_parser_advance(parser);
                            }
                        }
                    }
                }
                
                if (!css_parser_consume_token(parser, CSS_TOKEN_RIGHT_BRACKET)) {
                    css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected ']'", css_parser_current_token(parser));
                }
            } else {
                css_parser_add_error(parser, CSS_ERROR_INVALID_SELECTOR, "Expected attribute name", attr_token);
                return NULL;
            }
            break;
            
        default:
            return NULL;
    }
    
    return component;
}

// Parse a declaration (property: value)
css_declaration_t* css_parse_declaration(css_parser_t* parser) {
    skip_whitespace_and_comments(parser);
    
    css_token_t* property_token = css_parser_current_token(parser);
    if (!property_token || property_token->type != CSS_TOKEN_IDENT) {
        css_parser_add_error(parser, CSS_ERROR_INVALID_PROPERTY, "Expected property name", property_token);
        return NULL;
    }
    
    const char* property_name = property_token->value;
    css_parser_advance(parser);
    
    skip_whitespace_and_comments(parser);
    
    // Expect colon
    if (!css_parser_consume_token(parser, CSS_TOKEN_COLON)) {
        css_parser_add_error(parser, CSS_ERROR_UNEXPECTED_TOKEN, "Expected ':'", css_parser_current_token(parser));
        return NULL;
    }
    
    skip_whitespace_and_comments(parser);
    
    // Parse value tokens
    int token_count = 0;
    css_token_t* value_tokens = css_parse_declaration_value(parser, &token_count);
    if (!value_tokens || token_count == 0) {
        css_parser_add_error(parser, CSS_ERROR_INVALID_VALUE, "Expected property value", css_parser_current_token(parser));
        return NULL;
    }
    
    // Check for !important
    css_importance_t importance = CSS_IMPORTANCE_NORMAL;
    if (token_count >= 2 && 
        value_tokens[token_count - 2].type == CSS_TOKEN_DELIM &&
        strcmp(value_tokens[token_count - 2].value, "!") == 0 &&
        value_tokens[token_count - 1].type == CSS_TOKEN_IDENTIFIER &&
        strcmp(value_tokens[token_count - 1].value, "important") == 0) {
        importance = CSS_IMPORTANCE_IMPORTANT;
        token_count -= 2; // Remove !important tokens from value
    }
    
    // Create declaration
    css_declaration_t* decl = css_declaration_create(property_name, value_tokens, token_count, importance, parser->pool);
    
    // Validate declaration
    css_declaration_validate(parser->property_db, decl);
    
    return decl;
}

// Parse CSS function (calc, var, rgb, etc.)
css_token_t* css_parse_function(css_parser_t* parser, const char* function_name, int* token_count) {
    css_token_t* tokens = NULL;
    int capacity = 20;
    int count = 0;
    int paren_depth = 1; // Already consumed opening paren
    
    tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
    if (!tokens) {
        *token_count = 0;
        return NULL;
    }
    
    // Add function name as first token
    tokens[count].type = CSS_TOKEN_FUNCTION;
    tokens[count].value = function_name;
    count++;
    
    while (!css_token_stream_at_end(parser->tokens) && paren_depth > 0) {
        css_token_t* token = css_parser_current_token(parser);
        if (!token) break;
        
        // Track parentheses depth for nested functions
        if (token->type == CSS_TOKEN_LEFT_PAREN) {
            paren_depth++;
        } else if (token->type == CSS_TOKEN_RIGHT_PAREN) {
            paren_depth--;
        }
        
        // Expand capacity if needed
        if (count >= capacity) {
            capacity *= 2;
            css_token_t* new_tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
            if (!new_tokens) {
                *token_count = count;
                return tokens; // Return what we have so far
            }
            memcpy(new_tokens, tokens, sizeof(css_token_t) * count);
            tokens = new_tokens;
        }
        
        // Only add token if we're not at the closing paren (paren_depth will be 0 after decrement)
        if (paren_depth > 0) {
            tokens[count++] = *token;
        }
        css_parser_advance(parser);
    }
    
    *token_count = count;
    return tokens;
}

// Parse declaration value tokens until semicolon or end of block
css_token_t* css_parse_declaration_value(css_parser_t* parser, int* token_count) {
    css_token_t* tokens = NULL;
    int capacity = 10;
    int count = 0;
    
    tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
    if (!tokens) {
        *token_count = 0;
        return NULL;
    }
    
    while (!css_token_stream_at_end(parser->tokens)) {
        size_t initial_position = parser->tokens->current;
        css_token_t* token = css_parser_current_token(parser);
        if (!token) break;
        
        // Stop at semicolon or closing brace
        if (token->type == CSS_TOKEN_SEMICOLON || token->type == CSS_TOKEN_RIGHT_BRACE) {
            break;
        }
        
        // Skip whitespace in values (but preserve structure)
        if (token->type == CSS_TOKEN_WHITESPACE) {
            css_parser_advance(parser);
            continue;
        }
        
        // Handle CSS functions with safe parsing
        if (token->type == CSS_TOKEN_FUNCTION) {
            // Add the function name token
            if (count >= capacity) {
                capacity *= 2;
                css_token_t* new_tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
                if (!new_tokens) {
                    *token_count = count;
                    return tokens;
                }
                memcpy(new_tokens, tokens, sizeof(css_token_t) * count);
                tokens = new_tokens;
            }
            tokens[count++] = *token;
            css_parser_advance(parser);
            
            // Skip function contents safely by counting parentheses
            int paren_depth = 1;
            while (!css_token_stream_at_end(parser->tokens) && paren_depth > 0) {
                css_token_t* func_token = css_parser_current_token(parser);
                if (!func_token) break;
                
                if (func_token->type == CSS_TOKEN_LEFT_PAREN) {
                    paren_depth++;
                } else if (func_token->type == CSS_TOKEN_RIGHT_PAREN) {
                    paren_depth--;
                }
                
                // Add token to our collection
                if (count >= capacity) {
                    capacity *= 2;
                    css_token_t* new_tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
                    if (!new_tokens) {
                        *token_count = count;
                        return tokens;
                    }
                    memcpy(new_tokens, tokens, sizeof(css_token_t) * count);
                    tokens = new_tokens;
                }
                tokens[count++] = *func_token;
                css_parser_advance(parser);
            }
            continue;
        }
        
        // Expand capacity if needed
        if (count >= capacity) {
            capacity *= 2;
            css_token_t* new_tokens = (css_token_t*)pool_calloc(parser->pool, sizeof(css_token_t) * capacity);
            if (!new_tokens) {
                *token_count = count;
                return tokens; // Return what we have so far
            }
            memcpy(new_tokens, tokens, sizeof(css_token_t) * count);
            tokens = new_tokens;
        }
        
        tokens[count++] = *token;
        css_parser_advance(parser);
        
        // Prevent infinite loops
        if (parser->tokens->current == initial_position) {
            break;
        }
    }
    
    *token_count = count;
    return tokens;
}

// Parse basic at-rule (more specific parsing can be added later)
css_at_rule_t* css_parse_at_rule(css_parser_t* parser) {
    css_token_t* at_token = css_parser_current_token(parser);
    if (!at_token || at_token->type != CSS_TOKEN_AT_KEYWORD) {
        css_parser_add_error(parser, CSS_ERROR_INVALID_AT_RULE, "Expected at-rule", at_token);
        return NULL;
    }
    
    css_at_rule_t* at_rule = (css_at_rule_t*)pool_calloc(parser->pool, sizeof(css_at_rule_t));
    at_rule->type = CSS_AT_RULE_UNKNOWN;
    at_rule->name = at_token->value;
    at_rule->prelude = NULL;
    at_rule->rules = NULL;
    at_rule->rule_count = 0;
    at_rule->rule_capacity = 0;
    at_rule->declarations = NULL;
    at_rule->declaration_count = 0;
    at_rule->declaration_capacity = 0;
    
    // Determine at-rule type with CSS3+ support
    if (strcmp(at_token->value, "@media") == 0) {
        at_rule->type = CSS_AT_RULE_MEDIA;
    } else if (strcmp(at_token->value, "@keyframes") == 0 || 
               strcmp(at_token->value, "@-webkit-keyframes") == 0 ||
               strcmp(at_token->value, "@-moz-keyframes") == 0) {
        at_rule->type = CSS_AT_RULE_KEYFRAMES;
    } else if (strcmp(at_token->value, "@font-face") == 0) {
        at_rule->type = CSS_AT_RULE_FONT_FACE;
    } else if (strcmp(at_token->value, "@import") == 0) {
        at_rule->type = CSS_AT_RULE_IMPORT;
    } else if (strcmp(at_token->value, "@charset") == 0) {
        at_rule->type = CSS_AT_RULE_CHARSET;
    } else if (strcmp(at_token->value, "@namespace") == 0) {
        at_rule->type = CSS_AT_RULE_NAMESPACE;
    } else if (strcmp(at_token->value, "@supports") == 0) {
        at_rule->type = CSS_AT_RULE_SUPPORTS;
    } else if (strcmp(at_token->value, "@page") == 0) {
        at_rule->type = CSS_AT_RULE_PAGE;
    } else if (strcmp(at_token->value, "@layer") == 0) {
        at_rule->type = CSS_AT_RULE_LAYER;
    } else if (strcmp(at_token->value, "@container") == 0) {
        at_rule->type = CSS_AT_RULE_CONTAINER;
    }
    
    css_parser_advance(parser);
    skip_whitespace_and_comments(parser);
    
    // Parse prelude (everything before { or ;)
    // For now, just consume tokens until we hit a delimiter
    while (!css_token_stream_at_end(parser->tokens)) {
        size_t initial_position = parser->tokens->current;
        css_token_t* token = css_parser_current_token(parser);
        if (!token) break;
        
        if (token->type == CSS_TOKEN_LEFT_BRACE || token->type == CSS_TOKEN_SEMICOLON) {
            break;
        }
        
        // TODO: Properly parse and store prelude
        css_parser_advance(parser);
        
        // Safety check to prevent infinite loops
        if (parser->tokens->current == initial_position) {
            break;
        }
    }
    
    // Handle block at-rules vs statement at-rules
    if (css_parser_expect_token(parser, CSS_TOKEN_LEFT_BRACE)) {
        css_parser_advance(parser);
        skip_whitespace_and_comments(parser);
        
        // Parse nested rules or declarations
        while (!css_parser_expect_token(parser, CSS_TOKEN_RIGHT_BRACE) && 
               !css_token_stream_at_end(parser->tokens)) {
            
            // For now, just skip content - specific at-rule parsing can be added later
            css_parser_advance(parser);
        }
        
        css_parser_consume_token(parser, CSS_TOKEN_RIGHT_BRACE);
    } else {
        css_parser_consume_token(parser, CSS_TOKEN_SEMICOLON);
    }
    
    return at_rule;
}

// AST creation helper functions
css_rule_t* css_rule_create_style(css_parser_t* parser, css_selector_t* selectors) {
    if (!selectors) return NULL;
    
    css_rule_t* rule = (css_rule_t*)pool_calloc(parser->pool, sizeof(css_rule_t));
    rule->type = CSS_RULE_STYLE;
    rule->data.style_rule = (css_style_rule_t*)selectors; // This should be the parsed style rule
    rule->next = NULL;
    
    return rule;
}

css_rule_t* css_rule_create_at_rule(css_parser_t* parser, css_at_rule_t* at_rule) {
    if (!at_rule) return NULL;
    
    css_rule_t* rule = (css_rule_t*)pool_calloc(parser->pool, sizeof(css_rule_t));
    rule->type = CSS_RULE_AT_RULE;
    rule->data.at_rule = at_rule;
    rule->next = NULL;
    
    return rule;
}

css_rule_t* css_rule_create_comment(css_parser_t* parser, const char* comment) {
    css_rule_t* rule = (css_rule_t*)pool_calloc(parser->pool, sizeof(css_rule_t));
    rule->type = CSS_RULE_COMMENT;
    rule->data.comment = comment;
    rule->next = NULL;
    
    return rule;
}

void css_style_rule_add_declaration(css_style_rule_t* rule, css_declaration_t* decl, VariableMemPool* pool) {
    if (!rule || !decl) return;
    
    if (rule->declaration_count >= rule->declaration_capacity) {
        rule->declaration_capacity = rule->declaration_capacity == 0 ? 10 : rule->declaration_capacity * 2;
        css_declaration_t** new_declarations = (css_declaration_t**)pool_calloc(pool, 
                                                                                           sizeof(css_declaration_t*) * rule->declaration_capacity);
        if (rule->declarations) {
            memcpy(new_declarations, rule->declarations, sizeof(css_declaration_t*) * rule->declaration_count);
        }
        rule->declarations = new_declarations;
    }
    
    rule->declarations[rule->declaration_count++] = decl;
}

// Selector specificity calculation
int css_selector_calculate_specificity(const css_selector_t* selector) {
    if (!selector) return 0;
    
    int specificity = 0;
    css_selector_component_t* component = selector->components;
    
    while (component) {
        specificity += css_selector_component_specificity(component);
        component = component->next;
    }
    
    return specificity;
}

int css_selector_component_specificity(const css_selector_component_t* component) {
    if (!component) return 0;
    
    switch (component->type) {
        case CSS_SELECTOR_ID:
            return 100;
        case CSS_SELECTOR_CLASS:
        case CSS_SELECTOR_ATTRIBUTE:
        case CSS_SELECTOR_PSEUDO_CLASS:
            return 10;
        case CSS_SELECTOR_TYPE:
        case CSS_SELECTOR_PSEUDO_ELEMENT:
            return 1;
        default:
            return 0;
    }
}

// Debug functions
const char* css_error_type_to_string(css_error_type_t type) {
    switch (type) {
        case CSS_ERROR_NONE: return "No error";
        case CSS_ERROR_UNEXPECTED_TOKEN: return "Unexpected token";
        case CSS_ERROR_INVALID_SELECTOR: return "Invalid selector";
        case CSS_ERROR_INVALID_PROPERTY: return "Invalid property";
        case CSS_ERROR_INVALID_VALUE: return "Invalid value";
        case CSS_ERROR_MISSING_SEMICOLON: return "Missing semicolon";
        case CSS_ERROR_MISSING_BRACE: return "Missing brace";
        case CSS_ERROR_UNTERMINATED_BLOCK: return "Unterminated block";
        case CSS_ERROR_INVALID_AT_RULE: return "Invalid at-rule";
        case CSS_ERROR_MEMORY_ERROR: return "Memory error";
        default: return "Unknown error";
    }
}

const char* css_selector_type_to_string(css_selector_type_t type) {
    switch (type) {
        case CSS_SELECTOR_TYPE: return "type";
        case CSS_SELECTOR_CLASS: return "class";
        case CSS_SELECTOR_ID: return "id";
        case CSS_SELECTOR_ATTRIBUTE: return "attribute";
        case CSS_SELECTOR_PSEUDO_CLASS: return "pseudo-class";
        case CSS_SELECTOR_PSEUDO_ELEMENT: return "pseudo-element";
        case CSS_SELECTOR_UNIVERSAL: return "universal";
        case CSS_SELECTOR_DESCENDANT: return "descendant";
        case CSS_SELECTOR_CHILD: return "child";
        case CSS_SELECTOR_SIBLING: return "sibling";
        case CSS_SELECTOR_ADJACENT: return "adjacent";
        default: return "unknown";
    }
}

const char* css_at_rule_type_to_string(css_at_rule_type_t type) {
    switch (type) {
        case CSS_AT_RULE_MEDIA: return "media";
        case CSS_AT_RULE_KEYFRAMES: return "keyframes";
        case CSS_AT_RULE_FONT_FACE: return "font-face";
        case CSS_AT_RULE_IMPORT: return "import";
        case CSS_AT_RULE_CHARSET: return "charset";
        case CSS_AT_RULE_NAMESPACE: return "namespace";
        case CSS_AT_RULE_SUPPORTS: return "supports";
        case CSS_AT_RULE_PAGE: return "page";
        case CSS_AT_RULE_LAYER: return "layer";
        case CSS_AT_RULE_CONTAINER: return "container";
        case CSS_AT_RULE_UNKNOWN: return "unknown";
        default: return "invalid";
    }
}
