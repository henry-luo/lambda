#include "js_intl_locale.h"
#include "js_runtime_internal.hpp"
#include "js_object_meta.h"
#include "lib/utf.h"
#include <limits.h>

namespace {

struct JsListLiterals { const char* prefix; const char* separator; const char* suffix; };
struct JsListLocale { const char* name; uint16_t pattern_set; };

#include "js_intl_list_data.h"

enum ListField { LOCALE, TYPE, STYLE, FIELD_COUNT };
enum ListForm { PAIR, START, MIDDLE, END };
enum class ListContext { none, spanish, hebrew };
const char* const types[] = {"conjunction", "disjunction", "unit"};
const char* const styles[] = {"long", "short", "narrow"};
constexpr size_t style_count = sizeof(styles) / sizeof(*styles);

const JsListLocale* lookup_list_locale(Item record) {
    String* base = it2s(js_elements_get_int(record, JS_INTL_LOCALE_BASE));
    return js_intl_lookup_locale({base->chars, base->len}, js_list_locales);
}

Item read_list_state(Item receiver, int (&fields)[FIELD_COUNT]) {
    JS_ASSIGN_OR_RETURN(state, js_native_private_array_state(receiver, JS_CLASS_INTL_LIST_FORMAT,
        FIELD_COUNT, "Illegal Intl.ListFormat receiver"));
    for (int i = 0; i < FIELD_COUNT; ++i) fields[i] = (int)it2i(js_elements_get_int(state, i));
    return ItemNull;
}

Item read_locale_matcher(Item options) {
    int matcher = 0;
    return js_intl_string_option(options, "localeMatcher", js_intl_locale_matchers,
                                 &matcher, "Invalid ListFormat localeMatcher");
}

Item string_list_from_iterable(Item input) {
    RootFrame roots(4);
    Rooted<Item> iterable(roots, input), strings(roots, js_array_new(0));
    JS_RETURN_IF_ERROR(strings.get());
    if (get_type_id(iterable.get()) == LMD_TYPE_UNDEFINED) return strings.get();
    Rooted<Item> iterator(roots, js_get_iterator(iterable.get())), value(roots, ItemNull);
    JS_RETURN_IF_ERROR(iterator.get());
    for (;;) {
        value.set(js_iterator_step(iterator.get()));
        JS_RETURN_IF_ERROR(value.get());
        if (value.get().item == JS_ITER_DONE_SENTINEL) return strings.get();
        if (get_type_id(value.get()) != LMD_TYPE_STRING) {
            return js_iterator_close_preserve_exception(iterator.get(),
                js_throw_type_error("Intl.ListFormat requires string elements"));
        }
        JS_RETURN_IF_ERROR(js_array_push(strings.get(), value.get()));
    }
}

ListForm list_form(int64_t index, int64_t count) {
    if (count == 2) return PAIR;
    if (!index) return START;
    return index == count - 2 ? END : MIDDLE;
}

ListContext list_context(const char* locale) {
    if (locale[2] && locale[2] != '-') return ListContext::none;
    if (!strncmp(locale, "es", 2)) return ListContext::spanish;
    if (!strncmp(locale, "he", 2) || !strncmp(locale, "iw", 2)) return ListContext::hebrew;
    return ListContext::none;
}

bool in_hebrew_script(uint32_t codepoint) {
    for (const auto& range : js_list_hebrew_script)
        if (codepoint >= range[0] && codepoint <= range[1]) return true;
    return false;
}

const char* list_separator(ListContext context, const JsListLiterals& pattern, String* next) {
    if (!next->len || *pattern.prefix || *pattern.suffix) return pattern.separator;
    // ICU's Spanish and Hebrew contextual variants apply to the pair and end
    // forms when the underlying CLDR literal is the corresponding conjunction.
    if (context == ListContext::spanish) {
        unsigned char first = next->chars[0] | 0x20;
        unsigned char second = next->len > 1 ? next->chars[1] | 0x20 : 0;
        unsigned char third = next->len > 2 ? next->chars[2] | 0x20 : 0;
        if (!strcmp(pattern.separator, " y ") &&
                (first == 'i' || (first == 'h' && second == 'i' && third != 'a' && third != 'e')))
            return " e ";
        if (!strcmp(pattern.separator, " o ") &&
                (first == 'o' || (first == 'h' && second == 'o') || first == '8' ||
                 (first == '1' && second == '1' && (next->len == 2 || next->chars[2] == ' '))))
            return " u ";
    } else if (context == ListContext::hebrew && !strcmp(pattern.separator, " ו")) {
        uint32_t codepoint = 0;
        utf8_decode_replacement(next->chars, next->len, &codepoint);
        if (!in_hebrew_script(codepoint)) return " ו-";
    }
    return pattern.separator;
}

struct ListOutput {
    StrBuf* text;
    Item parts;

