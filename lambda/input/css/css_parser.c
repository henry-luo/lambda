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

// Helper: Parse a compound selector (e.g., "p.intro" or "div#main.content")
// A compound selector is a sequence of simple selectors with no whitespace
CssCompoundSelector* css_parse_compound_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    if (!compound) return NULL;

    // Allocate initial array for simple selectors
    int capacity = 4;
    compound->simple_selectors = (CssSimpleSelector**)pool_calloc(pool, capacity * sizeof(CssSimpleSelector*));
    if (!compound->simple_selectors) return NULL;
    compound->simple_selector_count = 0;

    // Parse simple selectors until we hit whitespace, combinator, comma, or brace
    while (*pos < token_count) {
        const CssToken* token = &tokens[*pos];

        // Stop at structural tokens (whitespace, combinators, commas, braces)
        if (token->type == CSS_TOKEN_WHITESPACE ||
            token->type == CSS_TOKEN_COMMA ||
            token->type == CSS_TOKEN_LEFT_BRACE ||
            token->type == CSS_TOKEN_RIGHT_BRACE) {
            break;
        }

        // Stop at combinator delimiters (>, +, ~)
        if (token->type == CSS_TOKEN_DELIM) {
            char delim = token->data.delimiter;
            if (delim == '>' || delim == '+' || delim == '~') {
                break;
            }
        }

        // Try to parse a simple selector (element, class, id, etc.)
        int start_pos = *pos;
        CssSimpleSelector* simple = css_parse_simple_selector_from_tokens(tokens, pos, token_count, pool);

        if (!simple) {
            // If we haven't parsed any selectors yet, this is an error
            if (compound->simple_selector_count == 0) {
                return NULL;
            }
            // Otherwise, we've finished the compound selector
            break;
        }

        // Add the simple selector to the compound
        if (compound->simple_selector_count >= capacity) {
            // Expand array
            capacity *= 2;
            CssSimpleSelector** new_array = (CssSimpleSelector**)pool_calloc(pool, capacity * sizeof(CssSimpleSelector*));
            if (!new_array) return NULL;
            memcpy(new_array, compound->simple_selectors, compound->simple_selector_count * sizeof(CssSimpleSelector*));
            compound->simple_selectors = new_array;
        }

        compound->simple_selectors[compound->simple_selector_count++] = simple;

        fprintf(stderr, "[CSS Parser] Added simple selector to compound (count=%zu)\n", compound->simple_selector_count);

        // Check if position actually advanced
        if (*pos == start_pos) {
            // Position didn't advance, prevent infinite loop
            break;
        }
    }

    if (compound->simple_selector_count == 0) {
        return NULL;
    }

    fprintf(stderr, "[CSS Parser] Parsed compound selector with %zu simple selectors\n", compound->simple_selector_count);
    return compound;
}

