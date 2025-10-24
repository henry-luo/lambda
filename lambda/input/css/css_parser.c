/**
 * CSS Parser Implementation
 *
 * This file contains the core CSS parsing logic that was restored from
 * git history (commit 1f849233~1) and adapted to work with the new
 * modular CSS architecture.
 *
 * Functions:
 * - Token navigation and whitespace skipping
 * - Selector parsing (element, class, ID, universal)
 * - Declaration parsing (property: value with !important support)
 * - Rule parsing with proper token consumption tracking
 */

#include "css_parser.h"
#include "css_style.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper: Skip whitespace and comment tokens
int css_skip_whitespace_tokens(const CssToken* tokens, int start, int token_count) {
    int pos = start;
    while (pos < token_count &&
           (tokens[pos].type == CSS_TOKEN_WHITESPACE ||
            tokens[pos].type == CSS_TOKEN_COMMENT)) {
        pos++;
    }
    return pos;
}

// Helper: Parse a simple CSS selector from tokens (simplified for now)
CssSimpleSelector* css_parse_simple_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    CssSimpleSelector* selector = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
    if (!selector) return NULL;

    const CssToken* token = &tokens[*pos];

    bool matched = false;  // Track if we found a valid selector

    // Parse based on token type
    if (token->type == CSS_TOKEN_IDENT) {
        // Element selector: div, span, etc.
        selector->type = CSS_SELECTOR_TYPE_ELEMENT;
        // Extract selector value from token
        if (token->value) {
            selector->value = pool_strdup(pool, token->value);
        } else if (token->start && token->length > 0) {
            char* value_buf = (char*)pool_calloc(pool, token->length + 1);
            if (value_buf) {
                memcpy(value_buf, token->start, token->length);
                value_buf[token->length] = '\0';
                selector->value = value_buf;
            }
        }
        fprintf(stderr, "[CSS Parser] Element selector: '%s'\n", selector->value ? selector->value : "(null)");
        (*pos)++;
        matched = true;
    } else if (token->type == CSS_TOKEN_DELIM && token->data.delimiter == '.') {
        // Class selector: .classname
        (*pos)++;
        if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_IDENT) {
            selector->type = CSS_SELECTOR_TYPE_CLASS;
            const CssToken* name_token = &tokens[*pos];
            if (name_token->value) {
                selector->value = pool_strdup(pool, name_token->value);
            } else if (name_token->start && name_token->length > 0) {
                char* value_buf = (char*)pool_calloc(pool, name_token->length + 1);
                if (value_buf) {
                    memcpy(value_buf, name_token->start, name_token->length);
                    value_buf[name_token->length] = '\0';
                    selector->value = value_buf;
                }
            }
            fprintf(stderr, "[CSS Parser] Class selector: '.%s'\n", selector->value ? selector->value : "(null)");
            (*pos)++;
            matched = true;
        } else {
            // No identifier after '.', invalid class selector
            fprintf(stderr, "[CSS Parser] ERROR: Expected identifier after '.'\n");
            (*pos)--;  // Back up to the '.' token
        }
    } else if (token->type == CSS_TOKEN_HASH) {
        // ID selector: #identifier
        selector->type = CSS_SELECTOR_TYPE_ID;
        // Extract ID value from token (skip the # character)
        if (token->value) {
            selector->value = pool_strdup(pool, token->value);
        } else if (token->start && token->length > 0) {
            // Hash token includes the #, skip it
            const char* start = token->start + 1;  // Skip '#'
            size_t length = token->length - 1;
            char* value_buf = (char*)pool_calloc(pool, length + 1);
            if (value_buf) {
                memcpy(value_buf, start, length);
                value_buf[length] = '\0';
                selector->value = value_buf;
            }
        }
        fprintf(stderr, "[CSS Parser] ID selector: '#%s'\n", selector->value ? selector->value : "(null)");
        (*pos)++;
        matched = true;
    } else if (token->type == CSS_TOKEN_DELIM && token->data.delimiter == '*') {
        // Universal selector: *
        selector->type = CSS_SELECTOR_TYPE_UNIVERSAL;
        selector->value = "*";
        fprintf(stderr, "[CSS Parser] Universal selector: '*'\n");
        (*pos)++;
        matched = true;
    }

    // If no valid selector was matched, return NULL
    if (!matched) {
        fprintf(stderr, "[CSS Parser] WARNING: No valid selector found at position %d (token type %d)\n",
                *pos, token->type);
        return NULL;
    }

    return selector;
}

