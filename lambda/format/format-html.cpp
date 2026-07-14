#include "format.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "html-defs.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include "../../lib/mem_factory.h"
#include "../../radiant/view.hpp"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

// MarkReader-based forward declarations using HtmlContext
static void format_item_reader(HtmlContext& ctx, const ItemReader& item, int depth, bool raw_text_mode);
static void format_element_reader(HtmlContext& ctx, const ElementReader& elem, int depth, bool raw_text_mode);

// Use shared html-defs.h for void/raw-text/boolean lookups
static inline bool is_void_element(const char* tag_name, size_t tag_len) {
    return html_is_void_element(tag_name, tag_len);
}
static inline bool is_boolean_attribute(const char* attr_name, size_t attr_len) {
    return html_is_boolean_attribute(attr_name, attr_len);
}
static inline bool is_raw_text_element(const char* tag_name, size_t tag_len) {
    return html_is_raw_text_element(tag_name, tag_len);
}

static void format_html_attr_value(HtmlContext& ctx, const ItemReader& value) {
    if (value.isString()) {
        String* str = value.asString();
        if (str) format_html_string_safe(ctx.output(), str, true);
    } else if (value.isInt()) {
        format_number(ctx.output(), value.item());
    } else if (value.isFloat()) {
        format_number(ctx.output(), value.item());
    } else if (value.isBool()) {
        ctx.emit("%b", value.asBool());
    } else if (value.isSymbol()) {
        Symbol* sym = value.asSymbol();
        if (sym) {
            stringbuf_append_format(ctx.output(), "%.*s", (int)sym->len, sym->chars);
        }
    }
}

static bool html_name_equals(const char* name, size_t name_len, const char* literal) {
    size_t literal_len = strlen(literal);
    return name && name_len == literal_len && memcmp(name, literal, literal_len) == 0;
}

static bool html_name_starts_with(const char* name, size_t name_len, const char* literal) {
    size_t literal_len = strlen(literal);
    return name && name_len >= literal_len && memcmp(name, literal, literal_len) == 0;
}

static void html_write_reader_string_raw(HtmlContext& ctx, const ItemReader& item) {
    if (!item.isString()) return;
    String* str = item.asString();
    if (str) stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
}

static void html_write_first_child_raw(HtmlContext& ctx, const ElementReader& elem) {
    html_write_reader_string_raw(ctx, elem.childAt(0));
}

static void html_write_children_direct(HtmlContext& ctx, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child_item;
    while (it.next(&child_item)) {
        format_item_reader(ctx, child_item, depth, false);
    }
}

typedef enum HtmlSpecialKind {
    HTML_SPECIAL_DOCUMENT,
    HTML_SPECIAL_DOCTYPE_NODE,
    HTML_SPECIAL_COMMENT_NODE,
    HTML_SPECIAL_LEGACY_COMMENT,
    HTML_SPECIAL_XML_DECL
} HtmlSpecialKind;

typedef struct HtmlSpecialTag {
    const char* name;
    HtmlSpecialKind kind;
} HtmlSpecialTag;

static const HtmlSpecialTag HTML_SPECIAL_TAGS[] = {
    {"#document", HTML_SPECIAL_DOCUMENT},
    {"#doctype", HTML_SPECIAL_DOCTYPE_NODE},
    {"#comment", HTML_SPECIAL_COMMENT_NODE},
    {"!--", HTML_SPECIAL_LEGACY_COMMENT},
    {"?xml", HTML_SPECIAL_XML_DECL}
};

static bool format_html_special_element(HtmlContext& ctx, const ElementReader& elem,
                                        const char* tag_name, size_t tag_len, int depth) {
    for (size_t i = 0; i < sizeof(HTML_SPECIAL_TAGS) / sizeof(HTML_SPECIAL_TAGS[0]); i++) {
        const HtmlSpecialTag& special = HTML_SPECIAL_TAGS[i];
        if (!html_name_equals(tag_name, tag_len, special.name)) continue;

        switch (special.kind) {
        case HTML_SPECIAL_DOCUMENT:
            html_write_children_direct(ctx, elem, depth);
            return true;
        case HTML_SPECIAL_DOCTYPE_NODE: {
            ctx.write_text("<!DOCTYPE ");
            ItemReader name_attr = elem.get_attr("name");
            if (name_attr.isString()) {
                html_write_reader_string_raw(ctx, name_attr);
            } else {
                ctx.write_text("html");
            }
            ctx.write_char('>');
            return true;
        }
        case HTML_SPECIAL_COMMENT_NODE: {
            ctx.write_text("<!--");
            html_write_reader_string_raw(ctx, elem.get_attr("data"));
            ctx.write_text("-->");
            return true;
        }
        case HTML_SPECIAL_LEGACY_COMMENT:
            ctx.write_text("<!--");
            html_write_first_child_raw(ctx, elem);
            ctx.write_text("-->");
            return true;
        case HTML_SPECIAL_XML_DECL:
            html_write_first_child_raw(ctx, elem);
            return true;
        }
    }

    if (html_name_starts_with(tag_name, tag_len, "!DOCTYPE") ||
        html_name_starts_with(tag_name, tag_len, "!doctype")) {
        stringbuf_append_format(ctx.output(), "<!%.*s", (int)(tag_len - 1), tag_name + 1);
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            stringbuf_append_char(ctx.output(), ' ');
            html_write_reader_string_raw(ctx, first_child);
        }
        ctx.write_char('>');
        return true;
    }

    if (html_name_equals(tag_name, tag_len, "html")) {
        ItemReader type_attr = elem.get_attr("type");
        if (type_attr.isString()) {
            String* type_str = type_attr.asString();
            if (type_str && type_str->len == 3 && memcmp(type_str->chars, "raw", 3) == 0) {
                html_write_first_child_raw(ctx, elem);
                stringbuf_append_char(ctx.output(), '\n');
                return true;
            }
        }
    }

    return false;
}

