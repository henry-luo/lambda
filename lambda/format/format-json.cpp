#include "format.h"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"

// Forward declarations using MarkReader API
static void format_item_reader_with_indent(StringBuf* sb, const ItemReader& item, int indent);
static void format_string(StringBuf* sb, String* str);
static void format_array_reader_with_indent(StringBuf* sb, const ArrayReader& arr, int indent);
static void format_map_reader_with_indent(StringBuf* sb, const MapReader& mp, int indent);
static void format_element_reader_with_indent(StringBuf* sb, const ElementReaderWrapper& elem, int indent);

// Helper function to add indentation
static void add_indent(StringBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

// Format a MapReader's contents as JSON object properties
static void format_map_reader_contents(StringBuf* sb, const MapReader& map_reader, int indent) {
    // Prevent infinite recursion
    if (indent > 10) {
        stringbuf_append_str(sb, "\"[MAX_DEPTH]\":null");
        return;
    }

    bool first = true;
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        if (!first) {
            stringbuf_append_str(sb, ",\n");
        } else {
            stringbuf_append_char(sb, '\n');
            first = false;
        }

        add_indent(sb, indent + 1);

        // Format the key (always quoted in JSON)
        stringbuf_append_format(sb, "\"%s\":", key);

        // Format the value
        format_item_reader_with_indent(sb, value, indent + 1);
    }

    if (!first) {
        stringbuf_append_char(sb, '\n');
        add_indent(sb, indent);
    }
}

static void format_string(StringBuf* sb, String* str) {
    // Handle EMPTY_STRING specially
    if (str == &EMPTY_STRING) {
        stringbuf_append_str(sb, "\"\"");
        return;
    }

    // Handle literal "lambda.nil" content as empty string
    if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
        stringbuf_append_str(sb, "\"\"");
        return;
    }

    stringbuf_append_char(sb, '"');

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '"':
            stringbuf_append_str(sb, "\\\"");
            break;
        case '\\':
            stringbuf_append_str(sb, "\\\\");
            break;
        case '/':
            stringbuf_append_str(sb, "\\/");
            break;
        case '\b':
            stringbuf_append_str(sb, "\\b");
            break;
        case '\f':
            stringbuf_append_str(sb, "\\f");
            break;
        case '\n':
            stringbuf_append_str(sb, "\\n");
            break;
        case '\r':
            stringbuf_append_str(sb, "\\r");
            break;
        case '\t':
            stringbuf_append_str(sb, "\\t");
            break;
        default:
            if (c < 0x20) {
                // Control characters - encode as \uXXXX
                char hex_buf[7];
                snprintf(hex_buf, sizeof(hex_buf), "\\u%04x", (unsigned char)c);
                stringbuf_append_str(sb, hex_buf);
            } else {
                stringbuf_append_char(sb, c);
            }
            break;
        }
    }
    stringbuf_append_char(sb, '"');
}

static void format_array_reader_with_indent(StringBuf* sb, const ArrayReader& arr, int indent) {
    stringbuf_append_char(sb, '[');

    int64_t arr_length = arr.length();
    if (arr_length > 0) {
        stringbuf_append_char(sb, '\n');

        auto iter = arr.items();
        ItemReader item;
        bool first = true;

        while (iter.next(&item)) {
            if (!first) {
                stringbuf_append_str(sb, ",\n");
            }
            first = false;

            add_indent(sb, indent + 1);
            format_item_reader_with_indent(sb, item, indent + 1);
        }

        stringbuf_append_char(sb, '\n');
        add_indent(sb, indent);
    }
    stringbuf_append_char(sb, ']');
}

static void format_map_reader_with_indent(StringBuf* sb, const MapReader& mp, int indent) {
    stringbuf_append_char(sb, '{');
    format_map_reader_contents(sb, mp, indent);
    stringbuf_append_char(sb, '}');
}

static void format_element_reader_with_indent(StringBuf* sb, const ElementReaderWrapper& elem, int indent) {
    stringbuf_append_format(sb, "\n{\"$\":\"%s\"", elem.tagName());

    // Add attributes as direct properties
    if (elem.attrCount() > 0) {
        AttributeReaderWrapper attrs(elem);
        auto iter = attrs.iterator();
        const char* key;
        ItemReader value;

        while (iter.next(&key, &value)) {
            stringbuf_append_str(sb, ",\n");
            add_indent(sb, indent + 1);
            stringbuf_append_format(sb, "\"%s\":", key);
            format_item_reader_with_indent(sb, value, indent + 1);
        }
    }

    // Add children if any
    if (elem.childCount() > 0) {
        stringbuf_append_str(sb, ",\n");
        add_indent(sb, indent + 1);
        stringbuf_append_str(sb, "\"_\":");

        // Format children as an array
        stringbuf_append_char(sb, '[');
        auto child_iter = elem.children();
        ItemReader child;
        bool first = true;

        while (child_iter.next(&child)) {
            if (!first) {
                stringbuf_append_str(sb, ",\n");
            }
            first = false;

            stringbuf_append_char(sb, '\n');
            add_indent(sb, indent + 2);
            format_item_reader_with_indent(sb, child, indent + 2);
        }

        if (!first) {
            stringbuf_append_char(sb, '\n');
            add_indent(sb, indent + 1);
        }
        stringbuf_append_char(sb, ']');
    }

    stringbuf_append_char(sb, '\n');
    add_indent(sb, indent);
    stringbuf_append_char(sb, '}');
}

static void format_item_reader_with_indent(StringBuf* sb, const ItemReader& item, int indent) {
    if (item.isNull()) {
        stringbuf_append_str(sb, "null");
    } else if (item.isBool()) {
        stringbuf_append_str(sb, item.asBool() ? "true" : "false");
    } else if (item.isInt()) {
        stringbuf_append_format(sb, "%" PRId64, item.asInt());
    } else if (item.isFloat()) {
        stringbuf_append_format(sb, "%g", item.asFloat());
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_string(sb, str);
        } else {
            stringbuf_append_str(sb, "null");
        }
    } else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        format_array_reader_with_indent(sb, arr, indent);
    } else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader_with_indent(sb, mp, indent);
    } else if (item.isElement()) {
        ElementReaderWrapper elem = item.asElement();
        format_element_reader_with_indent(sb, elem, indent);
    } else {
        // Unknown type
        stringbuf_append_str(sb, "null");
    }
}

String* format_json(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    ItemReader reader(root_item);
    format_item_reader_with_indent(sb, reader, 0);

    return stringbuf_to_string(sb);
}

// Convenience function that formats JSON to a provided StringBuf
void format_json_to_strbuf(StringBuf* sb, Item root_item) {
    ItemReader reader(root_item);
    format_item_reader_with_indent(sb, reader, 0);
}
