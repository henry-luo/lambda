#include "mvp_lmd_runtime.h"
#include "mvp_lmd_dynamic.h"
#include "../js_props.h"
#include "../js_builtin_catalog.hpp"
#include "../js_runtime.h"
#include "../js_runtime_internal.hpp"
#include "../js_source_module.hpp"
#include "../js_runtime_state.hpp"
#include "../js_typed_array_carrier.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"
#include "../../input/input.hpp"
#include "../../input/input-parsers.h"
#include "../../core/lambda-decimal.hpp"
#include "../js_well_known_names.h"
#include "../../../lib/datetime.h"
#include "../../../lib/escape.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/utf.h"
#include <math.h>

struct JsonAncestor { Item value; const JsonAncestor* parent; };
const char* const mvp_lmd_error_names[LMD_ERROR_COUNT] = {"Error", "EvalError", "RangeError",
    "ReferenceError", "SyntaxError", "TypeError", "URIError", "AggregateError"};
const char* const mvp_lmd_primitive_names[LMD_PRIMITIVE_COUNT] = {"String", "Number", "Boolean", "BigInt"};

int mvp_lmd_error_kind(const TypeNominal* nominal, const MvpLmdLibraryState* library) {
    if (!library || !library->errors) return -1;
    // native error families derive directly from Error; prefer the specific family over its base.
    for (int i = LMD_ERROR_COUNT - 1; i >= 0; i--)
        if (lambda_nominal_derives_from(nominal, &library->errors[i].nominal)) return i;
    return -1;
}

static Item primitive_payload(Item object) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state || !state->primitive_key || get_type_id(object) != LMD_TYPE_MAP || mvp_lmd_is_proxy(object))
        return Item{.item = ITEM_JS_UNDEFINED};
    ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)object.map->type, state->primitive_key);
    // this borrow is valid only while the wrapper stays rooted, until reentry.
    return field ? map_shape_field_to_item(object.map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
}

Item mvp_lmd_box_primitive(Item value, Item prototype) {
    if (mvp_lmd_is_object(value)) return value;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state || !state->primitives) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    int kind = type == LMD_TYPE_STRING ? LMD_PRIMITIVE_STRING :
        type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? LMD_PRIMITIVE_NUMBER :
        type == LMD_TYPE_BOOL ? LMD_PRIMITIVE_BOOLEAN : type == LMD_TYPE_DECIMAL ? LMD_PRIMITIVE_BIGINT : -1;
    if (kind < 0 && type != LMD_TYPE_SYMBOL) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdClass* cls = kind < 0 ? &state->symbol : &state->primitives[kind];
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> primitive(roots, lambda_item_adopt_scalar_home(value, &home)), parent(roots, prototype),
        object(roots, ItemNull), key(roots, ItemNull), character(roots, ItemNull);
    object.set(mvp_lmd_object_new(&cls->shape, 0));
    if (item_is_error(object.get())) return object.get();
    // the private shape field owns scalar storage and cannot be forged by JS properties.
    if (!map_shape_set(object.get().map, state->primitive_key, primitive.get())) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (parent.get().item != ITEM_JS_UNDEFINED && parent.get().item != mvp_lmd_class_values(cls)[1].item) {
        Item linked = mvp_lmd_set_prototype(object.get(), parent.get());
        if (item_is_error(linked)) return linked;
    }
    if (kind == LMD_PRIMITIVE_STRING) {
        String* string = primitive.get().get_string();
        Utf16Iterator iter = {(const unsigned char*)string->chars, string->len, 0, -1};
        uint16_t unit; uint32_t length = 0;
        while (utf16_iterator_next(&iter, &unit)) {
            key.set(mvp_lmd_number_to_string(length++));
            if (item_is_error(key.get())) return key.get();
            character.set(mvp_lmd_string_at(ItemNull, unit, LMD_STRING_FROM_CODE));
            if (item_is_error(character.get())) return character.get();
            Item stored = mvp_lmd_named_set(object.get(), key.get().get_string()->chars, character.get(),
                JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
            if (item_is_error(stored)) return stored;
        }
        Item stored = mvp_lmd_named_set(object.get(), "length", Item{.item = i2it(length)},
            JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
        if (item_is_error(stored)) return stored;
    }
    return object.get();
}

static Item primitive_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item target) {
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    MvpLmdClass* cls = mvp_lmd_class_record(self);
    int kind = (int)(cls - state->primitives);
    bool construct = target.item != ITEM_JS_UNDEFINED;
    if (construct && kind == LMD_PRIMITIVE_BIGINT) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> value(roots, count ? lambda_item_adopt_scalar_home(args[0], &home) : Item{.item = ITEM_JS_UNDEFINED}),
        prototype(roots, ItemNull);
    if (kind == LMD_PRIMITIVE_STRING) {
        value.set(count ? mvp_lmd_primitive_to_string(value.get(), construct ? 1 : 3) :
            Item{.item = s2it(heap_create_name("", 0))});
    } else if (kind == LMD_PRIMITIVE_BOOLEAN) {
        value.set(Item{.item = b2it(mvp_lmd_truthy(value.get()))});
    } else {
        value.set(lambda_item_adopt_scalar_home(mvp_lmd_to_primitive(value.get(), 1), &home));
        if (item_is_error(value.get())) return value.get();
        TypeId type = get_type_id(value.get());
        if (kind == LMD_PRIMITIVE_NUMBER) {
            value.set(type == LMD_TYPE_DECIMAL ? push_d(bigint_to_double(value.get())) :
                count ? mvp_lmd_primitive_to_number(value.get()) : Item{.item = i2it(0)});
        } else if (type == LMD_TYPE_STRING) {
            const char* bytes; size_t length;
            mvp_lmd_numeric_string_span(value.get().get_string(), &bytes, &length);
            Item parsed = bigint_from_string(bytes, (int)length);
            value.set(item_is_error(parsed) ? mvp_lmd_fail(LMD_MVP_SYNTAX, 0) : parsed);
        } else if (type == LMD_TYPE_BOOL) value.set(bigint_from_int64(value.get().bool_val));
        else if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) {
            double number = it2d(value.get());
            value.set(isfinite(number) && number == trunc(number) ? bigint_from_double(number) : mvp_lmd_fail(LMD_MVP_RANGE, 0));
        } else if (type != LMD_TYPE_DECIMAL) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    }
    if (item_is_error(value.get())) return value.get();
    value.set(lambda_item_adopt_scalar_home(value.get(), &home));
    if (!construct) return get_type_id(value.get()) == LMD_TYPE_FLOAT ? push_d(it2d(value.get())) : value.get();
    prototype.set(mvp_lmd_constructor_prototype(target, mvp_lmd_class_values(cls)[1]));
    if (item_is_error(prototype.get())) return prototype.get();
    return mvp_lmd_box_primitive(value.get(), prototype.get());
}

enum { PRIMITIVE_PRECISION = 1000, PRIMITIVE_EXPONENTIAL };
static Item primitive_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    intptr_t operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    bool locale = operation == 16 + LMD_PRIMITIVE_NUMBER || operation == 16 + LMD_PRIMITIVE_BIGINT;
    bool fixed = operation == LMD_METHOD_TO_FIXED;
    bool scientific = operation == PRIMITIVE_PRECISION || operation == PRIMITIVE_EXPONENTIAL;
    int kind = fixed || scientific ? LMD_PRIMITIVE_NUMBER : locale ? (int)operation - 16 : (int)(operation / 2);
    bool stringify = fixed || scientific || locale || (operation & 1);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> value(roots, lambda_item_adopt_scalar_home(receiver, &home));
    if (get_type_id(value.get()) == LMD_TYPE_MAP && !mvp_lmd_is_proxy(value.get())) {
        value.set(lambda_item_adopt_scalar_home(primitive_payload(value.get()), &home));
    }
    TypeId type = get_type_id(value.get());
    TypeId expected[] = {LMD_TYPE_STRING, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL};
    if (type != expected[kind] && !(kind == LMD_PRIMITIVE_NUMBER && type == LMD_TYPE_INT))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (!stringify) return type == LMD_TYPE_FLOAT ? push_d(it2d(value.get())) : value.get();
    if (scientific) {
        // guest coercion and range errors precede the shared primitive-only formatter.
        Item argument = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
        bool supplied = argument.item != ITEM_JS_UNDEFINED;
        if (supplied) {
            argument = mvp_lmd_primitive_to_number(argument);
            if (item_is_error(argument)) return argument;
            double digits = it2d(argument);
            digits = isnan(digits) ? 0 : trunc(digits);
            if (isfinite(it2d(value.get())) &&
                    (digits < (operation == PRIMITIVE_PRECISION ? 1 : 0) || digits > 100))
                return mvp_lmd_fail(LMD_MVP_RANGE, 0);
            // non-finite receivers ignore the range, after observing argument coercion.
            argument = Item{.item = i2it(isfinite(digits) && digits >= 0 && digits <= 100 ? (int64_t)digits : 0)};
        }
        return js_numeric_prototype_algorithm(value.get(), operation == PRIMITIVE_PRECISION ?
            JS_NUMERIC_TO_PRECISION : JS_NUMERIC_TO_EXPONENTIAL, &argument, supplied ? 1 : 0);
    }
    if (fixed) {
        // the generic prototype entry shares the direct MIR formatter and receiver brand check.
        Item digits = count ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(0)};
        return item_is_error(digits) ? digits : mvp_lmd_number_to_fixed(it2d(value.get()), it2d(digits));
    }
    if (kind == LMD_PRIMITIVE_NUMBER || kind == LMD_PRIMITIVE_BIGINT) {
        Item radix = !locale && count && args[0].item != ITEM_JS_UNDEFINED ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(10)};
        if (item_is_error(radix)) return radix;
        return kind == LMD_PRIMITIVE_NUMBER ? mvp_lmd_number_to_radix_string(it2d(value.get()), it2d(radix)) :
            mvp_lmd_bigint_to_string(value.get(), it2d(radix));
    }
    return mvp_lmd_primitive_to_string(value.get(), 3);
}

static Item primitive_static(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (operation == 11 || operation == 12) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> value(roots, count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}), converted(roots, ItemNull);
        Item width = mvp_lmd_primitive_to_number(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
        if (item_is_error(width)) return width;
        double bits = it2d(width);
        bits = isnan(bits) ? 0 : trunc(bits);
        if (bits < 0 || bits > 9007199254740991.0) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
        // ToIndex must finish before ToBigInt can invoke guest conversion hooks.
        Item bigint = ItemNull;
        Item status = mvp_lmd_buffer_ops.bigint(value.get(), &bigint);
        if (item_is_error(status)) return status;
        converted.set(bigint);
        width = push_d(bits);
        return operation == 11 ? js_bigint_as_int_n(width, converted.get()) : js_bigint_as_uint_n(width, converted.get());
    }
    if (operation == 9 || operation == 10) {
        Item number = mvp_lmd_primitive_to_number(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
        return item_is_error(number) ? number : Item{.item = b2it(operation == 9 ? isnan(it2d(number)) : isfinite(it2d(number)))};
    }
    if (!operation) {
        static const JsStringRawOps ops = {
            [](Item value) { return mvp_lmd_box_primitive(value); },
            [](Item object, Item key) {
                key = mvp_lmd_property_key(key);
                return item_is_error(key) ? key : mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, object);
            },
            [](Item value) { return mvp_lmd_primitive_to_number(value); },
            [](Item value) { return mvp_lmd_primitive_to_string(value); }
        };
        return js_string_raw_with_ops(args, count, &ops);
    }
    if (operation >= 3 && operation <= 6) {
        Item value = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
        TypeId type = get_type_id(value);
        if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT) return Item{.item = ITEM_FALSE};
        double number = it2d(value);
        return Item{.item = b2it(operation == 5 ? isnan(number) : isfinite(number) &&
            (operation == 3 || (trunc(number) == number && (operation != 6 || fabs(number) <= 9007199254740991.0))))};
    }
    if (operation == 1 || operation == 2) {
        const JsStringCodeOps ops = {
            [](void* source, int64_t index) { return ((Item*)source)[index]; },
            [](Item value) { return mvp_lmd_primitive_to_number(value); },
            []() { return mvp_lmd_fail(LMD_MVP_RANGE, 0); }
        };
        return js_string_from_codes_with_ops(args, count, operation == 2, &ops);
    }
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> result(roots, ItemNull), value(roots, ItemNull);
    result.set(mvp_lmd_primitive_to_string(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(result.get())) return result.get();
    value.set(count > 1 && operation == 7 ? mvp_lmd_primitive_to_number(args[1]) : Item{.item = i2it(0)});
    if (item_is_error(value.get())) return value.get();
    return push_d(operation == 7 ? mvp_lmd_parse_integer(result.get().get_string(), js_to_int32(it2d(value.get()))) :
        mvp_lmd_parse_number(result.get().get_string(), 1));
}

