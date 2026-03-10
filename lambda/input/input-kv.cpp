/**
 * input-kv.cpp — unified key-value flat-file parser
 *
 * Handles both INI-style (sections, comment chars ';' and '#', key=value)
 * and Java-Properties-style (no sections, comment chars '#' and '!',
 * key=value or key:value, escape sequences, line continuation) formats.
 *
 * Entry points (signatures match the old separate files):
 *   void parse_ini(Input*, const char*)
 *   void parse_properties(Input*, const char*)
 */

#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"

extern "C" {
#include "../../lib/log.h"
}

using namespace lambda;

// ── per-format configuration ──────────────────────────────────────────────────

struct KvConfig {
    bool        support_sections;     // INI=true, prop=false
    const char* comment_chars;        // chars that start a comment line
    const char* kv_separators;        // chars accepted as key/value separators
    bool        support_escape;       // prop=true: handle \n \t \uXXXX etc.
    bool        strip_value_comments; // INI=true: trim inline ; # from values
};

static const KvConfig INI_CONFIG  = { true,  ";#", "=",  false, true  };
static const KvConfig PROP_CONFIG = { false, "#!", "=:", true,  false };

// ── helpers ───────────────────────────────────────────────────────────────────

static bool kv_is_comment(char c, const char* comment_chars) {
    for (const char* p = comment_chars; *p; p++)
        if (c == *p) return true;
    return false;
}

static bool kv_is_separator(char c, const char* seps) {
    for (const char* p = seps; *p; p++)
        if (c == *p) return true;
    return false;
}

// Parse a key name — stops at separator, whitespace, or end-of-line.
static String* kv_parse_key(InputContext& ctx, const char** pos) {
    SourceTracker& tracker = ctx.tracker;
    SourceLocation key_loc = tracker.location();
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**pos && **pos != '=' && **pos != ':' &&
           **pos != '\n' && **pos != '\r' && !isspace(**pos)) {
        stringbuf_append_char(sb, **pos);
        tracker.advance(1);
        (*pos)++;
    }

    if (sb->length == 0) {
        ctx.addError(key_loc, "kv parser: empty key name");
        return NULL;
    }
    return ctx.builder.createName(sb->str->chars, sb->length);
}

// Parse an INI-style raw value: optional quotes, strips inline comments.
static String* ini_parse_raw_value(InputContext& ctx, const char** pos) {
    SourceTracker& tracker = ctx.tracker;
    SourceLocation value_loc = tracker.location();
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    skip_tab_pace(pos);

    bool quoted = (**pos == '"' || **pos == '\'');
    if (quoted) {
        char qch = **pos;
        (*pos)++; tracker.advance(1);
        while (**pos && **pos != qch) {
            if (**pos == '\\' && *(*pos + 1) == qch) {
                (*pos)++; tracker.advance(1);
                stringbuf_append_char(sb, **pos);
                tracker.advance(1);
            } else {
                stringbuf_append_char(sb, **pos);
                tracker.advance(1);
            }
            (*pos)++;
        }
        if (**pos == qch) {
            (*pos)++; tracker.advance(1);
        } else {
            ctx.addError(value_loc, "kv ini parser: unterminated quoted value");
        }
    } else {
        // read until end of line or inline comment (';' or '#')
        while (**pos && **pos != '\n' && **pos != '\r' &&
               **pos != ';' && **pos != '#') {
            stringbuf_append_char(sb, **pos);
            tracker.advance(1);
            (*pos)++;
        }
        // trim trailing whitespace
        while (sb->length > 0 && isspace((unsigned char)sb->str->chars[sb->length - 1]))
            sb->length--;
    }

    return (sb->length > 0) ? ctx.builder.createString(sb->str->chars, sb->length) : NULL;
}

// Parse a properties-style raw value: escape sequences, line continuation.
static String* prop_parse_raw_value(InputContext& ctx, const char** pos) {
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    skip_tab_pace(pos);

    while (**pos && **pos != '\n' && **pos != '\r') {
        if (**pos == '\\') {
            const char* next = *pos + 1;
            if (*next == '\n' || *next == '\r') {
                // line continuation
                (*pos)++;
                if (**pos == '\r' && *(*pos + 1) == '\n') (*pos) += 2;
                else (*pos)++;
                skip_tab_pace(pos);
                continue;
            }
            (*pos)++;  // skip backslash
            parse_escape_char(pos, sb);
            continue;
        }
        stringbuf_append_char(sb, **pos);
        (*pos)++;
    }

    // trim trailing whitespace
    while (sb->length > 0 && isspace((unsigned char)sb->str->chars[sb->length - 1]))
        sb->length--;

    return (sb->length > 0) ? ctx.builder.createString(sb->str->chars, sb->length) : NULL;
}

// ── section name parser (INI only) ────────────────────────────────────────────