    Item element(Item value) const {
        if (!text) return js_intl_append_part(parts, "element", value);
        String* string = it2s(value);
        strbuf_append_str_n(text, string->chars, string->len);
        return ItemNull;
    }

    Item literal(const char* string) const {
        if (!*string) return ItemNull;
        if (!text) return js_intl_append_part(parts, "literal", js_make_string(string));
        strbuf_append_str(text, string);
        return ItemNull;
    }
};

Item emit_list(Item strings, const uint16_t* patterns, ListContext context, const ListOutput& output) {
    int64_t count = js_array_length(strings);
    if (!count) return ItemNull;

    // The pinned patterns place {0} before {1}. Emit their nested prefixes
    // and separators toward the final item, then close suffixes in reverse.
    for (int64_t i = 0; i < count - 1; ++i) {
        ListForm form = list_form(i, count);
        const JsListLiterals& pattern = js_list_literals[patterns[form]];
        JS_RETURN_IF_ERROR(output.literal(pattern.prefix));
        JS_RETURN_IF_ERROR(output.element(js_elements_get_int(strings, i)));
        const char* separator = pattern.separator;
        if (context != ListContext::none && (form == PAIR || form == END))
            separator = list_separator(context, pattern, it2s(js_elements_get_int(strings, i + 1)));
        JS_RETURN_IF_ERROR(output.literal(separator));
    }
    JS_RETURN_IF_ERROR(output.element(js_elements_get_int(strings, count - 1)));
    for (int64_t i = count - 2; i >= 0; --i) {
        const JsListLiterals& pattern = js_list_literals[patterns[list_form(i, count)]];
        JS_RETURN_IF_ERROR(output.literal(pattern.suffix));
    }
    return ItemNull;
}

Item render_list(Item receiver, Item input, bool parts) {
    RootFrame roots(4);
    Rooted<Item> owner(roots, receiver), iterable(roots, input);
    int fields[FIELD_COUNT];
    JS_RETURN_IF_ERROR(read_list_state(owner.get(), fields));
    Rooted<Item> strings(roots, string_list_from_iterable(iterable.get()));
    JS_RETURN_IF_ERROR(strings.get());
    Rooted<Item> result(roots, parts ? js_array_new(0) : ItemNull);
    JS_RETURN_IF_ERROR(result.get());
    StrBuf* text = parts ? nullptr : strbuf_new();
    if (!parts && !text) return ItemError;
    const JsListLocale& locale = js_list_locales[fields[LOCALE]];
    const uint16_t* patterns = js_list_patterns[locale.pattern_set][fields[TYPE] * style_count + fields[STYLE]];
    Item status = emit_list(strings.get(), patterns, list_context(locale.name), {text, result.get()});
    if (parts) return item_is_error(status) ? status : result.get();
    if (item_is_error(status) || text->length > INT_MAX) {
        strbuf_free(text);
        return item_is_error(status) ? status : js_throw_range_error("ListFormat result is too long");
    }
    return js_strbuf_take_item(text);
}

Item format_list(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return render_list(receiver, argc ? args[0] : make_js_undefined(), false);
}

Item format_list_to_parts(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return render_list(receiver, argc ? args[0] : make_js_undefined(), true);
}

Item resolved_list_options(Item, Item receiver, Item*, int, uint64_t*) {
    int fields[FIELD_COUNT];
    JS_RETURN_IF_ERROR(read_list_state(receiver, fields));
    const char* names[] = {"locale", "type", "style"};
    const char* values[] = {js_list_locales[fields[LOCALE]].name, types[fields[TYPE]], styles[fields[STYLE]]};
    RootFrame roots(2);
    Rooted<Item> result(roots, js_new_object()), value(roots, ItemNull);
    JS_RETURN_IF_ERROR(result.get());
    for (int i = 0; i < FIELD_COUNT; ++i) {
        value.set(js_make_string(values[i]));
        JS_RETURN_IF_ERROR(js_create_data_property(result.get(), js_name_item(names[i]), value.get()));
    }
    return result.get();
}

Item supported_list_locales(Item, Item, Item* args, int argc, uint64_t*) {
    RootFrame roots(4);
    Rooted<Item> requested(roots, argc ? args[0] : make_js_undefined());
    Rooted<Item> options(roots, argc > 1 ? args[1] : make_js_undefined());
    requested.set(js_intl_requested_locales(requested.get()));
    JS_RETURN_IF_ERROR(requested.get());
    options.set(js_intl_coerce_options(options.get()));
    JS_RETURN_IF_ERROR(options.get());
    JS_RETURN_IF_ERROR(read_locale_matcher(options.get()));
    Rooted<Item> result(roots, js_array_new(0)), record(roots, ItemNull);
    JS_RETURN_IF_ERROR(result.get());
    for (int64_t i = 0; i < js_array_length(requested.get()); ++i) {
        record.set(js_elements_get_int(requested.get(), i));
        if (lookup_list_locale(record.get()))
            JS_RETURN_IF_ERROR(js_array_push(result.get(), js_elements_get_int(record.get(), JS_INTL_LOCALE_TAG)));
    }
    return result.get();
}

Item construct_list_format(Item callee, Item* args, int argc, Item new_target, uint64_t* result_home) {
    RootFrame roots(8);
    Rooted<Item> constructor(roots, callee), target(roots, new_target);
    Rooted<Item> locales(roots, argc ? args[0] : make_js_undefined());
    Rooted<Item> options(roots, argc > 1 ? args[1] : make_js_undefined());
    Rooted<Item> prototype(roots, js_get_prototype_from_constructor_default(target.get(), JS_CLASS_INTL_LIST_FORMAT, -1));
    JS_RETURN_IF_ERROR(prototype.get());
    locales.set(js_intl_requested_locales(locales.get()));
    JS_RETURN_IF_ERROR(locales.get());
    if (get_type_id(options.get()) != LMD_TYPE_UNDEFINED && !js_is_object_value(options.get()))
        return js_throw_type_error("Intl.ListFormat options must be an object");
    JS_RETURN_IF_ERROR(read_locale_matcher(options.get()));

    const JsListLocale* locale = nullptr;
    for (int64_t i = 0; !locale && i < js_array_length(locales.get()); ++i)
        locale = lookup_list_locale(js_elements_get_int(locales.get(), i));
    if (!locale) locale = js_intl_lookup_locale({"en", 2}, js_list_locales);
    if (!locale) return ItemError;
    int fields[FIELD_COUNT] = {};
    fields[LOCALE] = (int)(locale - js_list_locales);
    JS_RETURN_IF_ERROR(js_intl_string_option(options.get(), "type", types, &fields[TYPE], "Invalid ListFormat type"));
    JS_RETURN_IF_ERROR(js_intl_string_option(options.get(), "style", styles, &fields[STYLE], "Invalid ListFormat style"));

    Rooted<Item> record(roots, js_array_new(0)), key(roots, ItemNull), result(roots, ItemNull);
    JS_RETURN_IF_ERROR(record.get());
    for (int field : fields)
        JS_RETURN_IF_ERROR(js_array_push(record.get(), Item{.item = i2it(field)}));
    key.set(js_private_key_for_class(constructor.get(), js_name_item("#IntlListFormat")));
    JS_RETURN_IF_ERROR(key.get());
    result.set(js_native_private_array_object(JS_CLASS_INTL_LIST_FORMAT, key.get(), record.get(), prototype.get()));
    JS_RETURN_IF_ERROR(result.get());
    if (result_home) *result_home = result.get().item;
    return result.get();
}

} // namespace

Item js_intl_list_format_install(Item intl) {
    static const JsIntlServiceMethod methods[] = {
        {"format", format_list, 1, false},
        {"formatToParts", format_list_to_parts, 1, false},
        {"resolvedOptions", resolved_list_options, 0, false},
        {"supportedLocalesOf", supported_list_locales, 1, true},
    };
    return js_intl_install_service(intl, "ListFormat", "Intl.ListFormat", construct_list_format,
                                  methods, sizeof(methods) / sizeof(*methods));
}
