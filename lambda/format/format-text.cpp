#include "format.h"
#include "../lambda-decimal.hpp"
#include <string.h>
#include <ctype.h>
#include "../../lib/stringbuf.h"
#include "../../lib/mem_factory.h"
#include "../mark_reader.hpp"
#include "format-utils.hpp"

// Forward declarations (MarkReader-based with context)
static void format_item_text_reader(TextContext& ctx, const ItemReader& item);
static void format_element_text_reader(TextContext& ctx, const ElementReader& elem);
static void format_array_text_reader(TextContext& ctx, const ArrayReader& arr);
static void format_map_text_reader(TextContext& ctx, const MapReader& mp);
static void format_scalar_value_reader(TextContext& ctx, const ItemReader& item);

// Helper function to format scalar values as raw text (no quotes) - MarkReader version
static void format_scalar_value_reader(TextContext& ctx, const ItemReader& item) {
    if (item.isBool()) {
        bool val = item.asBool();
        ctx.write_text(val ? "true" : "false");
    }
    else if (item.isInt()) {
        format_number(ctx.output(), item.item());
    }
    else if (item.isFloat()) {
        format_number(ctx.output(), item.item());
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str && str->len > 0) {
            // Output string content without quotes
            ctx.write_text(str);
        }
    }
    else {
        // For other types like DateTime, use raw Item access
        Item raw_item = item.item();
        TypeId type = get_type_id(raw_item);

        if (type == LMD_TYPE_DTIME) {
            DateTime* dt_ptr = (DateTime*)raw_item.datetime_ptr;
            if (dt_ptr) {
                format_write_datetime_iso8601(ctx.output(), dt_ptr);
            }
        } else {
            // For non-scalar types, recursively process them
            format_item_text_reader(ctx, item);
        }
    }
}

// MarkReader-based version: format an array by extracting scalar values
static void format_array_text_reader(TextContext& ctx, const ArrayReader& arr) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;

    auto items_iter = arr.items();
    ItemReader item;
    bool first = true;

    while (items_iter.next(&item)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;

        format_item_text_reader(ctx, item);
    }
}

// MarkReader-based version: format a map by extracting scalar values
static void format_map_text_reader(TextContext& ctx, const MapReader& mp) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;

    auto entries = mp.entries();
    const char* key;
    ItemReader value;
    bool first = true;

    while (entries.next(&key, &value)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;

        format_item_text_reader(ctx, value);
    }
}

// MarkReader-based version: format element by extracting scalar values
static void format_element_text_reader(TextContext& ctx, const ElementReader& elem) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;

    // for text extraction, we focus on element children/content
    // attributes are typically metadata, not content text

    // process element children
    auto children_iter = elem.children();
    ItemReader child;
    bool first = true;

    while (children_iter.next(&child)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;

        format_item_text_reader(ctx, child);
    }
}

// MarkReader-based version: main recursive function to extract scalar values
static void format_item_text_reader(TextContext& ctx, const ItemReader& item) {
    class TextItemHandlers : public FormatItemHandlersDefault {
    public:
        explicit TextItemHandlers(TextContext& ctx) : ctx_(ctx) {}

        void max_depth(const ItemReader& item) override { (void)item; }
        void null_value(const ItemReader& item) override { (void)item; }
        void bool_value(const ItemReader& item) override { format_scalar_value_reader(ctx_, item); }
        void number_value(const ItemReader& item) override { format_scalar_value_reader(ctx_, item); }
        void string_value(const ItemReader& item, String* str) override {
            (void)str;
            format_scalar_value_reader(ctx_, item);
        }
        void binary_value(const ItemReader& item, String* bin) override {
            (void)item;
            format_binary_base64_string(ctx_.output(), bin);
        }
        void array_value(const ItemReader& item, ArrayReader arr) override {
            (void)item;
            format_array_text_reader(ctx_, arr);
        }
        void map_value(const ItemReader& item, MapReader mp) override {
            (void)item;
            format_map_text_reader(ctx_, mp);
        }
        void element_value(const ItemReader& item, ElementReader elem) override {
            (void)item;
            format_element_text_reader(ctx_, elem);
        }
        void datetime_value(const ItemReader& item, DateTime* dt) override {
            (void)dt;
            format_scalar_value_reader(ctx_, item);
        }
        void unknown_value(const ItemReader& item) override {
            format_scalar_value_reader(ctx_, item);
        }

    private:
        TextContext& ctx_;
    };

    TextItemHandlers handlers(ctx);
    ctx.dispatch_item(item, handlers);
}

// Public interface function that formats a Lambda Item as plain text
void format_text(StringBuf* sb, Item root_item) {
    if (!sb) return;

    // Create a temporary pool for context operations
    ScopedFormatPool temp_pool("format.text");
    TextContext ctx(temp_pool.get(), sb);

    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    format_item_text_reader(ctx, root);

}

// String variant that returns a String* allocated from the pool
String* format_text_string(Pool* pool, Item root_item) {
    if (!pool) return NULL;

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Create context using the passed pool (not a temporary one)
    TextContext ctx(pool, sb);

    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    format_item_text_reader(ctx, root);

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}
