#include "css_selector_parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// CSS4 Specificity calculation according to CSS Selectors Level 4
CSSSpecificityDetail css_calculate_specificity_detailed(CSSComplexSelector* selector) {
    CSSSpecificityDetail detail = {0, 0, 0, 0, false, false, false};
    
    if (!selector) return detail;
    
    // Traverse the entire complex selector chain
    CSSComplexSelector* current = selector;
    while (current) {
        CSSSelectorComponent* component = current->components;
        
        // Process all components in the compound selector
        while (component) {
            switch (component->type) {
                // ID selectors contribute to 'b' (ids)
                case CSS_SELECTOR_TYPE_ID:
                    detail.ids++;
                    break;
                    
                // Class selectors contribute to 'c' (classes)
                case CSS_SELECTOR_TYPE_CLASS:
                    detail.classes++;
                    break;
                    
                // Attribute selectors contribute to 'c' (classes)
                case CSS_SELECTOR_ATTR_EXACT:
                case CSS_SELECTOR_ATTR_CONTAINS:
                case CSS_SELECTOR_ATTR_BEGINS:
                case CSS_SELECTOR_ATTR_ENDS:
                case CSS_SELECTOR_ATTR_SUBSTRING:
                case CSS_SELECTOR_ATTR_LANG:
                case CSS_SELECTOR_ATTR_EXISTS:
                case CSS_SELECTOR_ATTR_CASE_INSENSITIVE:
                case CSS_SELECTOR_ATTR_CASE_SENSITIVE:
                    detail.classes++;
                    break;
                    
                // Most pseudo-classes contribute to 'c' (classes)
                case CSS_SELECTOR_PSEUDO_HOVER:
                case CSS_SELECTOR_PSEUDO_ACTIVE:
                case CSS_SELECTOR_PSEUDO_FOCUS:
                case CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE:
                case CSS_SELECTOR_PSEUDO_FOCUS_WITHIN:
                case CSS_SELECTOR_PSEUDO_VISITED:
                case CSS_SELECTOR_PSEUDO_LINK:
                case CSS_SELECTOR_PSEUDO_TARGET:
                case CSS_SELECTOR_PSEUDO_TARGET_WITHIN:
                case CSS_SELECTOR_PSEUDO_ENABLED:
                case CSS_SELECTOR_PSEUDO_DISABLED:
                case CSS_SELECTOR_PSEUDO_CHECKED:
                case CSS_SELECTOR_PSEUDO_INDETERMINATE:
                case CSS_SELECTOR_PSEUDO_VALID:
                case CSS_SELECTOR_PSEUDO_INVALID:
                case CSS_SELECTOR_PSEUDO_REQUIRED:
                case CSS_SELECTOR_PSEUDO_OPTIONAL:
                case CSS_SELECTOR_PSEUDO_READ_ONLY:
                case CSS_SELECTOR_PSEUDO_READ_WRITE:
                case CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN:
                case CSS_SELECTOR_PSEUDO_DEFAULT:
                case CSS_SELECTOR_PSEUDO_IN_RANGE:
                case CSS_SELECTOR_PSEUDO_OUT_OF_RANGE:
                case CSS_SELECTOR_PSEUDO_ROOT:
                case CSS_SELECTOR_PSEUDO_EMPTY:
                case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
                case CSS_SELECTOR_PSEUDO_LAST_CHILD:
                case CSS_SELECTOR_PSEUDO_ONLY_CHILD:
                case CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE:
                case CSS_SELECTOR_PSEUDO_LAST_OF_TYPE:
                case CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE:
                case CSS_SELECTOR_PSEUDO_NTH_CHILD:
                case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD:
                case CSS_SELECTOR_PSEUDO_NTH_OF_TYPE:
                case CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE:
                case CSS_SELECTOR_PSEUDO_ANY_LINK:
                case CSS_SELECTOR_PSEUDO_LOCAL_LINK:
                case CSS_SELECTOR_PSEUDO_SCOPE:
                case CSS_SELECTOR_PSEUDO_CURRENT:
                case CSS_SELECTOR_PSEUDO_PAST:
                case CSS_SELECTOR_PSEUDO_FUTURE:
                case CSS_SELECTOR_PSEUDO_PLAYING:
                case CSS_SELECTOR_PSEUDO_PAUSED:
                case CSS_SELECTOR_PSEUDO_SEEKING:
                case CSS_SELECTOR_PSEUDO_BUFFERING:
                case CSS_SELECTOR_PSEUDO_STALLED:
                case CSS_SELECTOR_PSEUDO_MUTED:
                case CSS_SELECTOR_PSEUDO_VOLUME_LOCKED:
                case CSS_SELECTOR_PSEUDO_FULLSCREEN:
                case CSS_SELECTOR_PSEUDO_PICTURE_IN_PICTURE:
                case CSS_SELECTOR_PSEUDO_USER_INVALID:
                case CSS_SELECTOR_PSEUDO_USER_VALID:
                case CSS_SELECTOR_PSEUDO_DIR:
                case CSS_SELECTOR_PSEUDO_LANG:
                    detail.classes++;
                    break;
                    
                // Element and pseudo-element selectors contribute to 'd' (elements)
                case CSS_SELECTOR_TYPE_ELEMENT:
                case CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
                case CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:
                case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE:
                case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER:
                case CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION:
                case CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP:
                case CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER:
                case CSS_SELECTOR_PSEUDO_ELEMENT_MARKER:
                case CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON:
                case CSS_SELECTOR_PSEUDO_ELEMENT_TARGET_TEXT:
                case CSS_SELECTOR_PSEUDO_ELEMENT_HIGHLIGHT:
                case CSS_SELECTOR_PSEUDO_ELEMENT_SPELLING_ERROR:
                case CSS_SELECTOR_PSEUDO_ELEMENT_GRAMMAR_ERROR:
                case CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION:
                case CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_GROUP:
                case CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_IMAGE_PAIR:
                case CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_OLD:
                case CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_NEW:
                    detail.elements++;
                    break;
                    
                // Special handling for functional pseudo-classes
                case CSS_SELECTOR_PSEUDO_NOT:
                    // :not() contributes the specificity of its argument
                    // (This would require parsing the inner selector)
                    // For now, we'll count it as a class selector
                    detail.classes++;
                    break;
                    
                case CSS_SELECTOR_PSEUDO_IS:
                    // :is() contributes the specificity of its most specific argument
                    detail.is_forgiving = true;
                    detail.classes++; // Simplified - should be max of arguments
                    break;
                    
                case CSS_SELECTOR_PSEUDO_WHERE:
                    // :where() always has zero specificity (CSS4)
                    detail.zero_specificity = true;
                    break;
                    
                case CSS_SELECTOR_PSEUDO_HAS:
                    // :has() contributes the specificity of its argument
                    detail.classes++; // Simplified - should calculate argument specificity
                    break;
                    
                // Universal selector and nesting selector contribute nothing
                case CSS_SELECTOR_TYPE_UNIVERSAL:
                case CSS_SELECTOR_NESTING_PARENT:
                case CSS_SELECTOR_NESTING_DESCENDANT:
                case CSS_SELECTOR_NESTING_PSEUDO:
                    break;
                    
                default:
                    // Unknown selector type - treat as class for safety
                    detail.classes++;
                    break;
            }
            
            component = component->next;
        }
        
        current = current->next;
    }
    
    return detail;
}