static Item string_method(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int method = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (receiver.item == ITEM_NULL || receiver.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, receiver), first(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    Rooted<Item> second(roots, count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}), value(roots, ItemNull);
    uint64_t substr_homes[2] = {};
    if (method == LMD_METHOD_STRING_SUBSTR) {
        // receiver conversion can reenter before either numeric argument is consumed.
        first.set(lambda_item_adopt_scalar_home(first.get(), &substr_homes[0]));
        second.set(lambda_item_adopt_scalar_home(second.get(), &substr_homes[1]));
    }
    if (method == LMD_METHOD_MATCH || method == LMD_METHOD_SEARCH || method == LMD_METHOD_REPLACE || method == LMD_METHOD_SPLIT || method == LMD_METHOD_MATCH_ALL || method == LMD_METHOD_REPLACE_ALL)
        return mvp_lmd_regexp_string_method(mvp_lmd_program_library(), source.get(), first.get(), second.get(),
            method == LMD_METHOD_REPLACE_ALL ? 5 : method == LMD_METHOD_MATCH_ALL ? 4 : method == LMD_METHOD_SPLIT ? 3 : method == LMD_METHOD_SEARCH ? 2 : method == LMD_METHOD_REPLACE);
    source.set(mvp_lmd_primitive_to_string(source.get()));
    if (item_is_error(source.get())) return source.get();
    if (method == LMD_METHOD_LOWER || method == LMD_METHOD_UPPER)
        return method == LMD_METHOD_LOWER ? fn_lower(source.get()) : fn_upper(source.get());
    if (method == LMD_METHOD_STRING_SUBSTR)
        return js_string_substr_with_ops(source.get(), first.get(), second.get(), &mvp_lmd_buffer_ops);
    if (method >= LMD_METHOD_STRING_NORMALIZE && method <= LMD_METHOD_STRING_SUP) {
        static const JsStringIntrinsicOp operations[] = {
            JS_STRING_INTRINSIC_NORMALIZE, JS_STRING_INTRINSIC_LOCALE_COMPARE, JS_STRING_INTRINSIC_IS_WELL_FORMED, JS_STRING_INTRINSIC_TO_WELL_FORMED,
            JS_STRING_INTRINSIC_ANCHOR, JS_STRING_INTRINSIC_BIG, JS_STRING_INTRINSIC_BLINK, JS_STRING_INTRINSIC_BOLD,
            JS_STRING_INTRINSIC_FIXED, JS_STRING_INTRINSIC_FONTCOLOR, JS_STRING_INTRINSIC_FONTSIZE, JS_STRING_INTRINSIC_ITALICS,
            JS_STRING_INTRINSIC_LINK, JS_STRING_INTRINSIC_SMALL, JS_STRING_INTRINSIC_STRIKE, JS_STRING_INTRINSIC_SUB,
            JS_STRING_INTRINSIC_SUP,
        };
        static const JsStringTextOps ops = {
            [](Item item) { return mvp_lmd_primitive_to_string(item); },
            [](const char*) { return mvp_lmd_fail(LMD_MVP_RANGE, 0); }
        };
        return js_string_text_with_ops(source.get(), operations[method - LMD_METHOD_STRING_NORMALIZE], first.get(), &ops);
    }
    if (method == LMD_METHOD_STRING_CONCAT) {
        for (uint64_t i = 0; i < count; i++) {
            value.set(mvp_lmd_primitive_to_string(args[i]));
            if (item_is_error(value.get())) return value.get();
            source.set(mvp_lmd_string_concat(source.get(), value.get()));
            if (item_is_error(source.get())) return source.get();
        }
        return source.get();
    }
    if (method >= LMD_METHOD_TRIM && method <= LMD_METHOD_TRIM_END) {
        const char* bytes; size_t length;
        String* string = source.get().get_string();
        mvp_lmd_numeric_string_span(string, &bytes, &length);
        size_t first_byte = method == LMD_METHOD_TRIM_END ? 0 : bytes - string->chars;
        size_t end_byte = method == LMD_METHOD_TRIM_START ? string->len : bytes - string->chars + length;
        String* result = heap_strcpy(string->chars + first_byte, end_byte - first_byte);
        return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    bool search = method == LMD_METHOD_INDEX_OF || method == LMD_METHOD_STARTS_WITH || method == LMD_METHOD_ENDS_WITH ||
        method == LMD_METHOD_STRING_INCLUDES || method == LMD_METHOD_STRING_LAST_INDEX_OF;
    if (search) {
        if (method == LMD_METHOD_STARTS_WITH || method == LMD_METHOD_ENDS_WITH || method == LMD_METHOD_STRING_INCLUDES) {
            if (mvp_lmd_is_object(first.get())) {
                value.set(mvp_lmd_property_with_receiver(first.get(), Item{.item = s2it(well_known_name_ref(JS_SYMBOL_MATCH))},
                    ItemNull, LMD_PROP_GET, first.get()));
                if (item_is_error(value.get())) return value.get();
                bool regex = value.get().item == ITEM_JS_UNDEFINED ?
                    mvp_lmd_class_record(first.get()) == &mvp_lmd_program_library()->regexp : mvp_lmd_truthy(value.get());
                if (regex) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            }
        }
        first.set(mvp_lmd_primitive_to_string(first.get()));
        if (item_is_error(first.get())) return first.get();
        second.set(second.get().item == ITEM_JS_UNDEFINED && (method == LMD_METHOD_ENDS_WITH || method == LMD_METHOD_STRING_LAST_INDEX_OF)
            ? push_d(INFINITY) : mvp_lmd_primitive_to_number(second.get()));
        if (item_is_error(second.get())) return second.get();
        double position = it2d(second.get());
        if (method == LMD_METHOD_STRING_LAST_INDEX_OF) {
            int64_t limit = utf8_to_utf16_length(source.get().get_string()->chars, source.get().get_string()->len);
            if (!isnan(position)) limit = (int64_t)fmin(fmax(trunc(position), 0), limit);
            int64_t found = -1, start = 0;
            while (start <= limit) {
                int64_t next = mvp_lmd_string_search(source.get(), first.get(), start, 0);
                if (next < 0 || next > limit) break;
                found = next; start = next + 1;
            }
            return Item{.item = i2it(found)};
        }
        int prefix = method == LMD_METHOD_STARTS_WITH ? 1 : method == LMD_METHOD_ENDS_WITH ? 2 : 0;
        int64_t found = mvp_lmd_string_search(source.get(), first.get(), position, prefix);
        return Item{.item = method == LMD_METHOD_STRING_INCLUDES ? b2it(found >= 0) : prefix ? b2it(found) : i2it(found)};
    }
    value.set(mvp_lmd_primitive_to_number(first.get()));
    if (item_is_error(value.get())) return value.get();
    double position = it2d(value.get());
    if (method == LMD_METHOD_CHAR_AT || method == LMD_METHOD_CODE_AT)
        return mvp_lmd_string_at(source.get(), position, method == LMD_METHOD_CODE_AT ? LMD_STRING_CODE : LMD_STRING_CHAR);
    if (method == LMD_METHOD_CODE_POINT_AT || method == LMD_METHOD_STRING_AT) {
        position = isnan(position) ? 0 : trunc(position);
        int64_t length = utf8_to_utf16_length(source.get().get_string()->chars, source.get().get_string()->len);
        if (method == LMD_METHOD_STRING_AT && position < 0) position += length;
        if (!(position >= 0 && position < length)) return Item{.item = ITEM_JS_UNDEFINED};
        if (method == LMD_METHOD_STRING_AT) return mvp_lmd_string_at(source.get(), position, LMD_STRING_CHAR);
        int64_t lead = it2i(mvp_lmd_string_at(source.get(), position, LMD_STRING_CODE));
        if (lead >= 0xd800 && lead <= 0xdbff && position + 1 < length) {
            int64_t trail = it2i(mvp_lmd_string_at(source.get(), position + 1, LMD_STRING_CODE));
            if (trail >= 0xdc00 && trail <= 0xdfff) lead = 0x10000 + ((lead - 0xd800) << 10) + trail - 0xdc00;
        }
        return Item{.item = i2it(lead)};
    }
    if (method == LMD_METHOD_PAD_START || method == LMD_METHOD_PAD_END)
        return mvp_lmd_string_pad(source.get(), position, second.get(), method == LMD_METHOD_PAD_END);
    if (method == LMD_METHOD_REPEAT) {
        position = isnan(position) ? 0 : trunc(position);
        if (position < 0 || !isfinite(position)) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
        String* text = source.get().get_string();
        if (!text->len) return source.get();
        if (position > (INT_MAX - sizeof(String) - 1) / text->len) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
        String* result = str_repeat(text, (int64_t)position);
        return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    second.set(second.get().item == ITEM_JS_UNDEFINED ? push_d(INFINITY) : mvp_lmd_primitive_to_number(second.get()));
    return item_is_error(second.get()) ? second.get() :
        mvp_lmd_string_range(source.get(), position, it2d(second.get()), method == LMD_METHOD_STRING_SLICE);
}

static Item error_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item target) {
    MvpLmdClass* cls = mvp_lmd_class_record(self);
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    bool aggregate = cls == &state->errors[LMD_ERROR_AGGREGATE];
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, ItemNull), prototype(roots, ItemNull), text(roots, ItemNull), list(roots, ItemNull);
    // GetPrototypeFromConstructor precedes message conversion and preserves the
    // native Error brand while keeping the live JS prototype independent (D2.6.9v3).
    prototype.set(mvp_lmd_class_values(cls)[1]);
    if (target.item != ITEM_JS_UNDEFINED) {
        prototype.set(mvp_lmd_constructor_prototype(target, mvp_lmd_class_values(cls)[1]));
        if (item_is_error(prototype.get())) return prototype.get();
    }
    MvpLmdClass* actual = target.item == ITEM_JS_UNDEFINED ? NULL : mvp_lmd_class_record(target);
    // preserve the declared Lambda subtype when native Error construction supplies its instance storage.
    TypeMap* shape = actual && actual->ast && lambda_nominal_derives_from(&actual->nominal, &cls->nominal)
        ? &actual->shape : &cls->shape;
    object.set(mvp_lmd_object_new(shape, 0));
    if (item_is_error(object.get())) return object.get();
    // a newTarget supplies ancestry, but only native Error allocation supplies [[ErrorData]].
    if (!state->error_data_key)
        state->error_data_key = mvp_lmd_internal_name(LMD_INTERNAL_ERROR);
    if (!state->error_data_key || !map_shape_set(object.get().map, state->error_data_key, Item{.item = ITEM_TRUE}))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (prototype.get().item != mvp_lmd_class_values(cls)[1].item) {
        Item linked = mvp_lmd_set_prototype(object.get(), prototype.get());
        if (item_is_error(linked)) return linked;
    }
    uint64_t message_index = aggregate ? 1 : 0;
    Item message = count > message_index ? args[message_index] : Item{.item = ITEM_JS_UNDEFINED};
    if (message.item != ITEM_JS_UNDEFINED) {
        text.set(mvp_lmd_primitive_to_string(message));
        if (item_is_error(text.get())) return text.get();
        Item stored = mvp_lmd_named_set(object.get(), "message", text.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(stored)) return stored;
    }
    Item options = count > message_index + 1 ? args[message_index + 1] : ItemNull;
    if (mvp_lmd_is_object(options)) {
        Item key = {.item = s2it(heap_create_name("cause", 5))};
        Item present = mvp_lmd_property_with_receiver(options, key, ItemNull, LMD_PROP_HAS, options);
        if (item_is_error(present)) return present;
        if (present.item == ITEM_TRUE) {
            text.set(mvp_lmd_property_with_receiver(options, key, ItemNull, LMD_PROP_GET, options));
            if (item_is_error(text.get())) return text.get();
            Item stored = mvp_lmd_named_set(object.get(), "cause", text.get(), JSPD_NON_ENUMERABLE);
            if (item_is_error(stored)) return stored;
        }
    }
    if (aggregate) {
        list.set(mvp_lmd_array_new(0));
        if (item_is_error(list.get())) return list.get();
        Item absent = {.item = ITEM_JS_UNDEFINED};
        list.set(mvp_lmd_iterator_collect(program, list.get(), count ? args[0] : absent, absent, absent));
        if (item_is_error(list.get())) return list.get();
        Item stored = mvp_lmd_named_set(object.get(), "errors", list.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(stored)) return stored;
    }
    return object.get();
}

static Item error_to_string(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    if (!mvp_lmd_is_object(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, receiver), name(roots, ItemNull), message(roots, ItemNull);
    Item key = {.item = s2it(heap_create_name("name", 4))};
    name.set(mvp_lmd_property_with_receiver(owner.get(), key, ItemNull, LMD_PROP_GET, owner.get()));
    if (item_is_error(name.get())) return name.get();
    name.set(name.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_create_name("Error", 5))} :
        mvp_lmd_primitive_to_string(name.get()));
    if (item_is_error(name.get())) return name.get();
    key.item = s2it(heap_create_name("message", 7));
    message.set(mvp_lmd_property_with_receiver(owner.get(), key, ItemNull, LMD_PROP_GET, owner.get()));
    if (item_is_error(message.get())) return message.get();
    message.set(message.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_create_name("", 0))} :
        mvp_lmd_primitive_to_string(message.get()));
    if (item_is_error(message.get())) return message.get();
    if (!name.get().get_string()->len) return message.get();
    if (!message.get().get_string()->len) return name.get();
    name.set(mvp_lmd_string_concat(name.get(), Item{.item = s2it(heap_create_name(": ", 2))}));
    return item_is_error(name.get()) ? name.get() : mvp_lmd_string_concat(name.get(), message.get());
}

extern "C" Item mvp_lmd_caught_value(MvpLmdLibraryState* state, Item carrier) {
    LambdaError* error = it2err(carrier);
    if (!error || error->code == ERR_NOT_IMPLEMENTED || error->code == ERR_POOL_EXHAUSTED) return carrier;
    if (error->thrown_value_item) return Item{.item = error->thrown_value_item};
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, carrier), object(roots, ItemNull), text(roots, ItemNull);
    int kind = error->code == ERR_TYPE_MISMATCH ? LMD_ERROR_TYPE :
        error->code == ERR_UNDEFINED_VARIABLE ? LMD_ERROR_REFERENCE :
        (error->code == ERR_INDEX_OUT_OF_BOUNDS || error->code == ERR_STACK_OVERFLOW) ? LMD_ERROR_RANGE : error->code == ERR_SYNTAX_ERROR ? LMD_ERROR_SYNTAX : LMD_ERROR_BASE;
    String* host_name = get_type_id(Item{.item = error->js_name_item}) == LMD_TYPE_STRING
        ? Item{.item = error->js_name_item}.get_string() : NULL;
    const char* host_class = js_class_to_name((JsClass)error->js_class_id);
    if (host_class) for (int i = 0; i < LMD_ERROR_COUNT; i++) {
        if (!strcmp(host_class, mvp_lmd_error_names[i])) { kind = i; break; }
    }
    if (!state->errors) return carrier;
    const char* message = error->code == ERR_STACK_OVERFLOW
        ? "Maximum call stack size exceeded" : error->message ? error->message : "";
    String* copied = heap_strcpy(message, strlen(message));
    if (!copied) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    text.set(Item{.item = s2it(copied)});
    Item argument = text.get();
    MvpLmdClass* cls = &state->errors[kind];
    object.set(error_construct(context, cls->program, &argument, 1,
        mvp_lmd_class_values(cls)[0], ItemNull, Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(object.get())) return object.get();
    // shared host errors carry their JS class/name independently of the Lambda code.
    if (host_name && ((error->js_own_flags & JS_ERROR_OWN_NAME) ||
            host_name->len != strlen(mvp_lmd_error_names[kind]) ||
            memcmp(host_name->chars, mvp_lmd_error_names[kind], host_name->len))) {
        Item status = mvp_lmd_named_set(object.get(), "name", Item{.item = error->js_name_item}, JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
    }
    // Preserve Error identity if the same carrier crosses another catch boundary.
    if (!error->is_static) error->thrown_value_item = object.get().item;
    return object.get();
}

static Item json_copy(Item source, TypeMap* shape) {
    TypeId type = get_type_id(source);
    if (type == LMD_TYPE_STRING) {
        String* value = source.get_string();
        String* copied = heap_strcpy(value->chars, value->len);
        return copied ? Item{.item = s2it(copied)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY) return source;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, type == LMD_TYPE_MAP ? mvp_lmd_object_new(shape, 0) : mvp_lmd_array_new(0));
    Rooted<Item> key(roots, ItemNull), value(roots, ItemNull);
    if (item_is_error(output.get())) return output.get();
    if (type == LMD_TYPE_ARRAY) {
        for (int64_t i = 0; i < source.array->length; i++) {
            value.set(json_copy(source.array->items[i], shape));
            if (item_is_error(value.get())) return value.get();
            Item result = mvp_lmd_array_store(output.get(), i, value.get());
            if (item_is_error(result)) return result;
        }
    } else {
        FOR_EACH_MAP_FIELD((TypeMap*)source.map->type, field) {
            String* name = heap_create_name(field->name->str, field->name->length);
            if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            key.set(mvp_lmd_property_key(Item{.item = s2it(name)}));
            if (item_is_error(key.get())) return key.get();
            value.set(json_copy(map_shape_field_to_item(source.map->data, field), shape));
            if (item_is_error(value.get())) return value.get();
            Item result = mvp_lmd_property_set(output.get(), key.get(), value.get());
            if (item_is_error(result)) return result;
        }
    }
    return output.get();
}

static Item json_parse(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> text(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    Rooted<Item> reviver(roots, count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}), holder(roots, ItemNull);
    text.set(mvp_lmd_primitive_to_string(text.get()));
    if (item_is_error(text.get())) return text.get();
    String* source = text.get().get_string();
    if (memchr(source->chars, 0, source->len)) return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    Pool* pool = pool_create();
    if (!pool) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Input* input = Input::create(pool, NULL);
    bool ok = false;
    Item parsed = input ? parse_json_to_item_strict(input, source->chars, &ok, true) : ItemError;
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    Item result = ok ? json_copy(parsed, state->object_shape) : ItemError;
    // Input scalars are borrowed only until this boundary; compound stores already own their copies.
    bool number = get_type_id(result) == LMD_TYPE_FLOAT || get_type_id(result) == LMD_TYPE_INT;
    double scalar = number ? it2d(result) : 0;
    pool_destroy(pool);
    if (!ok) return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    if (item_is_error(result)) return result;
    if (!mvp_lmd_is_callable(reviver.get())) return number ? push_d(scalar) : result;
    uint64_t home = 0;
    text.set(number ? lambda_item_adopt_scalar_home(push_d(scalar), &home) : result);
    holder.set(mvp_lmd_object_new(state->object_shape, 0));
    if (item_is_error(holder.get())) return holder.get();
    Item empty = {.item = s2it(heap_create_name("", 0))};
    if (!empty.get_string()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item status = mvp_lmd_property_with_receiver(holder.get(), empty, text.get(), LMD_PROP_CREATE_DATA, holder.get());
    if (item_is_error(status)) return status;
    static const JsJsonReviveOps ops = {
        &mvp_lmd_collection_ops,
        [](Item object, Item key) { return mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, object); },
        mvp_lmd_array_is_array,
        [](Item object) { return mvp_lmd_object_project(object, 0); },
        [](Item object, Item key, Item value) {
            if (value.item == ITEM_JS_UNDEFINED)
                return mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_DELETE, object);
            JsPropertyDescriptor descriptor = {};
            descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
                JS_PD_HAS_ENUMERABLE | JS_PD_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            js_pd_set_configurable(&descriptor, true);
            descriptor.value = value;
            return mvp_lmd_define_own_property(object, key, &descriptor);
        },
        [](void*, Item, Item, Item) { return mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0); }
    };
    return js_json_revive_with_ops(holder.get(), empty, reviver.get(), NULL, &ops);
}

