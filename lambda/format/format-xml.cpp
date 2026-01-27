#include "format.h"
#include "../windows_compat.h"  // For Windows compatibility functions like strndup
#include "../../lib/stringbuf.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

static void format_item_reader(XmlContext& ctx, const ItemReader& item, const char* tag_name);

static void format_xml_string(XmlContext& ctx, String* str) {
    if (!str || str->len == 0) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '<':
            stringbuf_append_str(ctx.output(), "&lt;");
            break;
        case '>':
            stringbuf_append_str(ctx.output(), "&gt;");
            break;
        case '&':
            // Check if this is already an entity reference
            if (i + 1 < len && (s[i + 1] == '#' || isalpha(s[i + 1]))) {
                // Look for closing semicolon
                size_t j = i + 1;
                while (j < len && s[j] != ';' && s[j] != ' ' && s[j] != '<' && s[j] != '&') {
                    j++;
                }
                if (j < len && s[j] == ';') {
                    // This looks like an entity, preserve it as-is
                    for (size_t k = i; k <= j; k++) {
                        stringbuf_append_char(ctx.output(), s[k]);
                    }
                    i = j; // Skip past the entity
                    break;
                }
            }
            stringbuf_append_str(ctx.output(), "&amp;");
            break;
        case '"':
            stringbuf_append_str(ctx.output(), "&quot;");
            break;
        case '\'':
            stringbuf_append_str(ctx.output(), "&apos;");
            break;
        default:
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                // Control characters - encode as numeric character reference
                char hex_buf[10];
                snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                stringbuf_append_str(ctx.output(), hex_buf);
            } else {
                stringbuf_append_char(ctx.output(), c);
            }
            break;
        }
    }
}

static void format_array_reader(XmlContext& ctx, const ArrayReader& arr, const char* tag_name) {
    printf("format_array_reader: arr with %lld items\n", (long long)arr.length());

    auto iter = arr.items();
    ItemReader item;
    while (iter.next(&item)) {
        format_item_reader(ctx, item, tag_name ? tag_name : "item");
    }
}

static void format_map_attributes(XmlContext& ctx, const MapReader& map_reader) {
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        // Only output simple types as attributes
        if (value.isString() || value.isInt() || value.isFloat() || value.isBool()) {
            stringbuf_append_char(ctx.output(), ' ');
            stringbuf_append_format(ctx.output(), "%s=\"", key);

            if (value.isString()) {
                String* str = value.asString();
                if (str) {
                    format_xml_string(ctx, str);
                }
            } else if (value.isInt()) {
                int64_t int_val = value.asInt();
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%" PRId64, int_val);
                stringbuf_append_str(ctx.output(), num_buf);
            } else if (value.isFloat()) {
                double float_val = value.asFloat();
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                stringbuf_append_str(ctx.output(), num_buf);
            } else if (value.isBool()) {
                bool bool_val = value.asBool();
                stringbuf_append_str(ctx.output(), bool_val ? "true" : "false");
            }

            stringbuf_append_char(ctx.output(), '"');
        }
    }
}

static void format_map_elements(XmlContext& ctx, const MapReader& map_reader) {
    printf("format_map_elements: formatting map elements\n");

    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;

    while (iter.next(&key, &value)) {
        // Only output complex types as child elements (not simple attributes)
        if (!value.isString() && !value.isInt() && !value.isFloat() && !value.isBool()) {
            if (value.isNull()) {
                // Create empty element for null
                stringbuf_append_format(ctx.output(), "<%s/>", key);
            } else {
                // Format complex types as child elements
                format_item_reader(ctx, value, key);
            }
        }
    }
}

static void format_map_reader(XmlContext& ctx, const MapReader& map_reader, const char* tag_name) {
    printf("format_map_reader: formatting map\n");

    if (!tag_name) tag_name = "object";

    stringbuf_append_char(ctx.output(), '<');
    stringbuf_append_str(ctx.output(), tag_name);

    // Add simple types as attributes for better XML structure
    format_map_attributes(ctx, map_reader);

    // Check if there are complex child elements
    bool has_children = false;
    auto check_iter = map_reader.entries();
    const char* check_key;
    ItemReader check_value;
    while (check_iter.next(&check_key, &check_value)) {
        if (!check_value.isString() && !check_value.isInt() && !check_value.isFloat() && !check_value.isBool() && !check_value.isNull()) {
            has_children = true;
            break;
        }
    }

    if (has_children) {
        stringbuf_append_char(ctx.output(), '>');

        // Add complex types as child elements
        format_map_elements(ctx, map_reader);

        stringbuf_append_str(ctx.output(), "</");
        stringbuf_append_str(ctx.output(), tag_name);
        stringbuf_append_char(ctx.output(), '>');
    } else {
        // Self-closing tag if no children
        stringbuf_append_str(ctx.output(), "/>");
    }
}