// Helper: Parse a full selector with combinators (e.g., "div p.intro" or "nav > ul li")
CssSelector* css_parse_selector_with_combinators(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    CssSelector* selector = (CssSelector*)pool_calloc(pool, sizeof(CssSelector));
    if (!selector) return NULL;

    // Allocate arrays
    int capacity = 4;
    selector->compound_selectors = (CssCompoundSelector**)pool_calloc(pool, capacity * sizeof(CssCompoundSelector*));
    selector->combinators = (CssCombinator*)pool_calloc(pool, capacity * sizeof(CssCombinator));
    if (!selector->compound_selectors || !selector->combinators) return NULL;
    selector->compound_selector_count = 0;

    // Parse first compound selector
    CssCompoundSelector* compound = css_parse_compound_selector_from_tokens(tokens, pos, token_count, pool);
    if (!compound) return NULL;

    selector->compound_selectors[0] = compound;
    selector->compound_selector_count = 1;

    // Parse combinators and subsequent compound selectors
    while (*pos < token_count) {
        int saved_pos = *pos;

        // Check for combinators
        CssCombinator combinator = CSS_COMBINATOR_NONE;
        bool has_whitespace = false;

        // Skip whitespace and detect descendant combinator
        while (*pos < token_count && tokens[*pos].type == CSS_TOKEN_WHITESPACE) {
            has_whitespace = true;
            (*pos)++;
        }

        if (*pos >= token_count) break;

        // Check for explicit combinators (>, +, ~)
        const CssToken* token = &tokens[*pos];
        if (token->type == CSS_TOKEN_DELIM) {
            char delim = token->data.delimiter;
            if (delim == '>') {
                combinator = CSS_COMBINATOR_CHILD;
                (*pos)++;
                fprintf(stderr, "[CSS Parser] Found child combinator '>'\n");
            } else if (delim == '+') {
                combinator = CSS_COMBINATOR_NEXT_SIBLING;
                (*pos)++;
                fprintf(stderr, "[CSS Parser] Found next-sibling combinator '+'\n");
            } else if (delim == '~') {
                combinator = CSS_COMBINATOR_SUBSEQUENT_SIBLING;
                (*pos)++;
                fprintf(stderr, "[CSS Parser] Found subsequent-sibling combinator '~'\n");
            }
        }

        // If no explicit combinator but we had whitespace, it's a descendant combinator
        if (combinator == CSS_COMBINATOR_NONE && has_whitespace) {
            // Check if the next token could start a selector
            if (*pos < token_count) {
                const CssToken* next = &tokens[*pos];
                if (next->type == CSS_TOKEN_IDENT ||
                    (next->type == CSS_TOKEN_DELIM && (next->data.delimiter == '.' || next->data.delimiter == '*')) ||
                    next->type == CSS_TOKEN_HASH) {
                    combinator = CSS_COMBINATOR_DESCENDANT;
                    fprintf(stderr, "[CSS Parser] Detected descendant combinator (whitespace)\n");
                }
            }
        }

        // If we found a combinator, parse the next compound selector
        if (combinator != CSS_COMBINATOR_NONE) {
            // Skip whitespace after combinator
            *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

            // Parse next compound selector
            CssCompoundSelector* next_compound = css_parse_compound_selector_from_tokens(tokens, pos, token_count, pool);
            if (!next_compound) {
                // Restore position if parsing failed
                *pos = saved_pos;
                break;
            }

            // Expand arrays if needed
            if (selector->compound_selector_count >= capacity) {
                capacity *= 2;
                CssCompoundSelector** new_compounds = (CssCompoundSelector**)pool_calloc(pool, capacity * sizeof(CssCompoundSelector*));
                CssCombinator* new_combinators = (CssCombinator*)pool_calloc(pool, capacity * sizeof(CssCombinator));
                if (!new_compounds || !new_combinators) break;

                memcpy(new_compounds, selector->compound_selectors, selector->compound_selector_count * sizeof(CssCompoundSelector*));
                memcpy(new_combinators, selector->combinators, selector->compound_selector_count * sizeof(CssCombinator));

                selector->compound_selectors = new_compounds;
                selector->combinators = new_combinators;
            }

            // Add combinator and compound selector
            selector->combinators[selector->compound_selector_count - 1] = combinator;
            selector->compound_selectors[selector->compound_selector_count] = next_compound;
            selector->compound_selector_count++;

            fprintf(stderr, "[CSS Parser] Added compound selector with combinator (total count=%zu)\n",
                    selector->compound_selector_count);
        } else {
            // No combinator found, restore position and stop
            *pos = saved_pos;
            break;
        }
    }

    fprintf(stderr, "[CSS Parser] Completed selector with %zu compound parts\n", selector->compound_selector_count);
    return selector;
}

