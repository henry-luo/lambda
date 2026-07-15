#include "format.h"
#include "format-utils.hpp"
#include "../lambda-decimal.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mem_factory.h"
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
        // Skip deleted properties (JS delete operator sentinel).
        if (lam::is_hole_sentinel(value.item())) continue;

        if (!first) {
            ctx.write_text(",\n");
        } else {
            ctx.write_char('\n');
            first = false;
        }

        ctx.write_indent(indent + 1);

        // Format the key (always quoted in JSON, with proper escaping)
        escape_append_json_stringbuf(ctx.output(), key, strlen(key), true, false);
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

    escape_append_json_stringbuf(ctx.output(), str->chars, str->len, true, false);
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
        auto attrs = elem.attrs();
        const char* key;
        ItemReader value;
        while (attrs.next(&key, &value)) {
            if (!key) continue;

            ctx.emit(",%n%i\"%N\": ", indent + 1, key);
            format_item_reader_with_indent(ctx, value, indent + 1);
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
    class JsonItemHandlers : public FormatItemHandlersDefault {
    public:
        JsonItemHandlers(JsonContext& ctx, int indent) : ctx_(ctx), indent_(indent) {}

        void max_depth(const ItemReader& item) override { (void)item; ctx_.write_text("\"[max_depth]\""); }
        void null_value(const ItemReader& item) override { (void)item; ctx_.write_text("null"); }
        void bool_value(const ItemReader& item) override { ctx_.emit("%b", item.asBool()); }
        void number_value(const ItemReader& item) override { format_number(ctx_.output(), item.item()); }
        void string_value(const ItemReader& item, String* str) override {
            (void)item;
            if (str) {
                format_string(ctx_, str);
            } else {
                ctx_.write_text("null");
            }
        }
        void binary_value(const ItemReader& item, String* bin) override {
            (void)item;
            // JSON has no binary scalar, so egress uses an explicit base64 string.
            ctx_.write_char('"');
            format_binary_base64_string(ctx_.output(), bin);
            ctx_.write_char('"');
        }
        void symbol_value(const ItemReader& item, Symbol* sym) override {
            (void)item;
            if (sym) {
                ctx_.write_char('"');
                stringbuf_append_str_n(ctx_.output(), sym->chars, sym->len);
                ctx_.write_char('"');
            } else {
                ctx_.write_text("null");
            }
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            (void)item;
            // isArray() covers typed ArrayNum; the reader walks every backing.
            format_array_reader_with_indent(ctx_, arr, indent_);
        }
        void map_value(const ItemReader& item, MapReader mp) override {
            (void)item;
            format_map_reader_with_indent(ctx_, mp, indent_);
        }
        void object_value(const ItemReader& item, Object* obj) override {
            TypeObject* obj_type = (TypeObject*)obj->type;
            ctx_.emit("{%n%i\"@\": \"", indent_ + 1);
            if (obj_type->type_name.str) {
                stringbuf_append_str_n(ctx_.output(), obj_type->type_name.str, obj_type->type_name.length);
            }
            ctx_.write_char('"');
            // Object fields use the same packed layout as maps, so reuse MapReader.
            MapReader mp = MapReader::fromItem(item.item());
            auto iter = mp.entries();
            const char* key;
            ItemReader value;
            while (iter.next(&key, &value)) {
                ctx_.emit(",%n%i\"%N\": ", indent_ + 1, key);
                format_item_reader_with_indent(ctx_, value, indent_ + 1);
            }
            ctx_.emit("%n%i}", indent_);
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            format_element_reader_with_indent(ctx_, elem, indent_);
        }
        void datetime_value(const ItemReader& item, DateTime* dt) override {
            (void)item;
            ctx_.write_char('"');
            format_write_datetime_iso8601(ctx_.output(), dt);
            ctx_.write_char('"');
        }
        void unknown_value(const ItemReader& item) override { (void)item; ctx_.write_text("null"); }

    private:
        JsonContext& ctx_;
        int indent_;
    };

    JsonItemHandlers handlers(ctx, indent);
    ctx.dispatch_item(item, handlers);
}

String* format_json(Pool* pool, const Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    ScopedFormatPool ctx_pool("format.json");
    JsonContext ctx(ctx_pool.get(), sb);
    ItemReader reader(root_item.to_const());
    format_item_reader_with_indent(ctx, reader, 0);

    return stringbuf_to_string(sb);
}

// Convenience function that formats JSON to a provided StringBuf
void format_json_to_strbuf(StringBuf* sb, Item root_item) {
    ScopedFormatPool pool("format.json");
    JsonContext ctx(pool.get(), sb);
    ItemReader reader(root_item.to_const());
    format_item_reader_with_indent(ctx, reader, 0);
}
