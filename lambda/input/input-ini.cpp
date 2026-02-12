#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include "lib/log.h"
#include "lib/memtrack.h"

using namespace lambda;

static void skip_to_newline(const char **ini, SourceTracker* tracker = nullptr) {
    while (**ini && **ini != '\n' && **ini != '\r') {
        if (tracker) tracker->advance(**ini);
        (*ini)++;
    }
    if (**ini == '\r' && *(*ini + 1) == '\n') {
        if (tracker) {
            tracker->advance('\r');
            tracker->advance('\n');
        }
        (*ini) += 2; // skip \r\n
    } else if (**ini == '\n' || **ini == '\r') {
        if (tracker) tracker->advance(**ini);
        (*ini)++; // skip \n or \r
    }
}

static bool is_section_start(const char *ini) {
    return *ini == '[';
}

static bool is_comment(const char *ini) {
    return *ini == ';' || *ini == '#';
}

static String* parse_section_name(InputContext& ctx, const char **ini) {
    if (**ini != '[') return NULL;
    SourceTracker& tracker = ctx.tracker;
    SourceLocation section_loc = tracker.location();
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    (*ini)++; // skip '['
    tracker.advance(1);
    while (**ini && **ini != ']' && **ini != '\n' && **ini != '\r') {
        stringbuf_append_char(sb, **ini);
        tracker.advance(1);
        (*ini)++;
    }
    if (**ini == ']') {
        (*ini)++; // skip ']'
        tracker.advance(1);
    } else {
        ctx.addError(section_loc, "Unterminated section name: missing ']'");
    }

    if (sb->length > 0) {
        return builder.createName(sb->str->chars, sb->length);
    }

    ctx.addError(section_loc, "Empty section name");
    return NULL;
}

static String* parse_key(InputContext& ctx, const char **ini) {
    SourceTracker& tracker = ctx.tracker;
    SourceLocation key_loc = tracker.location();
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // Read until '=' or whitespace
    while (**ini && **ini != '=' && **ini != '\n' && **ini != '\r' && !isspace(**ini)) {
        stringbuf_append_char(sb, **ini);
        tracker.advance(1);
        (*ini)++;
    }

    if (sb->length == 0) {
        ctx.addError(key_loc, "Empty key name");
        return NULL;
    }

    return builder.createName(sb->str->chars, sb->length);
}

static String* parse_raw_value(InputContext& ctx, const char **ini) {
    SourceTracker& tracker = ctx.tracker;
    SourceLocation value_loc = tracker.location();
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    skip_tab_pace(ini);
    // handle quoted values
    bool quoted = false;
    if (**ini == '"' || **ini == '\'') {
        char quote_char = **ini;
        quoted = true;
        (*ini)++; // skip opening quote
        tracker.advance(quote_char);

        while (**ini && **ini != quote_char) {
            if (**ini == '\\' && *(*ini + 1) == quote_char) {
                // handle escaped quotes
                (*ini)++; // skip backslash
                tracker.advance(1);
                stringbuf_append_char(sb, **ini);
                tracker.advance(1);
            } else {
                stringbuf_append_char(sb, **ini);
                tracker.advance(1);
            }
            (*ini)++;
        }

        if (**ini == quote_char) {
            (*ini)++; // skip closing quote
            tracker.advance(quote_char);
        } else {
            ctx.addError(value_loc, "Unterminated quoted value: missing closing %c", quote_char);
        }
    } else {
        // read until end of line or comment
        while (**ini && **ini != '\n' && **ini != '\r' && **ini != ';' && **ini != '#') {
            stringbuf_append_char(sb, **ini);
            tracker.advance(1);
            (*ini)++;
        }
        // trim trailing whitespace
        while (sb->length > 0 && isspace(sb->str->chars[sb->length - 1])) {
            sb->length--;
        }
    }

    if (sb->length > 0) {
        return builder.createString(sb->str->chars, sb->length);
    }
    return nullptr;  // empty string maps to null
}

// use shared parse_typed_value from input-utils.hpp

