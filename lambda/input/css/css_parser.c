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
#include "../../../lib/log.h"
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
    size_t capacity = 4;
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
    size_t capacity = 4;
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
    } else if (token->type == CSS_TOKEN_FUNCTION) {
        // Functional pseudo-class like nth-child(1)
        // Token value contains the function name without the opening parenthesis
        fprintf(stderr, "[CSS Parser] Detected CSS_TOKEN_FUNCTION: '%s'\n", token->value ? token->value : "(null)");
        const char* func_name = token->value;
        if (!func_name && token->start && token->length > 0) {
            char* name_buf = (char*)pool_calloc(pool, token->length + 1);
            if (name_buf) {
                memcpy(name_buf, token->start, token->length);
                name_buf[token->length] = '\0';
                func_name = name_buf;
            }
        }

        // Function tokens include the opening '(', strip it
        size_t func_len = func_name ? strlen(func_name) : 0;
        if (func_len > 0 && func_name[func_len - 1] == '(') {
            char* clean_name = (char*)pool_calloc(pool, func_len);
            if (clean_name) {
                memcpy(clean_name, func_name, func_len - 1);
                clean_name[func_len - 1] = '\0';
                func_name = clean_name;
            }
        }

        (*pos)++; // skip FUNCTION token

        // Collect tokens until matching ')'
        int arg_start = *pos;
        int paren_depth = 1; // Already inside the function
        while (*pos < token_count && paren_depth > 0) {
            if (tokens[*pos].type == CSS_TOKEN_LEFT_PAREN) {
                paren_depth++;
            } else if (tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                paren_depth--;
                if (paren_depth == 0) break;
            }
            (*pos)++;
        }

        // Extract argument as string
        char* pseudo_arg = NULL;
        if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
            int arg_end = *pos;
            // Build argument string from tokens
            size_t arg_len = 0;
            for (int i = arg_start; i < arg_end; i++) {
                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                if (tokens[i].value) {
                    arg_len += strlen(tokens[i].value);
                } else if (tokens[i].length > 0) {
                    arg_len += tokens[i].length;
                }
            }

            if (arg_len > 0) {
                pseudo_arg = (char*)pool_calloc(pool, arg_len + 1);
                if (pseudo_arg) {
                    char* p = pseudo_arg;
                    for (int i = arg_start; i < arg_end; i++) {
                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[i].value) {
                            size_t len = strlen(tokens[i].value);
                            memcpy(p, tokens[i].value, len);
                            p += len;
                        } else if (tokens[i].start && tokens[i].length > 0) {
                            memcpy(p, tokens[i].start, tokens[i].length);
                            p += tokens[i].length;
                        }
                    }
                    *p = '\0';
                }
            }

            (*pos)++; // skip ')'
        }

        // Map function name to pseudo-class type
        if (strcmp(func_name, "nth-child") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_NTH_CHILD;
        } else if (strcmp(func_name, "nth-of-type") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_NTH_OF_TYPE;
        } else if (strcmp(func_name, "nth-last-child") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD;
        } else if (strcmp(func_name, "nth-last-of-type") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
        } else if (strcmp(func_name, "not") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_NOT;
        } else if (strcmp(func_name, "is") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_IS;
        } else if (strcmp(func_name, "where") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_WHERE;
        } else if (strcmp(func_name, "has") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_HAS;
        } else if (strcmp(func_name, "lang") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_LANG;
        } else if (strcmp(func_name, "dir") == 0) {
            selector->type = CSS_SELECTOR_PSEUDO_DIR;
        } else if (strcmp(func_name, "host") == 0 || strcmp(func_name, "host-context") == 0) {
            // :host() is for Shadow DOM
            selector->type = CSS_SELECTOR_PSEUDO_IS; // treat similar to :is()
            fprintf(stderr, "[CSS Parser] Shadow DOM function: '%s()'\n", func_name);
        } else {
            // Accept unknown pseudo-class functions
            selector->type = CSS_SELECTOR_PSEUDO_NOT; // default fallback
            fprintf(stderr, "[CSS Parser] Generic functional pseudo-class: '%s()'\n", func_name);
        }

        selector->value = func_name;
        selector->argument = pseudo_arg;

        fprintf(stderr, "[CSS Parser] Functional pseudo-class: '%s(%s)'\n",
               func_name, pseudo_arg ? pseudo_arg : "");
        matched = true;
    } else if (token->type == CSS_TOKEN_COLON) {
        // Pseudo-class selector: :hover, :nth-child(), etc.
        (*pos)++;
        if (*pos < token_count) {
            const CssToken* pseudo_token = &tokens[*pos];

            // Check for pseudo-element (double colon ::before, ::after)
            if (pseudo_token->type == CSS_TOKEN_COLON) {
                // This is a pseudo-element
                (*pos)++;
                if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_IDENT) {
                    const char* elem_name = tokens[*pos].value;
                    if (!elem_name && tokens[*pos].start && tokens[*pos].length > 0) {
                        char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
                        if (name_buf) {
                            memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
                            name_buf[tokens[*pos].length] = '\0';
                            elem_name = name_buf;
                        }
                    }

                    (*pos)++;

                    // Map pseudo-element name to type
                    if (strcmp(elem_name, "before") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE;
                    } else if (strcmp(elem_name, "after") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_AFTER;
                    } else if (strcmp(elem_name, "first-line") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE;
                    } else if (strcmp(elem_name, "first-letter") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER;
                    } else if (strcmp(elem_name, "selection") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION;
                    } else if (strcmp(elem_name, "backdrop") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP;
                    } else if (strcmp(elem_name, "placeholder") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER;
                    } else if (strcmp(elem_name, "marker") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_MARKER;
                    } else if (strcmp(elem_name, "file-selector-button") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON;
                    } else {
                        // Accept but treat as generic pseudo-element
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE; // default fallback
                        fprintf(stderr, "[CSS Parser] Generic pseudo-element: '::%s'\n", elem_name);
                    }

                    selector->value = elem_name;
                    selector->argument = NULL;
                    fprintf(stderr, "[CSS Parser] Pseudo-element: '::%s'\n", elem_name);
                    matched = true;
                }
                if (!matched) {
                    return NULL;
                }
            }

            // Single colon - pseudo-class
            else if (pseudo_token->type == CSS_TOKEN_IDENT) {
                const char* pseudo_name = pseudo_token->value;
                if (!pseudo_name && pseudo_token->start && pseudo_token->length > 0) {
                    char* name_buf = (char*)pool_calloc(pool, pseudo_token->length + 1);
                    if (name_buf) {
                        memcpy(name_buf, pseudo_token->start, pseudo_token->length);
                        name_buf[pseudo_token->length] = '\0';
                        pseudo_name = name_buf;
                    }
                }

                (*pos)++;

                // Map pseudo-class name to type (comprehensive list)
                if (strcmp(pseudo_name, "first-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FIRST_CHILD;
                } else if (strcmp(pseudo_name, "last-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LAST_CHILD;
                } else if (strcmp(pseudo_name, "only-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ONLY_CHILD;
                } else if (strcmp(pseudo_name, "first-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE;
                } else if (strcmp(pseudo_name, "last-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LAST_OF_TYPE;
                } else if (strcmp(pseudo_name, "only-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE;
                } else if (strcmp(pseudo_name, "root") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ROOT;
                } else if (strcmp(pseudo_name, "empty") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_EMPTY;
                } else if (strcmp(pseudo_name, "hover") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_HOVER;
                } else if (strcmp(pseudo_name, "active") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ACTIVE;
                } else if (strcmp(pseudo_name, "focus") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS;
                } else if (strcmp(pseudo_name, "focus-visible") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE;
                } else if (strcmp(pseudo_name, "focus-within") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS_WITHIN;
                } else if (strcmp(pseudo_name, "visited") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_VISITED;
                } else if (strcmp(pseudo_name, "link") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LINK;
                } else if (strcmp(pseudo_name, "any-link") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ANY_LINK;
                } else if (strcmp(pseudo_name, "enabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ENABLED;
                } else if (strcmp(pseudo_name, "disabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DISABLED;
                } else if (strcmp(pseudo_name, "checked") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_CHECKED;
                } else if (strcmp(pseudo_name, "indeterminate") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INDETERMINATE;
                } else if (strcmp(pseudo_name, "valid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_VALID;
                } else if (strcmp(pseudo_name, "invalid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INVALID;
                } else if (strcmp(pseudo_name, "required") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_REQUIRED;
                } else if (strcmp(pseudo_name, "optional") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OPTIONAL;
                } else if (strcmp(pseudo_name, "read-only") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_READ_ONLY;
                } else if (strcmp(pseudo_name, "read-write") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_READ_WRITE;
                } else if (strcmp(pseudo_name, "placeholder-shown") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN;
                } else if (strcmp(pseudo_name, "default") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DEFAULT;
                } else if (strcmp(pseudo_name, "in-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_IN_RANGE;
                } else if (strcmp(pseudo_name, "out-of-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OUT_OF_RANGE;
                } else if (strcmp(pseudo_name, "target") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_TARGET;
                } else if (strcmp(pseudo_name, "scope") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_SCOPE;
                } else if (strcmp(pseudo_name, "fullscreen") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FULLSCREEN;
                } else {
                    // Accept unknown pseudo-classes but use a generic type
                    selector->type = CSS_SELECTOR_PSEUDO_HOVER; // default fallback
                    fprintf(stderr, "[CSS Parser] Generic pseudo-class: ':%s'\n", pseudo_name);
                }

                selector->value = pseudo_name;
                selector->argument = NULL;

                fprintf(stderr, "[CSS Parser] Simple pseudo-class: ':%s'\n", pseudo_name);
                matched = true;

            } else if (pseudo_token->type == CSS_TOKEN_FUNCTION) {
                // Functional pseudo-class after colon: :nth-child(...)
                const char* func_name = pseudo_token->value;
                if (!func_name && pseudo_token->start && pseudo_token->length > 0) {
                    char* name_buf = (char*)pool_calloc(pool, pseudo_token->length + 1);
                    if (name_buf) {
                        memcpy(name_buf, pseudo_token->start, pseudo_token->length);
                        name_buf[pseudo_token->length] = '\0';
                        func_name = name_buf;
                    }
                }

                // Function tokens include the opening '(', strip it
                size_t func_len = func_name ? strlen(func_name) : 0;
                if (func_len > 0 && func_name[func_len - 1] == '(') {
                    char* clean_name = (char*)pool_calloc(pool, func_len);
                    if (clean_name) {
                        memcpy(clean_name, func_name, func_len - 1);
                        clean_name[func_len - 1] = '\0';
                        func_name = clean_name;
                    }
                }

                (*pos)++; // skip FUNCTION token

                // Collect tokens until matching ')'
                int arg_start = *pos;
                int paren_depth = 1; // Already inside the function
                while (*pos < token_count && paren_depth > 0) {
                    if (tokens[*pos].type == CSS_TOKEN_LEFT_PAREN) {
                        paren_depth++;
                    } else if (tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                        paren_depth--;
                        if (paren_depth == 0) break;
                    }
                    (*pos)++;
                }

                // Extract argument as string
                char* pseudo_arg = NULL;
                if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                    int arg_end = *pos;
                    // Build argument string from tokens
                    size_t arg_len = 0;
                    for (int i = arg_start; i < arg_end; i++) {
                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[i].value) {
                            arg_len += strlen(tokens[i].value);
                        } else if (tokens[i].length > 0) {
                            arg_len += tokens[i].length;
                        }
                    }

                    if (arg_len > 0) {
                        pseudo_arg = (char*)pool_calloc(pool, arg_len + 1);
                        if (pseudo_arg) {
                            char* p = pseudo_arg;
                            for (int i = arg_start; i < arg_end; i++) {
                                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                                if (tokens[i].value) {
                                    size_t len = strlen(tokens[i].value);
                                    memcpy(p, tokens[i].value, len);
                                    p += len;
                                } else if (tokens[i].start && tokens[i].length > 0) {
                                    memcpy(p, tokens[i].start, tokens[i].length);
                                    p += tokens[i].length;
                                }
                            }
                            *p = '\0';
                        }
                    }

                    (*pos)++; // skip ')'
                }

                // Map function name to pseudo-class type
                if (strcmp(func_name, "nth-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_NTH_CHILD;
                } else if (strcmp(func_name, "nth-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_NTH_OF_TYPE;
                } else if (strcmp(func_name, "nth-last-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD;
                } else if (strcmp(func_name, "nth-last-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
                } else if (strcmp(func_name, "not") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_NOT;
                } else if (strcmp(func_name, "is") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_IS;
                } else if (strcmp(func_name, "where") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_WHERE;
                } else if (strcmp(func_name, "has") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_HAS;
                } else if (strcmp(func_name, "lang") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LANG;
                } else if (strcmp(func_name, "dir") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DIR;
                } else if (strcmp(func_name, "host") == 0 || strcmp(func_name, "host-context") == 0) {
                    // :host() is for Shadow DOM
                    selector->type = CSS_SELECTOR_PSEUDO_IS; // treat similar to :is()
                    fprintf(stderr, "[CSS Parser] Shadow DOM pseudo-class: ':%s()'\n", func_name);
                } else {
                    // Accept unknown functional pseudo-classes
                    selector->type = CSS_SELECTOR_PSEUDO_NOT; // default fallback
                    fprintf(stderr, "[CSS Parser] Generic functional pseudo-class: ':%s()'\n", func_name);
                }

                selector->value = func_name;
                selector->argument = pseudo_arg;

                fprintf(stderr, "[CSS Parser] Functional pseudo-class after colon: ':%s(%s)'\n",
                       func_name, pseudo_arg ? pseudo_arg : "");
                matched = true;
            }
        }
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

        // Count non-whitespace, non-comma tokens as values
        if (t != CSS_TOKEN_WHITESPACE && t != CSS_TOKEN_COMMA) {
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
    log_debug("[CSS Parser] Property: '%s' -> ID: %d, important=%d, value_count=%d",
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
                const char* token_val = NULL;
                if (tokens[i].value) {
                    token_val = tokens[i].value;
                } else if (tokens[i].start && tokens[i].length > 0) {
                    char* keyword_buf = (char*)pool_calloc(pool, tokens[i].length + 1);
                    if (keyword_buf) {
                        memcpy(keyword_buf, tokens[i].start, tokens[i].length);
                        keyword_buf[tokens[i].length] = '\0';
                        token_val = keyword_buf;
                    }
                }

                // Strip quotes from keyword (font names can be quoted)
                if (token_val) {
                    size_t len = strlen(token_val);
                    if (len >= 2 && ((token_val[0] == '\'' && token_val[len-1] == '\'') ||
                                     (token_val[0] == '"' && token_val[len-1] == '"'))) {
                        // Create unquoted copy
                        char* unquoted = (char*)pool_calloc(pool, len - 1);
                        if (unquoted) {
                            memcpy(unquoted, token_val + 1, len - 2);
                            unquoted[len - 2] = '\0';
                            value->data.keyword = unquoted;
                        } else {
                            value->data.keyword = pool_strdup(pool, token_val);
                        }
                    } else {
                        value->data.keyword = pool_strdup(pool, token_val);
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
            if (tokens[i].type == CSS_TOKEN_WHITESPACE || tokens[i].type == CSS_TOKEN_COMMA) continue;

            // Allocate individual CssValue
            CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!value) return NULL;

            list_value->data.list.values[list_idx++] = value;

            if (tokens[i].type == CSS_TOKEN_IDENT) {
                value->type = CSS_VALUE_KEYWORD;
                const char* token_val = NULL;
                if (tokens[i].value) {
                    token_val = tokens[i].value;
                } else if (tokens[i].start && tokens[i].length > 0) {
                    char* keyword_buf = (char*)pool_calloc(pool, tokens[i].length + 1);
                    if (keyword_buf) {
                        memcpy(keyword_buf, tokens[i].start, tokens[i].length);
                        keyword_buf[tokens[i].length] = '\0';
                        token_val = keyword_buf;
                    }
                }

                // Strip quotes from keyword (font names can be quoted)
                if (token_val) {
                    size_t len = strlen(token_val);
                    if (len >= 2 && ((token_val[0] == '\'' && token_val[len-1] == '\'') ||
                                     (token_val[0] == '"' && token_val[len-1] == '"'))) {
                        // Create unquoted copy
                        char* unquoted = (char*)pool_calloc(pool, len - 1);
                        if (unquoted) {
                            memcpy(unquoted, token_val + 1, len - 2);
                            unquoted[len - 2] = '\0';
                            value->data.keyword = unquoted;
                        } else {
                            value->data.keyword = pool_strdup(pool, token_val);
                        }
                    } else {
                        value->data.keyword = pool_strdup(pool, token_val);
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

    // Debug: print the value type
    if (decl->value) {
        log_debug("[CSS Parse] Declaration for property ID %d: value type = %d",
               decl->property_id, decl->value->type);
        if (decl->value->type == CSS_VALUE_LENGTH) {
            log_debug("[CSS Parse]   Length value = %.2f", decl->value->data.length.value);
        }
    }

    // Validate the parsed value before returning
    if (decl->value) {
        bool disallow_negative = false;
        float value_to_check = 0;

        // Check if this is a length or number value
        if (decl->value->type == CSS_VALUE_LENGTH) {
            value_to_check = decl->value->data.length.value;
        } else if (decl->value->type == CSS_VALUE_NUMBER) {
            value_to_check = decl->value->data.number.value;
        } else {
            // Not a numeric value, skip validation
            return decl;
        }

        // Properties that cannot have negative values
        switch (decl->property_id) {
            // width, height, and their min/max variants cannot be negative
            case CSS_PROPERTY_WIDTH:
            case CSS_PROPERTY_HEIGHT:
            case CSS_PROPERTY_MIN_WIDTH:
            case CSS_PROPERTY_MIN_HEIGHT:
            case CSS_PROPERTY_MAX_WIDTH:
            case CSS_PROPERTY_MAX_HEIGHT:
            // padding properties cannot be negative
            case CSS_PROPERTY_PADDING_TOP:
            case CSS_PROPERTY_PADDING_RIGHT:
            case CSS_PROPERTY_PADDING_BOTTOM:
            case CSS_PROPERTY_PADDING_LEFT:
            case CSS_PROPERTY_PADDING_BLOCK:
            case CSS_PROPERTY_PADDING_BLOCK_START:
            case CSS_PROPERTY_PADDING_BLOCK_END:
            case CSS_PROPERTY_PADDING_INLINE:
            case CSS_PROPERTY_PADDING_INLINE_START:
            case CSS_PROPERTY_PADDING_INLINE_END:
            // border widths cannot be negative
            case CSS_PROPERTY_BORDER_TOP_WIDTH:
            case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
            case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
            case CSS_PROPERTY_BORDER_LEFT_WIDTH:
            case CSS_PROPERTY_BORDER_WIDTH:
                disallow_negative = true;
                break;

            // margins, positioning (top/right/bottom/left) CAN be negative
            default:
                disallow_negative = false;
                break;
        }

        if (disallow_negative && value_to_check < 0) {
            // reject negative value for properties that don't allow it
            // per CSS spec, return NULL to prevent invalid declaration from entering cascade
            log_debug("[CSS Parse] Rejecting negative value %.2f for property ID %d",
                   value_to_check, decl->property_id);
            return NULL;
        }
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
        const char* at_keyword = tokens[pos].value;
        fprintf(stderr, "[CSS Parser] Parsing @-rule: %s\n", at_keyword ? at_keyword : "(null)");
        pos++; // consume @keyword token

        // Skip leading '@' in keyword name if present
        const char* keyword_name = at_keyword;
        if (keyword_name && keyword_name[0] == '@') {
            keyword_name++;
        }

        // Create rule structure
        CssRule* rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
        if (!rule) {
            fprintf(stderr, "[CSS Parser] ERROR: Failed to allocate rule\n");
            return 0;
        }
        rule->pool = pool;

        // Determine rule type and parse accordingly
        if (keyword_name && (strcmp(keyword_name, "media") == 0 ||
                          strcmp(keyword_name, "supports") == 0 ||
                          strcmp(keyword_name, "container") == 0)) {
            // Conditional at-rules: @media, @supports, @container
            rule->type = strcmp(keyword_name, "media") == 0 ? CSS_RULE_MEDIA :
                        strcmp(keyword_name, "supports") == 0 ? CSS_RULE_SUPPORTS :
                        CSS_RULE_CONTAINER;            // Parse condition (everything until '{')
            int cond_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_LEFT_BRACE) {
                pos++;
            }

            // Extract condition text
            if (pos > cond_start) {
                size_t cond_length = 0;
                for (int i = cond_start; i < pos; i++) {
                    if (tokens[i].value) {
                        cond_length += strlen(tokens[i].value) + 1; // +1 for space
                    }
                }

                char* condition = (char*)pool_alloc(pool, cond_length + 1);
                if (condition) {
                    condition[0] = '\0';
                    for (int i = cond_start; i < pos; i++) {
                        if (tokens[i].value) {
                            if (condition[0] != '\0') strcat(condition, " ");
                            strcat(condition, tokens[i].value);
                        }
                    }
                    rule->data.conditional_rule.condition = condition;
                }
            }

            // Parse block with nested rules
            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                pos++; // consume '{'

                // Parse nested rules
                int nested_capacity = 4;
                rule->data.conditional_rule.rules = (CssRule**)pool_calloc(pool,
                    nested_capacity * sizeof(CssRule*));
                rule->data.conditional_rule.rule_count = 0;

                while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                    // Skip whitespace
                    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
                    if (pos >= token_count || tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

                    // Recursively parse nested rule
                    CssRule* nested_rule = NULL;
                    int nested_consumed = css_parse_rule_from_tokens_internal(
                        tokens + pos, token_count - pos, pool, &nested_rule);

                    if (nested_consumed > 0) {
                        pos += nested_consumed;
                        if (nested_rule) {
                            // Expand array if needed
                            if (rule->data.conditional_rule.rule_count >= nested_capacity) {
                                nested_capacity *= 2;
                                CssRule** new_rules = (CssRule**)pool_alloc(pool,
                                    nested_capacity * sizeof(CssRule*));
                                memcpy(new_rules, rule->data.conditional_rule.rules,
                                    rule->data.conditional_rule.rule_count * sizeof(CssRule*));
                                rule->data.conditional_rule.rules = new_rules;
                            }
                            rule->data.conditional_rule.rules[rule->data.conditional_rule.rule_count++] = nested_rule;
                        }
                    } else {
                        break;
                    }
                }

                if (pos < token_count && tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
                    pos++; // consume '}'
                }
            }

            *out_rule = rule;
            fprintf(stderr, "[CSS Parser] Parsed conditional @-rule with %zu nested rules\n",
                rule->data.conditional_rule.rule_count);
            return pos - start_pos;

        } else if (keyword_name && (strcmp(keyword_name, "import") == 0 ||
                                 strcmp(keyword_name, "charset") == 0)) {
            // Simple at-rules with no block
            rule->type = strcmp(keyword_name, "import") == 0 ? CSS_RULE_IMPORT : CSS_RULE_CHARSET;            // Parse until semicolon
            int value_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }

            // Extract value
            if (pos > value_start && tokens[value_start].value) {
                if (rule->type == CSS_RULE_IMPORT) {
                    rule->data.import_rule.url = tokens[value_start].value;
                } else {
                    rule->data.charset_rule.charset = tokens[value_start].value;
                }
            }

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                pos++; // consume ';'
            }

            *out_rule = rule;
            fprintf(stderr, "[CSS Parser] Parsed simple @-rule: %s\n", keyword_name);
            return pos - start_pos;

        } else {
            // Other at-rules (like @font-face, @keyframes) - store raw content
            // Determine specific type
            if (keyword_name && strcmp(keyword_name, "font-face") == 0) {
                rule->type = CSS_RULE_FONT_FACE;
            } else if (keyword_name && strcmp(keyword_name, "keyframes") == 0) {
                rule->type = CSS_RULE_KEYFRAMES;
            } else {
                // Unknown at-rule - skip it
                fprintf(stderr, "[CSS Parser] Skipping unknown @-rule: %s\n", keyword_name ? keyword_name : "null");
                return 0;
            }

            // Store the name
            rule->data.generic_rule.name = keyword_name;

            // Build prefix content (e.g., animation name for @keyframes)
            // This is everything between the at-keyword and the opening brace
            int prefix_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_LEFT_BRACE &&
                   tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }
            int prefix_end = pos;

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                int brace_start = pos;
                pos++; // consume '{'
                int content_start = pos; // Content starts after '{'

                // Skip contents until closing brace
                int brace_depth = 1;
                while (pos < token_count && brace_depth > 0) {
                    if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                        brace_depth++;
                    } else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
                        brace_depth--;
                    }
                    pos++;
                }
                int content_end = pos - 1; // Content ends before '}'

                // Build content string: prefix + { + content + }
                size_t content_length = 0;
                // Calculate prefix length
                for (int i = prefix_start; i < prefix_end; i++) {
                    if (tokens[i].value && tokens[i].type != CSS_TOKEN_WHITESPACE) {
                        content_length += strlen(tokens[i].value) + 1;
                    }
                }
                // Calculate body content length
                for (int i = content_start; i < content_end; i++) {
                    if (tokens[i].value) {
                        content_length += strlen(tokens[i].value) + 1;
                    }
                }
                content_length += 10; // for { }, spaces, etc.

                char* content = (char*)pool_alloc(pool, content_length + 1);
                content[0] = '\0';

                // Build prefix (e.g., "fadeIn")
                for (int i = prefix_start; i < prefix_end; i++) {
                    if (tokens[i].value && tokens[i].type != CSS_TOKEN_WHITESPACE) {
                        if (content[0] != '\0') {
                            strcat(content, " ");
                        }
                        strcat(content, tokens[i].value);
                    }
                }

                // Add opening brace
                if (content[0] != '\0') {
                    strcat(content, " ");
                }
                strcat(content, "{");

                // Build body content
                for (int i = content_start; i < content_end; i++) {
                    if (tokens[i].value) {
                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
                            // Preserve some whitespace for readability
                            strcat(content, " ");
                        } else {
                            // Add space before tokens that need it
                            if (strlen(content) > 0 && content[strlen(content)-1] != '{' &&
                                content[strlen(content)-1] != ' ' &&
                                tokens[i].type != CSS_TOKEN_SEMICOLON &&
                                tokens[i].type != CSS_TOKEN_COLON &&
                                tokens[i].type != CSS_TOKEN_COMMA &&
                                tokens[i].type != CSS_TOKEN_RIGHT_BRACE) {
                                strcat(content, " ");
                            }
                            strcat(content, tokens[i].value);
                        }
                    }
                }

                // Add closing brace
                strcat(content, " }");

                rule->data.generic_rule.content = content;
                fprintf(stderr, "[CSS Parser] Stored content for %s: '%s'\n", keyword_name, content);

            } else if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                pos++; // consume ';'
            }

            *out_rule = rule;
            fprintf(stderr, "[CSS Parser] Parsed generic @-rule: %s\n", keyword_name);
            return pos - start_pos;
        }
    }    // Parse selector(s) using enhanced parser (supports compound, descendant, and comma-separated selectors)
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
        fprintf(stderr, "[CSS Parser] After parsing: decl=%p\n", (void*)decl);
        if (decl) {
            fprintf(stderr, "[CSS Parser] Parsed declaration: property_id=%d for position %d\n",
                    decl->property_id, decl_count);

            // Expand array if needed
            if (decl_count >= decl_capacity) {
                decl_capacity *= 2;
                CssDeclaration** new_decls = (CssDeclaration**)pool_calloc(pool, decl_capacity * sizeof(CssDeclaration*));
                memcpy(new_decls, declarations, decl_count * sizeof(CssDeclaration*));
                declarations = new_decls;
            }
            declarations[decl_count++] = decl;
            fprintf(stderr, "[CSS Parser] Stored declaration at index %d, now have %d declarations\n",
                    decl_count - 1, decl_count);
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
    rule->data.style_rule.declarations = declarations;
    rule->data.style_rule.declaration_count = decl_count;

    fprintf(stderr, "[CSS Parser] Created rule with %d declarations:\n", decl_count);
    for (int i = 0; i < decl_count && i < 5; i++) {
        if (declarations[i]) {
            fprintf(stderr, "[CSS Parser]   Declaration[%d]: property_id=%d\n", i, declarations[i]->property_id);
        }
    }

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
