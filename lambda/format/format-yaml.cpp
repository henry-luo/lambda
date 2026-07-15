// YAML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
// Updated to use MarkReader API and YamlContext (Nov 2025)
#include "format.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/datetime.h"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"
#include <string.h>

// forward declarations
static void format_item_reader(YamlContext& ctx, const ItemReader& item, int indent_level);
static void format_array_reader(YamlContext& ctx, const ArrayReader& arr, int indent_level);
static void format_map_reader(YamlContext& ctx, const MapReader& map_reader, int indent_level);
static void format_element_reader(YamlContext& ctx, const ElementReader& elem, int indent_level);
static void format_yaml_string(YamlContext& ctx, String* str);

static bool yaml_scalar_needs_quotes(const char* s, size_t len) {
    if (!s) return false;

    if (len == 0 || strchr(s, ':') || strchr(s, '\n') || strchr(s, '"') ||
        strchr(s, '\'') || strchr(s, '#') || strchr(s, '-') || strchr(s, '[') ||
        strchr(s, ']') || strchr(s, '{') || strchr(s, '}') || strchr(s, '|') ||
        strchr(s, '>') || strchr(s, '&') || strchr(s, '*') || strchr(s, '!') ||
        (len > 0 && (str_char_is_ascii_space(s[0]) || str_char_is_ascii_space(s[len-1])))) {
        return true;
    }

    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
        strcmp(s, "null") == 0 || strcmp(s, "yes") == 0 ||
        strcmp(s, "no") == 0 || strcmp(s, "on") == 0 ||
        strcmp(s, "off") == 0 || strcmp(s, "~") == 0 ||
        strcmp(s, ".inf") == 0 || strcmp(s, "-.inf") == 0 ||
        strcmp(s, ".nan") == 0 || strcmp(s, ".Inf") == 0 ||
        strcmp(s, "-.Inf") == 0 || strcmp(s, ".NaN") == 0) {
        return true;
    }

    char* end;
    strtol(s, &end, 10);
    if (*end == '\0' && len > 0) return true;
    strtod(s, &end);
    return *end == '\0' && len > 0;
}

// format a string value for YAML - handle quoting and escaping
static void format_yaml_string(YamlContext& ctx, String* str) {
    if (!str) {
        stringbuf_append_str(ctx.output(), "null");
        return;
    }

    const char* s = str->chars;
    size_t len = str->len;
    bool needs_quotes = yaml_scalar_needs_quotes(s, len);

    if (needs_quotes) {
        stringbuf_append_char(ctx.output(), '"');
        format_escaped_string(ctx.output(), s, len,
            YAML_ESCAPE_RULES, YAML_ESCAPE_RULES_COUNT);
        stringbuf_append_char(ctx.output(), '"');
    } else {
        stringbuf_append_str_n(ctx.output(), s, len);
    }
}


// format array items for YAML using ArrayReader
static void format_array_reader(YamlContext& ctx, const ArrayReader& arr, int indent_level) {
    if (!arr.isValid() || arr.isEmpty()) {
        stringbuf_append_str(ctx.output(), "[]");
        return;
    }

    auto iter = arr.items();
    ItemReader item;
    int index = 0;

    while (iter.next(&item)) {
        if (index > 0 || indent_level > 0) {
            ctx.emit("%n%i", indent_level);
        }
        ctx.write_text("- ");

        // for complex types, add proper indentation
        if (item.isMap() || item.isElement() || item.isArray() || item.isList()) {
            format_item_reader(ctx, item, indent_level + 1);
        } else {
            format_item_reader(ctx, item, 0);
        }
        index++;
    }
}

// format map items using MapReader
static void format_map_reader(YamlContext& ctx, const MapReader& map_reader, int indent_level) {
    if (!map_reader.isValid()) {
            ctx.write_text("{}");
    }

    bool first_item = true;
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        // skip null/unset fields appropriately
        if (value.isNull()) {
            if (!first_item) ctx.write_char('\n');
            first_item = false;
            ctx.emit("%i%N: null", indent_level, key);
            continue;
        }

        if (!first_item) ctx.write_char('\n');
        first_item = false;

        // add field name
        ctx.emit("%i%N: ", indent_level, key);

        // format field value
        if (value.isMap() || value.isElement() || value.isArray() || value.isList()) {
            // for complex types, add newline and proper indentation
            if (value.isMap() || value.isElement()) {
                stringbuf_append_char(ctx.output(), '\n');
            }
            format_item_reader(ctx, value, indent_level + 1);
        } else {
            format_item_reader(ctx, value, 0);
        }
    }
}