// Convert detailed specificity to simple CssSpecificity structure
CssSpecificity css_calculate_specificity(CSSComplexSelector* selector) {
    CSSSpecificityDetail detail = css_calculate_specificity_detailed(selector);
    
    // :where() always has zero specificity
    if (detail.zero_specificity) {
        return css_specificity_create(0, 0, 0, 0, false);
    }
    
    return css_specificity_create(
        detail.inline_style,
        detail.ids,
        detail.classes,
        detail.elements,
        detail.important
    );
}

// Calculate maximum specificity in a selector list
CssSpecificity css_selector_list_max_specificity(CSSSelectorList* list) {
    CssSpecificity max_spec = css_specificity_create(0, 0, 0, 0, false);
    
    if (!list) return max_spec;
    
    CSSComplexSelector* current = list->selectors;
    while (current) {
        CssSpecificity spec = css_calculate_specificity(current);
        if (css_specificity_compare(spec, max_spec) > 0) {
            max_spec = spec;
        }
        current = current->next;
    }
    
    return max_spec;
}

// CSS4 nth-expression parsing (supports "2n+1", "odd", "even", etc.)
CSSNthExpression* css_parse_nth_expression(CSSSelectorParser* parser, const char* expr) {
    if (!expr || !parser || !parser->pool) return NULL;
    
    CSSNthExpression* nth = (CSSNthExpression*)pool_calloc(parser->pool, sizeof(CSSNthExpression));
    if (!nth) return NULL;
    
    // Handle special keywords
    if (strcmp(expr, "odd") == 0) {
        nth->odd = true;
        nth->a = 2;
        nth->b = 1;
        return nth;
    }
    
    if (strcmp(expr, "even") == 0) {
        nth->even = true;
        nth->a = 2;
        nth->b = 0;
        return nth;
    }
    
    // Parse "an+b" format
    const char* p = expr;
    
    // Skip whitespace
    while (*p && isspace(*p)) p++;
    
    // Parse coefficient 'a'
    if (*p == 'n') {
        // Just "n" means "1n"
        nth->a = 1;
        p++;
    } else if (*p == '-' && *(p+1) == 'n') {
        // "-n" means "-1n"
        nth->a = -1;
        p += 2;
    } else if (*p == '+' && *(p+1) == 'n') {
        // "+n" means "+1n"
        nth->a = 1;
        p += 2;
    } else {
        // Parse number before 'n'
        char* endptr;
        long a_val = strtol(p, &endptr, 10);
        if (endptr != p && *endptr == 'n') {
            nth->a = (int)a_val;
            p = endptr + 1;
        } else if (endptr != p && *endptr == '\0') {
            // Just a number with no 'n' (e.g., "5")
            nth->a = 0;
            nth->b = (int)a_val;
            return nth;
        } else {
            // Invalid format
            return NULL;
        }
    }
    
    // Skip whitespace
    while (*p && isspace(*p)) p++;
    
    // Parse constant 'b'
    if (*p == '+') {
        p++;
        while (*p && isspace(*p)) p++;
        char* endptr;
        long b_val = strtol(p, &endptr, 10);
        if (endptr != p) {
            nth->b = (int)b_val;
        }
    } else if (*p == '-') {
        char* endptr;
        long b_val = strtol(p, &endptr, 10);
        if (endptr != p) {
            nth->b = (int)b_val;
        }
    } else if (isdigit(*p)) {
        // Positive number without explicit '+'
        char* endptr;
        long b_val = strtol(p, &endptr, 10);
        if (endptr != p) {
            nth->b = (int)b_val;
        }
    }
    
    return nth;
}

