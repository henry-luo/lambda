#include "mvp_lmd_runtime.h"
#include "../js_props.h"
#include "../js_runtime.h"
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

struct MvpLmdDate : Map { double milliseconds; };
struct JsonAncestor { Item value; const JsonAncestor* parent; };
const char* const mvp_lmd_error_names[LMD_ERROR_COUNT] = {"Error", "EvalError", "RangeError",
    "ReferenceError", "SyntaxError", "TypeError", "URIError", "AggregateError"};
const char* const mvp_lmd_primitive_names[LMD_PRIMITIVE_COUNT] = {"String", "Number", "Boolean", "BigInt"};

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
    Item key = {.item = s2it(heap_create_name("prototype", 9))};
    prototype.set(mvp_lmd_property_with_receiver(target, key, ItemNull, LMD_PROP_GET, target));
    if (item_is_error(prototype.get())) return prototype.get();
    if (!mvp_lmd_is_object(prototype.get())) prototype.set(mvp_lmd_class_values(cls)[1]);
    return mvp_lmd_box_primitive(value.get(), prototype.get());
}

static Item primitive_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    intptr_t operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    bool fixed = operation == LMD_METHOD_TO_FIXED;
    int kind = fixed ? LMD_PRIMITIVE_NUMBER : (int)(operation / 2);
    bool stringify = fixed || (operation & 1);
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
    if (fixed) {
        // the generic prototype entry shares the direct MIR formatter and receiver brand check.
        Item digits = count ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(0)};
        return item_is_error(digits) ? digits : mvp_lmd_number_to_fixed(it2d(value.get()), it2d(digits));
    }
    if (kind == LMD_PRIMITIVE_NUMBER || kind == LMD_PRIMITIVE_BIGINT) {
        Item radix = count && args[0].item != ITEM_JS_UNDEFINED ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(10)};
        if (item_is_error(radix)) return radix;
        return kind == LMD_PRIMITIVE_NUMBER ? mvp_lmd_number_to_radix_string(it2d(value.get()), it2d(radix)) :
            mvp_lmd_bigint_to_string(value.get(), it2d(radix));
    }
    return mvp_lmd_primitive_to_string(value.get(), 3);
}