static void format_item_reader(XmlContext& ctx, const ItemReader& item, const char* tag_name) {
    if (!tag_name) tag_name = "value";

    // Check if item is null
    if (item.isNull()) {
        stringbuf_append_format(ctx.output(), "<%s/>", tag_name);
        return;
    }

    printf("format_item_reader: formatting item, tag_name '%s'\n", tag_name);

    if (item.isBool()) {
        bool val = item.asBool();
        stringbuf_append_format(ctx.output(), "<%s>%s</%s>", tag_name, val ? "true" : "false", tag_name);
    }
    else if (item.isInt()) {
        stringbuf_append_format(ctx.output(), "<%s>", tag_name);
        int64_t int_val = item.asInt();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, int_val);
        stringbuf_append_str(ctx.output(), num_buf);
        stringbuf_append_format(ctx.output(), "</%s>", tag_name);
    }
    else if (item.isFloat()) {
        stringbuf_append_format(ctx.output(), "<%s>", tag_name);
        double float_val = item.asFloat();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
        stringbuf_append_str(ctx.output(), num_buf);
        stringbuf_append_format(ctx.output(), "</%s>", tag_name);
    }
    else if (item.isString()) {
        String* str = item.asString();
        stringbuf_append_format(ctx.output(), "<%s>", tag_name);
        if (str) {
            format_xml_string(ctx, str);
        }
        stringbuf_append_format(ctx.output(), "</%s>", tag_name);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        stringbuf_append_format(ctx.output(), "<%s>", tag_name);
        format_array_reader(ctx, arr, "item");
        stringbuf_append_format(ctx.output(), "</%s>", tag_name);
    }
    else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader(ctx, mp, tag_name);
    }
    else if (item.isElement()) {
        printf("format_item_reader: handling element\n");
        ElementReader elem = item.asElement();

        const char* elem_name = elem.tagName();
        if (!elem_name || elem_name[0] == '\0') {
            printf("format_item_reader: element has no name\n");
            stringbuf_append_format(ctx.output(), "<%s/>", tag_name);
            return;
        }

        printf("format_item_reader: element name='%s', attr_count=%lld, child_count=%lld\n",
               elem_name, (long long)elem.attrCount(), (long long)elem.childCount());

        // Special handling for XML declaration
        if (strcmp(elem_name, "?xml") == 0) {
            stringbuf_append_str(ctx.output(), "<?xml");

            // Handle XML declaration content as attributes
            auto child_iter = elem.children();
            ItemReader child;
            while (child_iter.next(&child)) {
                if (child.isString()) {
                    String* str = child.asString();
                    if (str) {
                        stringbuf_append_char(ctx.output(), ' ');
                        stringbuf_append_str(ctx.output(), str->chars);
                    }
                }
            }

            stringbuf_append_str(ctx.output(), "?>");
            return; // Early return for XML declaration
        }

        stringbuf_append_char(ctx.output(), '<');
        stringbuf_append_str(ctx.output(), elem_name);

        // Handle attributes
        if (elem.attrCount() > 0) {
            printf("format_item_reader: element has attributes, formatting\n");
            // Access attributes directly from ElementReader
            const TypeMap* map_type = (const TypeMap*)elem.element()->type;
            const ShapeEntry* field = map_type->shape;

            while (field) {
                const char* key = field->name->str;
                ItemReader value = elem.get_attr(key);

                stringbuf_append_char(ctx.output(), ' ');
                stringbuf_append_str(ctx.output(), key);
                stringbuf_append_str(ctx.output(), "=\"");

                if (value.isString()) {
                    String* str = value.asString();
                    if (str) {
                        format_xml_string(ctx, str);
                    }
                } else if (value.isInt()) {
                    stringbuf_append_format(ctx.output(), "%lld", (long long)value.asInt());
                } else if (value.isFloat()) {
                    stringbuf_append_format(ctx.output(), "%g", value.asFloat());
                } else if (value.isBool()) {
                    stringbuf_append_str(ctx.output(), value.asBool() ? "true" : "false");
                }

                stringbuf_append_char(ctx.output(), '"');

                field = field->next;
            }
        }

        stringbuf_append_char(ctx.output(), '>');

        // Handle element content (text/child elements)
        if (elem.childCount() > 0) {
            printf("format_item_reader: element has %lld children\n", (long long)elem.childCount());

            auto child_iter = elem.children();
            ItemReader child;
            while (child_iter.next(&child)) {
                if (child.isString()) {
                    String* str = child.asString();
                    if (str) {
                        format_xml_string(ctx, str);
                    }
                } else if (child.isSymbol()) {
                    // Symbol items (named entities) - output as &name;
                    String* sym = child.asSymbol();
                    if (sym && sym->chars) {
                        stringbuf_append_char(ctx.output(), '&');
                        stringbuf_append_str(ctx.output(), sym->chars);
                        stringbuf_append_char(ctx.output(), ';');
                    }
                } else {
                    // For child elements, format them recursively
                    format_item_reader(ctx, child, NULL);
                }
            }
        }

        stringbuf_append_str(ctx.output(), "</");
        stringbuf_append_str(ctx.output(), elem_name);
        stringbuf_append_char(ctx.output(), '>');
    }
    else {
        // Unknown type or null
        printf("format_item_reader: unknown type, outputting empty element\n");
        stringbuf_append_format(ctx.output(), "<%s/>", tag_name);
    }
}

