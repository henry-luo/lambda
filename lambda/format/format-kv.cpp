// Key-Value Formatter — unified INI & Properties output
// Parameterized by KeyValueFormatConfig for format-specific behavior
#include "format.h"
#include "format-utils.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../mark_reader.hpp"
#include <string.h>

// ==============================================================================
// Escape tables
// ==============================================================================

// INI escapes: \n \r \t \\ \" ; #
static const EscapeRule INI_ESCAPE_RULES[] = {
    { '\n', "\\n" },
    { '\r', "\\r" },
    { '\t', "\\t" },
    { '\\', "\\\\" },
    { '"',  "\\\"" },
    { ';',  "\\;" },
    { '#',  "\\#" },
};
static const int INI_ESCAPE_COUNT = sizeof(INI_ESCAPE_RULES) / sizeof(INI_ESCAPE_RULES[0]);

// Properties escapes: \n \r \t \\ = : # !
static const EscapeRule PROP_ESCAPE_RULES[] = {
    { '\n', "\\n" },
    { '\r', "\\r" },
    { '\t', "\\t" },
    { '\\', "\\\\" },
    { '=',  "\\=" },
    { ':',  "\\:" },
    { '#',  "\\#" },
    { '!',  "\\!" },
};
static const int PROP_ESCAPE_COUNT = sizeof(PROP_ESCAPE_RULES) / sizeof(PROP_ESCAPE_RULES[0]);

// ==============================================================================
// Config struct
// ==============================================================================

struct KeyValueFormatConfig {
    const char*       header_comment;    // e.g. "; ini formatted output"
    const EscapeRule* escape_rules;
    int               escape_count;
    bool              support_sections;  // true for INI, false for Properties
    const char*       global_section;    // "global" for INI, NULL for Properties
};

static const KeyValueFormatConfig INI_CONFIG = {
    "; ini formatted output",
    INI_ESCAPE_RULES,
    INI_ESCAPE_COUNT,
    true,
    "global",
};

static const KeyValueFormatConfig PROP_CONFIG = {
    "# Properties formatted output",
    PROP_ESCAPE_RULES,
    PROP_ESCAPE_COUNT,
    false,
    NULL,
};

// ==============================================================================
// Forward declarations
// ==============================================================================

static void format_kv_item(StringBuf* sb, const ItemReader& item,
                           const KeyValueFormatConfig* cfg);
static void format_kv_section(StringBuf* sb, const MapReader& map,
                              const char* section_name, const KeyValueFormatConfig* cfg);

// ==============================================================================
// Escaped string
// ==============================================================================

static void format_kv_string(StringBuf* sb, String* str, const KeyValueFormatConfig* cfg) {
    if (!str) return;
    format_escaped_string(sb, str->chars, str->len, cfg->escape_rules, cfg->escape_count);
}

// ==============================================================================
// Item dispatch (shared)
// ==============================================================================

static void format_kv_item(StringBuf* sb, const ItemReader& item,
                           const KeyValueFormatConfig* cfg) {
    if (item.isNull()) return;

    if (item.isBool()) {
        stringbuf_append_str(sb, item.asBool() ? "true" : "false");
    }
    else if (item.isInt() || item.isFloat()) {
        format_number(sb, item.item());
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) format_kv_string(sb, str, cfg);
    }
    else if (item.isArray()) {
        // arrays are comma-separated values
        ArrayReader arr = item.asArray();
        auto it = arr.items();
        ItemReader arr_item;
        bool first = true;

        while (it.next(&arr_item)) {
            if (!first) stringbuf_append_str(sb, ",");
            first = false;

            if (arr_item.isNull() || arr_item.isBool() || arr_item.isInt() ||
                arr_item.isFloat() || arr_item.isString()) {
                format_kv_item(sb, arr_item, cfg);
            } else {
                stringbuf_append_str(sb, "[complex]");
            }
        }
    }
    else if (item.isMap()) {
        stringbuf_append_str(sb, "[map]");
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        const char* tag = elem.tagName();
        stringbuf_append_str(sb, tag ? tag : "[element]");
    }
    else {
        stringbuf_append_str(sb, "[unknown]");
    }
}