// Parse CSS identifier with validation
bool css_is_valid_identifier(const char* identifier) {
    if (!identifier || *identifier == '\0') return false;
    
    // Must start with letter, underscore, or escaped character
    if (!isalpha(*identifier) && *identifier != '_' && *identifier != '\\') {
        return false;
    }
    
    const char* p = identifier + 1;
    while (*p) {
        if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '\\') {
            return false;
        }
        p++;
    }
    
    return true;
}

// Validate CSS selector syntax
bool css_validate_selector_syntax(const char* selector_text) {
    if (!selector_text) return false;
    
    // Basic validation - check for balanced brackets and quotes
    int paren_count = 0;
    int bracket_count = 0;
    bool in_string = false;
    char quote_char = '\0';
    
    const char* p = selector_text;
    while (*p) {
        if (!in_string) {
            switch (*p) {
                case '(':
                    paren_count++;
                    break;
                case ')':
                    paren_count--;
                    if (paren_count < 0) return false;
                    break;
                case '[':
                    bracket_count++;
                    break;
                case ']':
                    bracket_count--;
                    if (bracket_count < 0) return false;
                    break;
                case '"':
                case '\'':
                    in_string = true;
                    quote_char = *p;
                    break;
            }
        } else {
            if (*p == quote_char && (p == selector_text || *(p-1) != '\\')) {
                in_string = false;
                quote_char = '\0';
            }
        }
        p++;
    }
    
    return paren_count == 0 && bracket_count == 0 && !in_string;
}

// Normalize selector by removing redundant whitespace
char* css_normalize_selector(const char* selector_text, Pool* pool) {
    if (!selector_text || !pool) return NULL;
    
    size_t len = strlen(selector_text);
    char* normalized = (char*)pool_alloc(pool, len + 1);
    if (!normalized) return NULL;
    
    const char* src = selector_text;
    char* dst = normalized;
    bool prev_was_space = true; // Start as true to trim leading space
    bool in_string = false;
    char quote_char = '\0';
    
    while (*src) {
        if (!in_string && (*src == '"' || *src == '\'')) {
            in_string = true;
            quote_char = *src;
            *dst++ = *src;
            prev_was_space = false;
        } else if (in_string && *src == quote_char && (src == selector_text || *(src-1) != '\\')) {
            in_string = false;
            quote_char = '\0';
            *dst++ = *src;
            prev_was_space = false;
        } else if (!in_string && isspace(*src)) {
            if (!prev_was_space) {
                *dst++ = ' ';
                prev_was_space = true;
            }
        } else {
            *dst++ = *src;
            prev_was_space = false;
        }
        src++;
    }
    
    // Trim trailing space
    if (dst > normalized && *(dst-1) == ' ') {
        dst--;
    }
    
    *dst = '\0';
    return normalized;
}