String* format_xml(Pool* pool, Item root_item) {
    Pool* ctx_pool = pool_create();
    StringBuf* sb = stringbuf_new(pool);
    XmlContext ctx(ctx_pool, sb);

    ItemReader reader(root_item.to_const());

    // Check if we have a document structure with multiple children (XML declaration + root element)
    if (reader.isElement()) {
        ElementReader root_elem = reader.asElement();
        const char* root_tag = root_elem.tagName();

        printf("format_xml: root element name='%s', children=%lld\n",
               root_tag, (long long)root_elem.childCount());

        // Check if this is a "document" element containing multiple children
        if (strcmp(root_tag, "document") == 0 && root_elem.childCount() > 0) {
            printf("format_xml: document element with %lld children\n", (long long)root_elem.childCount());

            // Format all children in order (XML declaration, then actual elements)
            auto child_iter = root_elem.children();
            ItemReader child;

            while (child_iter.next(&child)) {
                if (child.isElement()) {
                    ElementReader child_elem = child.asElement();
                    const char* child_tag = child_elem.tagName();

                    // Check if this is XML declaration
                    if (strcmp(child_tag, "?xml") == 0) {
                        printf("format_xml: formatting XML declaration\n");
                        format_item_reader(ctx, child, NULL);
                        stringbuf_append_char(ctx.output(), '\n');
                    } else {
                        // Format actual XML element with its proper name
                        printf("format_xml: formatting element '%s'\n", child_tag);
                        format_item_reader(ctx, child, child_tag);
                    }
                }
            }

            pool_destroy(ctx_pool);
            return stringbuf_to_string(sb);
        }
    }

    // Fallback: format as single element
    const char* tag_name = NULL;
    if (reader.isElement()) {
        ElementReader elem = reader.asElement();
        tag_name = elem.tagName();
        printf("format_xml: using element name '%s'\n", tag_name);
    }

    if (!tag_name) {
        tag_name = "root";
        printf("format_xml: using default name 'root'\n");
    }

    format_item_reader(ctx, reader, tag_name);

    pool_destroy(ctx_pool);
    return stringbuf_to_string(sb);
}

// Convenience function that formats XML to a provided StringBuf
void format_xml_to_stringbuf(StringBuf* sb, Item root_item) {
    Pool* pool = pool_create();
    XmlContext ctx(pool, sb);
    ItemReader reader(root_item.to_const());
    format_item_reader(ctx, reader, "root");
    pool_destroy(pool);
}