static Item json_value(Item input, Item key) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if ((!mvp_lmd_is_object(input) && get_type_id(input) != LMD_TYPE_DECIMAL) ||
            !state || (!state->mutable_prototypes && !state->primitives)) return input;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, input), property(roots, key), method(roots, ItemNull);
    if (state->mutable_prototypes) {
        String* name = heap_create_name("toJSON", 6);
        if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        method.set(mvp_lmd_property_with_receiver(owner.get(), Item{.item = s2it(name)}, ItemNull, LMD_PROP_GET, owner.get()));
        if (item_is_error(method.get())) return method.get();
        if (mvp_lmd_is_callable(method.get())) {
            Item argument = property.get();
            owner.set(mvp_lmd_class_invoke(method.get(), owner.get(), &argument, 1, Item{.item = ITEM_JS_UNDEFINED}));
            if (item_is_error(owner.get())) return owner.get();
        }
    }
    TypeId type = get_type_id(primitive_payload(owner.get()));
    // JSON unboxes after toJSON. Number/String retain observable conversion,
    // Boolean/BigInt use their internal payload and Symbol stays an object.
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) return mvp_lmd_primitive_to_number(owner.get());
    if (type == LMD_TYPE_STRING) return mvp_lmd_primitive_to_string(owner.get());
    if (type == LMD_TYPE_BOOL || type == LMD_TYPE_DECIMAL) return primitive_payload(owner.get());
    return owner.get();
}
static Item json_write(StrBuf* buffer, Item input, const JsonAncestor* parent, int depth) {
    if (depth > 512) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    TypeId type = get_type_id(input);
    if (type == LMD_TYPE_STRING) {
        String* text = input.get_string();
        escape_append_json_string(buffer, text->chars, text->len, true, true);
    } else if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) {
        double value = it2d(input); char bytes[64];
        if (isfinite(value)) {
            lambda_finite_double_to_shortest(value, bytes, sizeof(bytes));
            strbuf_append_str(buffer, bytes);
        } else strbuf_append_str(buffer, "null");
    } else if (type == LMD_TYPE_DECIMAL) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    else if (type == LMD_TYPE_BOOL) strbuf_append_str(buffer, input.bool_val ? "true" : "false");
    else if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_FUNC ||
            input.item == ITEM_JS_DELETED_SENTINEL) strbuf_append_str(buffer, "null");
    else if (type == LMD_TYPE_ARRAY || type == LMD_TYPE_MAP ||
            (type == LMD_TYPE_VMAP && js_module_namespace_record(input))) {
        for (const JsonAncestor* current = parent; current; current = current->parent)
            if (current->value.item == input.item) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> held(roots, input), keys(roots, ItemNull), value(roots, ItemNull);
        JsonAncestor ancestor = {input, parent};
        Item brand = mvp_lmd_array_is_array(held.get());
        if (item_is_error(brand)) return brand;
        bool array = brand.item == ITEM_TRUE, first = true;
        strbuf_append_char(buffer, array ? '[' : '{');
        if (!array) {
            keys.set(mvp_lmd_object_project(held.get(), 0));
            if (item_is_error(keys.get())) return keys.get();
        }
        Item extent = array ? mvp_lmd_sequence_length(held.get()) : Item{.item = i2it(keys.get().array->length)};
        if (item_is_error(extent)) return extent;
        int64_t length = it2i(extent);
        for (int64_t i = 0; i < length; i++) {
            Item key = array ? ItemNull : keys.get().array->items[i];
            uint64_t home = 0;
            value.set(lambda_item_adopt_scalar_home(array ? mvp_lmd_array_index_property(held.get(), i, ItemNull, LMD_PROP_GET) :
                mvp_lmd_class_property(held.get(), key, ItemNull, LMD_PROP_GET, NULL), &home));
            if (item_is_error(value.get())) return value.get();
            if (mvp_lmd_is_object(value.get()) || get_type_id(value.get()) == LMD_TYPE_DECIMAL) {
                if (array) key = mvp_lmd_number_to_string(i);
                if (item_is_error(key)) return key;
                value.set(lambda_item_adopt_scalar_home(json_value(value.get(), key), &home));
                if (item_is_error(value.get())) return value.get();
            }
            TypeId value_type = get_type_id(value.get());
            bool omitted = value_type == LMD_TYPE_UNDEFINED || value_type == LMD_TYPE_SYMBOL || mvp_lmd_is_callable(value.get());
            if (omitted && !array) continue;
            if (omitted) value.set(ItemNull);
            if (!first) strbuf_append_char(buffer, ',');
            first = false;
            if (!array) {
                String* text = key.get_string();
                escape_append_json_string(buffer, text->chars, text->len, true, true);
                strbuf_append_char(buffer, ':');
            }
            Item result = json_write(buffer, value.get(), &ancestor, depth + 1);
            if (item_is_error(result)) return result;
        }
        strbuf_append_char(buffer, array ? ']' : '}');
    } else return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return ItemNull;
}
static Item json_stringify(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if ((count > 1 && args[1].item != ITEM_JS_UNDEFINED && args[1].item != ITEM_NULL) ||
            (count > 2 && args[2].item != ITEM_JS_UNDEFINED)) {
        static const JsJsonStringifyOps ops = {
            &mvp_lmd_collection_ops,
            [](Item object, Item key) { return mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, object); },
            mvp_lmd_array_is_array,
            [](Item object) { return mvp_lmd_object_project(object, 0); },
            [](Item value) { return mvp_lmd_primitive_to_string(value); }, primitive_payload,
            []() { return mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0); },
            [](Item object, Item key, Item value) {
                return mvp_lmd_property_with_receiver(object, key, value, LMD_PROP_CREATE_DATA, object);
            },
            NULL
        };
        return js_json_stringify_with_ops(args[0], args[1],
            count > 2 ? args[2] : Item{.item = ITEM_JS_UNDEFINED}, &ops);
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    if (mvp_lmd_is_object(value.get()) || get_type_id(value.get()) == LMD_TYPE_DECIMAL) {
        String* key = heap_create_name("", 0);
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        value.set(json_value(value.get(), Item{.item = s2it(key)}));
        if (item_is_error(value.get())) return value.get();
    }
    if (get_type_id(value.get()) == LMD_TYPE_UNDEFINED || get_type_id(value.get()) == LMD_TYPE_SYMBOL || mvp_lmd_is_callable(value.get()))
        return Item{.item = ITEM_JS_UNDEFINED};
    StrBuf* buffer = strbuf_new();
    if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item result = json_write(buffer, value.get(), NULL, 0);
    if (!item_is_error(result)) {
        String* text = heap_strcpy(buffer->str, buffer->length);
        result = text ? Item{.item = s2it(text)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    strbuf_free(buffer); return result;
}

static Item date_now(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    return push_d(js_date_number_to_double(js_date_now()));
}
static const JsDateOps date_ops = {
    [](Item value) { return mvp_lmd_primitive_to_number(value); },
    js_date_get_time_value, js_date_store_time_value,
    [](bool range, const char*) { return mvp_lmd_fail(range ? LMD_MVP_RANGE : LMD_MVP_TYPE, 0); }
};

static Item date_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED) {
        double now = js_date_number_to_double(js_date_now());
        // the shared formatter needs a branded receiver; the constructor call ignores all arguments.
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> date(roots, mvp_lmd_object_new(&mvp_lmd_program_library()->date.shape, 0));
        if (item_is_error(date.get())) return date.get();
        Item stored = date_ops.store(date.get(), now);
        return item_is_error(stored) ? stored : js_date_method_with_ops(date.get(), 17, &date_ops);
    }
    RootFrame roots(10);
    if (!roots.valid()) return ItemError;
    Rooted<Item> inputs[7] = {{roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull},
        {roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull}};
    uint64_t homes[7] = {};
    for (uint64_t i = 0; i < count && i < 7; i++) inputs[i].set(lambda_item_adopt_scalar_home(args[i], &homes[i]));
    Rooted<Item> actual_target(roots, target), prototype(roots, ItemNull), object(roots, ItemNull);
    // no-argument construction follows the same host frame clock as Date.now().
    double value = count ? NAN : js_date_number_to_double(js_date_now());
    if (count == 1) {
        bool branded = false;
        Item stored = date_ops.read(inputs[0].get(), &branded);
        if (branded) value = it2d(stored);
        else {
            inputs[0].set(lambda_item_adopt_scalar_home(mvp_lmd_to_primitive(inputs[0].get(), 0), &homes[0]));
            if (item_is_error(inputs[0].get())) return inputs[0].get();
            Item converted = get_type_id(inputs[0].get()) == LMD_TYPE_STRING
                ? js_date_parse_text(inputs[0].get().get_string()) : date_ops.number(inputs[0].get());
            if (item_is_error(converted)) return converted;
            value = it2d(converted);
        }
    } else if (count > 1) {
        double parts[7] = {NAN, NAN, 1, 0, 0, 0, 0};
        for (uint64_t i = 0; i < count && i < 7; i++) {
            Item converted = date_ops.number(inputs[i].get());
            if (item_is_error(converted)) return converted;
            parts[i] = it2d(converted);
        }
        value = js_date_components_time(parts, true, true);
    }
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    prototype.set(mvp_lmd_constructor_prototype(actual_target.get(), state->date.values[1]));
    if (item_is_error(prototype.get())) return prototype.get();
    MvpLmdClass* subtype = mvp_lmd_class_record(actual_target.get());
    TypeMap* shape = subtype && subtype->ast && lambda_nominal_derives_from(&subtype->nominal, &state->date.nominal)
        ? &subtype->shape : &state->date.shape;
    object.set(mvp_lmd_object_new(shape, 0));
    if (item_is_error(object.get())) return object.get();
    Item stored = date_ops.store(object.get(), js_date_time_clip(value));
    if (item_is_error(stored)) return stored;
    Item linked = mvp_lmd_set_prototype(object.get(), prototype.get());
    return item_is_error(linked) ? linked : object.get();
}
static Item date_value(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    return js_date_method_with_ops(receiver, 0, &date_ops);
}

enum DateMethod { DATE_UTC = 60, DATE_PARSE, DATE_JSON, DATE_PRIMITIVE };
static Item date_method(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    Item absent = {.item = ITEM_JS_UNDEFINED};
    if (operation == DATE_UTC) {
        RootFrame roots(7);
        if (!roots.valid()) return ItemError;
        Rooted<Item> inputs[7] = {{roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull},
            {roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull}, {roots, ItemNull}};
        uint64_t homes[7] = {};
        for (uint64_t i = 0; i < count && i < 7; i++) inputs[i].set(lambda_item_adopt_scalar_home(args[i], &homes[i]));
        double parts[7] = {NAN, 0, 1, 0, 0, 0, 0};
        for (uint64_t i = 0; i < count && i < 7; i++) {
            Item converted = date_ops.number(inputs[i].get());
            if (item_is_error(converted)) return converted;
            parts[i] = it2d(converted);
        }
        return push_d(js_date_components_time(parts, false, true));
    }
    if (operation == DATE_PARSE) {
        Item text = mvp_lmd_primitive_to_string(count ? args[0] : absent);
        return item_is_error(text) ? text : js_date_parse_text(text.get_string());
    }
    if (operation == DATE_PRIMITIVE) {
        if (!mvp_lmd_is_object(receiver) || !count || get_type_id(args[0]) != LMD_TYPE_STRING)
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        String* hint = args[0].get_string();
        bool number = hint->len == 6 && !memcmp(hint->chars, "number", 6);
        bool string = (hint->len == 6 && !memcmp(hint->chars, "string", 6)) ||
            (hint->len == 7 && !memcmp(hint->chars, "default", 7));
        // bit 4 selects OrdinaryToPrimitive and prevents reentering this symbol method.
        return number || string ? mvp_lmd_to_primitive(receiver, 4 | (number ? 1 : 2)) : mvp_lmd_fail(LMD_MVP_TYPE, 0);
    }
    if (operation == DATE_JSON) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> owner(roots, mvp_lmd_box_primitive(receiver)), value(roots, ItemNull);
        if (item_is_error(owner.get())) return owner.get();
        value.set(mvp_lmd_to_primitive(owner.get(), 1));
        if (item_is_error(value.get())) return value.get();
        TypeId type = get_type_id(value.get());
        if ((type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) && !isfinite(it2d(value.get()))) return ItemNull;
        Item key = {.item = s2it(heap_create_name("toISOString", 11))};
        value.set(mvp_lmd_property_with_receiver(owner.get(), key, ItemNull, LMD_PROP_GET, owner.get()));
        return item_is_error(value.get()) ? value.get() : mvp_lmd_class_invoke(value.get(), owner.get(), NULL, 0, absent);
    }
    // the native arity is immutable; surplus setter arguments are not converted.
    if (count > self.function->arity) count = self.function->arity;
    return operation < 20 ? js_date_method_with_ops(receiver, operation, &date_ops)
        : js_date_setter_with_ops(receiver, operation, count ? args[0] : absent,
            count > 1 ? args[1] : ItemError, count > 2 ? args[2] : ItemError,
            count > 3 ? args[3] : ItemError, &date_ops);
}

Item mvp_lmd_date_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->date_observed) return ItemNull;
    ((MvpLmdCallable*)state->date.values[0].function)->custom_new_target = true;
    Item result = mvp_lmd_named_set(state->date.values[1], "constructor", state->date.values[0], JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    const struct { const char* name; int operation; uint8_t arity; bool constructor; } methods[] = {
        {"UTC", DATE_UTC, 7, true}, {"parse", DATE_PARSE, 1, true},
        {"getTime", 0, 0}, {"valueOf", 0, 0}, {"getFullYear", 1, 0}, {"getMonth", 2, 0},
        {"getDate", 3, 0}, {"getHours", 4, 0}, {"getMinutes", 5, 0}, {"getSeconds", 6, 0},
        {"getMilliseconds", 7, 0}, {"toISOString", 8, 0}, {"toLocaleDateString", 9, 0},
        {"getUTCFullYear", 10, 0}, {"getUTCMonth", 11, 0}, {"getUTCDate", 12, 0},
        {"getUTCHours", 13, 0}, {"getUTCMinutes", 14, 0}, {"getUTCSeconds", 15, 0},
        {"getUTCMilliseconds", 16, 0}, {"toString", 17, 0}, {"toLocaleString", 17, 0},
        {"getDay", 40, 0}, {"getUTCDay", 41, 0}, {"getTimezoneOffset", 42, 0},
        {"toJSON", DATE_JSON, 1}, {"toUTCString", 45, 0}, {"toDateString", 46, 0},
        {"toTimeString", 47, 0}, {"toLocaleTimeString", 47, 0}, {"getYear", 50, 0}, {"setYear", 51, 1},
        {"setTime", 20, 1}, {"setFullYear", 21, 3}, {"setMonth", 22, 2}, {"setDate", 23, 1},
        {"setHours", 24, 4}, {"setMinutes", 25, 3}, {"setSeconds", 26, 2}, {"setMilliseconds", 27, 1},
        {"setUTCFullYear", 30, 3}, {"setUTCMonth", 31, 2}, {"setUTCDate", 32, 1},
        {"setUTCHours", 33, 4}, {"setUTCMinutes", 34, 3}, {"setUTCSeconds", 35, 2}, {"setUTCMilliseconds", 36, 1}
    };
    for (const auto& method : methods) {
        Item owner = state->date.values[method.constructor ? 0 : 1];
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> function(roots, mvp_lmd_native_function(program,
            (void*)(intptr_t)method.operation, date_method, method.arity, method.name));
        if (item_is_error(function.get())) return function.get();
        result = mvp_lmd_named_set(owner, method.name, function.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        if (method.operation == 45) {
            result = mvp_lmd_named_set(owner, "toGMTString", function.get(), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, mvp_lmd_native_function(program, (void*)(intptr_t)DATE_PRIMITIVE,
        date_method, 1, "[Symbol.toPrimitive]"));
    if (item_is_error(function.get())) return function.get();
    JsPropertyDescriptor descriptor = {};
    descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    descriptor.value = function.get();
    js_pd_set_configurable(&descriptor, true);
    return mvp_lmd_define_own_property(state->date.values[1],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_PRIMITIVE))}, &descriptor);
}

