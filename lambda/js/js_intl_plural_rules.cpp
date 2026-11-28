#include "js_intl_locale.h"
#include "js_runtime_internal.hpp"
#include "js_object_meta.h"
#include <mpdecimal.h>

namespace {

enum JsPluralOperand : uint8_t { JS_PLURAL_N, JS_PLURAL_I, JS_PLURAL_V, JS_PLURAL_W,
    JS_PLURAL_F, JS_PLURAL_T, JS_PLURAL_C, JS_PLURAL_E };
enum JsPluralCategory : uint8_t { JS_PLURAL_ZERO, JS_PLURAL_ONE, JS_PLURAL_TWO,
    JS_PLURAL_FEW, JS_PLURAL_MANY, JS_PLURAL_OTHER };
struct JsPluralBounds { uint32_t low, high; };
struct JsPluralRelation { JsPluralOperand operand; uint32_t mod; uint16_t first, count; bool negate; };
struct JsPluralSlice { uint16_t first; uint8_t count; };
struct JsPluralRangeEntry { uint8_t start, end, result; };
struct JsPluralLocale { const char* name; uint16_t cardinal, ordinal, first_range; uint8_t range_count; };

#include "js_intl_plural_data.h"

const char* const categories[] = {"zero", "one", "two", "few", "many", "other"};
const char* const types[] = {"cardinal", "ordinal"};
const char* const rounding_modes[] = {"ceil", "floor", "expand", "trunc", "halfCeil",
    "halfFloor", "halfExpand", "halfTrunc", "halfEven"};
const char* const priorities[] = {"auto", "morePrecision", "lessPrecision"};
const char* const trailing_zeros[] = {"auto", "stripIfInteger"};

enum JsPluralField { LOCALE, TYPE, MIN_INTEGER, MIN_FRACTION, MAX_FRACTION, MIN_SIGNIFICANT,
    MAX_SIGNIFICANT, INCREMENT, ROUNDING, PRIORITY, TRAILING_ZEROS, FIELD_COUNT };
enum JsPluralRounding { CEIL, FLOOR, EXPAND, TRUNC, HALF_CEIL, HALF_FLOOR, HALF_EXPAND, HALF_TRUNC, HALF_EVEN };
enum JsPluralPriority { AUTOMATIC, MORE_PRECISION, LESS_PRECISION };

const JsPluralLocale* lookup_requested_locale(Item record) {
    String* base = it2s(js_elements_get_int(record, JS_INTL_LOCALE_BASE));
    return js_intl_lookup_locale({base->chars, base->len}, js_plural_locales);
}

template <size_t Count>
Item string_option(Item options, const char* name, const char* const (&choices)[Count], int* index) {
    return js_intl_string_option(options, name, choices, index, "Invalid PluralRules option");
}

Item integer_option(Item input, int low, int high, int* result) {
    if (get_type_id(input) == LMD_TYPE_UNDEFINED) return ItemNull;
    JS_ASSIGN_OR_RETURN(number, js_to_number(input));
    double value = 0.0;
    if (!item_try_to_double(number, &value) || !isfinite(value) || value < low || value > high)
        return js_throw_range_error("Invalid PluralRules digit option");
    *result = (int)floor(value);
    return ItemNull;
}

Item read_plural_digits(Item options, int (&fields)[FIELD_COUNT]) {
    const char* names[] = {"minimumFractionDigits", "maximumFractionDigits",
        "minimumSignificantDigits", "maximumSignificantDigits"};
    RootFrame roots(6);
    Rooted<Item> owner(roots, options), minimum(roots, ItemNull), maximum(roots, ItemNull);
    Rooted<Item> significant_min(roots, ItemNull), significant_max(roots, ItemNull), value(roots, ItemNull);
    value.set(js_get_key_cstr(owner.get(), "minimumIntegerDigits"));
    JS_RETURN_IF_ERROR(value.get());
    fields[MIN_INTEGER] = 1;
    JS_RETURN_IF_ERROR(integer_option(value.get(), 1, 21, &fields[MIN_INTEGER]));
    Rooted<Item>* raw[] = {&minimum, &maximum, &significant_min, &significant_max};
    for (size_t i = 0; i < sizeof(raw) / sizeof(*raw); ++i) {
        raw[i]->set(js_get_key_cstr(owner.get(), names[i]));
        JS_RETURN_IF_ERROR(raw[i]->get());
    }
    value.set(js_get_key_cstr(owner.get(), "roundingIncrement"));
    JS_RETURN_IF_ERROR(value.get());
    fields[INCREMENT] = 1;
    JS_RETURN_IF_ERROR(integer_option(value.get(), 1, 5000, &fields[INCREMENT]));
    static const int increments[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500,
        1000, 2000, 2500, 5000};
    bool valid_increment = false;
    for (int increment : increments) if (increment == fields[INCREMENT]) valid_increment = true;
    if (!valid_increment) return js_throw_range_error("Invalid PluralRules roundingIncrement");
    fields[ROUNDING] = HALF_EXPAND;
    JS_RETURN_IF_ERROR(string_option(owner.get(), "roundingMode", rounding_modes, &fields[ROUNDING]));
    JS_RETURN_IF_ERROR(string_option(owner.get(), "roundingPriority", priorities, &fields[PRIORITY]));
    JS_RETURN_IF_ERROR(string_option(owner.get(), "trailingZeroDisplay", trailing_zeros, &fields[TRAILING_ZEROS]));
    bool significant = get_type_id(significant_min.get()) != LMD_TYPE_UNDEFINED ||
        get_type_id(significant_max.get()) != LMD_TYPE_UNDEFINED;
    bool fraction = fields[PRIORITY] != AUTOMATIC || !significant;
    if (significant || fields[PRIORITY] != AUTOMATIC) {
        fields[MIN_SIGNIFICANT] = 1;
        fields[MAX_SIGNIFICANT] = 21;
        JS_RETURN_IF_ERROR(integer_option(significant_min.get(), 1, 21, &fields[MIN_SIGNIFICANT]));
        JS_RETURN_IF_ERROR(integer_option(significant_max.get(), fields[MIN_SIGNIFICANT], 21, &fields[MAX_SIGNIFICANT]));
    }
    if (fraction) {
        int min_fraction = -1, max_fraction = -1;
        JS_RETURN_IF_ERROR(integer_option(minimum.get(), 0, 100, &min_fraction));
        JS_RETURN_IF_ERROR(integer_option(maximum.get(), 0, 100, &max_fraction));
        if (min_fraction < 0) min_fraction = 0;
        int default_max = fields[INCREMENT] == 1 ? 3 : 0;
        if (max_fraction < 0) max_fraction = min_fraction > default_max ? min_fraction : default_max;
        if (min_fraction > max_fraction) return js_throw_range_error("Invalid PluralRules fraction range");
        fields[MIN_FRACTION] = min_fraction;
        fields[MAX_FRACTION] = max_fraction;
    } else fields[MIN_FRACTION] = fields[MAX_FRACTION] = -1;
    if (fields[INCREMENT] != 1) {
        if (fields[MAX_SIGNIFICANT]) return js_throw_type_error("Rounding increments require fraction digits");
        if (fields[MIN_FRACTION] != fields[MAX_FRACTION])
            return js_throw_range_error("Rounding increments require equal fraction limits");
    }
    return ItemNull;
}

struct JsPluralQuantity {
    char* text = nullptr;
    int integer_digits = 0;
    int fraction_digits = 0;
    int trim_digits = 0;
    ~JsPluralQuantity() { if (text) mpd_free(text); }
};

int decimal_rounding(int mode, bool negative) {
    // The quantity is absolute. Sign-sensitive rounding still follows the original number.
    switch (mode) {
        case CEIL: return negative ? MPD_ROUND_DOWN : MPD_ROUND_UP;
        case FLOOR: return negative ? MPD_ROUND_UP : MPD_ROUND_DOWN;
        case EXPAND: return MPD_ROUND_UP;
        case TRUNC: return MPD_ROUND_DOWN;
        case HALF_CEIL: return negative ? MPD_ROUND_HALF_DOWN : MPD_ROUND_HALF_UP;
        case HALF_FLOOR: return negative ? MPD_ROUND_HALF_UP : MPD_ROUND_HALF_DOWN;
        case HALF_TRUNC: return MPD_ROUND_HALF_DOWN;
        case HALF_EVEN: return MPD_ROUND_HALF_EVEN;
        default: return MPD_ROUND_HALF_UP;
    }
}

bool format_quantity(double value, const int (&fields)[FIELD_COUNT], JsPluralQuantity* result) {
    char raw[64];
    lambda_double_to_shortest(fabs(value), raw, sizeof(raw));
    mpd_t* number = mpd_qnew();
    if (!number) return false;
    mpd_context_t context;
    mpd_maxcontext(&context);
    // Binary64's coefficient and exponent plus the maximum fraction padding fit this precision.
    context.prec = 768;
    context.traps = 0;
    context.round = decimal_rounding(fields[ROUNDING], signbit(value));
    uint32_t status = 0;
    mpd_qset_string_exact(number, raw, &status);
    int fraction_exp = -fields[MAX_FRACTION];
    mpd_ssize_t magnitude = mpd_iszero(number) ? 0 : mpd_adjexp(number);
    mpd_ssize_t significant_exp = magnitude + 1 - fields[MAX_SIGNIFICANT];
    bool significant = fields[MAX_SIGNIFICANT] && (fields[MAX_FRACTION] < 0 ||
        (fields[PRIORITY] == MORE_PRECISION ? significant_exp <= fraction_exp : significant_exp > fraction_exp));
    uint32_t increment = (uint32_t)fields[INCREMENT];
    if (increment != 1) mpd_qdiv_u32(number, number, increment, &context, &status);
    mpd_qrescale(number, number, significant ? significant_exp : fraction_exp, &context, &status);
    if (increment != 1) mpd_qmul_u32(number, number, increment, &context, &status);
    int minimum_fraction = fields[MIN_FRACTION];
    if (significant) {
        mpd_ssize_t integer_places = mpd_iszero(number) ? 1 : mpd_adjexp(number) + 1;
        minimum_fraction = fields[MIN_SIGNIFICANT] > integer_places
            ? (int)(fields[MIN_SIGNIFICANT] - integer_places) : 0;
    }
    if (!(status & MPD_Errors)) result->text = mpd_qformat(number, "f", &context, &status);
    mpd_del(number);
    if (!result->text) return false;

    char* point = strchr(result->text, '.');
    result->integer_digits = (int)(point ? point - result->text : strlen(result->text));
    if (!point) return true;
    char* fraction = point + 1;
    int end = (int)strlen(fraction);
    while (end > 0 && fraction[end - 1] == '0') --end;
    result->trim_digits = end;
    result->fraction_digits = end == 0 && fields[TRAILING_ZEROS] ? 0
        : end < minimum_fraction ? minimum_fraction : end;
    if (!result->fraction_digits) *point = 0;
    else fraction[result->fraction_digits] = 0;
    return true;
}

uint32_t quantity_digits(const char* text, int count, uint32_t mod) {
    uint64_t value = 0;
    for (int i = 0; i < count; ++i) {
        value = value * 10 + (unsigned)(text[i] - '0');
        if (mod) value %= mod;
        else if (value >= UINT32_MAX) return UINT32_MAX;
    }
    return (uint32_t)value;
}

bool plural_relation(const JsPluralRelation& relation, const JsPluralQuantity& number) {
    // The integer-only CLDR '=' operator cannot match a non-integer `n`.
    if (relation.operand == JS_PLURAL_N && number.trim_digits) return relation.negate;
    uint32_t value = 0;
    switch (relation.operand) {
        case JS_PLURAL_N: case JS_PLURAL_I:
            value = quantity_digits(number.text, number.integer_digits, relation.mod); break;
        case JS_PLURAL_F: case JS_PLURAL_T:
            value = quantity_digits(number.text + number.integer_digits + 1,
                relation.operand == JS_PLURAL_F ? number.fraction_digits : number.trim_digits, relation.mod); break;
        case JS_PLURAL_V: case JS_PLURAL_W:
            value = (uint32_t)(relation.operand == JS_PLURAL_V ? number.fraction_digits : number.trim_digits);
            if (relation.mod) value %= relation.mod;
            break;
        case JS_PLURAL_C: case JS_PLURAL_E: break; // Standard notation does not scale the value.
    }
    bool match = false;
    for (int i = 0; i < relation.count; ++i) {
        const auto& range = js_plural_bounds[relation.first + i];
        if (value >= range.low && value <= range.high) { match = true; break; }
    }
    return match != relation.negate;
}

bool plural_rule(uint16_t rule, const JsPluralQuantity& number) {
    JsPluralSlice alternatives = js_plural_rules[rule];
    for (int i = 0; i < alternatives.count; ++i) {
        JsPluralSlice parts = js_plural_conjunctions[alternatives.first + i];
        int matched = 0;
        while (matched < parts.count && plural_relation(js_plural_relations[parts.first + matched], number))
            ++matched;
        if (matched == parts.count) return true;
    }
    return false;
}

const uint16_t* locale_rules(const int (&fields)[FIELD_COUNT]) {
    const JsPluralLocale& locale = js_plural_locales[fields[LOCALE]];
    return js_plural_rule_sets[fields[TYPE] ? locale.ordinal : locale.cardinal];
}

int select_category(const int (&fields)[FIELD_COUNT], const JsPluralQuantity& number) {
    if (!number.text) return JS_PLURAL_OTHER;
    const uint16_t* rules = locale_rules(fields);
    for (int category = JS_PLURAL_ZERO; category < JS_PLURAL_OTHER; ++category)
        if (plural_rule(rules[category], number)) return category;
    return JS_PLURAL_OTHER;
}

Item read_plural_state(Item receiver, int (&out)[FIELD_COUNT]) {
    JS_ASSIGN_OR_RETURN(state, js_native_private_array_state(receiver, JS_CLASS_INTL_PLURAL_RULES,
        FIELD_COUNT, "Illegal Intl.PluralRules receiver"));
    for (int i = 0; i < FIELD_COUNT; ++i) out[i] = (int)it2i(js_elements_get_int(state, i));
    return ItemNull;
}

Item plural_number(Item value, double* result) {
    JS_ASSIGN_OR_RETURN(number, js_to_number(value));
    return item_try_to_double(number, result) ? ItemNull : ItemError;
}

Item select_plural(Item, Item receiver, Item* args, int argc, uint64_t*) {
    int fields[FIELD_COUNT];
    JS_RETURN_IF_ERROR(read_plural_state(receiver, fields));
    double value = 0.0;
    JS_RETURN_IF_ERROR(plural_number(argc ? args[0] : make_js_undefined(), &value));
    JsPluralQuantity quantity;
    if (isfinite(value) && !format_quantity(value, fields, &quantity)) return ItemError;
    return js_name_item(categories[select_category(fields, quantity)]);
}

Item select_plural_range(Item, Item receiver, Item* args, int argc, uint64_t*) {
    int fields[FIELD_COUNT];
    JS_RETURN_IF_ERROR(read_plural_state(receiver, fields));
    if (argc < 2 || get_type_id(args[0]) == LMD_TYPE_UNDEFINED || get_type_id(args[1]) == LMD_TYPE_UNDEFINED)
        return js_throw_type_error("PluralRules.selectRange requires both endpoints");
    RootFrame roots(2);
    Rooted<Item> start(roots, args[0]), end(roots, args[1]);
    double left = 0.0, right = 0.0;
    JS_RETURN_IF_ERROR(plural_number(start.get(), &left));
    JS_RETURN_IF_ERROR(plural_number(end.get(), &right));
    if (isnan(left) || isnan(right)) return js_throw_range_error("Invalid plural range endpoint");
    JsPluralQuantity first, last;
    if (isfinite(left) && !format_quantity(left, fields, &first)) return ItemError;
    if (isfinite(right) && !format_quantity(right, fields, &last)) return ItemError;
    int start_category = select_category(fields, first);
    // ResolvePluralRange uses the single category if the formatted endpoints coincide.
    if (first.text && last.text && strcmp(first.text, last.text) == 0)
        return js_name_item(categories[start_category]);
    int end_category = select_category(fields, last);
    const auto& locale = js_plural_locales[fields[LOCALE]];
    if (!fields[TYPE]) for (int i = 0; i < locale.range_count; ++i) {
        const auto& range = js_plural_range_entries[locale.first_range + i];
        if (range.start == start_category && range.end == end_category)
            return js_name_item(categories[range.result]);
    }
    return js_name_item(categories[JS_PLURAL_OTHER]);
}

Item resolved_plural_options(Item, Item receiver, Item*, int, uint64_t*) {
    int fields[FIELD_COUNT];
    JS_RETURN_IF_ERROR(read_plural_state(receiver, fields));
    RootFrame roots(3);
    Rooted<Item> result(roots, js_new_object()), list(roots, js_array_new(0)), value(roots, ItemNull);
    auto string = [&](const char* name, const char* data) {
        value.set(js_name_item(data));
        return js_create_data_property(result.get(), js_name_item(name), value.get());
    };
    JS_RETURN_IF_ERROR(string("locale", js_plural_locales[fields[LOCALE]].name));
    JS_RETURN_IF_ERROR(string("type", types[fields[TYPE]]));
    const char* names[] = {"minimumIntegerDigits", "minimumFractionDigits", "maximumFractionDigits",
        "minimumSignificantDigits", "maximumSignificantDigits"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        int digits = fields[MIN_INTEGER + i];
        if (digits < 0 || (i >= 3 && !digits)) continue;
        JS_RETURN_IF_ERROR(js_create_data_property(result.get(), js_name_item(names[i]), Item{.item = i2it(digits)}));
    }
    const uint16_t* rules = locale_rules(fields);
    for (int i = 0; i <= JS_PLURAL_OTHER; ++i) if (i == JS_PLURAL_OTHER || rules[i])
        JS_RETURN_IF_ERROR(js_array_push(list.get(), js_name_item(categories[i])));
    JS_RETURN_IF_ERROR(js_create_data_property(result.get(), js_name_item("pluralCategories"), list.get()));
    JS_RETURN_IF_ERROR(js_create_data_property(result.get(), js_name_item("roundingIncrement"),
        Item{.item = i2it(fields[INCREMENT])}));
    JS_RETURN_IF_ERROR(string("roundingMode", rounding_modes[fields[ROUNDING]]));
    JS_RETURN_IF_ERROR(string("roundingPriority", priorities[fields[PRIORITY]]));
    JS_RETURN_IF_ERROR(string("trailingZeroDisplay", trailing_zeros[fields[TRAILING_ZEROS]]));
    return result.get();
}

Item supported_plural_locales(Item, Item, Item* args, int argc, uint64_t*) {
    RootFrame roots(5);
    Rooted<Item> source(roots, argc ? args[0] : make_js_undefined());
    Rooted<Item> options(roots, argc > 1 ? args[1] : make_js_undefined());
    Rooted<Item> requested(roots, js_intl_requested_locales(source.get()));
    JS_RETURN_IF_ERROR(requested.get());
    options.set(js_intl_coerce_options(options.get()));
    JS_RETURN_IF_ERROR(options.get());
    int matcher = 0;
    JS_RETURN_IF_ERROR(string_option(options.get(), "localeMatcher", js_intl_locale_matchers, &matcher));
    Rooted<Item> result(roots, js_array_new(0)), record(roots, ItemNull);
    for (int64_t i = 0; i < js_array_length(requested.get()); ++i) {
        record.set(js_elements_get_int(requested.get(), i));
        if (!lookup_requested_locale(record.get())) continue;
        JS_RETURN_IF_ERROR(js_array_push(result.get(), js_elements_get_int(record.get(), JS_INTL_LOCALE_TAG)));
    }
    return result.get();
}

Item construct_plural(Item callee, Item* args, int argc, Item new_target, uint64_t* result_home) {
    RootFrame roots(8);
    Rooted<Item> constructor(roots, callee), target(roots, new_target);
    Rooted<Item> locales(roots, argc ? args[0] : make_js_undefined());
    Rooted<Item> options(roots, argc > 1 ? args[1] : make_js_undefined());
    Rooted<Item> record(roots, ItemNull), key(roots, ItemNull), result(roots, ItemNull);
    Rooted<Item> prototype(roots, js_get_prototype_from_constructor_default(target.get(), JS_CLASS_INTL_PLURAL_RULES, -1));
    JS_RETURN_IF_ERROR(prototype.get());
    locales.set(js_intl_requested_locales(locales.get()));
    JS_RETURN_IF_ERROR(locales.get());
    options.set(js_intl_coerce_options(options.get()));
    JS_RETURN_IF_ERROR(options.get());
    int fields[FIELD_COUNT] = {};
    int matcher = 0;
    JS_RETURN_IF_ERROR(string_option(options.get(), "localeMatcher", js_intl_locale_matchers, &matcher));
    JS_RETURN_IF_ERROR(string_option(options.get(), "type", types, &fields[TYPE]));
    JS_RETURN_IF_ERROR(read_plural_digits(options.get(), fields));
    const JsPluralLocale* locale = nullptr;
    for (int64_t i = 0; !locale && i < js_array_length(locales.get()); ++i)
        locale = lookup_requested_locale(js_elements_get_int(locales.get(), i));
    if (!locale) locale = js_intl_lookup_locale({"en", 2}, js_plural_locales);
    if (!locale) return ItemError;
    fields[LOCALE] = (int)(locale - js_plural_locales);
    record.set(js_array_new(0));
    for (int field : fields)
        JS_RETURN_IF_ERROR(js_array_push(record.get(), Item{.item = i2it(field)}));
    key.set(js_private_key_for_class(constructor.get(), js_name_item("#IntlPluralRules")));
    JS_RETURN_IF_ERROR(key.get());
    result.set(js_native_private_array_object(JS_CLASS_INTL_PLURAL_RULES, key.get(), record.get(), prototype.get()));
    JS_RETURN_IF_ERROR(result.get());
    if (result_home) *result_home = result.get().item;
    return result.get();
}

} // namespace

Item js_intl_plural_rules_install(Item intl) {
    static const JsIntlServiceMethod methods[] = {
        {"select", select_plural, 1, false}, {"selectRange", select_plural_range, 2, false},
        {"resolvedOptions", resolved_plural_options, 0, false},
        {"supportedLocalesOf", supported_plural_locales, 1, true},
    };
    return js_intl_install_service(intl, "PluralRules", "Intl.PluralRules", construct_plural,
                                  methods, sizeof(methods) / sizeof(*methods));
}
