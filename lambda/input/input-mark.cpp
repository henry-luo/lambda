#include "input.h"

static Element* parse_element(Input *input, const char **mark);
static Item parse_value(Input *input, const char **mark);
static Item parse_content(Input *input, const char **mark);

static void skip_whitespace(const char **mark) {
    while (**mark && (**mark == ' ' || **mark == '\n' || **mark == '\r' || **mark == '\t')) {
        (*mark)++;
    }
}

static void skip_comments(const char **mark) {
    skip_whitespace(mark);
    while (**mark == '/' && *(*mark + 1) == '/') {
        // Skip single-line comment
        while (**mark && **mark != '\n' && **mark != '\r') {
            (*mark)++;
        }
        skip_whitespace(mark);
    }
    
    // Handle multi-line comments /* ... */
    while (**mark == '/' && *(*mark + 1) == '*') {
        *mark += 2; // Skip /*
        while (**mark && !(**mark == '*' && *(*mark + 1) == '/')) {
            (*mark)++;
        }
        if (**mark == '*' && *(*mark + 1) == '/') {
            *mark += 2; // Skip */
        }
        skip_whitespace(mark);
    }
}

static String* parse_string(Input *input, const char **mark) {
    if (**mark != '"') return NULL;
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);  // Reset buffer before use
    
    (*mark)++; // Skip opening quote
    while (**mark && **mark != '"') {
        if (**mark == '\\') {
            (*mark)++;
            switch (**mark) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case '/': stringbuf_append_char(sb, '/'); break;
                case 'b': stringbuf_append_char(sb, '\b'); break;
                case 'f': stringbuf_append_char(sb, '\f'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'u': {
                    (*mark)++; // skip 'u'
                    char hex[5] = {0};
                    strncpy(hex, *mark, 4);
                    (*mark) += 4; // skip 4 hex digits
                    int codepoint = (int)strtol(hex, NULL, 16);
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
                } break;
                default: break; // invalid escape
            }
        } else {
            stringbuf_append_char(sb, **mark);
        }
        (*mark)++;
    }

    if (**mark == '"') {
        (*mark)++; // skip closing quote
    }
    return stringbuf_to_string(sb);
}

static String* parse_symbol(Input *input, const char **mark) {
    if (**mark != '\'') return NULL;
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);  // Reset buffer before use
    
    (*mark)++; // Skip opening quote
    while (**mark && **mark != '\'' && **mark != '\n') {
        if (**mark == '\\') {
            (*mark)++;
            switch (**mark) {
                case '\'': stringbuf_append_char(sb, '\''); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                default: stringbuf_append_char(sb, **mark); break;
            }
        } else {
            stringbuf_append_char(sb, **mark);
        }
        (*mark)++;
    }

    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    }
    
    return stringbuf_to_string(sb);
}

static String* parse_unquoted_identifier(Input *input, const char **mark) {
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);  // Reset buffer before use
    
    // First character must be alpha or underscore
    if (!(**mark >= 'a' && **mark <= 'z') && 
        !(**mark >= 'A' && **mark <= 'Z') && 
        **mark != '_') {
        return NULL;
    }
    
    while (**mark && ((**mark >= 'a' && **mark <= 'z') || 
                      (**mark >= 'A' && **mark <= 'Z') || 
                      (**mark >= '0' && **mark <= '9') || 
                      **mark == '_' || **mark == '-')) {
        stringbuf_append_char(sb, **mark);
        (*mark)++;
    }
    
    return stringbuf_to_string(sb);
}

static Item parse_binary(Input *input, const char **mark) {
    if (**mark != 'b' || *(*mark + 1) != '\'') return {.item = ITEM_ERROR};
    
    *mark += 2; // Skip b'
    skip_whitespace(mark);
    
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);  // Reset buffer before use
    
    // Check for hex format
    if (**mark == '\\' && *(*mark + 1) == 'x') {
        *mark += 2; // Skip \x
        while (**mark && **mark != '\'') {
            if ((**mark >= '0' && **mark <= '9') ||
                (**mark >= 'a' && **mark <= 'f') ||
                (**mark >= 'A' && **mark <= 'F')) {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid hex character
            }
            (*mark)++;
        }
    }
    // Check for base64 format
    else if (**mark == '\\' && (*(*mark + 1) == '6' && *(*mark + 2) == '4')) {
        *mark += 3; // Skip \64
        while (**mark && **mark != '\'') {
            if ((**mark >= 'A' && **mark <= 'Z') ||
                (**mark >= 'a' && **mark <= 'z') ||
                (**mark >= '0' && **mark <= '9') ||
                **mark == '+' || **mark == '/' || **mark == '=') {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid base64 character
            }
            (*mark)++;
        }
    }
    // Default hex format without \x prefix
    else {
        while (**mark && **mark != '\'') {
            if ((**mark >= '0' && **mark <= '9') ||
                (**mark >= 'a' && **mark <= 'f') ||
                (**mark >= 'A' && **mark <= 'F')) {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid hex character
            }
            (*mark)++;
        }
    }
    
    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    }
    
    String* binary_str = stringbuf_to_string(sb);
    return binary_str ? (Item){.item = s2it(binary_str)} : (Item){.item = ITEM_ERROR};
}