// ==============================================================================
// Section formatting (INI-specific, called only when support_sections == true)
// ==============================================================================

static void format_kv_section(StringBuf* sb, const MapReader& map,
                              const char* section_name, const KeyValueFormatConfig* cfg) {
    if (section_name && strlen(section_name) > 0) {
        stringbuf_append_format(sb, "[%s]\n", section_name);
    }

    auto entries = map.entries();
    const char* key;
    ItemReader value;

    while (entries.next(&key, &value)) {
        stringbuf_append_format(sb, "%s=", key);
        format_kv_item(sb, value, cfg);
        stringbuf_append_char(sb, '\n');
    }
}

// ==============================================================================
// Core entry point
// ==============================================================================

static String* format_kv(Pool* pool, Item root_item, const KeyValueFormatConfig* cfg) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        log_error("format_kv: failed to create string buffer");
        return NULL;
    }

    // header comment
    stringbuf_append_str(sb, cfg->header_comment);
    stringbuf_append_char(sb, '\n');

    ItemReader root(root_item.to_const());

    if (root.isMap()) {
        MapReader root_map = root.asMap();

        if (cfg->support_sections) {
            // INI path: detect nested maps → sections
            bool has_nested_maps = false;
            {
                auto entries = root_map.entries();
                const char* key;
                ItemReader value;
                while (entries.next(&key, &value)) {
                    if (value.isMap()) { has_nested_maps = true; break; }
                }
            }

            if (has_nested_maps) {
                auto entries = root_map.entries();
                const char* key;
                ItemReader value;
                bool first = true;
                bool has_global = false;

                while (entries.next(&key, &value)) {
                    if (value.isMap()) {
                        if (!first) stringbuf_append_char(sb, '\n');
                        MapReader section_map = value.asMap();
                        format_kv_section(sb, section_map, key, cfg);
                        first = false;
                    } else {
                        if (!has_global) {
                            if (!first) stringbuf_append_char(sb, '\n');
                            stringbuf_append_format(sb, "[%s]\n", cfg->global_section);
                            has_global = true;
                            first = false;
                        }
                        stringbuf_append_format(sb, "%s=", key);
                        format_kv_item(sb, value, cfg);
                        stringbuf_append_char(sb, '\n');
                    }
                }
            } else {
                // no nested maps — flat section
                format_kv_section(sb, root_map, NULL, cfg);
            }
        } else {
            // Properties path: flat key=value
            auto entries = root_map.entries();
            const char* key;
            ItemReader value;

            while (entries.next(&key, &value)) {
                stringbuf_append_format(sb, "%s=", key);
                format_kv_item(sb, value, cfg);
                stringbuf_append_char(sb, '\n');
            }
        }
    }
    else if (root.isNull() || root.isBool() || root.isInt() ||
             root.isFloat() || root.isString()) {
        // single scalar → value=...
        stringbuf_append_str(sb, "value=");
        format_kv_item(sb, root, cfg);
        stringbuf_append_char(sb, '\n');
    }
    else {
        // unsupported root type
        stringbuf_append_str(sb, cfg->header_comment[0] == ';' ? "; " : "# ");
        stringbuf_append_str(sb, "Unsupported root type\n");
    }

    return stringbuf_to_string(sb);
}

// ==============================================================================
// Public API wrappers
// ==============================================================================

String* format_ini(Pool* pool, Item root_item) {
    log_debug("format_ini: entry");
    String* result = format_kv(pool, root_item, &INI_CONFIG);
    log_debug("format_ini: completed");
    return result;
}

String* format_properties(Pool* pool, Item root_item) {
    return format_kv(pool, root_item, &PROP_CONFIG);
}
