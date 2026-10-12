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

extern "C" Item mvp_lmd_caught_value(MvpLmdLibraryState* state, Item carrier) {
    LambdaError* error = it2err(carrier);
    if (!error || error->code == ERR_NOT_IMPLEMENTED || error->code == ERR_POOL_EXHAUSTED) return carrier;
    if (error->thrown_value_item) return Item{.item = error->thrown_value_item};
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, carrier), object(roots, mvp_lmd_object_new(state->object_shape, 0)), text(roots, ItemNull);
    if (item_is_error(object.get())) return object.get();
    const char* name = error->code == ERR_TYPE_MISMATCH ? "TypeError" :
        error->code == ERR_UNDEFINED_VARIABLE ? "ReferenceError" :
        error->code == ERR_INDEX_OUT_OF_BOUNDS ? "RangeError" : error->code == ERR_SYNTAX_ERROR ? "SyntaxError" : "Error";
    const char* names[] = {"name", "message"};
    const char* values[] = {name, error->message ? error->message : ""};
    for (int i = 0; i < 2; i++) {
        String* copied = heap_strcpy(values[i], strlen(values[i]));
        if (!copied) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        text.set(Item{.item = s2it(copied)});
        Item stored = mvp_lmd_named_set(object.get(), names[i], text.get());
        if (item_is_error(stored)) return stored;
    }
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
    if (!mvp_lmd_is_object(input) || !state || !state->mutable_prototypes) return input;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, input), property(roots, key), method(roots, ItemNull);
    String* name = heap_create_name("toJSON", 6);
    if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    method.set(mvp_lmd_property_with_receiver(owner.get(), Item{.item = s2it(name)}, ItemNull, LMD_PROP_GET, owner.get()));
    if (item_is_error(method.get())) return method.get();
    if (!mvp_lmd_is_callable(method.get())) return owner.get();
    Item argument = property.get();
    return mvp_lmd_class_invoke(method.get(), owner.get(), &argument, 1, Item{.item = ITEM_JS_UNDEFINED});
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
        MvpLmdClass* cls, int index, const char* name, MvpLmdNativeEntry entry, uint8_t arity) {
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
    result = mvp_lmd_named_set(cls->values[2], "prototype", cls->values[1]);
    if (item_is_error(result)) return result;
    String* spelling = heap_create_name(name, strlen(name));
    if (!spelling) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    result = mvp_lmd_named_set(cls->values[2], "name", Item{.item = s2it(spelling)});
    return item_is_error(result) ? result : mvp_lmd_named_set(cls->values[2], "length", Item{.item = i2it(arity)});
}
static Item object_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    Item value = count ? args[0] : ItemNull;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
        return mvp_lmd_object_new(state->object_shape, 0);
    }
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM || type == LMD_TYPE_FUNC
        ? value : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
}
static Item object_string(Context*, MvpLmdProgram*, Item*, uint64_t, Item self, Item receiver, Item) {
    TypeId type = get_type_id(receiver);
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    const char* tag = type == LMD_TYPE_NULL ? "[object Null]" : type == LMD_TYPE_UNDEFINED ? "[object Undefined]" :
        type == LMD_TYPE_ARRAY ? "[object Array]" : type == LMD_TYPE_STRING ? "[object String]" :
        type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? "[object Number]" : type == LMD_TYPE_BOOL ? "[object Boolean]" :
        type == LMD_TYPE_FUNC ? "[object Function]" : type == LMD_TYPE_DECIMAL ? "[object BigInt]" : "[object Object]";
    if (type == LMD_TYPE_MAP) {
        TypeNominal* nominal = ((TypeMap*)receiver.map->type)->nominal;
        if (nominal == &state->date.nominal) tag = "[object Date]";
        else if (nominal == &state->regexp.nominal) tag = "[object RegExp]";
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
    return mvp_lmd_class_property(owner.get(), key.get(), ItemNull, LMD_PROP_OWN, NULL);
}
Item mvp_lmd_object_value(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    TypeId type = get_type_id(receiver);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM || type == LMD_TYPE_FUNC
        ? receiver : mvp_lmd_fail(type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
}
static Item object_predicate(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, receiver), value(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    if (operation == 1) {
        value.set(mvp_lmd_property_key(value.get()));
        if (item_is_error(value.get())) return value.get();
        JsPropertyDescriptor descriptor = {};
        Item status = mvp_lmd_get_own_property(object.get(), value.get(), &descriptor);
        return item_is_error(status) ? status : Item{.item = b2it(status.item == ITEM_TRUE && (descriptor.flags & JS_PD_ENUMERABLE))};
    }
    TypeId type = get_type_id(value.get());
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC) return Item{.item = ITEM_FALSE};
    if (!mvp_lmd_property_map(object.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
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
    if (count < 3) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]), descriptor(roots, args[2]);
    Rooted<Item> key(roots, mvp_lmd_property_key(args[1]));
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
    Rooted<Item> key(roots, mvp_lmd_property_key(count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}));
    return item_is_error(key.get()) ? key.get() : mvp_lmd_get_own_descriptor(owner.get(), key.get());
}
static Item object_projection(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    if (!count || args[0].item == ITEM_NULL || args[0].item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]), keys(roots, ItemNull), result(roots, ItemNull), value(roots, ItemNull);
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
    TypeId type = get_type_id(target.get());
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC)
        return mvp_lmd_fail(type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    for (uint64_t i = 1; i < count; i++) {
        Item result = mvp_lmd_object_copy(target.get(), args[i], 1);
        if (item_is_error(result)) return result;
    }
    return target.get();
}
static Item object_prototype(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    return mvp_lmd_get_prototype(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
}
static Item object_set_prototype(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item, Item) {
    if (count < 2) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, args[0]);
    Item status = mvp_lmd_set_prototype(owner.get(), args[1]);
    return item_is_error(status) ? status : status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : owner.get();
}
static Item object_proto_accessor(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    bool setter = (intptr_t)((MvpLmdNativeCallable*)self.function)->state != 0;
    if (!setter) return mvp_lmd_get_prototype(receiver);
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
    return method == LMD_METHOD_ARRAY_MAP || method == LMD_METHOD_FILTER || method == LMD_METHOD_FOREACH ||
        method == LMD_METHOD_SOME || method == LMD_METHOD_REDUCE ? mvp_lmd_array_visit(receiver, args, count, method) :
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
    state->values[LMD_LIBRARY_SYMBOL_VALUES] = mvp_lmd_array_new(0);
    if (item_is_error(state->values[LMD_LIBRARY_SYMBOL_VALUES])) return state->values[LMD_LIBRARY_SYMBOL_VALUES];
    const struct { const char* name; NameId id; } symbols[] = {
        {"iterator", JS_SYMBOL_ITERATOR}, {"toPrimitive", JS_SYMBOL_TO_PRIMITIVE},
        {"hasInstance", JS_SYMBOL_HAS_INSTANCE}, {"toStringTag", JS_SYMBOL_TO_STRING_TAG},
        {"species", JS_SYMBOL_SPECIES}, {"isConcatSpreadable", JS_SYMBOL_IS_CONCAT_SPREADABLE}};
    for (int i = 0; i < (state->mutable_prototypes ? 6 : 1); i++) {
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
            mvp_lmd_native_function(program, state, binding.entry, binding.arity));
        if (item_is_error(result)) return result;
    }
    const char* predicates[] = {"isPrototypeOf", "propertyIsEnumerable"};
    for (int i = 0; i < 2; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_PROTOTYPE], predicates[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_predicate, 1));
        if (item_is_error(result)) return result;
    }
    const char* projections[] = {"keys", "values", "entries", "hasOwn", "getOwnPropertyNames", "getOwnPropertySymbols", "getOwnPropertyDescriptors"};
    for (int i = 0; i < 7; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_STATICS], projections[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_projection, i == 3 ? 2 : 1));
        if (item_is_error(result)) return result;
    }
    const char* integrity[] = {"preventExtensions", "seal", "freeze", "isExtensible", "isSealed", "isFrozen"};
    for (int i = 0; i < 6; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_OBJECT_STATICS], integrity[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, object_integrity, 1));
        if (item_is_error(result)) return result;
    }
    {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> key(roots, Item{.item = s2it(heap_create_name("__proto__", 9))});
        Rooted<Item> callable(roots, ItemNull);
        for (int i = 0; i < 2; i++) {
            callable.set(mvp_lmd_native_function(program, (void*)(intptr_t)i, object_proto_accessor, i));
            if (item_is_error(callable.get())) return callable.get();
            Item defined = mvp_lmd_define_accessor(state->values[LMD_LIBRARY_OBJECT_PROTOTYPE], key.get(), callable.get(),
                (JSPD_NON_ENUMERABLE << 8) | i);
            if (item_is_error(defined)) return defined;
        }
    }
    if (state->mutable_prototypes) {
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))};
        result = mvp_lmd_define_accessor(state->values[LMD_LIBRARY_ARRAY], key,
            mvp_lmd_native_function(program, NULL, mvp_lmd_object_value, 0), JSPD_NON_ENUMERABLE << 8);
        if (item_is_error(result)) return result;
    }
    for (int i = 0; i < mvp_lmd_builtin_method_count; i++) {
        const MvpLmdBuiltinMethod& method = mvp_lmd_builtin_methods[i];
        if (method.owner != LMD_TYPE_ARRAY) continue;
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], method.name,
            mvp_lmd_native_function(program, (void*)(intptr_t)method.id, array_method, method.id == LMD_METHOD_SPLICE ? 2 : 1));
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_regexp_initialize(program, state);
    if (item_is_error(result)) return result;
    result = mvp_lmd_function_initialize(program, state);
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
    if (mvp_lmd_program_library() && mvp_lmd_program_library()->mutable_prototypes) {
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
