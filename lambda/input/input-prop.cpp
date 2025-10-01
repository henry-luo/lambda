#include "input.h"

static void skip_whitespace(const char **prop) {
    while (**prop && (**prop == ' ' || **prop == '\t')) {
        (*prop)++;
    }
}

static void skip_to_newline(const char **prop) {
    while (**prop && **prop != '\n' && **prop != '\r') {
        (*prop)++;
    }
    if (**prop == '\r' && *(*prop + 1) == '\n') {
        (*prop) += 2; // skip \r\n
    } else if (**prop == '\n' || **prop == '\r') {
        (*prop)++; // skip \n or \r
    }
}

static bool is_comment(const char *prop) {
    return *prop == '#' || *prop == '!';
}

static String* parse_key(Input *input, const char **prop) {
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);

    // read until '=', ':', or whitespace
    while (**prop && **prop != '=' && **prop != ':' && **prop != '\n' && **prop != '\r' && !isspace(**prop)) {
        stringbuf_append_char(sb, **prop);
        (*prop)++;
    }

    if (sb->str && sb->str->len > 0) {
        return stringbuf_to_string(sb);
    }
    return NULL;
}

static String* parse_raw_value(Input *input, const char **prop) {
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);

    skip_whitespace(prop);

    // properties files don't use quotes for strings, read until end of line
    // but handle line continuations with backslash
    while (**prop && **prop != '\n' && **prop != '\r') {
        if (**prop == '\\') {
            // check for line continuation
            const char* next = *prop + 1;
            if (*next == '\n' || *next == '\r') {
                // line continuation - skip backslash and newline
                (*prop)++;
                if (**prop == '\r' && *(*prop + 1) == '\n') {
                    (*prop) += 2; // skip \r\n
                } else {
                    (*prop)++; // skip \n or \r
                }
                // skip leading whitespace on continuation line
                skip_whitespace(prop);
                continue;
            } else if (*next == 'n') {
                // escaped newline
                stringbuf_append_char(sb, '\n');
                (*prop) += 2;
                continue;
            } else if (*next == 't') {
                // escaped tab
                stringbuf_append_char(sb, '\t');
                (*prop) += 2;
                continue;
            } else if (*next == 'r') {
                // escaped carriage return
                stringbuf_append_char(sb, '\r');
                (*prop) += 2;
                continue;
            } else if (*next == '\\') {
                // escaped backslash
                stringbuf_append_char(sb, '\\');
                (*prop) += 2;
                continue;
            } else if (*next == 'u' && *(next+1) && *(next+2) && *(next+3) && *(next+4)) {
                // unicode escape sequence \uXXXX
                char hex_str[5];
                strncpy(hex_str, next + 1, 4);
                hex_str[4] = '\0';

                char* end;
                unsigned long unicode_val = strtoul(hex_str, &end, 16);
                if (end == hex_str + 4) {
                    // valid unicode escape - convert to UTF-8
                    if (unicode_val <= 0x7F) {
                        stringbuf_append_char(sb, (char)unicode_val);
                    } else if (unicode_val <= 0x7FF) {
                        stringbuf_append_char(sb, 0xC0 | (unicode_val >> 6));
                        stringbuf_append_char(sb, 0x80 | (unicode_val & 0x3F));
                    } else {
                        stringbuf_append_char(sb, 0xE0 | (unicode_val >> 12));
                        stringbuf_append_char(sb, 0x80 | ((unicode_val >> 6) & 0x3F));
                        stringbuf_append_char(sb, 0x80 | (unicode_val & 0x3F));
                    }
                    (*prop) += 6; // skip \uXXXX
                    continue;
                }
            }
            // if not a recognized escape, treat backslash literally
        }

        stringbuf_append_char(sb, **prop);
        (*prop)++;
    }

    // trim trailing whitespace using proper StringBuf API
    while (sb->length > 0 && isspace(sb->str->chars[sb->length - 1])) {
        sb->length--;
        sb->str->len = sb->length;
        sb->str->chars[sb->length] = '\0';
    }

    if (sb->str && sb->str->len > 0) {
        return stringbuf_to_string(sb);
    }
    return &EMPTY_STRING;
}

static int case_insensitive_compare(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = tolower((unsigned char)s1[i]);
        char c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2) return c1 - c2;
    }
    return 0;
}

