#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include "lib/log.h"

using namespace lambda;

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

static String* parse_key(InputContext& ctx, const char **prop) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // read until '=', ':', or whitespace
    while (**prop && **prop != '=' && **prop != ':' && **prop != '\n' && **prop != '\r' && !isspace(**prop)) {
        stringbuf_append_char(sb, **prop);
        (*prop)++;
    }

    if (sb->length > 0) {
        return builder.createString(sb->str->chars, sb->length);
    }
    return NULL;
}

static String* parse_raw_value(InputContext& ctx, const char **prop) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    skip_tab_pace(prop);

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
                skip_tab_pace(prop);
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
                // unicode escape sequence \uXXXX (including surrogate pairs for emojis)
                char hex_str[5];
                strncpy(hex_str, next + 1, 4);
                hex_str[4] = '\0';

                char* end;
                unsigned long codepoint = strtoul(hex_str, &end, 16);
                if (end == hex_str + 4) {
                    // check for surrogate pairs (used for characters > U+FFFF like emojis)
                    // high surrogate: 0xD800-0xDBFF, low surrogate: 0xDC00-0xDFFF
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        // this is a high surrogate, look for low surrogate
                        const char* look_ahead = next + 5; // after first \uXXXX
                        if (*look_ahead == '\\' && *(look_ahead+1) == 'u' &&
                            *(look_ahead+2) && *(look_ahead+3) && *(look_ahead+4) && *(look_ahead+5)) {
                            char hex_low[5];
                            strncpy(hex_low, look_ahead + 2, 4);
                            hex_low[4] = '\0';
                            char* end_low;
                            unsigned long low_surrogate = strtoul(hex_low, &end_low, 16);
                            uint32_t combined = decode_surrogate_pair((uint16_t)codepoint, (uint16_t)low_surrogate);
                            if (end_low == hex_low + 4 && combined != 0) {
                                // valid surrogate pair - combine into full codepoint
                                codepoint = combined;
                                (*prop) += 12; // skip both \uXXXX\uXXXX
                            } else {
                                // not a valid low surrogate, output replacement char
                                codepoint = 0xFFFD;
                                (*prop) += 6;
                            }
                        } else {
                            // lone high surrogate - output replacement character
                            codepoint = 0xFFFD;
                            (*prop) += 6;
                        }
                    } else {
                        (*prop) += 6; // skip \uXXXX
                    }

                    // valid unicode escape - convert to UTF-8
                    append_codepoint_utf8(sb, (uint32_t)codepoint);
                    continue;
                }
            }
            // if not a recognized escape, treat backslash literally
        }

        stringbuf_append_char(sb, **prop);
        (*prop)++;
    }

    // trim trailing whitespace
    while (sb->length > 0 && isspace(sb->str->chars[sb->length - 1])) {
        sb->length--;
    }

    if (sb->length > 0) {
        return builder.createString(sb->str->chars, sb->length);
    }
    return nullptr;  // empty string maps to null
}

// parse_typed_value provided by input-utils.hpp

void parse_properties(Input* input, const char* prop_string) {
    if (!prop_string || !*prop_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    // create error tracking context with source tracking
    InputContext ctx(input, prop_string, strlen(prop_string));
    MarkBuilder& builder = ctx.builder;

    // create root map to hold all properties
    Map* root_map = map_pooled(input->pool);
    if (!root_map) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for properties map");
        return;
    }
    input->root = {.item = (uint64_t)root_map};

    const char *current = prop_string;

    while (*current) {
        skip_tab_pace(&current);

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
        String* key = parse_key(ctx, &current);
        if (!key) {
            ctx.addWarning(ctx.tracker.location(), "Failed to parse property key, skipping line");
            skip_to_newline(&current);
            continue;
        }

        skip_tab_pace(&current);

        // skip separator (= or :)
        if (*current == '=' || *current == ':') {
            current++;
            skip_tab_pace(&current);
        }

        // parse value
        String* raw_value = parse_raw_value(ctx, &current);
        if (raw_value) {
            Item typed_value = parse_typed_value(ctx, raw_value->chars, raw_value->len);
            ctx.builder.putToMap(root_map, key, typed_value);
        } else {
            ctx.addWarning(ctx.tracker.location(), "Failed to parse value for key '%.*s'",
                          (int)key->len, key->chars);
        }        // move to next line
        skip_to_newline(&current);
    }

    if (ctx.hasErrors()) {
        log_debug("Properties parsing completed with errors\n");
    } else {
        log_debug("Properties parsing completed\n");
    }
}