String* format_html(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Create HTML context
    ScopedFormatPool ctx_pool("format.html");
    HtmlContext ctx(ctx_pool.get(), sb);

    // check if root is already an HTML element or #document, if so, format as-is
    if (root_item.item) {
        TypeId type = get_type_id(root_item);

        // handle root-level List (Phase 3: may contain DOCTYPE, comments, and main element)
        if (type == LMD_TYPE_ARRAY) {
            List* list = root_item.array;
            if (list && list->length > 0) {
                // format each item in the root list using reader API
                for (long i = 0; i < list->length; i++) {
                    Item list_item = list->items[i];
                    ItemReader item_reader(list_item.to_const());
                    format_item_reader(ctx, item_reader, 0, false);
                    // add newline after DOCTYPE or comments if not the last item
                    if (i < list->length - 1) {
                        stringbuf_append_char(ctx.output(), '\n');
                    }
                }
                return stringbuf_to_string(sb);
            }
        }

        // check if it's an array (most likely case for parsed HTML)
        if (type == LMD_TYPE_ARRAY) {
            Array* arr = root_item.array;
            if (arr && arr->length > 0) {
                // check if the first element is an HTML element
                Item first_item = arr->items[0];
                TypeId first_type = get_type_id(first_item);

                if (first_type == LMD_TYPE_ELEMENT) {
                    Element* element = first_item.element;
                    if (element && element->type) {
                        TypeElmt* elmt_type = (TypeElmt*)element->type;
                        // check if this is an HTML or #document element
                        if ((elmt_type->name.length == 4 &&
                             strncmp(elmt_type->name.str, "html", 4) == 0) ||
                            (elmt_type->name.length == 9 &&
                             strncmp(elmt_type->name.str, "#document", 9) == 0)) {
                            // format the element directly without wrapping
                            ItemReader first_reader(first_item.to_const());
                            format_item_reader(ctx, first_reader, 0, false);
                            return stringbuf_to_string(sb);
                        }
                    }
                }
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* element = root_item.element;
            if (element && element->type) {
                TypeElmt* elmt_type = (TypeElmt*)element->type;
                // check if this is an HTML or #document element
                if ((elmt_type->name.length == 4 &&
                     strncmp(elmt_type->name.str, "html", 4) == 0) ||
                    (elmt_type->name.length == 9 &&
                     strncmp(elmt_type->name.str, "#document", 9) == 0)) {
                    // format the element directly without wrapping
                    ItemReader root_reader(root_item.to_const());
                    format_item_reader(ctx, root_reader, 0, false);
                    return stringbuf_to_string(sb);
                }
            }
        }
    }

    // add minimal HTML document structure for non-HTML root elements
    stringbuf_append_str(ctx.output(), "<!DOCTYPE html>\n<html>\n<head>"
                                       "<meta charset=\"UTF-8\">"
                                       "<title>Data</title>"
                                       "</head>\n<body>\n");

    ItemReader root_reader(root_item.to_const());
    format_item_reader(ctx, root_reader, 0, false);

    stringbuf_append_str(ctx.output(), "\n</body>\n</html>");

    return stringbuf_to_string(sb);
}

// Convenience function that formats HTML to a provided StringBuf
void format_html_to_strbuf(StringBuf* sb, Item root_item) {
    ScopedFormatPool pool("format.html");
    HtmlContext ctx(pool.get(), sb);
    ItemReader reader(root_item.to_const());
    format_item_reader(ctx, reader, 0, false);
}

// ===== MarkReader-based implementations =====

// format element using reader API
static void format_element_reader(HtmlContext& ctx, const ElementReader& elem, int depth, bool raw_text_mode) {
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        ctx.write_text("<element/>");
        return;
    }

    size_t tag_len = strlen(tag_name);

    if (format_html_special_element(ctx, elem, tag_name, tag_len, depth)) return;

    // format as proper HTML element
    ctx.emit("<%N", tag_name);

    // add attributes
    if (elem.element() && elem.element()->data) {
        auto attrs = elem.attrs();
        const char* field_name;
        ItemReader value;
        while (attrs.next(&field_name, &value)) {
            if (!field_name) continue;
            int field_name_len = (int)strlen(field_name);

            // skip the "_" field (children)
            if (field_name_len == 1 && field_name[0] == '_') {
                continue;
            }

            bool is_bool_attr = is_boolean_attribute(field_name, field_name_len);

            if (value.isNull()) {
                if (is_bool_attr) {
                    stringbuf_append_format(ctx.output(), " %.*s", field_name_len, field_name);
                } else {
                    stringbuf_append_format(ctx.output(), " %.*s=\"\"", field_name_len, field_name);
                }
            } else if (value.isBool() && is_bool_attr) {
                if (value.asBool()) {
                    stringbuf_append_format(ctx.output(), " %.*s", field_name_len, field_name);
                }
            } else if (value.isString() || value.isInt() || value.isFloat() ||
                       value.isBool() || value.isSymbol()) {
                stringbuf_append_format(ctx.output(), " %.*s=\"", field_name_len, field_name);
                format_html_attr_value(ctx, value);
                ctx.write_char('"');
            }
        }
    }

    // check if this is a void element (self-closing)
    bool is_void = is_void_element(tag_name, tag_len);

    if (is_void) {
        // void elements don't have closing tags in HTML5
        ctx.write_char('>');
    } else {
        ctx.write_char('>');

        // check if this is a raw text element (script, style, etc.)
        bool is_raw = is_raw_text_element(tag_name, tag_len);

        // add children if available
        auto it = elem.children();
        ItemReader child_item;
        while (it.next(&child_item)) {
            format_item_reader(ctx, child_item, depth + 1, is_raw);
        }

        // close tag (only for non-void elements)
        ctx.emit("</%N>", tag_name);
    }
}

