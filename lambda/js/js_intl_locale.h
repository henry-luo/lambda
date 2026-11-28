#pragma once

#include "../lambda-data.hpp"
#include <string.h>

enum JsIntlLocaleField { JS_INTL_LOCALE_TAG, JS_INTL_LOCALE_BASE,
    JS_INTL_LOCALE_LANGUAGE, JS_INTL_LOCALE_SCRIPT, JS_INTL_LOCALE_REGION, JS_INTL_LOCALE_FIELDS };

// Canonical locale records retain both the full language tag and its base for lookup.
Item js_intl_requested_locales(Item source);
Item js_intl_coerce_options(Item source);
Item js_intl_plural_rules_install(Item intl);
Item js_intl_list_format_install(Item intl);
void js_namespace_set_to_string_tag(Item object, const char* name, int len);
Item js_intl_append_part(Item parts, const char* type, Item value);

inline constexpr const char* js_intl_locale_matchers[] = {"best fit", "lookup"};

Item js_intl_string_option(Item options, const char* name, const char* const* choices,
                            size_t count, int* index, const char* error);

template <size_t Count>
Item js_intl_string_option(Item options, const char* name, const char* const (&choices)[Count],
                            int* index, const char* error) {
    return js_intl_string_option(options, name, choices, Count, index, error);
}

template <typename Locale, size_t Count>
const Locale* js_intl_lookup_locale(StrView tag, const Locale (&locales)[Count]) {
    while (tag.length) {
        size_t low = 0, high = Count;
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            const char* name = locales[mid].name;
            int comparison = strncmp(tag.str, name, tag.length);
            if (!comparison) {
                if (!name[tag.length]) return &locales[mid];
                comparison = -1;
            }
            if (comparison < 0) high = mid;
            else low = mid + 1;
        }
        // Lookup removes subtags from the end of the validated base name.
        while (tag.length && tag.str[tag.length - 1] != '-') --tag.length;
        if (tag.length) --tag.length;
    }
    return nullptr;
}