// Parse comma-separated selector group (e.g., "h1, h2, h3" or "p.intro, div.outro")
CssSelectorGroup* css_parse_selector_group_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    fprintf(stderr, "[CSS Parser] Parsing selector group at position %d\n", *pos);

    // Initial capacity for selector array
    size_t capacity = 4;
    CssSelector** selectors = (CssSelector**)pool_calloc(pool, capacity * sizeof(CssSelector*));
    if (!selectors) return NULL;

    size_t count = 0;

    // Parse first selector
    CssSelector* first = css_parse_selector_with_combinators(tokens, pos, token_count, pool);
    if (!first) {
        fprintf(stderr, "[CSS Parser] ERROR: Failed to parse first selector in group\n");
        return NULL;
    }
    selectors[count++] = first;
    fprintf(stderr, "[CSS Parser] Parsed selector %zu in group\n", count);

    // Skip whitespace after selector
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Parse additional selectors separated by commas
    while (*pos < token_count && tokens[*pos].type == CSS_TOKEN_COMMA) {
        fprintf(stderr, "[CSS Parser] Found comma, parsing next selector in group\n");
        (*pos)++; // consume comma

        // Skip whitespace after comma
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

        // Parse next selector
        CssSelector* next = css_parse_selector_with_combinators(tokens, pos, token_count, pool);
        if (!next) {
            fprintf(stderr, "[CSS Parser] WARNING: Failed to parse selector after comma, stopping group\n");
            break;
        }

        // Expand array if needed
        if (count >= capacity) {
            capacity *= 2;
            CssSelector** new_selectors = (CssSelector**)pool_calloc(pool, capacity * sizeof(CssSelector*));
            if (!new_selectors) {
                fprintf(stderr, "[CSS Parser] ERROR: Failed to expand selector array\n");
                break;
            }
            memcpy(new_selectors, selectors, count * sizeof(CssSelector*));
            selectors = new_selectors;
        }

        selectors[count++] = next;
        fprintf(stderr, "[CSS Parser] Parsed selector %zu in group\n", count);

        // Skip whitespace after selector
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    }

    // Create selector group
    CssSelectorGroup* group = (CssSelectorGroup*)pool_calloc(pool, sizeof(CssSelectorGroup));
    if (!group) return NULL;

    group->selectors = selectors;
    group->selector_count = count;

    fprintf(stderr, "[CSS Parser] Completed selector group with %zu selectors\n", count);
    return group;
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
        if (token->value && token->value[0] == '#') {
            // Hash token value includes the #, skip it
            selector->value = pool_strdup(pool, token->value + 1);
        } else if (token->value) {
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
        // Skip to next semicolon or right brace to avoid infinite loop
        while (*pos < token_count &&
               tokens[*pos].type != CSS_TOKEN_SEMICOLON &&
               tokens[*pos].type != CSS_TOKEN_RIGHT_BRACE) {
            (*pos)++;
        }
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

    // Create value(s) from tokens
    if (value_count == 1) {
        // Single value - create directly
        for (int i = value_start; i < *pos; i++) {
            if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;

            CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!value) return NULL;

            // Determine value type from token
            if (tokens[i].type == CSS_TOKEN_IDENT) {
                value->type = CSS_VALUE_KEYWORD;
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
                value->type = CSS_VALUE_COLOR;
                value->data.color.type = CSS_COLOR_RGB;

                const char* hex_str = tokens[i].value ? tokens[i].value : NULL;
                if (hex_str && hex_str[0] == '#') {
                    size_t len = strlen(hex_str + 1);
                    unsigned int hex_val = 0;

                    if (len == 6) {
                        if (sscanf(hex_str + 1, "%6x", &hex_val) == 1) {
                            value->data.color.data.rgba.r = (hex_val >> 16) & 0xFF;
                            value->data.color.data.rgba.g = (hex_val >> 8) & 0xFF;
                            value->data.color.data.rgba.b = hex_val & 0xFF;
                            value->data.color.data.rgba.a = 255;
                        }
                    } else if (len == 3) {
                        if (sscanf(hex_str + 1, "%3x", &hex_val) == 1) {
                            unsigned int r = (hex_val >> 8) & 0xF;
                            unsigned int g = (hex_val >> 4) & 0xF;
                            unsigned int b = hex_val & 0xF;
                            value->data.color.data.rgba.r = (r << 4) | r;
                            value->data.color.data.rgba.g = (g << 4) | g;
                            value->data.color.data.rgba.b = (b << 4) | b;
                            value->data.color.data.rgba.a = 255;
                        }
                    } else if (len == 8) {
                        if (sscanf(hex_str + 1, "%8x", &hex_val) == 1) {
                            value->data.color.data.rgba.r = (hex_val >> 24) & 0xFF;
                            value->data.color.data.rgba.g = (hex_val >> 16) & 0xFF;
                            value->data.color.data.rgba.b = (hex_val >> 8) & 0xFF;
                            value->data.color.data.rgba.a = hex_val & 0xFF;
                        }
                    } else if (len == 4) {
                        if (sscanf(hex_str + 1, "%4x", &hex_val) == 1) {
                            unsigned int r = (hex_val >> 12) & 0xF;
                            unsigned int g = (hex_val >> 8) & 0xF;
                            unsigned int b = (hex_val >> 4) & 0xF;
                            unsigned int a = hex_val & 0xF;
                            value->data.color.data.rgba.r = (r << 4) | r;
                            value->data.color.data.rgba.g = (g << 4) | g;
                            value->data.color.data.rgba.b = (b << 4) | b;
                            value->data.color.data.rgba.a = (a << 4) | a;
                        }
                    } else {
                        value->data.color.data.rgba.r = 0;
                        value->data.color.data.rgba.g = 0;
                        value->data.color.data.rgba.b = 0;
                        value->data.color.data.rgba.a = 255;
                    }
                } else {
                    value->data.color.data.rgba.r = 0;
                    value->data.color.data.rgba.g = 0;
                    value->data.color.data.rgba.b = 0;
                    value->data.color.data.rgba.a = 255;
                }
            } else {
                value->type = CSS_VALUE_KEYWORD;
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
            break;
        }
    } else {
        // Multiple values - create list
        CssValue* list_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
        if (!list_value) return NULL;

        list_value->type = CSS_VALUE_LIST;
        list_value->data.list.count = value_count;

        // Allocate array of pointers to CssValue
        list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * value_count);
        if (!list_value->data.list.values) return NULL;        int list_idx = 0;
        for (int i = value_start; i < *pos && list_idx < value_count; i++) {
            if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;

            // Allocate individual CssValue
            CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!value) return NULL;

            list_value->data.list.values[list_idx++] = value;

            if (tokens[i].type == CSS_TOKEN_IDENT) {
                value->type = CSS_VALUE_KEYWORD;
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
                value->type = CSS_VALUE_COLOR;
                value->data.color.type = CSS_COLOR_RGB;

                const char* hex_str = tokens[i].value ? tokens[i].value : NULL;
                if (hex_str && hex_str[0] == '#') {
                    size_t len = strlen(hex_str + 1);
                    unsigned int hex_val = 0;

                    if (len == 6) {
                        if (sscanf(hex_str + 1, "%6x", &hex_val) == 1) {
                            value->data.color.data.rgba.r = (hex_val >> 16) & 0xFF;
                            value->data.color.data.rgba.g = (hex_val >> 8) & 0xFF;
                            value->data.color.data.rgba.b = hex_val & 0xFF;
                            value->data.color.data.rgba.a = 255;
                        }
                    } else if (len == 3) {
                        if (sscanf(hex_str + 1, "%3x", &hex_val) == 1) {
                            unsigned int r = (hex_val >> 8) & 0xF;
                            unsigned int g = (hex_val >> 4) & 0xF;
                            unsigned int b = hex_val & 0xF;
                            value->data.color.data.rgba.r = (r << 4) | r;
                            value->data.color.data.rgba.g = (g << 4) | g;
                            value->data.color.data.rgba.b = (b << 4) | b;
                            value->data.color.data.rgba.a = 255;
                        }
                    } else if (len == 8) {
                        if (sscanf(hex_str + 1, "%8x", &hex_val) == 1) {
                            value->data.color.data.rgba.r = (hex_val >> 24) & 0xFF;
                            value->data.color.data.rgba.g = (hex_val >> 16) & 0xFF;
                            value->data.color.data.rgba.b = (hex_val >> 8) & 0xFF;
                            value->data.color.data.rgba.a = hex_val & 0xFF;
                        }
                    } else if (len == 4) {
                        if (sscanf(hex_str + 1, "%4x", &hex_val) == 1) {
                            unsigned int r = (hex_val >> 12) & 0xF;
                            unsigned int g = (hex_val >> 8) & 0xF;
                            unsigned int b = (hex_val >> 4) & 0xF;
                            unsigned int a = hex_val & 0xF;
                            value->data.color.data.rgba.r = (r << 4) | r;
                            value->data.color.data.rgba.g = (g << 4) | g;
                            value->data.color.data.rgba.b = (b << 4) | b;
                            value->data.color.data.rgba.a = (a << 4) | a;
                        }
                    } else {
                        value->data.color.data.rgba.r = 0;
                        value->data.color.data.rgba.g = 0;
                        value->data.color.data.rgba.b = 0;
                        value->data.color.data.rgba.a = 255;
                    }
                } else {
                    value->data.color.data.rgba.r = 0;
                    value->data.color.data.rgba.g = 0;
                    value->data.color.data.rgba.b = 0;
                    value->data.color.data.rgba.a = 255;
                }
            } else {
                value->type = CSS_VALUE_KEYWORD;
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
        }

        decl->value = list_value;
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

    // Parse selector(s) using enhanced parser (supports compound, descendant, and comma-separated selectors)
    fprintf(stderr, "[CSS Parser] Parsing selectors at position %d\n", pos);

    // Parse selector group (handles single selectors and comma-separated groups)
    CssSelectorGroup* selector_group = css_parse_selector_group_from_tokens(tokens, &pos, token_count, pool);
    if (!selector_group) {
        fprintf(stderr, "[CSS Parser] ERROR: Failed to parse selector group\n");
        return 0;
    }

    fprintf(stderr, "[CSS Parser] Parsed selector group with %zu selector(s)\n", selector_group->selector_count);

    // Skip whitespace
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);

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
    rule->data.style_rule.selector_group = selector_group;
    // For backward compatibility, store the first selector in the single selector field
    rule->data.style_rule.selector = (selector_group->selector_count > 0) ? selector_group->selectors[0] : NULL;
    // Note: selector_list is legacy and uses CSSComplexSelector* (linked list format)
    // Our new CssSelector* uses arrays, so we leave selector_list NULL for now
    rule->selector_list = NULL;
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