static Item parse_datetime(Input *input, const char **mark) {
    if (**mark != 't' || *(*mark + 1) != '\'') return {.item = ITEM_ERROR};
    
    *mark += 2; // Skip t'
    skip_whitespace(mark);
    
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);  // Reset buffer before use
    
    while (**mark && **mark != '\'') {
        stringbuf_append_char(sb, **mark);
        (*mark)++;
    }
    
    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    }
    
    String* datetime_str = stringbuf_to_string(sb);
    return datetime_str ? (Item){.item = s2it(datetime_str)} : (Item){.item = ITEM_ERROR};
}

static Item parse_number(Input *input, const char **mark) {
    double *dval;
    dval = (double*)pool_calloc(input->pool, sizeof(double));
    if (dval == NULL) return {.item = ITEM_ERROR};
    
    char* end;
    *dval = strtod(*mark, &end);
    *mark = end;
    
    // Check for decimal suffix (n or N)
    if (**mark == 'n' || **mark == 'N') {
        (*mark)++;
        // For now, treat as regular double - could enhance for true decimal support
    }
    
    return {.item = d2it(dval)};
}

static Array* parse_array(Input *input, const char **mark) {
    if (**mark != '[') return NULL;
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*mark)++; // skip [
    skip_comments(mark);
    
    if (**mark == ']') { 
        (*mark)++;  
        return arr; 
    }

    while (**mark) {
        Item item = parse_value(input, mark);
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ']') { 
            (*mark)++;  
            break; 
        }
        if (**mark != ',') {
            return NULL; // invalid format
        }
        (*mark)++;
        skip_comments(mark);
    }
    return arr;
}

static Array* parse_list(Input *input, const char **mark) {
    if (**mark != '(') return NULL;
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*mark)++; // skip (
    skip_comments(mark);
    
    if (**mark == ')') { 
        (*mark)++;  
        return arr; 
    }

    while (**mark) {
        Item item = parse_value(input, mark);
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ')') { 
            (*mark)++;  
            break; 
        }
        if (**mark != ',') {
            return NULL; // invalid format
        }
        (*mark)++;
        skip_comments(mark);
    }
    return arr;
}