// Helper: Parse CSS declaration from tokens
CssDeclaration* css_parse_declaration_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    fprintf(stderr, "[CSS Parser] Parsing declaration at position %d\n", *pos);

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    // Expect property name (identifier)
    if (tokens[*pos].type != CSS_TOKEN_IDENT) {
        fprintf(stderr, "[CSS Parser] Expected IDENT for property, got token type %d\n", tokens[*pos].type);
        return NULL;
    }

    // Extract property name from token (use start/length since value may be NULL)
    const char* property_name;
    if (tokens[*pos].value) {
        property_name = tokens[*pos].value;
    } else if (tokens[*pos].start && tokens[*pos].length > 0) {
        // Create null-terminated string from start/length
        char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
        if (!name_buf) return NULL;
        memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
        name_buf[tokens[*pos].length] = '\0';
        property_name = name_buf;
    } else {
        fprintf(stderr, "[CSS Parser] No property name in token\n");
        return NULL; // No valid property name
    }

    fprintf(stderr, "[CSS Parser] Property name: '%s'\n", property_name);

    (*pos)++;

    // Skip whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Expect colon
    if (*pos >= token_count || tokens[*pos].type != CSS_TOKEN_COLON) return NULL;
    (*pos)++;

    // Skip whitespace after colon
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Parse value tokens until semicolon, right brace, or end
    int value_start = *pos;
    int value_count = 0;
    bool is_important = false;

    while (*pos < token_count) {
        CssTokenType t = tokens[*pos].type;

        // Check for !important
        if (t == CSS_TOKEN_DELIM && tokens[*pos].data.delimiter == '!' &&
            *pos + 1 < token_count && tokens[*pos + 1].type == CSS_TOKEN_IDENT &&
            strcmp(tokens[*pos + 1].value, "important") == 0) {
            is_important = true;
            (*pos) += 2;
            break;
        }

        // Stop at semicolon or closing brace
        if (t == CSS_TOKEN_SEMICOLON || t == CSS_TOKEN_RIGHT_BRACE) {
            break;
        }

        if (t != CSS_TOKEN_WHITESPACE) {
            value_count++;
        }
        (*pos)++;
    }

    if (value_count == 0) {
        fprintf(stderr, "[CSS Parser] No value tokens found\n");
        return NULL;
    }

    // Create declaration
    CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
    if (!decl) return NULL;

    // Get property ID from name
    decl->property_id = css_property_id_from_name(property_name);

    // Debug: Print property name and ID for troubleshooting
    fprintf(stderr, "[CSS Parser] Property: '%s' -> ID: %d, important=%d, value_count=%d\n",
            property_name, decl->property_id, is_important, value_count);

    decl->important = is_important;
    decl->valid = true;
    decl->ref_count = 1;

    // Create a simple CssValue from the first non-whitespace token
    for (int i = value_start; i < *pos; i++) {
        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;

        CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
        if (!value) return NULL;

        // Determine value type from token
        if (tokens[i].type == CSS_TOKEN_IDENT) {
            value->type = CSS_VALUE_KEYWORD;
            // Extract keyword from token start/length if value is NULL
            if (tokens[i].value) {
                value->data.keyword = pool_strdup(pool, tokens[i].value);
            } else if (tokens[i].start && tokens[i].length > 0) {
                char* keyword_buf = (char*)pool_calloc(pool, tokens[i].length + 1);
                if (keyword_buf) {
                    memcpy(keyword_buf, tokens[i].start, tokens[i].length);
                    keyword_buf[tokens[i].length] = '\0';
                    value->data.keyword = keyword_buf;
                }
            }
        } else if (tokens[i].type == CSS_TOKEN_NUMBER) {
            value->type = CSS_VALUE_NUMBER;
            value->data.number.value = tokens[i].data.number_value;
        } else if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            value->type = CSS_VALUE_LENGTH;
            value->data.length.value = tokens[i].data.dimension.value;
            value->data.length.unit = tokens[i].data.dimension.unit;
        } else if (tokens[i].type == CSS_TOKEN_PERCENTAGE) {
            value->type = CSS_VALUE_PERCENTAGE;
            value->data.percentage.value = tokens[i].data.number_value;
        } else if (tokens[i].type == CSS_TOKEN_HASH) {
            // Color value
            value->type = CSS_VALUE_COLOR;
            // Simple hex color parsing - default to black for now
            value->data.color.type = CSS_COLOR_RGB;
            value->data.color.data.rgba.r = 0;
            value->data.color.data.rgba.g = 0;
            value->data.color.data.rgba.b = 0;
            value->data.color.data.rgba.a = 255;
        } else {
            // Default to keyword
            value->type = CSS_VALUE_KEYWORD;
            // Extract keyword from token start/length if value is NULL
            if (tokens[i].value) {
                value->data.keyword = pool_strdup(pool, tokens[i].value);
            } else if (tokens[i].start && tokens[i].length > 0) {
                char* keyword_buf = (char*)pool_calloc(pool, tokens[i].length + 1);
                if (keyword_buf) {
                    memcpy(keyword_buf, tokens[i].start, tokens[i].length);
                    keyword_buf[tokens[i].length] = '\0';
                    value->data.keyword = keyword_buf;
                }
            }
        }

        decl->value = value;
        break; // Use first non-whitespace token for now
    }

    return decl;
}

