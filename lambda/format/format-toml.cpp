// TOML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
#include "format.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include <string.h>

// forward declarations
static void format_item_reader(TomlContext& ctx, ItemReader item, const char* parent_context);
static bool should_format_as_table_section_reader(MapReader map);
static void format_table_section_reader(TomlContext& ctx, MapReader map, const char* section_name, const char* parent_context);
static void format_inline_table_reader(TomlContext& ctx, MapReader map);
static void format_array_items_reader(TomlContext& ctx, ArrayReader arr);

// determine if a map should be formatted as a table section [name] vs inline table {key=val}
static bool should_format_as_table_section_reader(MapReader map) {
    if (!map.isValid()) {
        return false;
    }

    int64_t length = map.size();
    if (length >= 3) {
        return true; // many fields
    }

    // check for complex content (arrays or nested maps) even with fewer fields
    auto values = map.values();
    ItemReader value;
    while (values.next(&value)) {
        if (value.isArray() || value.isList() || value.isMap()) {
            return true;
        }
    }

    return false;
}

// central function to format any Lambda Item with proper type handling
static void format_item_reader(TomlContext& ctx, ItemReader item, const char* parent_context) {
    TomlContext::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        ctx.write_text("\"[max_depth]\"");
        return;
    }

    if (item.isNull()) {
        ctx.write_text("\"\"");
    } else if (item.isBool()) {
        ctx.emit("%b", item.asBool());
    } else if (item.isInt() || item.isFloat()) {
        format_number(ctx.output(), item.item());
    } else if (item.isString()) {
        ctx.emit("%Q", item.asString());
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        if (arr.length() > 0) {
            ctx.write_char('[');
            format_array_items_reader(ctx, arr);
            ctx.write_char(']');
        } else {
            ctx.write_text("[]");
        }
    } else if (item.isMap()) {
        MapReader map = item.asMap();
        if (map.isValid()) {
            format_inline_table_reader(ctx, map);
        } else {
            ctx.write_text("{}");
        }
    } else {
        ctx.emit("\"[type_%d]\"", (int)item.getType());
    }
}

// format array items with proper item processing
static void format_array_items_reader(TomlContext& ctx, ArrayReader arr) {
    if (!arr.isValid() || arr.length() == 0) {
        return;
    }

    auto items = arr.items();
    ItemReader item;
    bool first = true;

    while (items.next(&item)) {
        if (!first) ctx.write_text(", ");
        first = false;
        format_item_reader(ctx, item, NULL);
    }
}

// format a map as inline table {key=val, ...}
static void format_inline_table_reader(TomlContext& ctx, MapReader map) {
    if (!map.isValid() || map.size() == 0) {
        ctx.write_text("{}");
        return;
    }

    ctx.write_text("{ ");

    auto entries = map.entries();
    const char* key;
    ItemReader value;
    bool first_field = true;

    while (entries.next(&key, &value)) {
        if (!first_field) ctx.write_text(", ");
        first_field = false;
        ctx.write_text(key);
        ctx.write_text(" = ");
        format_item_reader(ctx, value, NULL);
    }

    ctx.write_text(" }");
}

// format a map as a table section [section_name]
static void format_table_section_reader(TomlContext& ctx, MapReader map, const char* section_name, const char* parent_context) {
    if (!map.isValid() || !section_name || map.size() == 0) return;

    TomlContext::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        ctx.write_text("# [max_depth_section]\n");
        return;
    }

    // section header
    if (parent_context && strlen(parent_context) > 0) {
        ctx.emit("%n[%s.%s]%n", parent_context, section_name);
    } else {
        ctx.emit("%n[%s]%n", section_name);
    }

    // build full section context for nested sections
    char full_section_name[256];
    if (parent_context && strlen(parent_context) > 0) {
        snprintf(full_section_name, sizeof(full_section_name), "%s.%s", parent_context, section_name);
    } else {
        snprintf(full_section_name, sizeof(full_section_name), "%s", section_name);
    }

    // process fields
    auto entries = map.entries();
    const char* key;
    ItemReader value;

    while (entries.next(&key, &value)) {
        if (value.isMap()) {
            MapReader nested_map = value.asMap();
            if (should_format_as_table_section_reader(nested_map)) {
                format_table_section_reader(ctx, nested_map, key, full_section_name);
                continue;
            }
        }
        ctx.emit("%s = ", key);
        format_item_reader(ctx, value, full_section_name);
        ctx.write_char('\n');
    }
}

// main function to format map attributes
static void format_toml_attrs_from_map_reader(TomlContext& ctx, MapReader map, const char* parent_name) {
    if (!map.isValid()) return;

    auto entries = map.entries();
    const char* key;
    ItemReader value;

    while (entries.next(&key, &value)) {
        if (value.isMap()) {
            MapReader nested_map = value.asMap();
            if (should_format_as_table_section_reader(nested_map)) {
                format_table_section_reader(ctx, nested_map, key, parent_name);
                continue;
            }
        }
        ctx.emit("%s = ", key);
        format_item_reader(ctx, value, parent_name);
        ctx.write_char('\n');
    }
}

// main formatter entry point
String* format_toml(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    TomlContext ctx(pool, sb);

    ctx.write_text("# TOML formatted output\n");
    ctx.write_text("# Generated by Lambda TOML formatter\n");
    ctx.write_char('\n');

    ItemReader reader(root_item.to_const());

    if (reader.isMap()) {
        MapReader map = reader.asMap();
        int64_t length = map.size();

        if (length > 0) {
            ctx.write_text("# Map with ");
            ctx.emit("%ld", (int64_t)length);
            ctx.write_text(" fields\n\n");
            format_toml_attrs_from_map_reader(ctx, map, NULL);
        } else {
            ctx.write_text("# Empty map\n");
        }
    } else {
        if (!reader.isNull()) {
            ctx.emit("# Root type: %d\n", (int)reader.getType());
            ctx.write_text("root_value = ");
            format_item_reader(ctx, reader, NULL);
            ctx.write_char('\n');
        } else {
            ctx.write_text("# Unable to determine root type\n");
            ctx.emit("# Raw value: 0x%llx\n", (unsigned long long)root_item.item);
            ctx.write_text("status = \"unable_to_format\"\n");
        }
    }

    return stringbuf_to_string(sb);
}