// Create selector parser
CSSSelectorParser* css_selector_parser_create(Pool* pool) {
    if (!pool) return NULL;
    
    CSSSelectorParser* parser = (CSSSelectorParser*)pool_calloc(pool, sizeof(CSSSelectorParser));
    if (!parser) return NULL;
    
    parser->pool = pool;
    parser->allow_nesting = true;  // CSS Nesting is widely supported
    parser->allow_scope = true;    // :scope is supported
    parser->strict_mode = false;   // Allow forgiving parsing by default
    
    return parser;
}

void css_selector_parser_destroy(CSSSelectorParser* parser) {
    // Memory managed by pool, nothing to do
    (void)parser;
}

// Error handling
void css_selector_parser_add_error(CSSSelectorParser* parser, const char* message) {
    if (!parser || !message) return;
    
    // Expand error array if needed
    if (parser->error_count >= 10) return; // Limit errors to prevent memory issues
    
    if (!parser->error_messages) {
        parser->error_messages = (char**)pool_alloc(parser->pool, 10 * sizeof(char*));
        if (!parser->error_messages) return;
    }
    
    // Store error message
    size_t msg_len = strlen(message);
    char* error_copy = (char*)pool_alloc(parser->pool, msg_len + 1);
    if (error_copy) {
        strcpy(error_copy, message);
        parser->error_messages[parser->error_count++] = error_copy;
    }
}

bool css_selector_parser_has_errors(CSSSelectorParser* parser) {
    return parser && parser->error_count > 0;
}

void css_selector_parser_clear_errors(CSSSelectorParser* parser) {
    if (parser) {
        parser->error_count = 0;
    }
}

// CSS4 feature support detection
bool css_supports_nesting(void) {
    return true; // CSS Nesting is supported in this implementation
}

bool css_supports_scope(void) {
    return true; // :scope is supported
}

bool css_supports_has(void) {
    return true; // :has() is supported (parsing only)
}

bool css_supports_forgiving_selectors(void) {
    return true; // :is() and :where() forgiving parsing is supported
}

// Debug utilities
char* css_describe_selector_component(CSSSelectorComponent* component, Pool* pool) {
    if (!component || !pool) return NULL;
    
    char* description = (char*)pool_alloc(pool, 256);
    if (!description) return NULL;
    
    switch (component->type) {
        case CSS_SELECTOR_TYPE_ELEMENT:
            snprintf(description, 256, "Element: %s", component->value ? component->value : "unknown");
            break;
        case CSS_SELECTOR_TYPE_CLASS:
            snprintf(description, 256, "Class: .%s", component->value ? component->value : "unknown");
            break;
        case CSS_SELECTOR_TYPE_ID:
            snprintf(description, 256, "ID: #%s", component->value ? component->value : "unknown");
            break;
        case CSS_SELECTOR_TYPE_UNIVERSAL:
            snprintf(description, 256, "Universal: *");
            break;
        case CSS_SELECTOR_PSEUDO_HOVER:
            snprintf(description, 256, "Pseudo-class: :hover");
            break;
        case CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
            snprintf(description, 256, "Pseudo-element: ::before");
            break;
        default:
            snprintf(description, 256, "Unknown selector type: %d", component->type);
            break;
    }
    
    return description;
}

void css_print_selector_specificity(CSSComplexSelector* selector) {
    if (!selector) {
        printf("Selector specificity: (null selector)\n");
        return;
    }
    
    CSSSpecificityDetail detail = css_calculate_specificity_detailed(selector);
    printf("Selector specificity: (%d, %d, %d, %d)", 
           detail.inline_style, detail.ids, detail.classes, detail.elements);
    
    if (detail.zero_specificity) {
        printf(" [zero specificity - :where()]");
    }
    if (detail.is_forgiving) {
        printf(" [forgiving - :is()]");
    }
    
    printf("\n");
}