static String* ini_parse_section_name(InputContext& ctx, const char** pos) {
    if (**pos != '[') return NULL;
    SourceTracker& tracker = ctx.tracker;
    SourceLocation loc = tracker.location();
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    (*pos)++; tracker.advance(1);  // skip '['
    while (**pos && **pos != ']' && **pos != '\n' && **pos != '\r') {
        stringbuf_append_char(sb, **pos);
        tracker.advance(1);
        (*pos)++;
    }
    if (**pos == ']') {
        (*pos)++; tracker.advance(1);
    } else {
        ctx.addError(loc, "kv ini parser: unterminated section name — missing ']'");
    }

    if (sb->length == 0) {
        ctx.addError(loc, "kv ini parser: empty section name");
        return NULL;
    }
    return ctx.builder.createName(sb->str->chars, sb->length);
}

// ── core document parser ──────────────────────────────────────────────────────

static void parse_kv_section(InputContext& ctx, const char** pos,
                              Map* target_map, const KvConfig* cfg) {
    SourceTracker& tracker = ctx.tracker;
    while (**pos) {
        skip_tab_pace(pos);
        if (!**pos) break;
        if (**pos == '\n' || **pos == '\r') { skip_to_newline(pos); continue; }
        if (kv_is_comment(**pos, cfg->comment_chars)) { skip_to_newline(pos); continue; }
        if (cfg->support_sections && **pos == '[') break;   // next section starts

        SourceLocation line_loc = tracker.location();
        String* key = kv_parse_key(ctx, pos);
        if (!key) { skip_to_newline(pos); continue; }

        skip_tab_pace(pos);
        if (!kv_is_separator(**pos, cfg->kv_separators)) {
            ctx.addError(tracker.location(),
                         "kv parser: expected separator after key '%.*s'",
                         (int)key->len, key->chars);
            skip_to_newline(pos);
            continue;
        }
        (*pos)++;  // consume separator
        tracker.advance(1);

        String* raw = cfg->support_escape ? prop_parse_raw_value(ctx, pos)
                                          : ini_parse_raw_value(ctx, pos);
        Item value = raw ? parse_typed_value(ctx, raw->chars, raw->len)
                         : (Item){.item = ITEM_NULL};
        ctx.builder.putToMap(target_map, key, value);
        skip_to_newline(pos);
    }
}

static void parse_kv_document(InputContext& ctx, const char* src, const KvConfig* cfg) {
    Input* input = ctx.input();
    SourceTracker& tracker = ctx.tracker;

    Map* root_map = map_pooled(input->pool);
    if (!root_map) return;
    input->root = {.item = (uint64_t)root_map};

    const char* current = src;
    bool global_added = false;

    while (*current) {
        skip_tab_pace(&current);
        if (!*current) break;
        if (*current == '\n' || *current == '\r') { skip_to_newline(&current); continue; }
        if (kv_is_comment(*current, cfg->comment_chars)) { skip_to_newline(&current); continue; }

        if (cfg->support_sections && *current == '[') {
            String* section_name = ini_parse_section_name(ctx, &current);
            if (!section_name) { skip_to_newline(&current); continue; }
            skip_to_newline(&current);

            Map* section_map = map_pooled(input->pool);
            if (!section_map) continue;
            parse_kv_section(ctx, &current, section_map, cfg);
            if (section_map->type && ((TypeMap*)section_map->type)->length > 0)
                ctx.builder.putToMap(root_map, section_name, {.item = (uint64_t)section_map});
        } else if (cfg->support_sections && !global_added) {
            // key-value before any section → "global" section
            global_added = true;
            String* global_name = (String*)pool_calloc(input->pool, sizeof(String) + 7);
            if (!global_name) { skip_to_newline(&current); continue; }
            global_name->len = 6;
            memcpy(global_name->chars, "global", 6);
            global_name->chars[6] = '\0';

            Map* global_map = map_pooled(input->pool);
            if (global_map) {
                parse_kv_section(ctx, &current, global_map, cfg);
                if (global_map->type && ((TypeMap*)global_map->type)->length > 0)
                    ctx.builder.putToMap(root_map, global_name, {.item = (uint64_t)global_map});
            }
        } else if (!cfg->support_sections) {
            // flat key-value — parse directly into root_map
            parse_kv_section(ctx, &current, root_map, cfg);
        } else {
            ctx.addWarning(tracker.location(),
                           "kv ini parser: orphaned key-value pair outside any section");
            skip_to_newline(&current);
        }
    }

    if (ctx.hasErrors() || ctx.hasWarnings())
        ctx.logErrors();
}

// ── public entry points ───────────────────────────────────────────────────────

void parse_ini(Input* input, const char* ini_string) {
    if (!ini_string || !*ini_string) { input->root = {.item = ITEM_NULL}; return; }
    InputContext ctx(input, ini_string, strlen(ini_string));
    parse_kv_document(ctx, ini_string, &INI_CONFIG);
}

void parse_properties(Input* input, const char* prop_string) {
    if (!prop_string || !*prop_string) { input->root = {.item = ITEM_NULL}; return; }
    InputContext ctx(input, prop_string, strlen(prop_string));
    parse_kv_document(ctx, prop_string, &PROP_CONFIG);
}
