#include "format.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/datetime.h"
#include "../../lib/strbuf.h"
#include "../mark_reader.hpp"

// Forward declarations using MarkReader API with JsonContext
static void format_item_reader_with_indent(JsonContext& ctx, const ItemReader& item, int indent);
static void format_string(JsonContext& ctx, String* str);
static void format_array_reader_with_indent(JsonContext& ctx, const ArrayReader& arr, int indent);
static void format_map_reader_with_indent(JsonContext& ctx, const MapReader& mp, int indent);
static void format_element_reader_with_indent(JsonContext& ctx, const ElementReader& elem, int indent);

// Format a MapReader's contents as JSON object properties
static void format_map_reader_contents(JsonContext& ctx, const MapReader& map_reader, int indent) {
    bool first = true;
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        // Skip function-valued properties (like JSON.stringify)
        if (value.getType() == LMD_TYPE_FUNC) continue;

        if (!first) {
            ctx.write_text(",\n");
        } else {
            ctx.write_char('\n');
            first = false;
        }

        ctx.write_indent(indent + 1);

        // Format the key (always quoted in JSON, with proper escaping)
        ctx.write_char('"');
        format_escaped_string_ex(ctx.output(), key, strlen(key),
            JSON_ESCAPE_RULES, JSON_ESCAPE_RULES_COUNT,
            ESCAPE_CTRL_JSON_UNICODE);
        ctx.write_char('"');
        ctx.write_text(": ");

        // Format the value
        format_item_reader_with_indent(ctx, value, indent + 1);
    }

    if (!first) {
        ctx.emit("%n%i", indent);
    }
}

static void format_string(JsonContext& ctx, String* str) {
    // Handle null string pointer as null (empty strings map to null in Lambda)
    if (!str) {
        ctx.write_text("null");
        return;
    }

    ctx.write_char('"');
    format_escaped_string_ex(ctx.output(), str->chars, str->len,
        JSON_ESCAPE_RULES, JSON_ESCAPE_RULES_COUNT,
        ESCAPE_CTRL_JSON_UNICODE);
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

            ctx.write_indent(indent + 1);
            format_item_reader_with_indent(ctx, item, indent + 1);
        }

        ctx.emit("%n%i", indent);
    }
    ctx.write_char(']');
}

static void format_map_reader_with_indent(JsonContext& ctx, const MapReader& mp, int indent) {
    ctx.write_char('{');
    format_map_reader_contents(ctx, mp, indent);
    ctx.write_char('}');
}

static void format_element_reader_with_indent(JsonContext& ctx, const ElementReader& elem, int indent) {
    ctx.emit("%n{\"$\": \"%N\"", elem.tagName());

    // Add attributes as direct properties
    if (elem.attrCount() > 0) {
        // Access attributes directly from ElementReader
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;

        while (field) {
            const char* key = field->name->str;
            ItemReader value = elem.get_attr(key);

            ctx.emit(",%n%i\"%N\": ", indent + 1, key);
            format_item_reader_with_indent(ctx, value, indent + 1);

            field = field->next;
        }
    }

    // Add children if any
    if (elem.childCount() > 0) {
        ctx.emit(",%n%i\"_\": ", indent + 1);

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

            ctx.emit("%n%i", indent + 2);
            format_item_reader_with_indent(ctx, child, indent + 2);
        }

        if (!first) {
            ctx.emit("%n%i", indent + 1);
        }
        ctx.write_char(']');
    }

    ctx.emit("%n%i}", indent);
}

static void format_item_reader_with_indent(JsonContext& ctx, const ItemReader& item, int indent) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        ctx.write_text("\"[max_depth]\"");
        return;
    }

    if (item.isNull()) {
        ctx.write_text("null");
    } else if (item.isBool()) {
        ctx.emit("%b", item.asBool());
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
        Symbol* sym = item.asSymbol();
        if (sym) {
            // Format symbol chars as a JSON string
            ctx.write_char('"');
            stringbuf_append_str_n(ctx.output(), sym->chars, sym->len);
            ctx.write_char('"');
        } else {
            ctx.write_text("null");
        }
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        format_array_reader_with_indent(ctx, arr, indent);
    } else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader_with_indent(ctx, mp, indent);
    } else if (item.getType() == LMD_TYPE_OBJECT) {
        // Object: format as map with "@" type discriminator key
        Object* obj = (Object*)(uintptr_t)item.item().item;
        TypeObject* obj_type = (TypeObject*)obj->type;
        ctx.emit("{%n%i\"@\": \"", indent + 1);
        if (obj_type->type_name.str) {
            stringbuf_append_str_n(ctx.output(), obj_type->type_name.str, obj_type->type_name.length);
        }
        ctx.write_char('"');
        // Format fields using MapReader (Object has same field layout as Map)
        MapReader mp = MapReader::fromItem(item.item());
        auto iter = mp.entries();
        const char* key;
        ItemReader value;
        while (iter.next(&key, &value)) {
            ctx.emit(",%n%i\"%N\": ", indent + 1, key);
            format_item_reader_with_indent(ctx, value, indent + 1);
        }
        ctx.emit("%n%i}", indent);
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader_with_indent(ctx, elem, indent);
    } else if (item.isDatetime()) {
        // format datetime as ISO 8601 string in JSON
        DateTime dt = item.asDatetime();
        StrBuf* buf = strbuf_new();
        datetime_format_iso8601(buf, &dt);
        stringbuf_append_format(ctx.output(), "\"%.*s\"", (int)buf->length, buf->str);
        strbuf_free(buf);
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
