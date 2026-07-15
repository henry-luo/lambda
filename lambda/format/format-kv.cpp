// Key-Value Formatter — unified INI & Properties output
// Parameterized by KeyValueFormatConfig for format-specific behavior
#include "format.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
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

class KeyValueContext : public FormatterContextCpp {
public:
    KeyValueContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}
};

// ==============================================================================
// Forward declarations
// ==============================================================================

static void format_kv_item(KeyValueContext& ctx, const ItemReader& item,
                           const KeyValueFormatConfig* cfg);
static void format_kv_section(KeyValueContext& ctx, const MapReader& map,
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

static void format_kv_item(KeyValueContext& ctx, const ItemReader& item,
                           const KeyValueFormatConfig* cfg) {
    class KeyValueItemHandlers : public FormatItemHandlersDefault {
    public:
        KeyValueItemHandlers(KeyValueContext& ctx, const KeyValueFormatConfig* cfg)
            : ctx_(ctx), cfg_(cfg) {}

        void null_value(const ItemReader& item) override { (void)item; }
        void bool_value(const ItemReader& item) override { ctx_.emit("%b", item.asBool()); }
        void number_value(const ItemReader& item) override { format_number(ctx_.output(), item.item()); }
        void string_value(const ItemReader& item, String* str) override {
            (void)item;
            if (str) format_kv_string(ctx_.output(), str, cfg_);
        }
        void binary_value(const ItemReader& item, String* bin) override {
            (void)item;
            format_binary_base64_string(ctx_.output(), bin);
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            if (!item.isArray()) {
                unknown_value(item);
                return;
            }

            auto it = arr.items();
            ItemReader arr_item;
            bool first = true;
            while (it.next(&arr_item)) {
                if (!first) ctx_.write_char(',');
                first = false;

                if (arr_item.isNull() || arr_item.isBool() || arr_item.isInt() ||
                    arr_item.isFloat() || arr_item.isString()) {
                    format_kv_item(ctx_, arr_item, cfg_);
                } else {
                    ctx_.write_text("[complex]");
                }
            }
        }
        void map_value(const ItemReader& item, MapReader map) override {
            (void)item;
            (void)map;
            ctx_.write_text("[map]");
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            const char* tag = elem.tagName();
            ctx_.write_text(tag ? tag : "[element]");
        }
        void unknown_value(const ItemReader& item) override { (void)item; ctx_.write_text("[unknown]"); }

    private:
        KeyValueContext& ctx_;
        const KeyValueFormatConfig* cfg_;
    };

    KeyValueItemHandlers handlers(ctx, cfg);
    ctx.dispatch_item(item, handlers);
}

// ==============================================================================
// Section formatting (INI-specific, called only when support_sections == true)
// ==============================================================================

static void format_kv_section(KeyValueContext& ctx, const MapReader& map,
                              const char* section_name, const KeyValueFormatConfig* cfg) {
    if (section_name && strlen(section_name) > 0) {
        ctx.emit("[%s]\n", section_name);
    }

    auto entries = map.entries();
    const char* key;
    ItemReader value;

    while (entries.next(&key, &value)) {
        ctx.emit("%s=", key);
        format_kv_item(ctx, value, cfg);
        ctx.write_char('\n');
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

    KeyValueContext ctx(pool, sb);

    // header comment
    ctx.write_text(cfg->header_comment);
    ctx.write_char('\n');

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
                        if (!first) ctx.write_char('\n');
                        MapReader section_map = value.asMap();
                        format_kv_section(ctx, section_map, key, cfg);
                        first = false;
                    } else {
                        if (!has_global) {
                            if (!first) ctx.write_char('\n');
                            ctx.emit("[%s]\n", cfg->global_section);
                            has_global = true;
                            first = false;
                        }
                        ctx.emit("%s=", key);
                        format_kv_item(ctx, value, cfg);
                        ctx.write_char('\n');
                    }
                }
            } else {
                // no nested maps — flat section
                format_kv_section(ctx, root_map, NULL, cfg);
            }
        } else {
            // Properties path: flat key=value
            auto entries = root_map.entries();
            const char* key;
            ItemReader value;

            while (entries.next(&key, &value)) {
                ctx.emit("%s=", key);
                format_kv_item(ctx, value, cfg);
                ctx.write_char('\n');
            }
        }
    }
    else if (root.isNull() || root.isBool() || root.isInt() ||
             root.isFloat() || root.isString()) {
        // single scalar → value=...
        ctx.write_text("value=");
        format_kv_item(ctx, root, cfg);
        ctx.write_char('\n');
    }
    else {
        // unsupported root type
        ctx.write_text(cfg->header_comment[0] == ';' ? "; " : "# ");
        ctx.write_text("Unsupported root type\n");
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