Item mvp_lmd_library_class_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state,
        MvpLmdClass* cls, int index, const char* name, MvpLmdNativeEntry entry, uint8_t arity,
        bool immutable_metadata) {
    cls->values = state->values + index;
    cls->values[0] = mvp_lmd_native_function(program, state, entry, arity);
    if (item_is_error(cls->values[0])) return cls->values[0];
    MvpLmdCallable* ctor = (MvpLmdCallable*)cls->values[0].function;
    ctor->native_constructor = true; ctor->properties = cls;
    TypeMap* shapes[] = {&cls->prototype_shape, &cls->static_shape};
    for (int i = 0; i < 2; i++) {
        bool array_prototype = cls == &state->array && i == 0;
        cls->values[i + 1] = array_prototype ? mvp_lmd_array_new(0) : mvp_lmd_object_new(shapes[i], 0);
        if (item_is_error(cls->values[i + 1])) return cls->values[i + 1];
        if (array_prototype) {
            cls->values[1].array->type = shapes[i];
            if (!map_fill_reserve_data((Map*)cls->values[1].array)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            if (state->default_realm) {
                Item linked = mvp_lmd_set_prototype(cls->values[1], state->values[LMD_LIBRARY_OBJECT_PROTOTYPE]);
                if (item_is_error(linked)) return linked;
            }
        }
    }
    Item result = mvp_lmd_named_set(cls->values[1], "constructor", cls->values[0]);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(cls->values[2], "prototype", cls->values[1],
        immutable_metadata ? JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE : -1);
    if (item_is_error(result)) return result;
    String* spelling = heap_create_name(name, strlen(name));
    if (!spelling) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    ((MvpLmdNativeCallable*)cls->values[0].function)->source_name = spelling;
    int flags = immutable_metadata ? JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE : -1;
    result = mvp_lmd_named_set(cls->values[2], "name", Item{.item = s2it(spelling)}, flags);
    return item_is_error(result) ? result : mvp_lmd_named_set(cls->values[2], "length", Item{.item = i2it(arity)}, flags);
}
static Item object_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    // Object with a different newTarget ignores its value argument entirely.
    if (target.item != ITEM_JS_UNDEFINED && target.item != self.item) {
        MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
        return mvp_lmd_object_from_constructor(target, &state->object);
    }
    Item value = count ? args[0] : ItemNull;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
        return mvp_lmd_object_new(state->object_shape, 0);
    }
    return mvp_lmd_is_object(value) ? value : mvp_lmd_box_primitive(value);
}
Item mvp_lmd_object_string(Item receiver) {
    TypeId type = get_type_id(receiver);
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    const char* tag = type == LMD_TYPE_NULL ? "[object Null]" : type == LMD_TYPE_UNDEFINED ? "[object Undefined]" :
        type == LMD_TYPE_ARRAY ? "[object Array]" : type == LMD_TYPE_STRING ? "[object String]" :
        type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? "[object Number]" : type == LMD_TYPE_BOOL ? "[object Boolean]" :
        type == LMD_TYPE_FUNC ? "[object Function]" : type == LMD_TYPE_DECIMAL ? "[object BigInt]" :
        type == LMD_TYPE_SYMBOL ? "[object Symbol]" : "[object Object]";
    if (type == LMD_TYPE_MAP) {
        TypeNominal* nominal = ((TypeMap*)receiver.map->type)->nominal;
        bool date = false;
        date_ops.read(receiver, &date);
        if (date) tag = "[object Date]";
        else if (mvp_lmd_regexp_record(receiver, state)) tag = "[object RegExp]";
        else if (state->error_data_key && typemap_hash_lookup_key((TypeMap*)receiver.map->type, state->error_data_key))
            tag = "[object Error]";
        if (state->arguments_key && typemap_hash_lookup_key((TypeMap*)receiver.map->type, state->arguments_key))
            tag = "[object Arguments]";
        TypeId primitive = get_type_id(primitive_payload(receiver));
        if (primitive != LMD_TYPE_UNDEFINED)
            tag = primitive == LMD_TYPE_STRING ? "[object String]" : primitive == LMD_TYPE_BOOL ? "[object Boolean]" :
                primitive == LMD_TYPE_DECIMAL ? "[object BigInt]" : primitive == LMD_TYPE_SYMBOL ? "[object Symbol]" : "[object Number]";
    }
    if (state->mutable_prototypes && mvp_lmd_is_object(receiver)) {
        Item array = mvp_lmd_array_is_array(receiver);
        if (item_is_error(array)) return array;
        if (array.item == ITEM_TRUE) tag = "[object Array]";
        else if (mvp_lmd_is_callable(receiver)) tag = "[object Function]";
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))};
        // semantic lookup owns its receiver roots; no receiver state remains live after it returns.
        Item value = mvp_lmd_property_with_receiver(receiver, key, ItemNull, LMD_PROP_GET, receiver);
        if (item_is_error(value)) return value;
        if (get_type_id(value) == LMD_TYPE_STRING) {
            StrBuf* buffer = strbuf_new();
            if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            strbuf_append_str(buffer, "[object ");
            // StrBuf uses native allocation: finish copying the borrowed tag before the GC allocation.
            strbuf_append_str_n(buffer, value.get_string()->chars, value.get_string()->len);
            strbuf_append_char(buffer, ']');
            String* result = heap_strcpy(buffer->str, buffer->length); strbuf_free(buffer);
            return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
    }
    String* result = heap_strcpy(tag, strlen(tag));
    return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Item object_own(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item receiver, Item) {
    if (count && get_type_id(args[0]) == LMD_TYPE_STRING && get_type_id(receiver) == LMD_TYPE_MAP &&
            receiver.map->map_kind != MAP_KIND_PROXY &&
            utf8_key_is_canonical(args[0].get_string()->chars, args[0].get_string()->len)) {
        // a canonical string and passive own-map lookup need no coercion roots or guest dispatch.
        return mvp_lmd_property_has(receiver, args[0], 0);
    }
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, receiver), key(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    key.set(mvp_lmd_property_key(key.get()));
    if (item_is_error(key.get())) return key.get();
    // hasOwnProperty converts its key before ToObject, unlike Object.hasOwn.
    owner.set(mvp_lmd_box_primitive(owner.get()));
    if (item_is_error(owner.get())) return owner.get();
    return mvp_lmd_class_property(owner.get(), key.get(), ItemNull, LMD_PROP_OWN, NULL);
}
Item mvp_lmd_object_value(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    return mvp_lmd_is_object(receiver) ? receiver : mvp_lmd_box_primitive(receiver);
}
static Item object_predicate(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (operation >= 2) {
        static const JsObjectAccessorOps ops = {
            &mvp_lmd_collection_ops,
            [](Item value) { return mvp_lmd_box_primitive(value); },
            mvp_lmd_property_key,
            [](Item object, Item key, Item callback, bool getter) {
                JsPropertyDescriptor descriptor = {};
                descriptor.flags = JS_PD_HAS_ENUMERABLE | JS_PD_ENUMERABLE | JS_PD_HAS_CONFIGURABLE |
                    (getter ? JS_PD_HAS_GET : JS_PD_HAS_SET);
                js_pd_set_configurable(&descriptor, true);
                if (getter) descriptor.getter = callback; else descriptor.setter = callback;
                Item status = mvp_lmd_define_own_property(object, key, &descriptor);
                return status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : status;
            },
            [](Item object, Item key, bool getter, bool* found) {
                JsPropertyDescriptor descriptor = {};
                Item status = mvp_lmd_get_own_property(object, key, &descriptor);
                if (item_is_error(status)) return status;
                *found = status.item == ITEM_TRUE;
                return *found && (descriptor.flags & (getter ? JS_PD_HAS_GET : JS_PD_HAS_SET)) ?
                    (getter ? descriptor.getter : descriptor.setter) : Item{.item = ITEM_JS_UNDEFINED};
            }, mvp_lmd_get_prototype
        };
        return js_object_legacy_accessor_with_ops(receiver, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
            count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, operation < 4, !(operation & 1), &ops);
    }
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, receiver), value(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    if (operation == 1) {
        value.set(mvp_lmd_property_key(value.get()));
        if (item_is_error(value.get())) return value.get();
        object.set(mvp_lmd_box_primitive(object.get()));
        if (item_is_error(object.get())) return object.get();
        JsPropertyDescriptor descriptor = {};
        Item status = mvp_lmd_get_own_property(object.get(), value.get(), &descriptor);
        return item_is_error(status) ? status : Item{.item = b2it(status.item == ITEM_TRUE && (descriptor.flags & JS_PD_ENUMERABLE))};
    }
    if (!mvp_lmd_is_object(value.get())) return Item{.item = ITEM_FALSE};
    object.set(mvp_lmd_box_primitive(object.get()));
    if (item_is_error(object.get())) return object.get();
    while (value.get().item != ITEM_NULL) {
        value.set(mvp_lmd_get_prototype(value.get()));
        if (item_is_error(value.get())) return value.get();
        if (value.get().item == object.get().item) return Item{.item = ITEM_TRUE};
    }
    return Item{.item = ITEM_FALSE};
}
Item mvp_lmd_array_is_array(Item value) {
    while (mvp_lmd_is_proxy(value)) {
        JsProxyData* data = js_get_proxy_data(value);
        if (!data || data->revoked) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        value.item = data->target;
    }
    return Item{.item = b2it(get_type_id(value) == LMD_TYPE_ARRAY)};
}
static Item array_is_array(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    return mvp_lmd_array_is_array(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
}
static Item object_define(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    // reject the target before any user-observable key or descriptor conversion.
    if (!count || !mvp_lmd_is_object(args[0])) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]), descriptor(roots, count > 2 ? args[2] : Item{.item = ITEM_JS_UNDEFINED});
    Rooted<Item> key(roots, mvp_lmd_property_key(count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}));
    return item_is_error(key.get()) ? key.get() : mvp_lmd_define_property(owner.get(), key.get(), descriptor.get());
}
static Item object_define_properties(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    return count < 2 ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : mvp_lmd_define_properties(args[0], args[1]);
}
static Item object_descriptor(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if (!count) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]);
    Rooted<Item> key(roots, count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED});
    owner.set(mvp_lmd_box_primitive(owner.get()));
    if (item_is_error(owner.get())) return owner.get();
    key.set(mvp_lmd_property_key(key.get()));
    return item_is_error(key.get()) ? key.get() : mvp_lmd_get_own_descriptor(owner.get(), key.get());
}
static Item object_projection(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (!count || args[0].item == ITEM_NULL || args[0].item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]), keys(roots, ItemNull), result(roots, ItemNull), value(roots, ItemNull);
    owner.set(mvp_lmd_box_primitive(owner.get()));
    if (item_is_error(owner.get())) return owner.get();
    if (operation == 3) {
        keys.set(mvp_lmd_property_key(count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}));
        return item_is_error(keys.get()) ? keys.get() : mvp_lmd_class_property(owner.get(), keys.get(), ItemNull, LMD_PROP_OWN, NULL);
    }
    if (get_type_id(owner.get()) == LMD_TYPE_FUNC) {
        Item status = mvp_lmd_function_prepare(owner.get());
        if (item_is_error(status)) return status;
    }
    if (operation < 3 || operation == 4) return mvp_lmd_object_project(owner.get(), operation == 4 ? 3 : operation);
    keys.set(mvp_lmd_object_project(owner.get(), 5));
    if (item_is_error(keys.get())) return keys.get();
    result.set(operation == 5 ? mvp_lmd_array_new(0) : mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0));
    if (item_is_error(result.get())) return result.get();
    for (int64_t i = 0; i < keys.get().array->length; i++) {
        Item key = keys.get().array->items[i];
        if (operation == 5) {
            if (!property_key_requires_identity(key.get_string())) continue;
            value.set(mvp_lmd_property_symbol(key.get_string()));
            if (item_is_error(value.get())) return value.get();
            Item status = mvp_lmd_array_store(result.get(), result.get().array->length, value.get());
            if (item_is_error(status)) return status;
        } else {
            value.set(mvp_lmd_get_own_descriptor(owner.get(), key));
            if (item_is_error(value.get())) return value.get();
            if (value.get().item == ITEM_JS_UNDEFINED) continue;
            Item status = mvp_lmd_property_set(result.get(), key, value.get());
            if (item_is_error(status)) return status;
        }
    }
    return result.get();
}
static Item collection_group_by(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    const JsBuiltinMethodSpec* method = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    if (method->builtin_id == JS_BUILTIN_OBJECT_FROM_ENTRIES) {
        static const JsGroupByOps properties = {
            NULL,
            [](Item object, Item key, Item value) { return mvp_lmd_property_with_receiver(object, key, value, LMD_PROP_CREATE_DATA, object); },
            mvp_lmd_property_key,
            []() { return mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0); },
            NULL, NULL
        };
        return js_object_from_entries_with_ops(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
            &properties, &mvp_lmd_collection_ops);
    }
    static const JsGroupByOps map_groups = {
        [](Item output, Item key) {
            Item value = mvp_lmd_collection_storage_ops.get(output, key);
            return value.item == ITEM_JS_UNDEFINED ? ItemNull : value;
        },
        [](Item output, Item key, Item group) { return mvp_lmd_collection_storage_ops.put(output, key, group); },
        [](Item key) { return key; },
        []() { return mvp_lmd_collection_storage_ops.create(0); },
        []() { return mvp_lmd_array_new(0); },
        [](Item array, Item value) { return mvp_lmd_array_store(array, array.array->length, value); }
    };
    static const JsGroupByOps object_groups = {
        [](Item output, Item key) {
            Item value = mvp_lmd_property_get(output, key, 0);
            return value.item == ITEM_JS_UNDEFINED ? ItemNull : value;
        },
        [](Item output, Item key, Item group) {
            return mvp_lmd_property_with_receiver(output, key, group, LMD_PROP_CREATE_DATA, output);
        },
        mvp_lmd_property_key,
        []() { return mvp_lmd_object_create(mvp_lmd_program_library()->object.program, ItemNull); },
        []() { return mvp_lmd_array_new(0); },
        [](Item array, Item value) { return mvp_lmd_array_store(array, array.array->length, value); }
    };
    return js_group_by_with_ops(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
        count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED},
        method->builtin_id == JS_BUILTIN_MAP_GROUP_BY ? &map_groups : &object_groups, &mvp_lmd_collection_ops);
}