static Item parse_typed_value(Input *input, String* value_str) {
    if (!value_str || value_str->len == 0) {
        return {.item = s2it(value_str)};
    }

    char* str = value_str->chars;
    size_t len = value_str->len;

    // check for boolean values (case insensitive)
    if ((len == 4 && case_insensitive_compare(str, "true", 4) == 0) ||
        (len == 3 && case_insensitive_compare(str, "yes", 3) == 0) ||
        (len == 2 && case_insensitive_compare(str, "on", 2) == 0) ||
        (len == 1 && str[0] == '1')) {
        return {.item = b2it(true)};
    }
    if ((len == 5 && case_insensitive_compare(str, "false", 5) == 0) ||
        (len == 2 && case_insensitive_compare(str, "no", 2) == 0) ||
        (len == 3 && case_insensitive_compare(str, "off", 3) == 0) ||
        (len == 1 && str[0] == '0')) {
        return {.item = b2it(false)};
    }

    // check for null/empty values
    if ((len == 4 && case_insensitive_compare(str, "null", 4) == 0) ||
        (len == 3 && case_insensitive_compare(str, "nil", 3) == 0) ||
        (len == 5 && case_insensitive_compare(str, "empty", 5) == 0)) {
        return {.item = ITEM_NULL};
    }

    // try to parse as number
    char* end;
    bool is_number = true, has_dot = false;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (i == 0 && (c == '-' || c == '+')) {
            continue; // allow leading sign
        }
        if (c == '.' && !has_dot) {
            has_dot = true;
            continue;
        }
        if (c == 'e' || c == 'E') {
            // allow scientific notation
            if (i + 1 < len && (str[i + 1] == '+' || str[i + 1] == '-')) {
                i++; // skip the sign after e/E
            }
            continue;
        }
        if (!isdigit(c)) {
            is_number = false;
            break;
        }
    }

    if (is_number && len > 0) {
        // create null-terminated string for parsing
        char* temp_str = (char*)pool_calloc(input->pool, len + 1);
        if (temp_str) {
            memcpy(temp_str, str, len);
            temp_str[len] = '\0';

            if (has_dot || strchr(temp_str, 'e') || strchr(temp_str, 'E')) {
                // parse as floating point
                double dval = strtod(temp_str, &end);
                if (end == temp_str + len) {
                    double* dval_ptr;
                    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval_ptr);
                    if (err == MEM_POOL_ERR_OK) {
                        *dval_ptr = dval;
                        pool_variable_free(input->pool, temp_str);
                        return {.item = d2it(dval_ptr)};
                    }
                }
            } else {
                // parse as integer
                int64_t lval = strtol(temp_str, &end, 10);
                if (end == temp_str + len) {
                    int64_t* lval_ptr;
                    MemPoolError err = pool_variable_alloc(input->pool, sizeof(int64_t), (void**)&lval_ptr);
                    if (err == MEM_POOL_ERR_OK) {
                        *lval_ptr = lval;
                        pool_variable_free(input->pool, temp_str);
                        return {.item = l2it(lval_ptr)};
                    }
                }
            }

            pool_variable_free(input->pool, temp_str);
        }
    }

    // fallback to string
    return {.item = s2it(value_str)};
}

void parse_properties(Input* input, const char* prop_string) {
    input->sb = stringbuf_new(input->pool);

    // create root map to hold all properties
    Map* root_map = map_pooled(input->pool);
    if (!root_map) { return; }
    input->root = {.item = (uint64_t)root_map};

    const char *current = prop_string;

    while (*current) {
        skip_whitespace(&current);

        // check for end of file
        if (!*current) break;

        // skip empty lines
        if (*current == '\n' || *current == '\r') {
            skip_to_newline(&current);
            continue;
        }

        // check for comments
        if (is_comment(current)) {
            skip_to_newline(&current);
            continue;
        }

        // parse key-value pair
        String* key = parse_key(input, &current);
        if (!key) {
            skip_to_newline(&current);
            continue;
        }

        skip_whitespace(&current);

        // skip separator (= or :)
        if (*current == '=' || *current == ':') {
            current++;
            skip_whitespace(&current);
        }

        // parse value
        String* raw_value = parse_raw_value(input, &current);
        if (raw_value) {
            Item typed_value = parse_typed_value(input, raw_value);
            map_put(root_map, key, typed_value, input);
        }

        // move to next line
        skip_to_newline(&current);
    }

    printf("Properties parsing completed\n");
}
