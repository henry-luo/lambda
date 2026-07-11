#include "format.h"
#include "../windows_compat.h"  // For Windows compatibility functions like strndup
#include "../../lib/stringbuf.h"
#include "../../lib/mem_factory.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "html-defs.h"  // html_is_raw_text_element

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

static void format_item_reader(XmlContext& ctx, const ItemReader& item, const char* tag_name);

static void format_xml_string(XmlContext& ctx, String* str, bool is_attribute = false) {
    format_markup_string_safe(ctx.output(), str, is_attribute, true, true, "xml");
}

static void xml_emit_attr_value(XmlContext& ctx, const ItemReader& value) {
    if (value.isString()) {
        String* str = value.asString();
        if (str) {
            format_xml_string(ctx, str, true);
        }
    } else if (value.isInt() || value.isFloat()) {
        format_number(ctx.output(), value.item());
    } else if (value.isBool()) {
        ctx.emit("%b", value.asBool());
    }
}

static void format_array_reader(XmlContext& ctx, const ArrayReader& arr, const char* tag_name) {
    log_debug("format_array_reader: arr with %lld items", (long long)arr.length());

    auto iter = arr.items();
    ItemReader item;
    while (iter.next(&item)) {
        format_item_reader(ctx, item, tag_name ? tag_name : "item");
    }
}

static void format_map_reader(XmlContext& ctx, const MapReader& map_reader, const char* tag_name) {
    log_debug("format_map_reader: formatting map");

    if (!tag_name) tag_name = "object";

    ctx.emit("<%N", tag_name);

    StringBuf* child_buf = stringbuf_new(ctx.pool());
    XmlContext child_ctx(ctx.pool(), child_buf);
    bool has_non_null_complex_child = false;

    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;
    while (iter.next(&key, &value)) {
        if (value.isString() || value.isInt() || value.isFloat() || value.isBool()) {
            ctx.emit(" %N=\"", key);
            xml_emit_attr_value(ctx, value);
            ctx.write_char('"');
        } else if (value.isNull()) {
            // Preserve legacy null-field behavior: nulls are emitted only when
            // the map has at least one non-null complex child.
            child_ctx.emit("<%N/>", key);
        } else {
            has_non_null_complex_child = true;
            format_item_reader(child_ctx, value, key);
        }
    }

    if (has_non_null_complex_child) {
        ctx.write_char('>');
        stringbuf_append_str_n(ctx.output(), child_buf->str->chars, child_buf->length);
        ctx.emit("</%N>", tag_name);
    } else {
        // Self-closing tag if no children
        ctx.write_text("/>");
    }
}