static Map* parse_section(InputContext& ctx, const char **ini, String* section_name) {
    log_debug("parse_section: %.*s\n", (int)section_name->len, section_name->chars);

    SourceTracker& tracker = ctx.tracker;
    Input* input = ctx.input();
    Map* section_map = map_pooled(input->pool);
    if (!section_map) return NULL;

    while (**ini) {
        skip_tab_pace(ini);

        // check for end of file
        if (!**ini) break;

        // skip empty lines
        if (**ini == '\n' || **ini == '\r') { skip_to_newline(ini, &tracker);  continue; }

        // check for comments
        if (is_comment(*ini)) { skip_to_newline(ini, &tracker);  continue; }

        // check for next section
        if (is_section_start(*ini)) { break; }

        // parse key-value pair
        SourceLocation line_loc = tracker.location();
        String* key = parse_key(ctx, ini);
        if (!key || key->len == 0) {
            ctx.addError(line_loc, "Invalid or empty key");
            skip_to_newline(ini, &tracker);
            continue;
        }

        skip_tab_pace(ini);
        if (**ini != '=') {
            ctx.addError(tracker.location(), "Expected '=' after key '%.*s'", (int)key->len, key->chars);
            skip_to_newline(ini, &tracker);
            continue;
        }
        (*ini)++; // skip '='
        tracker.advance(1);

        String* value_str = parse_raw_value(ctx, ini);
        Item value = value_str ? parse_typed_value(ctx, value_str->chars, value_str->len) : (Item){.item = ITEM_NULL};
        ctx.builder.putToMap(section_map, key, value);

        skip_to_newline(ini, &tracker);
    }
    return section_map;
}

void parse_ini(Input* input, const char* ini_string) {
    if (!ini_string || !*ini_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, ini_string, strlen(ini_string));
    SourceTracker& tracker = ctx.tracker;

    // Create root map to hold all sections
    Map* root_map = map_pooled(input->pool);
    if (!root_map) { return; }
    input->root = {.item = (uint64_t)root_map};

    const char *current = ini_string;
    String* current_section_name = NULL;

    // handle key-value pairs before any section (global section)
    Map* global_section = NULL;
    while (*current) {
        skip_tab_pace(&current);

        // check for end of file
        if (!*current) break;

        // skip empty lines
        if (*current == '\n' || *current == '\r') { skip_to_newline(&current, &tracker);  continue; }

        // check for comments
        if (is_comment(current)) { skip_to_newline(&current, &tracker);  continue; }

        // check for section header
        if (is_section_start(current)) {
            current_section_name = parse_section_name(ctx, &current);
            if (!current_section_name) {
                skip_to_newline(&current, &tracker);
                continue;
            }

            skip_to_newline(&current, &tracker);
            // parse the section content
            Map* section_map = parse_section(ctx, &current, current_section_name);
            if (section_map && section_map->type && ((TypeMap*)section_map->type)->length > 0) {
                // add section to root map
                ctx.builder.putToMap(root_map, current_section_name,
                    {.item = (uint64_t)section_map});
            }
        } else {
            // key-value pair outside of any section (global)
            if (!global_section) {
                // Create a global section name (output data - use pool)
                String* global_name;
                global_name = (String*)pool_calloc(input->pool, sizeof(String) + 7);
                if (global_name != NULL) {
                    global_name->len = 6;
                    global_name->ref_cnt = 0;
                    memcpy(global_name->chars, "global", 6);
                    global_name->chars[6] = '\0';

                    global_section = parse_section(ctx, &current, global_name);
                    if (global_section && global_section->type && ((TypeMap*)global_section->type)->length > 0) {
                        // Add global section to root map
                        ctx.builder.putToMap(root_map, global_name,
                            {.item = (uint64_t)global_section});
                    }
                }
            } else {
                ctx.addWarning(tracker.location(), "Orphaned key-value pair outside of any section");
                skip_to_newline(&current, &tracker);
            }
        }
    }

    // Log any errors encountered during parsing
    if (ctx.hasErrors() || ctx.hasWarnings()) {
        ctx.logErrors();
    }
}