// Enhanced rule parsing from tokens (returns number of tokens consumed, or 0 on error)
int css_parse_rule_from_tokens_internal(const CssToken* tokens, int token_count, Pool* pool, CssRule** out_rule) {
    if (!tokens || token_count <= 0 || !pool || !out_rule) return 0;

    fprintf(stderr, "[CSS Parser] Parsing rule from %d tokens\n", token_count);

    int pos = 0;
    int start_pos = 0;

    // Skip leading whitespace and comments
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    if (pos >= token_count) {
        fprintf(stderr, "[CSS Parser] No tokens after whitespace skip\n");
        return 0;
    }

    start_pos = pos;

    // Check for @-rules
    if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
        fprintf(stderr, "[CSS Parser] Skipping @-rule (not yet supported)\n");
        // For now, skip @-rules (we don't parse them yet)
        // Find the ending semicolon or closing brace
        while (pos < token_count &&
               tokens[pos].type != CSS_TOKEN_SEMICOLON &&
               tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
            pos++;
        }
        if (pos < token_count) pos++; // consume the semicolon/brace
        *out_rule = NULL; // Skip @-rules for now
        return pos - start_pos; // Return tokens consumed
    }

    // Parse selector(s) - handle multiple comma-separated selectors
    fprintf(stderr, "[CSS Parser] Parsing selectors at position %d\n", pos);

    // Parse first selector
    CssSimpleSelector* simple_sel = css_parse_simple_selector_from_tokens(tokens, &pos, token_count, pool);
    if (!simple_sel) {
        fprintf(stderr, "[CSS Parser] ERROR: Failed to parse selector\n");
        return 0;
    }

    fprintf(stderr, "[CSS Parser] Parsed selector: type=%d, value='%s'\n",
            simple_sel->type, simple_sel->value ? simple_sel->value : "(null)");

    // Create compound selector
    CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    if (!compound) return 0;

    compound->simple_selectors = (CssSimpleSelector**)pool_calloc(pool, sizeof(CssSimpleSelector*));
    compound->simple_selectors[0] = simple_sel;
    compound->simple_selector_count = 1;

    // Create full selector
    CssSelector* selector = (CssSelector*)pool_calloc(pool, sizeof(CssSelector));
    if (!selector) return 0;

    selector->compound_selectors = (CssCompoundSelector**)pool_calloc(pool, sizeof(CssCompoundSelector*));
    selector->compound_selectors[0] = compound;
    selector->compound_selector_count = 1;
    selector->combinators = NULL;

    // Skip whitespace
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);

    // Check for additional selectors (comma-separated: "h1, h2, h3")
    while (pos < token_count && tokens[pos].type == CSS_TOKEN_COMMA) {
        fprintf(stderr, "[CSS Parser] Found comma, parsing additional selector\n");
        pos++;  // Skip comma

        // Skip whitespace after comma
        pos = css_skip_whitespace_tokens(tokens, pos, token_count);

        // Parse next selector
        CssSimpleSelector* next_sel = css_parse_simple_selector_from_tokens(tokens, &pos, token_count, pool);
        if (!next_sel) {
            fprintf(stderr, "[CSS Parser] WARNING: Failed to parse selector after comma\n");
            break;
        }

        fprintf(stderr, "[CSS Parser] Parsed additional selector: type=%d, value='%s'\n",
                next_sel->type, next_sel->value ? next_sel->value : "(null)");

        // Create compound selector for this additional selector
        CssCompoundSelector* next_compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
        if (!next_compound) break;

        next_compound->simple_selectors = (CssSimpleSelector**)pool_calloc(pool, sizeof(CssSimpleSelector*));
        next_compound->simple_selectors[0] = next_sel;
        next_compound->simple_selector_count = 1;

        // Expand selector's compound_selectors array
        int new_count = selector->compound_selector_count + 1;
        CssCompoundSelector** new_array = (CssCompoundSelector**)pool_calloc(pool,
                                                                               new_count * sizeof(CssCompoundSelector*));
        if (new_array) {
            memcpy(new_array, selector->compound_selectors,
                   selector->compound_selector_count * sizeof(CssCompoundSelector*));
            new_array[selector->compound_selector_count] = next_compound;
            selector->compound_selectors = new_array;
            selector->compound_selector_count = new_count;
        }

        // Skip whitespace after selector
        pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    }

    fprintf(stderr, "[CSS Parser] Total selectors in rule: %d\n", selector->compound_selector_count);

    // Expect opening brace
    if (pos >= token_count || tokens[pos].type != CSS_TOKEN_LEFT_BRACE) {
        fprintf(stderr, "[CSS Parser] ERROR: Expected '{' but got token type %d at position %d\n",
                pos < token_count ? tokens[pos].type : -1, pos);
        return 0;
    }
    fprintf(stderr, "[CSS Parser] Found '{', parsing declarations\n");
    pos++;

    // Parse declarations
    CssDeclaration** declarations = NULL;
    int decl_count = 0;
    int decl_capacity = 4;
    declarations = (CssDeclaration**)pool_calloc(pool, decl_capacity * sizeof(CssDeclaration*));

    while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
        // Skip whitespace
        pos = css_skip_whitespace_tokens(tokens, pos, token_count);
        if (pos >= token_count || tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

        // Parse declaration
        CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, token_count, pool);
        if (decl) {
            // Expand array if needed
            if (decl_count >= decl_capacity) {
                decl_capacity *= 2;
                CssDeclaration** new_decls = (CssDeclaration**)pool_calloc(pool, decl_capacity * sizeof(CssDeclaration*));
                memcpy(new_decls, declarations, decl_count * sizeof(CssDeclaration*));
                declarations = new_decls;
            }
            declarations[decl_count++] = decl;
        }

        // Skip optional semicolon
        if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
            pos++;
        }
    }

    // Expect closing brace
    if (pos >= token_count || tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
        return 0; // Missing closing brace
    }
    pos++; // consume the closing brace

    // Create the CSS rule
    CssRule* rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (!rule) return 0;

    rule->type = CSS_RULE_STYLE;
    rule->pool = pool;
    rule->data.style_rule.selector = selector;
    rule->data.style_rule.declarations = declarations;
    rule->data.style_rule.declaration_count = decl_count;

    *out_rule = rule;
    return pos - start_pos; // Return number of tokens consumed
}

// Legacy wrapper that returns CssRule* (for compatibility)
CssRule* css_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool) {
    CssRule* rule = NULL;
    css_parse_rule_from_tokens_internal(tokens, token_count, pool, &rule);
    return rule;
}

// Backward compatibility wrapper with old name
CssRule* css_enhanced_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool) {
    return css_parse_rule_from_tokens(tokens, token_count, pool);
}
