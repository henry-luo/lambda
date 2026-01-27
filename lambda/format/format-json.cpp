#include "format.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"

// Forward declarations using MarkReader API with JsonContext
static void format_item_reader_with_indent(JsonContext& ctx, const ItemReader& item, int indent);
static void format_string(JsonContext& ctx, String* str);
static void format_array_reader_with_indent(JsonContext& ctx, const ArrayReader& arr, int indent);
static void format_map_reader_with_indent(JsonContext& ctx, const MapReader& mp, int indent);
static void format_element_reader_with_indent(JsonContext& ctx, const ElementReader& elem, int indent);

// Helper function to add indentation
static void add_indent(JsonContext& ctx, int indent) {
    for (int i = 0; i < indent; i++) {
        ctx.write_text("  ");
    }
}

// Format a MapReader's contents as JSON object properties
static void format_map_reader_contents(JsonContext& ctx, const MapReader& map_reader, int indent) {
    // Prevent infinite recursion
    if (indent > 10) {
        ctx.write_text("\"[MAX_DEPTH]\":null");
        return;
    }

    bool first = true;
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        if (!first) {
            ctx.write_text(",\n");
        } else {
            ctx.write_char('\n');
            first = false;
        }

        add_indent(ctx, indent + 1);

        // Format the key (always quoted in JSON)
        stringbuf_append_format(ctx.output(), "\"%s\":", key);

        // Format the value
        format_item_reader_with_indent(ctx, value, indent + 1);
    }

    if (!first) {
        ctx.write_char('\n');
        add_indent(ctx, indent);
    }
}

static void format_string(JsonContext& ctx, String* str) {
    // Handle null string pointer as null (empty strings map to null in Lambda)
    if (!str) {
        ctx.write_text("null");
        return;
    }

    ctx.write_char('"');

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '"':
            ctx.write_text("\\\"");
            break;
        case '\\':
            ctx.write_text("\\\\");
            break;
        case '/':
            ctx.write_text("\\/");
            break;
        case '\b':
            ctx.write_text("\\b");
            break;
        case '\f':
            ctx.write_text("\\f");
            break;
        case '\n':
            ctx.write_text("\\n");
            break;
        case '\r':
            ctx.write_text("\\r");
            break;
        case '\t':
            ctx.write_text("\\t");
            break;
        default:
            if (c < 0x20) {
                // Control characters - encode as \uXXXX
                char hex_buf[7];
                snprintf(hex_buf, sizeof(hex_buf), "\\u%04x", (unsigned char)c);
                stringbuf_append_str(ctx.output(), hex_buf);
            } else {
                ctx.write_char(c);
            }
            break;
        }
    }
    ctx.write_char('"');
}

static void format_array_reader_with_indent(JsonContext& ctx, const ArrayReader& arr, int indent) {
    ctx.write_char('[');

    int64_t arr_length = arr.length();
    if (arr_length > 0) {
        ctx.write_char('\n');

        auto iter = arr.items();
        ItemReader item;
        bool first = true;

        while (iter.next(&item)) {
            if (!first) {
                ctx.write_text(",\n");
            }
            first = false;

            add_indent(ctx, indent + 1);
            format_item_reader_with_indent(ctx, item, indent + 1);
        }

        ctx.write_char('\n');
        add_indent(ctx, indent);
    }
    ctx.write_char(']');
}

static void format_map_reader_with_indent(JsonContext& ctx, const MapReader& mp, int indent) {
    ctx.write_char('{');
    format_map_reader_contents(ctx, mp, indent);
    ctx.write_char('}');
}

static void format_element_reader_with_indent(JsonContext& ctx, const ElementReader& elem, int indent) {
    stringbuf_append_format(ctx.output(), "\n{\"$\":\"%s\"", elem.tagName());

    // Add attributes as direct properties
    if (elem.attrCount() > 0) {
        // Access attributes directly from ElementReader
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;

        while (field) {
            const char* key = field->name->str;
            ItemReader value = elem.get_attr(key);

            ctx.write_text(",\n");
            add_indent(ctx, indent + 1);
            stringbuf_append_format(ctx.output(), "\"%s\":", key);
            format_item_reader_with_indent(ctx, value, indent + 1);

            field = field->next;
        }
    }

    // Add children if any
    if (elem.childCount() > 0) {
        ctx.write_text(",\n");
        add_indent(ctx, indent + 1);
        ctx.write_text("\"_\":");

        // Format children as an array
        ctx.write_char('[');
        auto child_iter = elem.children();
        ItemReader child;
        bool first = true;

        while (child_iter.next(&child)) {
            if (!first) {
                ctx.write_text(",\n");
            }
            first = false;

            ctx.write_char('\n');
            add_indent(ctx, indent + 2);
            format_item_reader_with_indent(ctx, child, indent + 2);
        }

        if (!first) {
            ctx.write_char('\n');
            add_indent(ctx, indent + 1);
        }
        ctx.write_char(']');
    }

    ctx.write_char('\n');
    add_indent(ctx, indent);
    ctx.write_char('}');
}

static void format_item_reader_with_indent(JsonContext& ctx, const ItemReader& item, int indent) {
    if (item.isNull()) {
        ctx.write_text("null");
    } else if (item.isBool()) {
        stringbuf_append_str(ctx.output(), item.asBool() ? "true" : "false");
    } else if (item.isInt()) {
        stringbuf_append_format(ctx.output(), "%" PRId64, item.asInt());
    } else if (item.isFloat()) {
        stringbuf_append_format(ctx.output(), "%g", item.asFloat());
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_string(ctx, str);
        } else {
            ctx.write_text("null");
        }
    } else if (item.isSymbol()) {
        // Format symbols as strings (they represent identifiers/keywords in CSS)
        String* str = item.asSymbol();
        if (str) {
            format_string(ctx, str);
        } else {
            ctx.write_text("null");
        }
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        format_array_reader_with_indent(ctx, arr, indent);
    } else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader_with_indent(ctx, mp, indent);
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader_with_indent(ctx, elem, indent);
    } else {
        // Unknown type
        ctx.write_text("null");
    }
}

String* format_json(Pool* pool, const Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    Pool* ctx_pool = pool_create();
    JsonContext ctx(ctx_pool, sb);
    ItemReader reader(root_item.to_const());
    format_item_reader_with_indent(ctx, reader, 0);
    pool_destroy(ctx_pool);

    return stringbuf_to_string(sb);
}

// Convenience function that formats JSON to a provided StringBuf
void format_json_to_strbuf(StringBuf* sb, Item root_item) {
    Pool* pool = pool_create();
    JsonContext ctx(pool, sb);
    ItemReader reader(root_item.to_const());
    format_item_reader_with_indent(ctx, reader, 0);
    pool_destroy(pool);
}