// format element using ElementReader
static void format_element_reader(YamlContext& ctx, const ElementReader& elem, int indent_level) {
    // for yaml, represent element as an object with special "$" key for tag name
    if (indent_level > 0) ctx.emit("%n%i", indent_level);
    ctx.emit("$: \"%N\"", elem.tagName());

    // add attributes if any
    if (elem.attrCount() > 0) {
        ctx.write_char('\n');
        auto attrs = elem.attrs();
        const char* key;
        ItemReader attr_value;
        bool first_attr = true;

        while (attrs.next(&key, &attr_value)) {
            if (!key) continue;

            if (!first_attr) ctx.write_char('\n');
            first_attr = false;
            ctx.emit("%i%N: ", indent_level, key);
            format_item_reader(ctx, attr_value, 0);
        }
    }

    // add children if any
    if (elem.childCount() > 0) {
        ctx.write_char('\n');
        ctx.write_indent(indent_level);
        ctx.write_text("_:");

        auto child_iter = elem.children();
        ItemReader child;
        int child_index = 0;

        while (child_iter.next(&child)) {
            if (child_index > 0 || indent_level >= 0) {
                ctx.emit("%n%i", indent_level + 1);
            }
            ctx.write_text("- ");

            if (child.isMap() || child.isElement() || child.isArray() || child.isList()) {
                format_item_reader(ctx, child, indent_level + 2);
            } else {
                format_item_reader(ctx, child, 0);
            }
            child_index++;
        }
    }
}

// centralized function to format any Lambda Item with proper type handling using MarkReader
static void format_item_reader(YamlContext& ctx, const ItemReader& item, int indent_level) {
    class YamlItemHandlers : public FormatItemHandlersDefault {
    public:
        YamlItemHandlers(YamlContext& ctx, int indent_level)
            : ctx_(ctx), indent_level_(indent_level) {}

        void max_depth(const ItemReader& item) override { (void)item; ctx_.write_text("\"[max_depth]\""); }
        void null_value(const ItemReader& item) override { (void)item; ctx_.write_text("null"); }
        void bool_value(const ItemReader& item) override { ctx_.emit("%b", item.asBool()); }
        void number_value(const ItemReader& item) override { format_number(ctx_.output(), item.item()); }
        void string_value(const ItemReader& item, String* str) override {
            (void)item;
            if (str) {
                format_yaml_string(ctx_, str);
            } else {
                ctx_.write_text("null");
            }
        }
        void binary_value(const ItemReader& item, Binary* bin) override {
            (void)item;
            // YAML egress matches other text formats with a base64 string;
            // ingress remains explicit and never guesses that strings are bytes.
            ctx_.write_char('"');
            format_binary_base64_string(ctx_.output(), bin);
            ctx_.write_char('"');
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            (void)item;
            format_array_reader(ctx_, arr, indent_level_);
        }
        void map_value(const ItemReader& item, MapReader mp) override {
            (void)item;
            format_map_reader(ctx_, mp, indent_level_);
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            format_element_reader(ctx_, elem, indent_level_);
        }
        void datetime_value(const ItemReader& item, DateTime* dt) override {
            (void)item;
            if (dt) {
                ctx_.write_char('"');
                format_write_datetime_iso8601(ctx_.output(), dt);
                ctx_.write_char('"');
            } else {
                ctx_.write_text("null");
            }
        }
        void unknown_value(const ItemReader& item) override {
            ctx_.emit("\"[type_%d]\"", (int)item.getType());
        }

    private:
        YamlContext& ctx_;
        int indent_level_;
    };

    YamlItemHandlers handlers(ctx, indent_level);
    ctx.dispatch_item(item, handlers);
}

// yaml formatter that produces proper YAML output
String* format_yaml(Pool* pool, Item root_item) {
    log_debug("format_yaml: ENTRY - MarkReader version");

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        log_error("format_yaml: failed to create string buffer");
        return NULL;
    }

    // Create YAML context
    ScopedFormatPool ctx_pool("format.yaml");
    YamlContext ctx(ctx_pool.get(), sb);

    ItemReader reader(root_item.to_const());

    // Check if root is an array that might represent multiple YAML documents
    if (reader.isArray() || reader.isList()) {
        ArrayReader arr = reader.asArray();
        if (arr.length() > 1) {
            // Treat as multi-document YAML
            auto iter = arr.items();
            ItemReader doc_item;
            int doc_index = 0;

            while (iter.next(&doc_item)) {
                if (doc_index > 0) {
                    stringbuf_append_str(ctx.output(), "\n---\n");
                } else {
                    stringbuf_append_str(ctx.output(), "---\n");
                }

                // add lowercase comment as requested
                stringbuf_append_str(ctx.output(), "# yaml formatted output\n");

                // format each document
                format_item_reader(ctx, doc_item, 0);
                stringbuf_append_char(ctx.output(), '\n');
                doc_index++;
            }
        } else {
            // Single document array
            stringbuf_append_str(ctx.output(), "---\n");
            stringbuf_append_str(ctx.output(), "# yaml formatted output\n");
            format_item_reader(ctx, reader, 0);
            stringbuf_append_char(ctx.output(), '\n');
        }
    } else {
        // Single document
        stringbuf_append_str(ctx.output(), "---\n");
        stringbuf_append_str(ctx.output(), "# yaml formatted output\n");
        format_item_reader(ctx, reader, 0);
        stringbuf_append_char(ctx.output(), '\n');
    }

    return stringbuf_to_string(sb);
}