static Item primitive_static(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
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
    source.set(mvp_lmd_primitive_to_string(source.get()));
    if (item_is_error(source.get())) return source.get();
    if (method == LMD_METHOD_LOWER || method == LMD_METHOD_UPPER)
        return method == LMD_METHOD_LOWER ? fn_lower(source.get()) : fn_upper(source.get());
    if (method == LMD_METHOD_MATCH || method == LMD_METHOD_REPLACE)
        return mvp_lmd_regexp_string_method(mvp_lmd_program_library(), source.get(), first.get(), second.get(), method == LMD_METHOD_REPLACE);
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
    if (method == LMD_METHOD_SPLIT) {
        value.set(second.get().item == ITEM_JS_UNDEFINED ? Item{.item = i2it(UINT32_MAX)} : mvp_lmd_primitive_to_number(second.get()));
        if (item_is_error(value.get())) return value.get();
        uint32_t limit = (uint32_t)js_to_int32(it2d(value.get()));
        if (first.get().item != ITEM_JS_UNDEFINED) first.set(mvp_lmd_primitive_to_string(first.get()));
        return item_is_error(first.get()) ? first.get() : mvp_lmd_string_split(source.get(), first.get(), limit);
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
    // native Error brand while keeping the live JS prototype independent (D2.6.10).
    prototype.set(mvp_lmd_class_values(cls)[1]);
    if (target.item != ITEM_JS_UNDEFINED) {
        Item key = {.item = s2it(heap_create_name("prototype", 9))};
        prototype.set(mvp_lmd_property_with_receiver(target, key, ItemNull, LMD_PROP_GET, target));
        if (item_is_error(prototype.get())) return prototype.get();
        if (!mvp_lmd_is_object(prototype.get())) prototype.set(mvp_lmd_class_values(cls)[1]);
    }
    object.set(mvp_lmd_object_new(&cls->shape, 0));
    if (item_is_error(object.get())) return object.get();
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
        error->code == ERR_INDEX_OUT_OF_BOUNDS ? LMD_ERROR_RANGE : error->code == ERR_SYNTAX_ERROR ? LMD_ERROR_SYNTAX : LMD_ERROR_BASE;
    if (!state->errors) return carrier;
    const char* message = error->message ? error->message : "";
    String* copied = heap_strcpy(message, strlen(message));
    if (!copied) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    text.set(Item{.item = s2it(copied)});
    Item argument = text.get();
    MvpLmdClass* cls = &state->errors[kind];
    object.set(error_construct(context, cls->program, &argument, 1,
        mvp_lmd_class_values(cls)[0], ItemNull, Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(object.get())) return object.get();
    // Preserve Error identity if the same carrier crosses another catch boundary.
    error->thrown_value_item = object.get().item;
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
    if (count > 1 && args[1].item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> text(roots, mvp_lmd_primitive_to_string(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}));
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
    return !ok ? mvp_lmd_fail(LMD_MVP_SYNTAX, 0) : number ? push_d(scalar) : result;
}

static Item json_value(Item input, Item key) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!mvp_lmd_is_object(input) || !state || (!state->mutable_prototypes && !state->primitives)) return input;
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
    else if (type == LMD_TYPE_ARRAY || type == LMD_TYPE_MAP) {
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
            if (mvp_lmd_is_object(value.get())) {
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
            (count > 2 && args[2].item != ITEM_JS_UNDEFINED)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    if (mvp_lmd_is_object(value.get())) {
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
    return push_d((double)datetime_now_ms());
}
static Item date_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED || count > 1) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    double value = count ? NAN : (double)datetime_now_ms();
    if (count) {
        if (get_type_id(args[0]) == LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        Item converted = mvp_lmd_primitive_to_number(args[0]);
        if (item_is_error(converted)) return converted;
        value = it2d(converted);
    }
    value = isfinite(value) && fabs(value) <= 8640000000000000.0 ? trunc(value) + 0.0 : NAN;
    MvpLmdDate* date = (MvpLmdDate*)heap_calloc(sizeof(MvpLmdDate), LMD_TYPE_MAP);
    if (!date) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    date->type_id = LMD_TYPE_MAP; date->type = &state->date.shape; date->milliseconds = value;
    return Item{.map = date};
}
static Item date_value(Context*, MvpLmdProgram*, Item*, uint64_t, Item self, Item receiver, Item) {
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    if (get_type_id(receiver) != LMD_TYPE_MAP ||
            ((TypeMap*)receiver.map->type)->nominal != &state->date.nominal)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return push_d(((MvpLmdDate*)receiver.map)->milliseconds);
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
        }
    }
    Item result = mvp_lmd_named_set(cls->values[1], "constructor", cls->values[0]);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(cls->values[2], "prototype", cls->values[1],
        immutable_metadata ? JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE : -1);
    if (item_is_error(result)) return result;
    String* spelling = heap_create_name(name, strlen(name));
    if (!spelling) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int flags = immutable_metadata ? JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE : -1;
    result = mvp_lmd_named_set(cls->values[2], "name", Item{.item = s2it(spelling)}, flags);
    return item_is_error(result) ? result : mvp_lmd_named_set(cls->values[2], "length", Item{.item = i2it(arity)}, flags);
}
static Item object_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    Item value = count ? args[0] : ItemNull;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
        return mvp_lmd_object_new(state->object_shape, 0);
    }
    return mvp_lmd_is_object(value) ? value : mvp_lmd_box_primitive(value);
}
static Item object_string(Context*, MvpLmdProgram*, Item*, uint64_t, Item self, Item receiver, Item) {
    TypeId type = get_type_id(receiver);
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    const char* tag = type == LMD_TYPE_NULL ? "[object Null]" : type == LMD_TYPE_UNDEFINED ? "[object Undefined]" :
        type == LMD_TYPE_ARRAY ? "[object Array]" : type == LMD_TYPE_STRING ? "[object String]" :
        type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? "[object Number]" : type == LMD_TYPE_BOOL ? "[object Boolean]" :
        type == LMD_TYPE_FUNC ? "[object Function]" : type == LMD_TYPE_DECIMAL ? "[object BigInt]" :
        type == LMD_TYPE_SYMBOL ? "[object Symbol]" : "[object Object]";
    if (type == LMD_TYPE_MAP) {
        TypeNominal* nominal = ((TypeMap*)receiver.map->type)->nominal;
        if (nominal == &state->date.nominal) tag = "[object Date]";
        else if (nominal == &state->regexp.nominal) tag = "[object RegExp]";
        else if (state->errors) for (int i = 0; i < LMD_ERROR_COUNT; i++)
            if (nominal == &state->errors[i].nominal) { tag = "[object Error]"; break; }
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
    TypeId type = get_type_id(value.get());
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC) return Item{.item = ITEM_FALSE};
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
static Item object_integrity(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    Item owner = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    TypeId kind = get_type_id(owner);
    bool object = kind == LMD_TYPE_MAP || kind == LMD_TYPE_ARRAY || kind == LMD_TYPE_FUNC;
    if (!object) return operation < 3 ? owner : Item{.item = b2it(operation != 3)};
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
    TypeId kind = get_type_id(prototype), owner_kind = get_type_id(receiver);
    if ((kind != LMD_TYPE_NULL && kind != LMD_TYPE_MAP && kind != LMD_TYPE_ARRAY && kind != LMD_TYPE_FUNC) ||
            (owner_kind != LMD_TYPE_MAP && owner_kind != LMD_TYPE_ARRAY && owner_kind != LMD_TYPE_FUNC))
        return Item{.item = ITEM_JS_UNDEFINED};
    Item result = mvp_lmd_set_prototype(receiver, prototype);
    return item_is_error(result) ? result : result.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : Item{.item = ITEM_JS_UNDEFINED};
}

static Item array_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if (count == 1 && (get_type_id(args[0]) == LMD_TYPE_INT || get_type_id(args[0]) == LMD_TYPE_FLOAT)) {
        double length = it2d(args[0]);
        return length >= 0 && length <= UINT32_MAX && length == trunc(length) ? mvp_lmd_array_new((int64_t)length) : mvp_lmd_fail(LMD_MVP_RANGE, 0);
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, mvp_lmd_array_new(0));
    if (item_is_error(output.get())) return output.get();
    for (uint64_t i = 0; i < count; i++) {
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
static Item array_from(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item, Item, Item) {
    Item absent = {.item = ITEM_JS_UNDEFINED};
    Item target = mvp_lmd_array_new(0);
    if (item_is_error(target)) return target;
    return mvp_lmd_iterator_collect(program, target, count ? args[0] : absent,
        count > 1 ? args[1] : absent, count > 2 ? args[2] : absent);
}
extern "C" Item mvp_lmd_array_method_value(MvpLmdProgram* program, Item owner, Item name, int64_t callee) {
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    if (get_type_id(owner) == LMD_TYPE_STRING) return property_key_id(name.get_string()) == JS_SYMBOL_ITERATOR && state
        ? state->values[LMD_LIBRARY_SEQUENCE_ITERATOR] : mvp_lmd_property_get(owner, name, callee);
    return mvp_lmd_array_property(owner, name, ItemNull, callee ? LMD_PROP_CALLEE : LMD_PROP_GET, owner);
}
Item mvp_lmd_property_symbol(NameRef name) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Array* symbols = state->values[LMD_LIBRARY_SYMBOL_VALUES].array;
    for (int64_t i = 0; i < symbols->length; i++) {
        Item symbol = symbols->items[i];
        if (symbol.get_symbol()->name_id == name_ref_id(name)) return symbol;
    }
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
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
Item mvp_lmd_library_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    if (!context->name_pool) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    state->prototype_key = name_pool_create_unique_private(context->name_pool, {"MVP [[Prototype]]", 17});
    state->extensibility_key = name_pool_create_unique_private(context->name_pool, {"MVP [[Extensible]]", 18});
    if (!state->prototype_key || !state->extensibility_key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    state->values[LMD_LIBRARY_JSON] = mvp_lmd_object_new(&state->json_shape, 0);
    if (item_is_error(state->values[LMD_LIBRARY_JSON])) return state->values[LMD_LIBRARY_JSON];
    Item result = mvp_lmd_library_class_initialize(program, state, &state->object, LMD_LIBRARY_OBJECT, "Object", object_construct, 1);
    if (item_is_error(result)) return result;
    result = mvp_lmd_library_class_initialize(program, state, &state->date, LMD_LIBRARY_DATE, "Date", date_construct, 7);
    if (item_is_error(result)) return result;
    result = mvp_lmd_library_class_initialize(program, state, &state->array, LMD_LIBRARY_ARRAY, "Array", array_construct, 1);
    if (item_is_error(result)) return result;
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
        {"unscopables", JS_SYMBOL_UNSCOPABLES}};
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
    struct Binding { int owner; const char* key; MvpLmdNativeEntry entry; uint8_t arity; };
    const Binding functions[] = {
        {LMD_LIBRARY_JSON, "parse", json_parse, 2}, {LMD_LIBRARY_JSON, "stringify", json_stringify, 3},
        {LMD_LIBRARY_DATE_STATICS, "now", date_now, 0},
        {LMD_LIBRARY_DATE_PROTOTYPE, "getTime", date_value, 0},
        {LMD_LIBRARY_DATE_PROTOTYPE, "valueOf", date_value, 0},
        {LMD_LIBRARY_OBJECT_PROTOTYPE, "toString", object_string, 0},
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
        {LMD_LIBRARY_ARRAY_STATICS, "from", array_from, 1}};
    for (const Binding& binding : functions) {
        Item result = mvp_lmd_named_set(state->values[binding.owner], binding.key,
            mvp_lmd_native_function(program, state, binding.entry, binding.arity, binding.key));
        if (item_is_error(result)) return result;
    }
    const char* predicates[] = {"isPrototypeOf", "propertyIsEnumerable"};
    for (int i = 0; i < 2; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_PROTOTYPE], predicates[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_predicate, 1, predicates[i]));
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
            mvp_lmd_native_function(program, NULL, mvp_lmd_object_value, 0, "get [Symbol.species]"), JSPD_NON_ENUMERABLE << 8);
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
        state->primitive_key = name_pool_create_unique_private(context->name_pool, {"MVP [[PrimitiveValue]]", 22});
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
                JsPropertyDescriptor descriptor = {};
                descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
                js_pd_set_configurable(&descriptor, true);
                const char* tag = i == LMD_PRIMITIVE_BIGINT ? "BigInt" : "Symbol";
                descriptor.value = Item{.item = s2it(heap_create_name(tag, strlen(tag)))};
                Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))};
                result = mvp_lmd_define_own_property(cls->values[1], key, &descriptor);
                if (item_is_error(result)) return result;
                if (i == LMD_PRIMITIVE_COUNT) {
                    descriptor.value = mvp_lmd_native_function(program, (void*)(intptr_t)(i * 2), primitive_method, 1, "[Symbol.toPrimitive]");
                    if (item_is_error(descriptor.value)) return descriptor.value;
                    key.item = s2it(well_known_name_ref(JS_SYMBOL_TO_PRIMITIVE));
                    result = mvp_lmd_define_own_property(cls->values[1], key, &descriptor);
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
    if (!item_is_error(result)) result = mvp_lmd_generator_initialize(program, state);
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
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC && type != LMD_TYPE_ARRAY_NUM) return value;
    // recursive array conversion retains the unit's admission before allocating property names.
    if (!library_objects && type != LMD_TYPE_ARRAY) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, value), callable(roots, ItemNull), result(roots, ItemNull);
    if (mvp_lmd_program_library() && (mvp_lmd_program_library()->mutable_prototypes || mvp_lmd_program_library()->primitives)) {
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
        if (state && cls == &state->date) hint = 2;
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
        TypeId converted = get_type_id(result.get());
        if (converted != LMD_TYPE_MAP && converted != LMD_TYPE_ARRAY && converted != LMD_TYPE_FUNC && converted != LMD_TYPE_ARRAY_NUM)
            return result.get();
    }
    return mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