static Item object_same_value(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    Item absent = {.item = ITEM_JS_UNDEFINED};
    return Item{.item = b2it(mvp_lmd_same_value(count ? args[0] : absent, count > 1 ? args[1] : absent))};
}
static Item object_integrity(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    Item owner = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    // native Promise carriers participate in the same Object integrity operations.
    if (!mvp_lmd_is_object(owner)) return operation < 3 ? owner : Item{.item = b2it(operation != 3)};
    if (operation == 0 || operation == 3) {
        Item status = mvp_lmd_object_extensible(owner, operation == 0);
        return item_is_error(status) || operation == 3 ? status :
            status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : owner;
    }
    return mvp_lmd_object_integrity(owner, operation - (operation < 3 ? 1 : 2));
}
static Item object_create(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item, Item, Item) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, mvp_lmd_object_create(program, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(object.get()) || count < 2 || args[1].item == ITEM_JS_UNDEFINED) return object.get();
    return mvp_lmd_define_properties(object.get(), args[1]);
}
static Item object_assign(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if (!count) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, args[0]);
    target.set(mvp_lmd_box_primitive(target.get()));
    if (item_is_error(target.get())) return target.get();
    for (uint64_t i = 1; i < count; i++) {
        Item result = mvp_lmd_object_copy(target.get(), args[i], 1);
        if (item_is_error(result)) return result;
    }
    return target.get();
}
static Item object_prototype(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    Item object = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    if (!mvp_lmd_is_object(object)) object = mvp_lmd_box_primitive(object);
    return item_is_error(object) ? object : mvp_lmd_get_prototype(object);
}
static Item object_set_prototype(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if (count < 2) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (args[0].item == ITEM_NULL || args[0].item == ITEM_JS_UNDEFINED ||
            (args[1].item != ITEM_NULL && !mvp_lmd_is_object(args[1]))) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (!mvp_lmd_is_object(args[0])) return args[0];
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]);
    Item status = mvp_lmd_set_prototype(owner.get(), args[1]);
    return item_is_error(status) ? status : status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : owner.get();
}
static Item object_proto_accessor(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    bool setter = (intptr_t)((MvpLmdNativeCallable*)self.function)->state != 0;
    if (!setter) {
        Item object = mvp_lmd_box_primitive(receiver);
        return item_is_error(object) ? object : mvp_lmd_get_prototype(object);
    }
    if (receiver.item == ITEM_NULL || receiver.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item prototype = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    if ((prototype.item != ITEM_NULL && !mvp_lmd_is_object(prototype)) || !mvp_lmd_is_object(receiver))
        return Item{.item = ITEM_JS_UNDEFINED};
    Item result = mvp_lmd_set_prototype(receiver, prototype);
    return item_is_error(result) ? result : result.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : Item{.item = ITEM_JS_UNDEFINED};
}

static Item array_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> prototype(roots, ItemNull), output(roots, ItemNull);
    bool observed = target.item != ITEM_JS_UNDEFINED && target.item != self.item;
    if (observed) {
        prototype.set(mvp_lmd_constructor_prototype(target, mvp_lmd_program_library()->array.values[1]));
        if (item_is_error(prototype.get())) return prototype.get();
    }
    bool extent = count == 1 && (get_type_id(args[0]) == LMD_TYPE_INT || get_type_id(args[0]) == LMD_TYPE_FLOAT);
    double length = extent ? it2d(args[0]) : 0;
    if (extent && !(length >= 0 && length <= UINT32_MAX && length == trunc(length)))
        return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    output.set(mvp_lmd_array_new((int64_t)length));
    if (item_is_error(output.get())) return output.get();
    if (observed) {
        Item linked = mvp_lmd_set_prototype(output.get(), prototype.get());
        if (item_is_error(linked)) return linked;
    }
    for (uint64_t i = 0; !extent && i < count; i++) {
        Item stored = mvp_lmd_array_store(output.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    return output.get();
}
static Item array_method(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int method = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    return mvp_lmd_array_visit_method(method) ? mvp_lmd_array_visit(receiver, args, count, method) :
        mvp_lmd_array_edit(receiver, args, count, method);
}
static Item array_of(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item receiver, Item) {
    return js_array_of_with_ops(receiver, args, count, &mvp_lmd_array_ops);
}
static Item array_from(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item, Item receiver, Item) {
    Item absent = {.item = ITEM_JS_UNDEFINED};
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, count ? args[0] : absent), mapper(roots, count > 1 ? args[1] : absent),
        self(roots, count > 2 ? args[2] : absent), constructor(roots, receiver),
        method(roots, ItemNull), target(roots, ItemNull);
    if (mapper.get().item != ITEM_JS_UNDEFINED && !mvp_lmd_is_callable(mapper.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    method.set(mvp_lmd_iterator_method(program, source.get()));
    if (item_is_error(method.get())) return method.get();
    bool iterable = method.get().item != ITEM_JS_UNDEFINED;
    int64_t length = -1;
    if (!iterable) {
        if (!mvp_lmd_is_object(source.get())) source.set(mvp_lmd_box_primitive(source.get()));
        if (item_is_error(source.get())) return source.get();
        Item extent = mvp_lmd_sequence_length(source.get());
        if (item_is_error(extent)) return extent;
        length = it2i(extent);
    }
    // Array.from constructs before iteration, and after length conversion for array-like inputs.
    Item argument = {.item = i2it(iterable ? 0 : length)};
    target.set(mvp_lmd_is_constructor(constructor.get()) ?
        mvp_lmd_class_invoke(constructor.get(), absent, &argument, iterable ? 0 : 1, constructor.get()) :
        mvp_lmd_array_new(iterable ? 0 : length));
    if (item_is_error(target.get())) return target.get();
    return mvp_lmd_iterator_collect_with_method(program, target.get(), source.get(), mapper.get(),
        self.get(), method.get(), length, true);
}
extern "C" Item mvp_lmd_array_method_value(MvpLmdProgram* program, Item owner, Item name, int64_t callee) {
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    if (get_type_id(owner) == LMD_TYPE_STRING) return property_key_id(name.get_string()) == JS_SYMBOL_ITERATOR && state
        ? state->values[LMD_LIBRARY_SEQUENCE_ITERATOR] : mvp_lmd_property_get(owner, name, callee);
    return mvp_lmd_array_property(owner, name, ItemNull, callee ? LMD_PROP_CALLEE : LMD_PROP_GET, owner);
}
extern "C" Item mvp_lmd_property_read(MvpLmdProgram* program, Item owner, Item name, int64_t callee) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, lambda_item_adopt_scalar_home(owner, &home)), key(roots, name);
    TypeId type = get_type_id(object.get());
    Item result;
    if (type == LMD_TYPE_ARRAY || type == LMD_TYPE_STRING) {
        result = mvp_lmd_array_method_value(program, object.get(), key.get(), callee);
    } else if (type == LMD_TYPE_ARRAY_NUM) {
        int64_t index = mvp_lmd_string_key(key.get().get_string(), true);
        if (index == -1) return Item{.item = i2it(object.get().array_num->length)};
        if (index == -4 && callee) return Item{.item = mvp_lmd_method_token(9)};
        if (index == -2 || index == -4) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if ((uint64_t)index >= (uint64_t)object.get().array_num->length)
            return Item{.item = ITEM_JS_UNDEFINED};
        // keep the legacy admitted lanes and JS bounds while Lambda owns numeric storage reads.
        ArrayNumElemType lane = object.get().array_num->get_elem_type();
        if (lane != ELEM_INT32 && lane != ELEM_UINT8 && lane != ELEM_FLOAT64)
            return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        result = array_num_get(object.get().array_num, index);
        // the Lambda sized integer tag must widen to the existing JS Number lane.
        if (get_type_id(result) == LMD_TYPE_NUM_SIZED)
            result.item = i2it(lambda_num_sized_item_to_lane(result.item));
    } else {
        result = mvp_lmd_class_property(object.get(), key.get(), ItemNull,
            callee ? LMD_PROP_CALLEE : LMD_PROP_GET, NULL);
    }
    // a getter may return the primitive receiver's C-local home; publish it before roots unwind.
    return js_retain_scalar(result);
}
Item mvp_lmd_property_symbol(NameRef name) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Array* symbols = state->values[LMD_LIBRARY_SYMBOL_VALUES].array;
    for (int64_t i = 0; i < symbols->length; i++) {
        Item symbol = symbols->items[i];
        if (symbol.get_symbol()->name_id == name_ref_id(name)) return symbol;
    }
    Item registered;
    return js_symbol_from_name_id(name_ref_id(name), &registered) ? registered : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
}
static Item symbol_create(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    NameRef name;
    SymbolKind kind;
    if (self.item == ITEM_JS_UNDEFINED) {
        name = well_known_name_ref(count ? (NameId)it2i(args[0]) : JS_SYMBOL_ITERATOR); kind = SYMBOL_JS_WELL_KNOWN;
    } else {
        Item description = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
        bool absent = description.item == ITEM_JS_UNDEFINED;
        if (!absent) description = mvp_lmd_primitive_to_string(description);
        if (item_is_error(description)) return description;
        name = name_pool_create_unique_symbol(context->name_pool, absent ? StrView{"", 0} :
            StrView{description.get_string()->chars, description.get_string()->len});
        kind = absent ? SYMBOL_JS_UNIQUE_UNDESCRIBED : SYMBOL_JS_UNIQUE;
    }
    if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // The pooled identity and description remain stable across managed allocation.
    Symbol* symbol = (Symbol*)heap_calloc(sizeof(Symbol) + name->len + 1, LMD_TYPE_SYMBOL);
    if (!symbol) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    symbol->len = name->len; symbol->kind = kind; symbol->name_id = name_ref_id(name);
    memcpy(symbol->chars, name->chars, name->len);
    Item value = {.item = y2it(symbol)};
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    return mvp_lmd_array_store(state->values[LMD_LIBRARY_SYMBOL_VALUES],
        state->values[LMD_LIBRARY_SYMBOL_VALUES].array->length, value);
}
static Item symbol_description(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, receiver);
    if (get_type_id(value.get()) != LMD_TYPE_SYMBOL) value.set(primitive_payload(value.get()));
    // only the unforgeable wrapper slot supplies SymbolData; inherited/proxy slots do not.
    return get_type_id(value.get()) == LMD_TYPE_SYMBOL ? js_symbol_get_description(value.get())
        : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
static Item symbol_registry_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(count ? args[0] :
        Item{.item = ITEM_JS_UNDEFINED}, &home));
    // Registered symbols reuse the realm's Input-owned symbols and indexes;
    // ordinary MVP symbols do not initialize a second registry or runtime.
    if ((!js_active_runtime_state && !js_runtime_state_init((EvalContext*)context)) ||
            !js_runtime_state_ensure_input((EvalContext*)context)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    static const JsSymbolOps ops = {
        [](Item value) { return mvp_lmd_primitive_to_string(value); },
        [](const char*) { return mvp_lmd_fail(LMD_MVP_TYPE, 0); }
    };
    const JsBuiltinMethodSpec* method = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    return method->builtin_id == JS_BUILTIN_SYMBOL_FOR ? js_symbol_for_with_ops(argument.get(), &ops)
        : js_symbol_key_for_with_ops(argument.get(), &ops);
}
const JsBufferViewOps mvp_lmd_buffer_ops = {
    [](Item value) { return mvp_lmd_primitive_to_number(value); },
    [](Item value, Item* output) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> primitive(roots, mvp_lmd_to_primitive(value, 1));
        if (item_is_error(primitive.get())) return primitive.get();
        TypeId type = get_type_id(primitive.get());
        if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        Item arg = primitive.get();
        *output = primitive_construct(context, NULL, &arg, 1,
            state->primitives[LMD_PRIMITIVE_BIGINT].values[0], ItemNull, Item{.item = ITEM_JS_UNDEFINED});
        return item_is_error(*output) ? *output : ItemNull;
    },
    [](Item owner, const char* name) {
        String* key = heap_create_name(name, strlen(name));
        return key ? mvp_lmd_property_with_receiver(owner, Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, owner) : ItemError;
    },
    [](Item target, JsBufferViewKind kind) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        MvpLmdClass* base = kind == JS_BUFFER_VIEW_SHARED_ARRAY_BUFFER ? state->shared_buffer : &state->buffers[kind];
        return mvp_lmd_constructor_prototype(target, base->values[1]);
    },
    [](Item owner, Item prototype) { return mvp_lmd_set_prototype(owner, prototype); },
    [](bool range, const char*) { return mvp_lmd_fail(range ? LMD_MVP_RANGE : LMD_MVP_TYPE, 0); },
    [](Item value) { return !!mvp_lmd_truthy(value); },
    mvp_lmd_is_object,
    [](JsBufferViewKind kind, Item target) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        MvpLmdClass* base = kind == JS_BUFFER_VIEW_SHARED_ARRAY_BUFFER ? state->shared_buffer : &state->buffers[kind];
        MvpLmdClass* actual = mvp_lmd_class_record(target);
        // a known declared subtype retains Lambda ancestry; the physical carrier supplies the JS brand.
        return actual && actual->ast && lambda_nominal_derives_from(&actual->nominal, &base->nominal)
            ? &actual->shape : &base->shape;
    },
    [](Item owner) { return mvp_lmd_property_with_receiver(owner,
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))}, ItemNull, LMD_PROP_GET, owner); },
    [](Item constructor, int length, bool shared) {
        if (constructor.item == ITEM_JS_UNDEFINED) {
            MvpLmdLibraryState* state = mvp_lmd_program_library();
            constructor = shared ? state->shared_buffer->values[0] : state->buffers[0].values[0];
        }
        if (!mvp_lmd_is_constructor(constructor)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        Item argument = {.item = i2it(length)};
        return mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, &argument, 1, constructor);
    }
};

static const JsTypedArrayOps typed_array_ops = {
    &mvp_lmd_buffer_ops,
    [](JsTypedArrayType type, Item target) {
        MvpLmdClass* base = &mvp_lmd_program_library()->typed_arrays[type];
        MvpLmdClass* actual = mvp_lmd_class_record(target);
        return actual && actual->ast && lambda_nominal_derives_from(&actual->nominal, &base->nominal)
            ? &actual->shape : &base->shape;
    },
    [](JsTypedArrayType type, Item target) {
        return mvp_lmd_constructor_prototype(target, mvp_lmd_program_library()->typed_arrays[type].values[1]);
    },
    [](Item source, int64_t index) { return mvp_lmd_array_index_property(source, index, ItemNull, LMD_PROP_GET); },
    [](Item source) {
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> owner(roots, source), method(roots, ItemNull), output(roots, ItemNull);
        method.set(mvp_lmd_iterator_method(NULL, owner.get()));
        if (item_is_error(method.get())) return method.get();
        if (method.get().item == ITEM_JS_UNDEFINED) return method.get();
        output.set(mvp_lmd_array_new(0));
        if (item_is_error(output.get())) return output.get();
        return mvp_lmd_iterator_collect_with_method(NULL, output.get(), owner.get(),
            Item{.item = ITEM_JS_UNDEFINED}, Item{.item = ITEM_JS_UNDEFINED}, method.get());
    },
    [](JsTypedArrayType type, Item constructor, Item* args, int count) {
        if (constructor.item == ITEM_JS_UNDEFINED)
            constructor = mvp_lmd_program_library()->typed_arrays[type].values[0];
        if (!mvp_lmd_is_constructor(constructor)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, args, count, constructor);
    },
    mvp_lmd_is_callable,
    [](Item callback, Item receiver, Item* args, int count) {
        return mvp_lmd_class_invoke(callback, receiver, args, count, Item{.item = ITEM_JS_UNDEFINED});
    },
    mvp_lmd_is_constructor,
    [](Item value) { return mvp_lmd_primitive_to_string(value); }
};
Item mvp_lmd_structured_clone(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    // reuse the shared clone traversal; only allocation and property semantics belong to the realm.
    static const JsStructuredCloneOps ops = {
        &mvp_lmd_array_ops, &typed_array_ops,
        []() { return mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0); },
        [](Item object) { return mvp_lmd_object_project(object, 0); },
        [](Item object, Item key) {
            return mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, object);
        },
        [](Item object, Item key, Item value) {
            return mvp_lmd_property_with_receiver(object, key, value, LMD_PROP_CREATE_DATA, object);
        }
    };
    return js_structured_clone_with_ops(count ? args[0] : make_js_undefined(), &ops);
}
static Item object_locale(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    return js_object_to_locale_string_with_ops(receiver, NULL, &typed_array_ops);
}
static Item array_locale(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, receiver), length(roots, ItemNull);
    if (!mvp_lmd_is_object(owner.get())) owner.set(mvp_lmd_box_primitive(owner.get()));
    if (item_is_error(owner.get())) return owner.get();
    // LengthOfArrayLike runs once, before any element locale callback.
    length.set(mvp_lmd_sequence_length(owner.get()));
    if (item_is_error(length.get())) return length.get();
    return js_array_like_join_kernel(owner.get(), false, it2i(length.get()), ",", 1, true, &typed_array_ops);
}
Item mvp_lmd_typed_array_store(Item owner, double index, Item value) {
    return js_typed_array_set_numeric_with_ops(owner, index, index == 0 && signbit(index), value, &mvp_lmd_buffer_ops);
}
static Item typed_array_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item target) {
    MvpLmdClass* cls = mvp_lmd_class_record(self);
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (target.item == ITEM_JS_UNDEFINED || cls == &state->typed_arrays[12]) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item absent = {.item = ITEM_JS_UNDEFINED};
    return js_typed_array_construct_with_ops((int)(cls - state->typed_arrays), count ? args[0] : absent,
        count > 1 ? args[1] : absent, count > 2 ? args[2] : absent, count, target, &typed_array_ops);
}
static Item typed_array_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (operation == 35 || operation == 36)
        return js_typed_array_static_with_ops(receiver, args, count, operation == 35, &typed_array_ops);
    JsTypedArray* array = get_type_id(receiver) == LMD_TYPE_MAP ? js_get_typed_array_ptr(receiver.map) : NULL;
    if (!array) return operation == 4 ? Item{.item = ITEM_JS_UNDEFINED} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (operation == 0) return Item{.item = array->base.buffer_item};
    if (operation == 1) return Item{.item = i2it(js_typed_array_byte_length(receiver))};
    if (operation == 2) return Item{.item = i2it(js_typed_array_byte_offset(receiver))};
    if (operation == 3) return Item{.item = i2it(js_typed_array_length(receiver))};
    if (operation == 4) {
        const char* name = js_typed_array_spec(array->element_type)->name;
        return Item{.item = s2it(heap_create_name(name, strlen(name)))};
    }
    if (operation == 8) return js_typed_array_subarray(receiver,
        count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
        count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, &typed_array_ops);
    if (operation == 33 || operation == 34)
        return js_typed_array_string_with_ops(receiver, args, count, operation == 34, &typed_array_ops);
    if (operation >= 20) return js_typed_array_method_with_ops(receiver, args, count,
        false, (JsTypedArrayMethodKind)(operation - 20), &typed_array_ops);
    if (operation >= 9) return js_typed_array_callback_algorithm(receiver, args, count,
        false, (JsTypedArrayCallbackKind)(operation - 9), &typed_array_ops);
    if (js_typed_array_is_out_of_bounds_item(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return mvp_lmd_sequence_iterator_create(program, receiver, operation - 5, false, false);
}
Item mvp_lmd_typed_array_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->typed_arrays) return ItemNull;
    // the abstract constructor/prototype are shared; concrete families preserve Lambda nominal ancestry.
    for (int step = 0; step < 13; step++) {
        int type = step ? step - 1 : 12;
        MvpLmdClass* cls = &state->typed_arrays[type];
        const char* name = type == 12 ? "TypedArray" : js_typed_array_spec((JsTypedArrayType)type)->name;
        Item status = mvp_lmd_library_class_initialize(program, state, cls,
            (int)(state->typed_values - state->values) + 3 * type, name, typed_array_construct, type == 12 ? 0 : 3);
        if (item_is_error(status)) return status;
        ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
        status = mvp_lmd_named_set(cls->values[1], "constructor", cls->values[0], JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
        if (type == 12) continue;
        status = mvp_lmd_set_prototype(cls->values[0], state->typed_arrays[12].values[0]);
        if (!item_is_error(status)) status = mvp_lmd_set_prototype(cls->values[1], state->typed_arrays[12].values[1]);
        if (item_is_error(status)) return status;
        Item width = {.item = i2it(js_typed_array_element_size((JsTypedArrayType)type))};
        for (int i = 0; i < 2; i++) {
            status = mvp_lmd_named_set(cls->values[i], "BYTES_PER_ELEMENT", width,
                JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE | JSPD_NON_WRITABLE);
            if (item_is_error(status)) return status;
        }
    }
    Item base = state->typed_arrays[12].values[1];
    const char* names[] = {"buffer", "byteLength", "byteOffset", "length", "[Symbol.toStringTag]"};
    for (int i = 0; i < 5; i++) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        char display[40]; snprintf(display, sizeof(display), "get %s", names[i]);
        Rooted<Item> getter(roots, mvp_lmd_native_function(program, (void*)(intptr_t)i, typed_array_method, 0, display));
        if (item_is_error(getter.get())) return getter.get();
        Item key = {.item = s2it(i == 4 ? well_known_name_ref(JS_SYMBOL_TO_STRING_TAG) : heap_create_name(names[i], strlen(names[i])))};
        Item status = mvp_lmd_define_accessor(base, key, getter.get(), JSPD_NON_ENUMERABLE << 8);
        if (item_is_error(status)) return status;
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> method(roots, mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0, "get [Symbol.species]"));
    if (item_is_error(method.get())) return method.get();
    Item status = mvp_lmd_define_accessor(state->typed_arrays[12].values[0],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))}, method.get(), JSPD_NON_ENUMERABLE << 8);
    if (item_is_error(status)) return status;
    const char* methods[] = {"values", "keys", "entries", "subarray", "forEach", "reduce",
        "find", "findIndex", "every", "some", "findLast", "findLastIndex", "reduceRight", "map", "filter",
        "indexOf", "lastIndexOf", "includes", "at", "fill", "set", "copyWithin", "reverse", "slice",
        "sort", "toSorted", "toReversed", "with", "join", "toLocaleString"};
    const uint8_t arities[] = {0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 2, 0, 2, 1, 1, 0, 2, 1, 0};
    for (int i = 0; i < (int)(sizeof(methods) / sizeof(methods[0])); i++) {
        method.set(mvp_lmd_native_function(program, (void*)(intptr_t)(5 + i), typed_array_method,
            arities[i], methods[i]));
        if (item_is_error(method.get())) return method.get();
        status = mvp_lmd_named_set(base, methods[i], method.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
        if (!i) {
            JsPropertyDescriptor descriptor = {};
            descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            js_pd_set_configurable(&descriptor, true); descriptor.value = method.get();
            status = mvp_lmd_define_own_property(base, Item{.item = s2it(well_known_name_ref(JS_SYMBOL_ITERATOR))}, &descriptor);
            if (item_is_error(status)) return status;
        }
    }
    // the specification shares the exact Array toString function identity.
    method.set(mvp_lmd_buffer_ops.get(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], "toString"));
    if (item_is_error(method.get())) return method.get();
    status = mvp_lmd_named_set(base, "toString", method.get(), JSPD_NON_ENUMERABLE);
    if (item_is_error(status)) return status;
    for (int i = 0; i < 2; i++) {
        method.set(mvp_lmd_native_function(program, (void*)(intptr_t)(35 + i),
            typed_array_method, i ? 0 : 1, i ? "of" : "from"));
        if (item_is_error(method.get())) return method.get();
        status = mvp_lmd_named_set(state->typed_arrays[12].values[0], i ? "of" : "from",
            method.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
    }
    return status;
}