static Map* parse_map(Input *input, const char **mark) {
    if (**mark != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    (*mark)++; // skip '{'
    skip_comments(mark);
    
    if (**mark == '}') { // empty map
        (*mark)++;  
        return mp;
    }

    while (**mark) {
        String* key = NULL;
        
        // Parse key - can be string, symbol, or identifier
        if (**mark == '"') {
            key = parse_string(input, mark);
        } else if (**mark == '\'') {
            key = parse_symbol(input, mark);
        } else {
            key = parse_unquoted_identifier(input, mark);
        }
        
        if (!key) return mp;

        skip_comments(mark);
        if (**mark != ':') return mp;
        (*mark)++;
        skip_comments(mark);

        Item value = parse_value(input, mark);
        map_put(mp, key, value, input);

        skip_comments(mark);
        if (**mark == '}') { 
            (*mark)++;  
            break; 
        }
        if (**mark != ',') return mp;
        (*mark)++;
        skip_comments(mark);
    }
    return mp;
}

static Element* parse_element(Input *input, const char **mark) {
    if (**mark != '<') return NULL;
    
    (*mark)++; // skip '<'
    skip_comments(mark);
    
    // Parse element name - can be symbol or identifier
    String* element_name = NULL;
    if (**mark == '\'') {
        element_name = parse_symbol(input, mark);
    } else {
        element_name = parse_unquoted_identifier(input, mark);
    }
    
    if (!element_name) return NULL;
    
    Element* element = input_create_element(input, element_name->chars);
    if (!element) return NULL;
    
    skip_comments(mark);
    
    // Parse attributes
    while (**mark && **mark != '>') {
        String* attr_name = NULL;
        
        // Check if this might be content instead of an attribute
        // If we see a quote, angle bracket, or brace that doesn't look like an attribute
        if (**mark == '"' || **mark == '<' || **mark == '{' || **mark == '[') {
            // Look ahead to see if this looks like an attribute or content
            const char* lookahead = *mark;
            if (**mark == '"') {
                // Skip the string to see what comes after
                lookahead++;
                while (*lookahead && *lookahead != '"') {
                    if (*lookahead == '\\') lookahead++; // skip escaped chars
                    if (*lookahead) lookahead++;
                }
                if (*lookahead == '"') lookahead++;
                skip_whitespace(&lookahead);
                
                // If we don't see a colon after the string, treat as content
                if (*lookahead != ':') {
                    break; // Start parsing content
                }
            } else {
                // For other content markers, just break to content parsing
                break;
            }
        }
        
        // Parse attribute name
        if (**mark == '"') {
            attr_name = parse_string(input, mark);
        } else if (**mark == '\'') {
            attr_name = parse_symbol(input, mark);
        } else {
            attr_name = parse_unquoted_identifier(input, mark);
        }
        
        if (!attr_name) break;
        
        skip_comments(mark);
        if (**mark != ':') break;
        (*mark)++;
        skip_comments(mark);
        
        // Parse attribute value
        Item attr_value = parse_value(input, mark);
        input_add_attribute_item_to_element(input, element, attr_name->chars, attr_value);
        
        skip_comments(mark);
        if (**mark == ',') {
            (*mark)++;
            skip_comments(mark);
        }
    }
    
    // Check for content separator (semicolon, newline, or just whitespace)
    skip_comments(mark);
    
    // Parse content - content can be separated by semicolons, newlines, or just whitespace
    while (**mark && **mark != '>') {
        Item content_item = parse_content(input, mark);
        if (content_item .item != ITEM_ERROR && content_item .item != ITEM_NULL) {
            // Add content to element
            list_push((List*)element, content_item);
            ((TypeElmt*)element->type)->content_length++;
        }
        skip_comments(mark);
        
        // Skip optional separators
        if (**mark == ';' || **mark == '\n') {
            (*mark)++;
            skip_comments(mark);
        }
    }
    
    if (**mark == '>') {
        (*mark)++; // skip closing '>'
    }
    
    return element;
}

static Item parse_content(Input *input, const char **mark) {
    skip_comments(mark);
    
    if (**mark == '<') {
        return {.item = (uint64_t)parse_element(input, mark)};
    } else {
        return parse_value(input, mark);
    }
}

static Item parse_value(Input *input, const char **mark) {
    skip_comments(mark);
    
    switch (**mark) {
        case '{':
            return {.item = (uint64_t)parse_map(input, mark)};
        case '[':
            return {.item = (uint64_t)parse_array(input, mark)};
        case '(':
            return {.item = (uint64_t)parse_list(input, mark)};
        case '<':
            return {.item = (uint64_t)parse_element(input, mark)};
        case '"':
            return {.item = s2it(parse_string(input, mark))};
        case '\'':
            {
                String* sym = parse_symbol(input, mark);
                return {.item = y2it(sym)};
            }
        case 'b':
            if (*(*mark + 1) == '\'') {
                return parse_binary(input, mark);
            }
            goto UNQUOTED_IDENTIFIER;
        case 't':
            if (*(*mark + 1) == '\'') {
                return parse_datetime(input, mark);
            } else if (strncmp(*mark, "true", 4) == 0) {
                *mark += 4;
                return {.item = b2it(true)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'f':
            if (strncmp(*mark, "false", 5) == 0) {
                *mark += 5;
                return {.item = b2it(false)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'n':
            if (strncmp(*mark, "null", 4) == 0) {
                *mark += 4;
                return {.item = ITEM_NULL};
            } else if (strncmp(*mark, "nan", 3) == 0) {
                *mark += 3;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = NAN;
                return {.item = d2it(dval)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'i':
            if (strncmp(*mark, "inf", 3) == 0) {
                *mark += 3;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = INFINITY;
                return {.item = d2it(dval)};
            }
            goto UNQUOTED_IDENTIFIER;
        case '-':
            if (strncmp(*mark, "-inf", 4) == 0) {
                *mark += 4;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = -INFINITY;
                return {.item = d2it(dval)};
            } else if (strncmp(*mark, "-nan", 4) == 0) {
                *mark += 4;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = -NAN;
                return {.item = d2it(dval)};
            }
            // Fall through to number parsing
        default:
            if ((**mark >= '0' && **mark <= '9') || **mark == '-' || **mark == '+') {
                return parse_number(input, mark);
            } 
            else if ((**mark >= 'a' && **mark <= 'z') || 
                (**mark >= 'A' && **mark <= 'Z') || **mark == '_') {
                UNQUOTED_IDENTIFIER:
                // Parse as identifier/symbol
                String* id = parse_unquoted_identifier(input, mark);
                return id ? (Item){.item = y2it(id)} : (Item){.item = ITEM_ERROR};
            }
            return {.item = ITEM_ERROR};
    }
}

void parse_mark(Input* input, const char* mark_string) {
    input->sb = stringbuf_new(input->pool);
    
    const char* mark = mark_string;
    skip_comments(&mark);
    
    // Parse the root content - could be a single value or element
    input->root = parse_content(input, &mark);
}