static void format_item_reader(XmlContext& ctx, const ItemReader& item, const char* tag_name) {
    if (!tag_name) tag_name = "value";

    class XmlItemHandlers : public FormatItemHandlersDefault {
    public:
        XmlItemHandlers(XmlContext& ctx, const char* tag_name)
            : ctx_(ctx), tag_name_(tag_name) {}

        void max_depth(const ItemReader& item) override {
            (void)item;
            ctx_.emit("<%N/><!-- max_depth -->", tag_name_);
        }
        void null_value(const ItemReader& item) override {
            (void)item;
            ctx_.emit("<%N/>", tag_name_);
        }
        void bool_value(const ItemReader& item) override {
            ctx_.emit("<%N>%b</%N>", tag_name_, item.asBool(), tag_name_);
        }
        void number_value(const ItemReader& item) override {
            ctx_.emit("<%N>", tag_name_);
            format_number(ctx_.output(), item.item());
            ctx_.emit("</%N>", tag_name_);
        }
        void string_value(const ItemReader& item, String* str) override {
            (void)item;
            ctx_.emit("<%N>", tag_name_);
            if (str) format_xml_string(ctx_, str);
            ctx_.emit("</%N>", tag_name_);
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            if (!item.isArray()) {
                unknown_value(item);
                return;
            }
            ctx_.emit("<%N>", tag_name_);
            format_array_reader(ctx_, arr, "item");
            ctx_.emit("</%N>", tag_name_);
        }
        void map_value(const ItemReader& item, MapReader mp) override {
            (void)item;
            format_map_reader(ctx_, mp, tag_name_);
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            log_debug("format_item_reader: handling element");
            format_element(elem);
        }
        void unknown_value(const ItemReader& item) override {
            (void)item;
            log_debug("format_item_reader: unknown type, outputting empty element");
            ctx_.emit("<%N/>", tag_name_);
        }

    private:
        void format_element(const ElementReader& elem) {
            const char* elem_name = elem.tagName();
            if (!elem_name || elem_name[0] == '\0') {
                log_debug("format_item_reader: element has no name");
                ctx_.emit("<%N/>", tag_name_);
                return;
            }

            log_debug("format_item_reader: element name='%s', attr_count=%lld, child_count=%lld",
                elem_name, (long long)elem.attrCount(), (long long)elem.childCount());

            // Special handling for XML declaration
            if (strcmp(elem_name, "?xml") == 0) {
                ctx_.write_text("<?xml");

                // Handle XML declaration content as attributes
                auto child_iter = elem.children();
                ItemReader child;
                while (child_iter.next(&child)) {
                    if (child.isString()) {
                        String* str = child.asString();
                        if (str) {
                            ctx_.emit(" %N", str->chars);
                        }
                    }
                }

                ctx_.write_text("?>");
                return; // Early return for XML declaration
            }

            ctx_.emit("<%N", elem_name);

            // Handle attributes
            if (elem.attrCount() > 0) {
                log_debug("format_item_reader: element has attributes, formatting");
                auto attrs = elem.attrs();
                const char* key;
                ItemReader value;
                while (attrs.next(&key, &value)) {
                    if (!key) continue;

                    ctx_.emit(" %N=\"", key);
                    xml_emit_attr_value(ctx_, value);
                    ctx_.write_char('"');
                }
            }

            ctx_.write_char('>');

            // Raw-text elements (<style>, <script>, etc.) carry plain CSS/JS;
            // entity-escaping their content (especially " → &quot;, ' → &apos;)
            // breaks downstream parsers — e.g. an injected
            // `@font-face { src: url("data:font/ttf;base64,...") }` block
            // becomes invalid CSS once quotes are entity-escaped.
            bool is_raw_text = html_is_raw_text_element(elem_name, strlen(elem_name));

            // Handle element content (text/child elements)
            if (elem.childCount() > 0) {
                log_debug("format_item_reader: element has %lld children", (long long)elem.childCount());

                auto child_iter = elem.children();
                ItemReader child;
                while (child_iter.next(&child)) {
                    if (child.isString()) {
                        String* str = child.asString();
                        if (str) {
                            if (is_raw_text) {
                                stringbuf_append_format(ctx_.output(), "%.*s",
                                    (int)str->len, str->chars);
                            } else {
                                format_xml_string(ctx_, str);
                            }
                        }
                    } else if (child.isSymbol()) {
                        // Symbol items (named entities) - output as &name;
                        Symbol* sym = child.asSymbol();
                        if (sym) {
                            ctx_.emit("&%N;", sym->chars);
                        }
                    } else if (child.isArray()) {
                        // flatten array children: render each item directly
                        // without wrapping in a <value> tag (needed for SVG groups etc.)
                        ArrayReader arr = child.asArray();
                        auto arr_iter = arr.items();
                        ItemReader arr_item;
                        while (arr_iter.next(&arr_item)) {
                            format_item_reader(ctx_, arr_item, NULL);
                        }
                    } else {
                        // For child elements, format them recursively
                        format_item_reader(ctx_, child, NULL);
                    }
                }
            }

            ctx_.emit("</%N>", elem_name);
        }

        XmlContext& ctx_;
        const char* tag_name_;
    };

    if (!item.isNull()) {
        log_debug("format_item_reader: formatting item, tag_name '%s'", tag_name);
    }
    XmlItemHandlers handlers(ctx, tag_name);
    ctx.dispatch_item(item, handlers);
}

String* format_xml(Pool* pool, Item root_item) {
    ScopedFormatPool ctx_pool("format.xml");
    StringBuf* sb = stringbuf_new(pool);
    XmlContext ctx(ctx_pool.get(), sb);

    ItemReader reader(root_item.to_const());

    // Check if we have a document structure with multiple children (XML declaration + root element)
    if (reader.isElement()) {
        ElementReader root_elem = reader.asElement();
        const char* root_tag = root_elem.tagName();

        log_debug("format_xml: root element name='%s', children=%lld",
               root_tag, (long long)root_elem.childCount());

        // Check if this is a "document" element containing multiple children
        if (strcmp(root_tag, "document") == 0 && root_elem.childCount() > 0) {
            log_debug("format_xml: document element with %lld children", (long long)root_elem.childCount());

            // Format all children in order (XML declaration, then actual elements)
            auto child_iter = root_elem.children();
            ItemReader child;

            while (child_iter.next(&child)) {
                if (child.isElement()) {
                    ElementReader child_elem = child.asElement();
                    const char* child_tag = child_elem.tagName();

                    // Check if this is XML declaration
                    if (strcmp(child_tag, "?xml") == 0) {
                        log_debug("format_xml: formatting XML declaration");
                        format_item_reader(ctx, child, NULL);
                        stringbuf_append_char(ctx.output(), '\n');
                    } else {
                        // Format actual XML element with its proper name
                        log_debug("format_xml: formatting element '%s'", child_tag);
                        format_item_reader(ctx, child, child_tag);
                    }
                }
            }

            return stringbuf_to_string(sb);
        }
    }

    // Fallback: format as single element
    const char* tag_name = NULL;
    if (reader.isElement()) {
        ElementReader elem = reader.asElement();
        tag_name = elem.tagName();
        log_debug("format_xml: using element name '%s'", tag_name);
    }

    if (!tag_name) {
        tag_name = "root";
        log_debug("format_xml: using default name 'root'");
    }

    format_item_reader(ctx, reader, tag_name);

    return stringbuf_to_string(sb);
}

// Convenience function that formats XML to a provided StringBuf
void format_xml_to_stringbuf(StringBuf* sb, Item root_item) {
    ScopedFormatPool pool("format.xml");
    XmlContext ctx(pool.get(), sb);
    ItemReader reader(root_item.to_const());
    format_item_reader(ctx, reader, "root");
}