static Item atomics_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item) {
    const auto* method = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    Item values[4];
    for (int i = 0; i < 4; i++) values[i] = (uint64_t)i < count ? args[i] : Item{.item = ITEM_JS_UNDEFINED};
    int operation;
    switch (method->builtin_id) {
    case JS_BUILTIN_ATOMICS_ADD: operation = JS_ATOMICS_OP_ADD; break;
    case JS_BUILTIN_ATOMICS_AND: operation = JS_ATOMICS_OP_AND; break;
    case JS_BUILTIN_ATOMICS_COMPAREEXCHANGE: operation = JS_ATOMICS_OP_COMPARE_EXCHANGE; break;
    case JS_BUILTIN_ATOMICS_EXCHANGE: operation = JS_ATOMICS_OP_EXCHANGE; break;
    case JS_BUILTIN_ATOMICS_LOAD: operation = JS_ATOMICS_OP_LOAD; break;
    case JS_BUILTIN_ATOMICS_OR: operation = JS_ATOMICS_OP_OR; break;
    case JS_BUILTIN_ATOMICS_STORE: operation = JS_ATOMICS_OP_STORE; break;
    case JS_BUILTIN_ATOMICS_SUB: operation = JS_ATOMICS_OP_SUB; break;
    case JS_BUILTIN_ATOMICS_XOR: operation = JS_ATOMICS_OP_XOR; break;
    case JS_BUILTIN_ATOMICS_NOTIFY:
        return js_atomics_notify_with_ops(values[0], values[1], values[2], &mvp_lmd_buffer_ops);
    case JS_BUILTIN_ATOMICS_WAIT:
    case JS_BUILTIN_ATOMICS_WAIT_ASYNC: {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> value(roots, ItemNull), result(roots, ItemNull);
        bool async = method->builtin_id == JS_BUILTIN_ATOMICS_WAIT_ASYNC;
        bool can_suspend = true;
        if (!async && mvp_lmd_program_library()->test262_host) {
            value.set(mvp_lmd_dynamic_global_this(program, false));
            if (item_is_error(value.get())) return value.get();
            if (value.get().item != ITEM_NULL) {
                value.set(mvp_lmd_buffer_ops.get(value.get(), "__lambda_can_block"));
                if (item_is_error(value.get())) return value.get();
                can_suspend = value.get().item != ITEM_FALSE;
            }
        }
        bool asynchronous = false;
        value.set(js_atomics_wait_with_ops(values[0], values[1], values[2], values[3],
            &mvp_lmd_buffer_ops, async, can_suspend, &asynchronous));
        if (item_is_error(value.get()) || !async) return value.get();
        result.set(mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0));
        if (item_is_error(result.get())) return result.get();
        Item status = mvp_lmd_named_set(result.get(), "async", Item{.item = b2it(asynchronous)}, 0);
        if (!item_is_error(status)) status = mvp_lmd_named_set(result.get(), "value", value.get(), 0);
        return item_is_error(status) ? status : result.get();
    }
    case JS_BUILTIN_ATOMICS_ISLOCKFREE:
        return js_atomics_is_lock_free_with_ops(values[0], &mvp_lmd_buffer_ops);
    case JS_BUILTIN_ATOMICS_PAUSE:
        return js_atomics_pause_with_ops(values[0], &mvp_lmd_buffer_ops);
    default:
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    return js_atomics_operation_with_ops(operation, values[0], values[1], values[2], values[3], &mvp_lmd_buffer_ops);
}

static Item buffer_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item absent = {.item = ITEM_JS_UNDEFINED};
    Item first = count ? args[0] : absent, second = count > 1 ? args[1] : absent;
    JsBufferViewKind kind = (JsBufferViewKind)(intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    return kind == JS_BUFFER_VIEW_DATA_VIEW
        ? js_dataview_construct_with_ops(first, second, count > 2 ? args[2] : absent, target, &mvp_lmd_buffer_ops)
        : js_construct_arraybuffer_target(first, second, target,
            kind == JS_BUFFER_VIEW_SHARED_ARRAY_BUFFER, &mvp_lmd_buffer_ops);
}

enum BufferMethod { BUFFER_BYTE_LENGTH = 32, BUFFER_MAX_LENGTH, BUFFER_RESIZABLE,
    BUFFER_DETACHED, BUFFER_RESIZE, BUFFER_TRANSFER, BUFFER_TRANSFER_FIXED, BUFFER_IS_VIEW, BUFFER_SLICE, BUFFER_DETACH_HOST,
    VIEW_BUFFER, VIEW_BYTE_LENGTH, VIEW_BYTE_OFFSET, BUFFER_UNSUPPORTED_HOST,
    SHARED_BUFFER_BYTE_LENGTH, SHARED_BUFFER_MAX_LENGTH, SHARED_BUFFER_GROWABLE,
    SHARED_BUFFER_SLICE, SHARED_BUFFER_GROW };