// format item using reader API
static void format_item_reader(HtmlContext& ctx, const ItemReader& item, int depth, bool raw_text_mode) {
    // safety check for null
    if (!ctx.output()) return;

    class HtmlItemHandlers : public FormatItemHandlersDefault {
    public:
        HtmlItemHandlers(HtmlContext& ctx, int depth, bool raw_text_mode)
            : ctx_(ctx), depth_(depth), raw_text_mode_(raw_text_mode) {}

        void max_depth(const ItemReader& item) override { (void)item; ctx_.write_text("<!-- max_depth -->"); }
        void null_value(const ItemReader& item) override { (void)item; ctx_.write_text("null"); }
        void bool_value(const ItemReader& item) override { ctx_.emit("%b", item.asBool()); }
        void number_value(const ItemReader& item) override { format_number(ctx_.output(), item.item()); }
        void string_value(const ItemReader& item, String* str) override {
            (void)item;
            if (!str) return;
            if (raw_text_mode_) {
                // in raw text mode (script, style, etc.), output string as-is without escaping
                stringbuf_append_format(ctx_.output(), "%.*s", (int)str->len, str->chars);
            } else {
                // in normal mode, escape HTML entities
                format_html_string_safe(ctx_.output(), str, false);
            }
        }
        void symbol_value(const ItemReader& item, Symbol* sym) override {
            (void)item;
            if (!sym) return;
            // resolve emoji before HTML entities to preserve existing shortcode priority.
            SymbolResolution res = resolve_symbol(sym->chars, sym->len);
            if (res.type == SYMBOL_EMOJI && res.utf8) {
                stringbuf_append_str(ctx_.output(), res.utf8);
            } else if (res.type == SYMBOL_HTML_ENTITY) {
                stringbuf_append_format(ctx_.output(), "&%.*s;", (int)sym->len, sym->chars);
            } else {
                stringbuf_append_format(ctx_.output(), ":%.*s:", (int)sym->len, sym->chars);
            }
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            if (!item.isArray()) {
                unknown_value(item);
                return;
            }
            if (raw_text_mode_) {
                format_json_to_strbuf(ctx_.output(), item.item());
                return;
            }

            if (!arr.isEmpty()) {
                ctx_.write_text("<ul>");
                auto it = arr.items();
                ItemReader array_item;
                while (it.next(&array_item)) {
                    ctx_.write_text("<li>");
                    format_item_reader(ctx_, array_item, depth_ + 1, raw_text_mode_);
                    ctx_.write_text("</li>");
                }
                ctx_.write_text("</ul>");
            } else {
                ctx_.write_text("[]");
            }
        }
        void map_value(const ItemReader& item, MapReader map) override {
            (void)map;
            if (raw_text_mode_) {
                format_json_to_strbuf(ctx_.output(), item.item());
                return;
            }
            ctx_.write_text("<div>{object}</div>");
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            format_element_reader(ctx_, elem, depth_, raw_text_mode_);
        }
        void unknown_value(const ItemReader& item) override { (void)item; ctx_.write_text("unknown"); }

    private:
        HtmlContext& ctx_;
        int depth_;
        bool raw_text_mode_;
    };

    HtmlItemHandlers handlers(ctx, depth, raw_text_mode);
    ctx.dispatch_item(item, handlers);
}