static Item buffer_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (operation == BUFFER_UNSUPPORTED_HOST) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Item value = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    if (operation == BUFFER_DETACH_HOST) {
        JsArrayBuffer* buffer = get_type_id(value) == LMD_TYPE_MAP ? js_get_arraybuffer_ptr(value.map) : NULL;
        if (!buffer || js_arraybuffer_shared(buffer)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        byte_buffer_detach(buffer);
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    if (operation == BUFFER_IS_VIEW) {
        TypeId type = get_type_id(value);
        return Item{.item = b2it(type == LMD_TYPE_ARRAY_NUM || (type == LMD_TYPE_MAP &&
            (value.map->map_kind == MAP_KIND_DATAVIEW || value.map->map_kind == MAP_KIND_TYPED_ARRAY)))};
    }
    if (operation < BUFFER_BYTE_LENGTH)
        return js_dataview_operation_with_ops(receiver, (JsDataViewOperation)operation, args, count, &mvp_lmd_buffer_ops);
    if (operation >= VIEW_BUFFER) {
        if (operation >= SHARED_BUFFER_BYTE_LENGTH) {
            if (!js_is_sharedarraybuffer(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            if (operation == SHARED_BUFFER_SLICE || operation == SHARED_BUFFER_GROW)
                return js_sharedarraybuffer_operation_with_ops(receiver,
                    operation == SHARED_BUFFER_SLICE ? JS_SHARED_ARRAY_BUFFER_SLICE : JS_SHARED_ARRAY_BUFFER_GROW,
                    args, count, &mvp_lmd_buffer_ops);
            JsArrayBuffer* shared = js_get_arraybuffer_ptr(receiver.map);
            if (operation == SHARED_BUFFER_GROWABLE) return Item{.item = b2it(js_arraybuffer_resizable(shared))};
            return Item{.item = i2it(operation == SHARED_BUFFER_MAX_LENGTH
                ? js_arraybuffer_max_length(shared) : js_arraybuffer_length(shared))};
        }
        JsDataView* view = get_type_id(receiver) == LMD_TYPE_MAP ? js_get_dataview_ptr_from_map(receiver.map) : NULL;
        if (!view) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (operation == VIEW_BUFFER) return Item{.item = view->buffer_item};
        if (js_arraybuffer_view_is_out_of_bounds(view)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return Item{.item = i2it(operation == VIEW_BYTE_OFFSET ? view->byte_offset : js_arraybuffer_view_current_byte_length(view))};
    }
    JsArrayBuffer* buffer = get_type_id(receiver) == LMD_TYPE_MAP ? js_get_arraybuffer_ptr(receiver.map) : NULL;
    if (!buffer || js_arraybuffer_shared(buffer)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (operation == BUFFER_SLICE) return js_arraybuffer_slice_with_ops(receiver, value,
        count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, count, &mvp_lmd_buffer_ops);
    if (operation == BUFFER_RESIZE) return js_arraybuffer_resize_with_ops(receiver, value, &mvp_lmd_buffer_ops);
    if (operation == BUFFER_TRANSFER || operation == BUFFER_TRANSFER_FIXED)
        return js_arraybuffer_transfer_with_ops(receiver, value, count, operation == BUFFER_TRANSFER_FIXED, &mvp_lmd_buffer_ops);
    if (operation == BUFFER_RESIZABLE) return Item{.item = b2it(js_arraybuffer_resizable(buffer))};
    if (operation == BUFFER_DETACHED) return Item{.item = b2it(js_arraybuffer_detached(buffer))};
    return Item{.item = i2it(js_arraybuffer_detached(buffer) ? 0 : operation == BUFFER_MAX_LENGTH
        ? js_arraybuffer_max_length(buffer) : js_arraybuffer_length(buffer))};
}

Item mvp_lmd_buffer_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->buffers) return ItemNull;
    Item result = ItemNull;
    if (state->atomics_value) {
        *state->atomics_value = mvp_lmd_object_new(state->object_shape, 0);
        if (item_is_error(*state->atomics_value)) return *state->atomics_value;
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != JS_BUILTIN_OWNER_ATOMICS_METHOD) continue;
            RootFrame roots(1);
            if (!roots.valid()) return ItemError;
            Rooted<Item> function(roots, mvp_lmd_native_function(program, (void*)method,
                atomics_method, method->param_count, method->name));
            if (item_is_error(function.get())) return function.get();
            result = mvp_lmd_named_set(*state->atomics_value, method->name,
                function.get(), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        result = mvp_lmd_named_set(*state->atomics_value,
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
            Item{.item = s2it(heap_create_name("Atomics", 7))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        if (item_is_error(result)) return result;
    }
    MvpLmdClass* classes[] = {&state->buffers[0], &state->buffers[1], state->shared_buffer};
    Item* values[] = {state->buffer_values, state->buffer_values + 3, state->shared_buffer_values};
    const char* names[] = {"ArrayBuffer", "DataView", "SharedArrayBuffer"};
    for (int i = 0; i < 3; i++) {
        MvpLmdClass* cls = classes[i];
        if (!cls) continue;
        const char* name = names[i];
        result = mvp_lmd_library_class_initialize(program, state, cls,
            (int)(values[i] - state->values), name, buffer_construct, 1);
        if (item_is_error(result)) return result;
        ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
        ((MvpLmdNativeCallable*)cls->values[0].function)->state = (void*)(intptr_t)i;
        result = mvp_lmd_named_set(cls->values[1], "constructor", cls->values[0], JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(cls->values[1],
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
            Item{.item = s2it(heap_create_name(name, strlen(name)))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        if (item_is_error(result)) return result;
        if (i != JS_BUFFER_VIEW_DATA_VIEW) {
            RootFrame roots(1);
            if (!roots.valid()) return ItemError;
            Rooted<Item> getter(roots, mvp_lmd_native_function(program, NULL,
                mvp_lmd_receiver_identity, 0, "get [Symbol.species]"));
            if (item_is_error(getter.get())) return getter.get();
            result = mvp_lmd_define_accessor(cls->values[0],
                Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))}, getter.get(), JSPD_NON_ENUMERABLE << 8);
            if (item_is_error(result)) return result;
        }
    }
    if (state->test262_host) {
        state->buffer_values[6] = mvp_lmd_object_new(state->object_shape, 0);
        if (item_is_error(state->buffer_values[6])) return state->buffer_values[6];
        result = mvp_lmd_named_set(state->buffer_values[6], "detachArrayBuffer",
            mvp_lmd_native_function(program, (void*)(intptr_t)BUFFER_DETACH_HOST, buffer_method, 1, "detachArrayBuffer"));
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(state->buffer_values[6], "evalScript",
            mvp_lmd_native_function(program, NULL, mvp_lmd_eval_script, 1, "evalScript"));
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(state->buffer_values[6], "createRealm",
            mvp_lmd_native_function(program, NULL, mvp_lmd_create_realm, 0, "createRealm"));
        if (item_is_error(result)) return result;
        // unavailable host facilities must not become a catchable TypeError from calling undefined.
        for (const char* name : {"agent"}) {
            RootFrame roots(1);
            if (!roots.valid()) return ItemError;
            Rooted<Item> getter(roots, mvp_lmd_native_function(program,
                (void*)(intptr_t)BUFFER_UNSUPPORTED_HOST, buffer_method, 0, name));
            if (item_is_error(getter.get())) return getter.get();
            result = mvp_lmd_define_accessor(state->buffer_values[6],
                Item{.item = s2it(heap_create_name(name, strlen(name)))}, getter.get(), 0);
            if (item_is_error(result)) return result;
        }
    }
    const struct { const char* name; int operation; int owner; int arity; bool getter; } methods[] = {
        {"byteLength", BUFFER_BYTE_LENGTH, 0, 0, true}, {"maxByteLength", BUFFER_MAX_LENGTH, 0, 0, true},
        {"resizable", BUFFER_RESIZABLE, 0, 0, true}, {"detached", BUFFER_DETACHED, 0, 0, true},
        {"slice", BUFFER_SLICE, 0, 2, false}, {"resize", BUFFER_RESIZE, 0, 1, false}, {"transfer", BUFFER_TRANSFER, 0, 0, false},
        {"transferToFixedLength", BUFFER_TRANSFER_FIXED, 0, 0, false}, {"isView", BUFFER_IS_VIEW, 2, 1, false},
        {"buffer", VIEW_BUFFER, 1, 0, true}, {"byteLength", VIEW_BYTE_LENGTH, 1, 0, true},
        {"byteOffset", VIEW_BYTE_OFFSET, 1, 0, true},
        {"getInt8", JS_DATAVIEW_GET_INT8, 1, 1, false}, {"getUint8", JS_DATAVIEW_GET_UINT8, 1, 1, false},
        {"getInt16", JS_DATAVIEW_GET_INT16, 1, 1, false}, {"getUint16", JS_DATAVIEW_GET_UINT16, 1, 1, false},
        {"getInt32", JS_DATAVIEW_GET_INT32, 1, 1, false}, {"getUint32", JS_DATAVIEW_GET_UINT32, 1, 1, false},
        {"getFloat32", JS_DATAVIEW_GET_FLOAT32, 1, 1, false}, {"getFloat64", JS_DATAVIEW_GET_FLOAT64, 1, 1, false},
        {"getBigInt64", JS_DATAVIEW_GET_BIGINT64, 1, 1, false}, {"getBigUint64", JS_DATAVIEW_GET_BIGUINT64, 1, 1, false},
        {"setInt8", JS_DATAVIEW_SET_INT8, 1, 2, false}, {"setUint8", JS_DATAVIEW_SET_UINT8, 1, 2, false},
        {"setInt16", JS_DATAVIEW_SET_INT16, 1, 2, false}, {"setUint16", JS_DATAVIEW_SET_UINT16, 1, 2, false},
        {"setInt32", JS_DATAVIEW_SET_INT32, 1, 2, false}, {"setUint32", JS_DATAVIEW_SET_UINT32, 1, 2, false},
        {"setFloat32", JS_DATAVIEW_SET_FLOAT32, 1, 2, false}, {"setFloat64", JS_DATAVIEW_SET_FLOAT64, 1, 2, false},
        {"setBigInt64", JS_DATAVIEW_SET_BIGINT64, 1, 2, false}, {"setBigUint64", JS_DATAVIEW_SET_BIGUINT64, 1, 2, false},
        {"byteLength", SHARED_BUFFER_BYTE_LENGTH, 3, 0, true}, {"maxByteLength", SHARED_BUFFER_MAX_LENGTH, 3, 0, true},
        {"growable", SHARED_BUFFER_GROWABLE, 3, 0, true}, {"slice", SHARED_BUFFER_SLICE, 3, 2, false},
        {"grow", SHARED_BUFFER_GROW, 3, 1, false}
    };
    for (const auto& method : methods) {
        if (method.owner == 3 && !state->shared_buffer) continue;
        char display[40];
        snprintf(display, sizeof(display), "%s%s", method.getter ? "get " : "", method.name);
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> callable(roots, mvp_lmd_native_function(program, (void*)(intptr_t)method.operation,
            buffer_method, method.arity, display));
        if (item_is_error(callable.get())) return callable.get();
        Item key = {.item = s2it(heap_create_name(method.name, strlen(method.name)))};
        // reload the rooted prototype after allocating the native callable.
        Item owner = method.owner == 3 ? state->shared_buffer->values[1]
            : state->buffers[method.owner & 1].values[method.owner == 2 ? 0 : 1];
        result = method.getter ? mvp_lmd_define_accessor(owner, key, callable.get(), JSPD_NON_ENUMERABLE << 8)
            : mvp_lmd_named_set(owner, method.name, callable.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    return result;
}

static Item uri_error(const char* message) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state || !state->errors) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    String* message_text = heap_create_name(message, strlen(message));
    if (!message_text) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Rooted<Item> text(roots, Item{.item = s2it(message_text)}), error(roots, ItemNull);
    Item constructor = state->errors[LMD_ERROR_URI].values[0];
    Item argument = text.get();
    error.set(mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, &argument, 1, constructor));
    return item_is_error(error.get()) ? error.get() : mvp_lmd_throw(error.get(), 0);
}

static Item uri_call(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item) {
    const JsBuiltinGlobalSpec* spec = (const JsBuiltinGlobalSpec*)((MvpLmdNativeCallable*)self.function)->state;
    bool component = spec->id == JS_BUILTIN_GLOBAL_FN_ENCODE_URICOMPONENT ||
        spec->id == JS_BUILTIN_GLOBAL_FN_DECODE_URICOMPONENT;
    bool decode = spec->id == JS_BUILTIN_GLOBAL_FN_DECODE_URI ||
        spec->id == JS_BUILTIN_GLOBAL_FN_DECODE_URICOMPONENT;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> text(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    text.set(mvp_lmd_primitive_to_string(text.get()));
    if (item_is_error(text.get())) return text.get();
    if (spec->id == JS_BUILTIN_GLOBAL_FN_ESCAPE) return js_escape(text.get());
    if (spec->id == JS_BUILTIN_GLOBAL_FN_UNESCAPE) return js_unescape(text.get());
    return js_uri_transform_string(text.get(), component, decode, uri_error);
}

Item mvp_lmd_library_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    if (!context->name_pool) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    state->prototype_key = mvp_lmd_internal_name(LMD_INTERNAL_PROTOTYPE);
    state->extensibility_key = mvp_lmd_internal_name(LMD_INTERNAL_EXTENSIBLE);
    if (!state->prototype_key || !state->extensibility_key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    state->values[LMD_LIBRARY_JSON] = mvp_lmd_object_new(&state->json_shape, 0);
    if (item_is_error(state->values[LMD_LIBRARY_JSON])) return state->values[LMD_LIBRARY_JSON];
    Item result = mvp_lmd_named_set(state->values[LMD_LIBRARY_JSON],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
        Item{.item = s2it(heap_create_name("JSON", 4))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_library_class_initialize(program, state, &state->object, LMD_LIBRARY_OBJECT, "Object", object_construct, 1);
    if (item_is_error(result)) return result;
    ((MvpLmdCallable*)state->object.values[0].function)->custom_new_target = true;
    result = mvp_lmd_library_class_initialize(program, state, &state->date, LMD_LIBRARY_DATE, "Date", date_construct, 7);
    if (item_is_error(result)) return result;
    result = mvp_lmd_library_class_initialize(program, state, &state->array, LMD_LIBRARY_ARRAY, "Array", array_construct, 1);
    if (item_is_error(result)) return result;
    ((MvpLmdCallable*)state->array.values[0].function)->custom_new_target = true;
    result = mvp_lmd_library_class_initialize(program, state, &state->symbol, LMD_LIBRARY_SYMBOL, "Symbol", symbol_create, 0);
    if (item_is_error(result)) return result;
    // Symbol has [[Construct]] for heritage/newTarget checks; its native entry throws on construction.
    ((MvpLmdCallable*)state->symbol.values[0].function)->custom_new_target = true;
    state->values[LMD_LIBRARY_SYMBOL_VALUES] = mvp_lmd_array_new(0);
    if (item_is_error(state->values[LMD_LIBRARY_SYMBOL_VALUES])) return state->values[LMD_LIBRARY_SYMBOL_VALUES];
    const struct { const char* name; NameId id; } symbols[] = {
        {"iterator", JS_SYMBOL_ITERATOR}, {"toPrimitive", JS_SYMBOL_TO_PRIMITIVE},
        {"hasInstance", JS_SYMBOL_HAS_INSTANCE}, {"toStringTag", JS_SYMBOL_TO_STRING_TAG},
        {"species", JS_SYMBOL_SPECIES}, {"isConcatSpreadable", JS_SYMBOL_IS_CONCAT_SPREADABLE},
        {"match", JS_SYMBOL_MATCH}, {"replace", JS_SYMBOL_REPLACE}, {"search", JS_SYMBOL_SEARCH},
        {"split", JS_SYMBOL_SPLIT}, {"matchAll", JS_SYMBOL_MATCH_ALL},
        {"unscopables", JS_SYMBOL_UNSCOPABLES}, {"asyncIterator", JS_SYMBOL_ASYNC_ITERATOR},
        {"dispose", JS_SYMBOL_DISPOSE}, {"asyncDispose", JS_SYMBOL_ASYNC_DISPOSE}};
    for (int i = 0; i < (state->mutable_prototypes ? (int)(sizeof(symbols) / sizeof(symbols[0])) : 1); i++) {
        Item id = {.item = i2it(symbols[i].id)};
        Item value = symbol_create(context, program, &id, 1, Item{.item = ITEM_JS_UNDEFINED}, ItemNull, Item{.item = ITEM_JS_UNDEFINED});
        if (item_is_error(value)) return value;
        if (!i) state->values[LMD_LIBRARY_ITERATOR_SYMBOL] = value;
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        descriptor.value = value;
        String* key = heap_create_name(symbols[i].name, strlen(symbols[i].name));
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        result = mvp_lmd_define_own_property(state->values[LMD_LIBRARY_SYMBOL_STATICS], Item{.item = s2it(key)}, &descriptor);
        if (item_is_error(result)) return result;
    }
    for (int i = 0; ; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
        if (!method) break;
        if (method->owner != JS_BUILTIN_OWNER_SYMBOL_STATIC_METHOD) continue;
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> function(roots, mvp_lmd_native_function(program, (void*)method, symbol_registry_method,
            method->param_count, method->display_name ? method->display_name : method->name));
        if (item_is_error(function.get())) return function.get();
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_SYMBOL_STATICS], method->name,
            function.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    {
        const JsBuiltinMethodSpec* method = js_builtin_catalog_find(JS_BUILTIN_OWNER_SYMBOL_INTERNAL, "description", 11);
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> getter(roots, mvp_lmd_native_function(program, NULL, symbol_description,
            method->param_count, method->display_name));
        if (item_is_error(getter.get())) return getter.get();
        String* key = heap_create_name(method->name, method->len);
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        result = mvp_lmd_define_accessor(state->symbol.values[1], Item{.item = s2it(key)},
            getter.get(), JSPD_NON_ENUMERABLE << 8);
        if (item_is_error(result)) return result;
    }
    struct Binding { int owner; const char* key; MvpLmdNativeEntry entry; uint8_t arity; };
    const Binding functions[] = {
        {LMD_LIBRARY_JSON, "parse", json_parse, 2}, {LMD_LIBRARY_JSON, "stringify", json_stringify, 3},
        {LMD_LIBRARY_DATE_STATICS, "now", date_now, 0},
        {LMD_LIBRARY_DATE_PROTOTYPE, "getTime", date_value, 0},
        {LMD_LIBRARY_DATE_PROTOTYPE, "valueOf", date_value, 0},
        {LMD_LIBRARY_OBJECT_PROTOTYPE, "toString",
            [](Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
                return mvp_lmd_object_string(receiver);
            }, 0},
        {LMD_LIBRARY_OBJECT_PROTOTYPE, "toLocaleString", object_locale, 0},
        {LMD_LIBRARY_ARRAY_PROTOTYPE, "toLocaleString", array_locale, 0},
        {LMD_LIBRARY_OBJECT_PROTOTYPE, "hasOwnProperty", object_own, 1},
        {LMD_LIBRARY_OBJECT_PROTOTYPE, "valueOf", mvp_lmd_object_value, 0},
        {LMD_LIBRARY_OBJECT_STATICS, "defineProperty", object_define, 3},
        {LMD_LIBRARY_OBJECT_STATICS, "defineProperties", object_define_properties, 2},
        {LMD_LIBRARY_OBJECT_STATICS, "getOwnPropertyDescriptor", object_descriptor, 2},
        {LMD_LIBRARY_OBJECT_STATICS, "create", object_create, 2},
        {LMD_LIBRARY_OBJECT_STATICS, "assign", object_assign, 2},
        {LMD_LIBRARY_OBJECT_STATICS, "getPrototypeOf", object_prototype, 1},
        {LMD_LIBRARY_OBJECT_STATICS, "setPrototypeOf", object_set_prototype, 2},
        {LMD_LIBRARY_ARRAY_STATICS, "isArray", array_is_array, 1},
        {LMD_LIBRARY_ARRAY_STATICS, "from", array_from, 1},
        {LMD_LIBRARY_ARRAY_STATICS, "of", array_of, 0}};
    for (const Binding& binding : functions) {
        Item result = mvp_lmd_named_set(state->values[binding.owner], binding.key,
            mvp_lmd_native_function(program, state, binding.entry, binding.arity, binding.key));
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_STATICS], "is",
        mvp_lmd_native_function(program, NULL, object_same_value, 2, "is"), JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    const char* predicates[] = {"isPrototypeOf", "propertyIsEnumerable", "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__"};
    for (int i = 0; i < 6; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_PROTOTYPE], predicates[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_predicate, i == 2 || i == 3 ? 2 : 1, predicates[i]), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    const char* projections[] = {"keys", "values", "entries", "hasOwn", "getOwnPropertyNames", "getOwnPropertySymbols", "getOwnPropertyDescriptors"};
    for (int i = 0; i < 7; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_STATICS], projections[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_projection, i == 3 ? 2 : 1, projections[i]));
        if (item_is_error(result)) return result;
    }
    const char* integrity[] = {"preventExtensions", "seal", "freeze", "isExtensible", "isSealed", "isFrozen"};
    for (int i = 0; i < 6; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_STATICS], integrity[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_integrity, 1, integrity[i]));
        if (item_is_error(result)) return result;
    }
    {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> key(roots, Item{.item = s2it(heap_create_name("__proto__", 9))});
        Rooted<Item> callable(roots, ItemNull);
        for (int i = 0; i < 2; i++) {
            callable.set(mvp_lmd_native_function(program, (void*)(intptr_t)i, object_proto_accessor, i,
                i ? "set __proto__" : "get __proto__"));
            if (item_is_error(callable.get())) return callable.get();
            Item defined = mvp_lmd_define_accessor(state->values[LMD_LIBRARY_OBJECT_PROTOTYPE], key.get(), callable.get(),
                (JSPD_NON_ENUMERABLE << 8) | i);
            if (item_is_error(defined)) return defined;
        }
    }
    if (state->mutable_prototypes) {
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))};
        result = mvp_lmd_define_accessor(state->values[LMD_LIBRARY_ARRAY], key,
            mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0, "get [Symbol.species]"), JSPD_NON_ENUMERABLE << 8);
        if (item_is_error(result)) return result;
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> excluded(roots, mvp_lmd_object_new(state->object_shape, 0));
        if (item_is_error(excluded.get())) return excluded.get();
        result = mvp_lmd_set_prototype(excluded.get(), ItemNull);
        if (item_is_error(result)) return result;
        for (int i = 0; js_array_unscopable_names[i]; i++) {
            result = mvp_lmd_named_set(excluded.get(), js_array_unscopable_names[i], Item{.item = ITEM_TRUE}, 0);
            if (item_is_error(result)) return result;
        }
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE |
            JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&descriptor, true);
        descriptor.value = excluded.get();
        result = mvp_lmd_define_own_property(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE],
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_UNSCOPABLES))}, &descriptor);
        if (item_is_error(result)) return result;
    }
    for (int i = 0; i < mvp_lmd_builtin_method_count; i++) {
        const MvpLmdBuiltinMethod& method = mvp_lmd_builtin_methods[i];
        if (method.owner != LMD_TYPE_ARRAY) continue;
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], method.name,
            mvp_lmd_native_function(program, (void*)(intptr_t)method.id, array_method, method.arity, method.name));
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_regexp_initialize(program, state);
    if (item_is_error(result)) return result;
    result = mvp_lmd_function_initialize(program, state);
    if (item_is_error(result)) return result;
    if (state->errors) for (int i = 0; i < LMD_ERROR_COUNT; i++) {
        MvpLmdClass* cls = &state->errors[i];
        result = mvp_lmd_library_class_initialize(program, state, cls,
            (int)(state->error_values - state->values) + 3 * i, mvp_lmd_error_names[i], error_construct,
            i == LMD_ERROR_AGGREGATE ? 2 : 1, true);
        if (item_is_error(result)) return result;
        ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
        if (i) {
            result = mvp_lmd_set_prototype(cls->values[0], state->error_values[0]);
            if (item_is_error(result)) return result;
        }
        result = mvp_lmd_named_set(cls->values[1], "name",
            Item{.item = s2it(heap_create_name(mvp_lmd_error_names[i], strlen(mvp_lmd_error_names[i])))}, JSPD_NON_ENUMERABLE);
        if (!item_is_error(result)) result = mvp_lmd_named_set(cls->values[1], "message",
            Item{.item = s2it(heap_create_name("", 0))}, JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        if (!i) {
            result = mvp_lmd_named_set(cls->values[1], "toString",
                mvp_lmd_native_function(program, state, error_to_string, 0, "toString"), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
    }
    if (state->primitives) {
        state->primitive_key = mvp_lmd_internal_name(LMD_INTERNAL_PRIMITIVE);
        if (!state->primitive_key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        for (int i = 0; i < LMD_PRIMITIVE_COUNT + 1; i++) {
            MvpLmdClass* cls = i == LMD_PRIMITIVE_COUNT ? &state->symbol : &state->primitives[i];
            if (i < LMD_PRIMITIVE_COUNT) {
                result = mvp_lmd_library_class_initialize(program, state, cls,
                    (int)(state->primitive_values - state->values) + 3 * i, mvp_lmd_primitive_names[i], primitive_construct, 1, true);
                if (item_is_error(result)) return result;
                ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
                if (i == LMD_PRIMITIVE_BIGINT) {
                    ((MvpLmdCallable*)cls->values[0].function)->native_constructor = false;
                    ((MvpLmdCallable*)cls->values[0].function)->non_constructible = true;
                }
            }
            if (i < LMD_PRIMITIVE_BIGINT) {
                Item initial = i == LMD_PRIMITIVE_STRING ? Item{.item = s2it(heap_create_name("", 0))} :
                    i == LMD_PRIMITIVE_BOOLEAN ? Item{.item = ITEM_FALSE} : Item{.item = i2it(0)};
                if (!map_shape_set(cls->values[1].map, state->primitive_key, initial)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
                if (i == LMD_PRIMITIVE_STRING) {
                    result = mvp_lmd_named_set(cls->values[1], "length", Item{.item = i2it(0)},
                        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
                    if (item_is_error(result)) return result;
                }
            }
            for (int method = 0; method < 2; method++) {
                result = mvp_lmd_named_set(cls->values[1], method ? "toString" : "valueOf",
                    mvp_lmd_native_function(program, (void*)(intptr_t)(i * 2 + method), primitive_method,
                        method && (i == LMD_PRIMITIVE_NUMBER || i == LMD_PRIMITIVE_BIGINT) ? 1 : 0,
                        method ? "toString" : "valueOf"), JSPD_NON_ENUMERABLE);
                if (item_is_error(result)) return result;
            }
            if (i >= LMD_PRIMITIVE_BIGINT) {
                const char* tag = i == LMD_PRIMITIVE_BIGINT ? "BigInt" : "Symbol";
                result = mvp_lmd_named_set(cls->values[1],
                    Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
                    Item{.item = s2it(heap_create_name(tag, strlen(tag)))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
                if (item_is_error(result)) return result;
                if (i == LMD_PRIMITIVE_COUNT) {
                    result = mvp_lmd_named_set(cls->values[1],
                        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_PRIMITIVE))},
                        mvp_lmd_native_function(program, (void*)(intptr_t)(i * 2), primitive_method, 1, "[Symbol.toPrimitive]"),
                        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
                    if (item_is_error(result)) return result;
                }
            }
        }
        for (int i = 0; i < mvp_lmd_builtin_method_count; i++) {
            const MvpLmdBuiltinMethod& method = mvp_lmd_builtin_methods[i];
            if (method.owner != LMD_TYPE_STRING) continue;
            Item prototype = state->primitives[LMD_PRIMITIVE_STRING].values[1];
            // legacy trim spellings alias the same callable rather than creating wrappers.
            const char* alias = !strcmp(method.name, "trimLeft") ? "trimStart" :
                !strcmp(method.name, "trimRight") ? "trimEnd" : NULL;
            Item callable = alias ? mvp_lmd_property_get(prototype, Item{.item = s2it(heap_create_name(alias, strlen(alias)))}, 0) :
                mvp_lmd_native_function(program, (void*)(intptr_t)method.id, string_method, method.arity, method.name);
            if (item_is_error(callable)) return callable;
            result = mvp_lmd_named_set(prototype, method.name, callable, JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        result = mvp_lmd_named_set(state->primitives[LMD_PRIMITIVE_NUMBER].values[1], "toFixed",
            mvp_lmd_native_function(program, (void*)(intptr_t)LMD_METHOD_TO_FIXED, primitive_method, 1, "toFixed"), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        const char* formats[] = {"toPrecision", "toExponential"};
        const char* widths[] = {"asIntN", "asUintN"};
        for (int i = 0; i < 2; i++) {
            result = mvp_lmd_named_set(state->primitives[LMD_PRIMITIVE_NUMBER].values[1], formats[i],
                mvp_lmd_native_function(program, (void*)(intptr_t)(PRIMITIVE_PRECISION + i), primitive_method, 1, formats[i]), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
            result = mvp_lmd_named_set(state->primitives[LMD_PRIMITIVE_BIGINT].values[0], widths[i],
                mvp_lmd_native_function(program, (void*)(intptr_t)(11 + i), primitive_static, 2, widths[i]), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        for (int kind : {LMD_PRIMITIVE_NUMBER, LMD_PRIMITIVE_BIGINT}) {
            // the shared no-Intl policy ignores locale arguments and keeps the primitive brand check.
            result = mvp_lmd_named_set(state->primitives[kind].values[1], "toLocaleString",
                mvp_lmd_native_function(program, (void*)(intptr_t)(16 + kind), primitive_method, 0, "toLocaleString"), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        const char* statics[] = {"raw", "fromCharCode", "fromCodePoint", "isFinite", "isInteger", "isNaN", "isSafeInteger", "parseInt", "parseFloat"};
        for (int i = 0; i < 9; i++) {
            Item owner = state->primitives[i < 3 ? LMD_PRIMITIVE_STRING : LMD_PRIMITIVE_NUMBER].values[0];
            result = mvp_lmd_named_set(owner, statics[i],
                mvp_lmd_native_function(program, (void*)(intptr_t)i, primitive_static, i == 7 ? 2 : 1, statics[i]), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        for (int i = 0; i < 2; i++) {
            state->values[LMD_LIBRARY_GLOBAL_NAN + i] = mvp_lmd_native_function(program,
                (void*)(intptr_t)(9 + i), primitive_static, 1, i ? "isFinite" : "isNaN");
            if (item_is_error(state->values[LMD_LIBRARY_GLOBAL_NAN + i])) return state->values[LMD_LIBRARY_GLOBAL_NAN + i];
        }
        const struct { const char* name; double value; } constants[] = {
            {"EPSILON", 0x1p-52}, {"MAX_SAFE_INTEGER", 9007199254740991.0}, {"MIN_SAFE_INTEGER", -9007199254740991.0},
            {"MAX_VALUE", 0x1.fffffffffffffp1023}, {"MIN_VALUE", 0x1p-1074},
            {"NaN", NAN}, {"POSITIVE_INFINITY", INFINITY}, {"NEGATIVE_INFINITY", -INFINITY}};
        for (const auto& constant : constants) {
            result = mvp_lmd_named_set(state->primitives[LMD_PRIMITIVE_NUMBER].values[0], constant.name, push_d(constant.value),
                JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
            if (item_is_error(result)) return result;
        }
    }
    if (state->errors) for (int i = 0; i < js_builtin_global_count(); i++) {
        const JsBuiltinGlobalSpec* spec = js_builtin_global_at(i);
        int slot = spec->id == JS_BUILTIN_GLOBAL_FN_DECODE_URI ? LMD_LIBRARY_DECODE_URI :
            spec->id == JS_BUILTIN_GLOBAL_FN_ENCODE_URI ? LMD_LIBRARY_ENCODE_URI :
            spec->id == JS_BUILTIN_GLOBAL_FN_DECODE_URICOMPONENT ? LMD_LIBRARY_DECODE_COMPONENT :
            spec->id == JS_BUILTIN_GLOBAL_FN_ENCODE_URICOMPONENT ? LMD_LIBRARY_ENCODE_COMPONENT :
            spec->id == JS_BUILTIN_GLOBAL_FN_ESCAPE ? LMD_LIBRARY_ESCAPE :
            spec->id == JS_BUILTIN_GLOBAL_FN_UNESCAPE ? LMD_LIBRARY_UNESCAPE : -1;
        if (slot < 0) continue;
        state->values[slot] = mvp_lmd_native_function(program, (void*)spec, uri_call, spec->param_count, spec->name);
        if (item_is_error(state->values[slot])) return state->values[slot];
    }
    if (!item_is_error(result)) result = mvp_lmd_buffer_initialize(program, state);
    if (!item_is_error(result)) result = mvp_lmd_date_initialize(program, state);
    if (!item_is_error(result)) result = mvp_lmd_generator_initialize(program, state);
    if (item_is_error(result)) return result;
    for (int i = 0; ; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
        if (!method) break;
        if (method->builtin_id != JS_BUILTIN_MAP_GROUP_BY && method->builtin_id != JS_BUILTIN_OBJECT_GROUP_BY &&
                method->builtin_id != JS_BUILTIN_OBJECT_FROM_ENTRIES) continue;
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> function(roots, mvp_lmd_native_function(program, (void*)method, collection_group_by,
            method->param_count, method->display_name ? method->display_name : method->name));
        if (item_is_error(function.get())) return function.get();
        result = mvp_lmd_named_set(method->builtin_id == JS_BUILTIN_MAP_GROUP_BY
            ? state->values[LMD_LIBRARY_MAP_STATICS] : state->values[LMD_LIBRARY_OBJECT_STATICS],
            method->name, function.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    if (!item_is_error(result)) result = mvp_lmd_typed_array_initialize(program, state);
    if (!item_is_error(result)) result = mvp_lmd_reflect_initialize(program, state);
    return item_is_error(result) ? result : mvp_lmd_proxy_initialize(program, state);
}

extern "C" Item mvp_lmd_array_to_string(Item source, Item separator, int64_t library_objects) {
    struct JoinFrame { Item source; JoinFrame* parent; };
    static thread_local JoinFrame* active = NULL;
    for (JoinFrame* parent = active; parent; parent = parent->parent) if (parent->source.item == source.item) {
        String* empty = heap_strcpy("", 0);
        return empty ? Item{.item = s2it(empty)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, source), sep(roots, separator), text(roots, ItemNull);
    Item extent = mvp_lmd_sequence_length(held.get());
    if (item_is_error(extent)) return extent;
    int64_t length = it2i(extent);
    if (separator.item == ITEM_JS_UNDEFINED) {
        String* comma = heap_strcpy(",", 1);
        if (!comma) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        sep.set(Item{.item = s2it(comma)});
    } else sep.set(mvp_lmd_primitive_to_string(separator, library_objects));
    if (item_is_error(sep.get())) return sep.get();
    StrBuf* buffer = strbuf_new();
    if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    JoinFrame frame = {source, active}; active = &frame;
    Item result = ItemNull;
    for (int64_t i = 0; i < length; i++) {
        String* delimiter = sep.get().get_string();
        if (i) strbuf_append_str_n(buffer, delimiter->chars, delimiter->len);
        Item value = mvp_lmd_array_index_property(held.get(), i, ItemNull, LMD_PROP_GET);
        if (item_is_error(value)) { result = value; break; }
        if (value.item == ITEM_NULL || value.item == ITEM_JS_UNDEFINED || value.item == ITEM_JS_DELETED_SENTINEL) continue;
        text.set(mvp_lmd_primitive_to_string(value, library_objects));
        if (item_is_error(text.get())) { result = text.get(); break; }
        String* part = text.get().get_string(); strbuf_append_str_n(buffer, part->chars, part->len);
    }
    active = frame.parent;
    if (!item_is_error(result)) {
        String* value = heap_strcpy(buffer->str, buffer->length);
        result = value ? Item{.item = s2it(value)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    strbuf_free(buffer); return result;
}

extern "C" Item mvp_lmd_to_primitive(Item value, int64_t hint, int64_t library_objects) {
    bool ordinary = (hint & 4) != 0;
    hint &= 3;
    TypeId type = get_type_id(value);
    if (!mvp_lmd_is_object(value)) return value;
    // recursive array conversion retains the unit's admission before allocating property names.
    if (!library_objects && type != LMD_TYPE_ARRAY) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, value), callable(roots, ItemNull), result(roots, ItemNull);
    if (!ordinary && mvp_lmd_program_library() && (mvp_lmd_program_library()->mutable_prototypes || mvp_lmd_program_library()->primitives || mvp_lmd_program_library()->date_observed)) {
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_TO_PRIMITIVE))};
        callable.set(mvp_lmd_property_with_receiver(owner.get(), key, ItemNull, LMD_PROP_GET, owner.get()));
        if (item_is_error(callable.get())) return callable.get();
        if (callable.get().item != ITEM_JS_UNDEFINED && callable.get().item != ITEM_NULL) {
            if (!mvp_lmd_is_callable(callable.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            const char* spelling = hint == 2 ? "string" : hint == 1 ? "number" : "default";
            Item argument = {.item = s2it(heap_create_name(spelling, strlen(spelling)))};
            result.set(mvp_lmd_class_invoke(callable.get(), owner.get(), &argument, 1, Item{.item = ITEM_JS_UNDEFINED}));
            return item_is_error(result.get()) || !mvp_lmd_is_object(result.get()) ? result.get() : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
    }
    // hint: 0 default, 1 number, 2 string. Date's default is string-preferred.
    MvpLmdClass* cls = mvp_lmd_class_record(value);
    if (!hint && cls) {
        MvpLmdLibraryState* state = mvp_lmd_program_library(cls->program);
        if (state && !state->date_observed && cls == &state->date) hint = 2;
    }
    const char* order[] = {hint == 2 ? "toString" : "valueOf", hint == 2 ? "valueOf" : "toString"};
    for (const char* name : order) {
        if (type == LMD_TYPE_ARRAY && (!mvp_lmd_program_library() || !mvp_lmd_program_library()->mutable_prototypes)) {
            ShapeEntry* field = owner.get().array->type ? typemap_hash_lookup((TypeMap*)owner.get().array->type, name, strlen(name)) : NULL;
            if (!field) {
                if (!strcmp(name, "valueOf")) continue;
                result.set(mvp_lmd_array_to_string(owner.get(), Item{.item = ITEM_JS_UNDEFINED}, library_objects));
            } else {
                if (field->flags & JSPD_IS_ACCESSOR) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
                callable.set(map_shape_field_to_item(owner.get().array->data, field));
                if (!mvp_lmd_is_callable(callable.get())) continue;
                result.set(mvp_lmd_class_invoke(callable.get(), owner.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED}));
            }
        } else {
            String* key = heap_create_name(name, strlen(name));
            if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            callable.set(mvp_lmd_class_property(owner.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, NULL));
            if (item_is_error(callable.get())) return callable.get();
            if (!mvp_lmd_is_callable(callable.get())) continue;
            result.set(mvp_lmd_class_invoke(callable.get(), owner.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED}));
        }
        if (item_is_error(result.get())) return result.get();
        // a native Promise returned by a conversion method is still an object.
        if (!mvp_lmd_is_object(result.get())) return result.get();
    }
    return mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
