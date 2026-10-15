#include "mvp_lmd_runtime.h"
#include "../js_regexp_compile.h"
#include "../js_runtime.h"
#include "../js_props.h"
#include "../js_property_attrs.h"
#include "../js_builtin_catalog.hpp"
#include "../js_well_known_names.h"
#include "../js_bt_regex.h"
#include "../js_regex_wrapper.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/utf.h"
#include "../../../lib/mem.h"
#include <math.h>

struct MvpLmdRegExp : Map {
    JsBtRegex* compiled;         // immutable AST in the execution pool; contains no Items
    JsRegExpCompileInfo flags;
    // pool-owned original spellings are independent of public metadata and the compiled AST.
    const char* original_source;
    const char* original_flags;
    uint32_t original_source_len;
    uint32_t original_flags_len;
};
struct MvpLmdRegExpAccessor {
    const JsBuiltinMethodSpec* spec;
    NameRef key;
    ShapeEntry* field;
};

static MvpLmdRegExp* regexp_record(Item value, MvpLmdLibraryState* state) {
    return state && get_type_id(value) == LMD_TYPE_MAP &&
        ((TypeMap*)value.map->type)->nominal == &state->regexp.nominal ? (MvpLmdRegExp*)value.map : NULL;
}
static StrBuf* regexp_buffer(String* source, bool unicode) {
    StrBuf* buffer = strbuf_new();
    if (!buffer) return NULL;
    if (unicode) strbuf_append_str_n(buffer, source->chars, source->len);
    else {
        // The shared matcher consumes code points; non-u JS patterns consume UTF-16 units.
        Utf16Iterator it = {(const unsigned char*)source->chars, source->len, 0, -1};
        uint16_t unit;
        while (utf16_iterator_next(&it, &unit)) {
            char bytes[4]; size_t length = utf8_encode_wtf8(unit, bytes);
            strbuf_append_str_n(buffer, bytes, length);
        }
    }
    return buffer;
}
struct MvpLmdRegExpInput {
    StrBuf view = {};
    StrBuf* owned = NULL;
    size_t units = 0;
    bool ascii = false;
    MvpLmdRegExpInput(String* source, bool unicode) {
        if (!source) return;
        ascii = source->is_ascii;
        // strings are rooted by the caller; only non-u surrogate splitting needs a copy.
        if (ascii || unicode) view = {source->chars, source->len, source->len};
        else {
            owned = regexp_buffer(source, false);
            if (owned) view = *owned;
        }
        units = ascii ? view.length : utf8_to_utf16_length(view.str, view.length);
    }
    ~MvpLmdRegExpInput() { if (owned) strbuf_free(owned); }
    size_t byte_offset(size_t unit) const {
        return ascii ? (unit < view.length ? unit : view.length) :
            utf16_to_utf8_offset(view.str, view.length, unit);
    }
    size_t unit_offset(size_t byte) const {
        return ascii ? byte : utf8_to_utf16_offset(view.str, view.length, byte);
    }
};
static Item regexp_exec(MvpLmdLibraryState*, Item, Item, bool, const MvpLmdRegExpInput* = NULL);
static Item regexp_string_native(MvpLmdLibraryState*, Item, Item, Item, int64_t);
static Item regexp_protocol(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
static Item regexp_create(Item pattern, Item flags);

static NameId regexp_symbol_id(int operation) {
    switch (operation) {
        case JS_BUILTIN_REGEXP_SYMBOL_MATCH: return JS_SYMBOL_MATCH;
        case JS_BUILTIN_REGEXP_SYMBOL_MATCHALL: return JS_SYMBOL_MATCH_ALL;
        case JS_BUILTIN_REGEXP_SYMBOL_SEARCH: return JS_SYMBOL_SEARCH;
        case JS_BUILTIN_REGEXP_SYMBOL_REPLACE: return JS_SYMBOL_REPLACE;
        case JS_BUILTIN_REGEXP_SYMBOL_SPLIT: return JS_SYMBOL_SPLIT;
        default: return 0;
    }
}
static bool regexp_ordinary(MvpLmdLibraryState* state, Item value, int symbol_operation = 0,
        bool check_flags = true) {
    MvpLmdRegExp* object = regexp_record(value, state);
    if (!object) return false;
    TypeMap* shape = (TypeMap*)object->type;
    // only the original private slots and lastIndex can be present; no observable own overrides.
    if (shape->length != 3) return false;
    ShapeEntry* link = typemap_hash_lookup_key(shape, state->prototype_key);
    if (link && map_shape_field_to_item(object->data, link).item != state->regexp.values[1].item) return false;
    Map* prototype = state->regexp.values[1].map;
    TypeMap* prototype_shape = (TypeMap*)prototype->type;
    // accessor shapes are private and may mutate in place: prove each getter from raw descriptor storage.
    for (int i = 0; check_flags && !state->regexp_protocol_unobserved && i < state->regexp_accessor_count; i++) {
        const MvpLmdRegExpAccessor& accessor = state->regexp_accessors[i];
        const JsBuiltinMethodSpec* spec = accessor.spec;
        // match/replace observe flags, but never the pattern's public source getter.
        if (spec->builtin_id == JS_BUILTIN_REGEXP_GET_SOURCE) continue;
        // initial entries remain owned by the rooted prototype only while its shape matches.
        ShapeEntry* field = prototype_shape == state->regexp_accessor_shape ? accessor.field :
            typemap_hash_lookup_key(prototype_shape, accessor.key);
        JsAccessorCell* cell = js_shape_entry_accessor_pair(field);
        if (!cell || (field->flags & JSPD_DELETED) || get_type_id(cell->getter) != LMD_TYPE_FUNC ||
                cell->getter.function->ptr != (fn_ptr)regexp_protocol ||
                (intptr_t)((MvpLmdNativeCallable*)cell->getter.function)->state != spec->builtin_id) return false;
    }
    ShapeEntry* exec = typemap_hash_lookup((TypeMap*)prototype->type, "exec", 4);
    if (!exec || (exec->flags & (JSPD_IS_ACCESSOR | JSPD_DELETED)) ||
            map_shape_field_to_item(prototype->data, exec).item != state->values[LMD_LIBRARY_REGEXP_EXEC].item) return false;
    ShapeEntry* last = typemap_hash_lookup(shape, "lastIndex", 9);
    if (!last || (last->flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE))) return false;
    TypeId number = get_type_id(map_shape_field_to_item(object->data, last));
    if (number != LMD_TYPE_INT && number != LMD_TYPE_FLOAT && number != LMD_TYPE_INT64) return false;
    if (symbol_operation) {
        NameRef key = well_known_name_ref(regexp_symbol_id(symbol_operation));
        ShapeEntry* method = typemap_hash_lookup_key((TypeMap*)prototype->type, key);
        if (!method || (method->flags & (JSPD_IS_ACCESSOR | JSPD_DELETED))) return false;
        Item callable = map_shape_field_to_item(prototype->data, method);
        if (get_type_id(callable) != LMD_TYPE_FUNC || callable.function->ptr != (fn_ptr)regexp_protocol ||
                (intptr_t)((MvpLmdNativeCallable*)callable.function)->state != symbol_operation) return false;
    }
    return true;
}

static const JsRegExpOps regexp_ops = {
    [](Item object, const char* name) {
        String* key = heap_create_name(name, strlen(name));
        return key ? mvp_lmd_property_with_receiver(object, Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, object)
            : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    },
    [](Item object, Item value) {
        String* key = heap_create_name("lastIndex", 9);
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        // strict Set is required even in sloppy JS. Keep a resolved writable native data slot direct.
        MvpLmdRegExp* native = regexp_record(object, mvp_lmd_program_library());
        ShapeEntry* field = native ? typemap_hash_lookup((TypeMap*)native->type, "lastIndex", 9) : NULL;
        if (field && !(field->flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE)))
            return map_shape_set_resolved(native, key, field, value, false) ? ItemNull : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        return mvp_lmd_property_with_receiver(object, Item{.item = s2it(key)}, value,
            LMD_PROP_SET | LMD_PROP_STRICT, object);
    },
    [](Item value) { return mvp_lmd_primitive_to_string(value); },
    [](Item value) { return mvp_lmd_primitive_to_number(value); },
    mvp_lmd_is_object, mvp_lmd_is_callable,
    [](Item value) { return !!mvp_lmd_truthy(value); }, mvp_lmd_same_value,
    [](Item function, Item receiver, Item* args, int count) {
        return mvp_lmd_class_invoke(function, receiver, args, count, Item{.item = ITEM_JS_UNDEFINED});
    },
    [](Item object, Item string, bool test) { return regexp_exec(mvp_lmd_program_library(), object, string, test); },
    []() { return mvp_lmd_program_library()->values[LMD_LIBRARY_REGEXP_EXEC]; },
    [](Item object, int operation) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        MvpLmdRegExp* native = regexp_record(object, state);
        if (!native) {
            if (object.item != state->regexp.values[1].item) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            return operation == JS_BUILTIN_REGEXP_GET_SOURCE ? Item{.item = s2it(heap_strcpy("(?:)", 4))}
                : Item{.item = ITEM_JS_UNDEFINED};
        }
        if (operation == JS_BUILTIN_REGEXP_GET_SOURCE) {
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)native->type, state->regexp_source_key);
            return map_shape_field_to_item(native->data, field);
        }
        bool flag;
        switch (operation) {
            case JS_BUILTIN_REGEXP_GET_GLOBAL: flag = native->flags.global; break;
            case JS_BUILTIN_REGEXP_GET_IGNORECASE: flag = native->flags.ignore_case; break;
            case JS_BUILTIN_REGEXP_GET_MULTILINE: flag = native->flags.multiline; break;
            case JS_BUILTIN_REGEXP_GET_DOTALL: flag = native->flags.dot_all; break;
            case JS_BUILTIN_REGEXP_GET_UNICODE: flag = native->flags.unicode; break;
            case JS_BUILTIN_REGEXP_GET_UNICODE_SETS: flag = native->flags.unicode_sets; break;
            case JS_BUILTIN_REGEXP_GET_STICKY: flag = native->flags.sticky; break;
            case JS_BUILTIN_REGEXP_GET_HASINDICES: flag = native->flags.has_indices; break;
            default: return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        return Item{.item = b2it(flag)};
    },
    [](int length) { return mvp_lmd_array_new(length); },
    [](Item array, Item value) { return mvp_lmd_array_store(array, array.array->length, value); },
    [](const char*) { return mvp_lmd_fail(LMD_MVP_TYPE, 0); },
    [](Item object, Item string, bool* admitted) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        *admitted = regexp_ordinary(state, object);
        return *admitted ? regexp_string_native(state, string, object, ItemNull, 0) : ItemNull;
    },
    NULL, NULL,
    [](Item object, Item key) { return mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, object); },
    [](Item value) { return mvp_lmd_box_primitive(value); },
    mvp_lmd_is_constructor,
    [](Item constructor, Item* args, int count) {
        return mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, args, count, constructor);
    },
    []() { return mvp_lmd_program_library()->regexp.values[0]; },
    [](Item object, Item string, Item replacement, bool* admitted) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        *admitted = regexp_ordinary(state, object);
        return *admitted ? regexp_string_native(state, string, object, replacement, 1) : ItemNull;
    },
    NULL, // MVP's existing matcher input already represents each non-u UTF-16 unit.
    [](Item value) { return regexp_record(value, mvp_lmd_program_library()) != NULL; },
    regexp_create
};

static const JsRegExpConstructorOps regexp_constructor_ops = {
    &regexp_ops,
    [](Item value, bool flags) {
        MvpLmdRegExp* native = regexp_record(value, mvp_lmd_program_library());
        String* text = native ? heap_strcpy(flags ? native->original_flags : native->original_source,
            flags ? native->original_flags_len : native->original_source_len) : NULL;
        return text ? Item{.item = s2it(text)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    },
    [](Item target) {
        Item prototype = regexp_ops.get(target, "prototype");
        return item_is_error(prototype) || mvp_lmd_is_object(prototype) ? prototype :
            mvp_lmd_program_library()->regexp.values[1];
    },
    [](Item pattern, Item flags, Item prototype) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> base(roots, prototype), result(roots, ItemNull);
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        result.set(mvp_lmd_regexp_new(state, pattern, flags));
        if (item_is_error(result.get())) return result.get();
        // the canonical prototype is already supplied by the unchanged native shape.
        if (base.get().item == state->regexp.values[1].item) return result.get();
        Item linked = mvp_lmd_set_prototype(result.get(), base.get());
        return item_is_error(linked) ? linked : result.get();
    },
    [](Item receiver, Item replacement) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> target(roots, receiver), source(roots, replacement);
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        MvpLmdRegExp* object = regexp_record(target.get(), state);
        MvpLmdRegExp* next = regexp_record(source.get(), state);
        if (!object || !next) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        // native ASTs and original spellings are Input-pool owned; only private Map fields contain GC Items.
        object->compiled = next->compiled; object->flags = next->flags;
        object->original_source = next->original_source; object->original_source_len = next->original_source_len;
        object->original_flags = next->original_flags; object->original_flags_len = next->original_flags_len;
        NameRef keys[] = {state->regexp_source_key, state->regexp_flags_key};
        for (int i = 0; i < 2; i++) {
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)source.get().map->type, keys[i]);
            if (!map_shape_set(target.get().map, keys[i], map_shape_field_to_item(source.get().map->data, field)))
                return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        return target.get();
    }
};
static Item regexp_create(Item pattern, Item flags) {
    return js_regexp_initialize_with_ops(pattern, flags,
        mvp_lmd_program_library()->regexp.values[1], &regexp_constructor_ops);
}

static const JsRegExpIteratorOps regexp_iterator_ops = {
    [](Item regexp, Item string, bool global, bool unicode) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        if (!state || !state->regexp_iterator) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> matcher(roots, regexp), source(roots, string);
        Rooted<Item> output(roots, mvp_lmd_object_new(&state->regexp_iterator->shape, 0));
        if (item_is_error(output.get())) return output.get();
        // ordinary Map transitions preserve the physical nominal brand and trace both retained Items.
        for (int i = 0; i < 5; i++) {
            Item value = i == 0 ? matcher.get() : i == 1 ? source.get() : Item{.item = b2it(i == 2 ? global : i == 3 ? unicode : false)};
            if (!map_shape_set(output.get().map, state->regexp_iterator_keys[i], value))
                return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        return output.get();
    },
    [](Item iterator, Item* values, bool* flags) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        if (!state || !state->regexp_iterator || get_type_id(iterator) != LMD_TYPE_MAP) return false;
        TypeMap* shape = (TypeMap*)iterator.map->type;
        if (!shape || shape->nominal != &state->regexp_iterator->nominal) return false;
        // own private slots and physical nominal identity cannot be forged through a prototype or Proxy.
        for (int i = 0; i < 5; i++) {
            ShapeEntry* field = typemap_hash_lookup_key(shape, state->regexp_iterator_keys[i]);
            if (!field) return false;
            Item value = map_shape_field_to_item(iterator.map->data, field);
            if (i < 2) values[i] = value;
            else flags[i - 2] = value.item == ITEM_TRUE;
        }
        return true;
    },
    [](Item iterator) {
        NameRef key = mvp_lmd_program_library()->regexp_iterator_keys[4];
        ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)iterator.map->type, key);
        return map_shape_set_resolved(iterator.map, key, field, Item{.item = ITEM_TRUE}, false)
            ? ItemNull : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    },
    [](Item value, bool done) { return mvp_lmd_iterator_result(NULL, value, done); }
};

static Item regexp_protocol(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (operation == JS_BUILTIN_REGEXP_COMPILE)
        return js_regexp_compile_with_ops(receiver, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
            count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, &regexp_constructor_ops);
    if (operation == JS_BUILTIN_REGEXP_SYMBOL_MATCHALL)
        return js_regexp_match_all_with_ops(receiver, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, &regexp_ops, &regexp_iterator_ops);
    if (operation == JS_BUILTIN_REGEXP_MATCHALL_ITER_NEXT)
        return js_regexp_match_all_next_with_ops(receiver, &regexp_ops, &regexp_iterator_ops);
    // primitive strings cannot reenter before Get(exec); test reads no public flag getters.
    if (operation == JS_BUILTIN_REGEXP_TEST && count &&
            get_type_id(args[0]) == LMD_TYPE_STRING && regexp_ordinary(state, receiver, 0, false))
        return regexp_exec(state, receiver, args[0], true);
    return js_regexp_protocol_with_ops(operation, receiver, args, count, &regexp_ops);
}

extern "C" Item mvp_lmd_regexp_new(MvpLmdLibraryState* state, Item pattern, Item flags) {
    if (!state) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, pattern), options(roots, flags), owner(roots, ItemNull), text(roots, ItemNull);
    source.set(source.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_strcpy("", 0))} :
        mvp_lmd_primitive_to_string(source.get()));
    if (item_is_error(source.get())) return source.get();
    options.set(options.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_strcpy("", 0))} :
        mvp_lmd_primitive_to_string(options.get()));
    if (item_is_error(options.get())) return options.get();
    String* spelling = source.get().get_string();
    StrBuf* normalized_names = NULL;
    if (!js_regexp_decode_name_escapes(spelling->chars, spelling->len, &normalized_names))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    text.set(source.get());
    if (normalized_names) {
        // the existing text root owns normalization across allocation; source retains exact public spelling.
        String* normalized = heap_strcpy(normalized_names->str, normalized_names->length);
        strbuf_free(normalized_names);
        if (!normalized) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        text.set(Item{.item = s2it(normalized)});
    }
    JsRegExpCompileInfo info = {};
    String* compiled_source = text.get().get_string(), *option = options.get().get_string();
    if (!js_regexp_compile_frontend(compiled_source->chars, compiled_source->len, option->chars, option->len, &info))
        return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    if (info.unicode_sets) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    StrBuf* input = regexp_buffer(compiled_source, info.unicode);
    if (!input) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    JsBtFlags matching = {info.ignore_case, info.multiline, info.dot_all, info.unicode, info.sticky};
    JsBtRegex* compiled = js_bt_compile(input->str, input->length, matching, context->pool);
    strbuf_free(input);
    if (!compiled) return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    // exact original bytes survive moving strings, public overrides, and custom prototypes.
    spelling = source.get().get_string(); option = options.get().get_string();
    uint32_t source_len = spelling->len, flags_len = option->len;
    char* originals = (char*)pool_alloc(context->pool, (size_t)source_len + flags_len + 2);
    if (!originals) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    memcpy(originals, spelling->chars, source_len); originals[source_len] = 0;
    memcpy(originals + source_len + 1, option->chars, flags_len); originals[source_len + flags_len + 1] = 0;
    // Shared Map storage owns the fields; the extra payload contains no GC references.
    MvpLmdRegExp* object = (MvpLmdRegExp*)heap_calloc(sizeof(MvpLmdRegExp), LMD_TYPE_MAP);
    if (!object) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    object->type_id = LMD_TYPE_MAP; object->type = &state->regexp.shape;
    object->compiled = compiled; object->flags = info;
    object->original_source = originals; object->original_source_len = source_len;
    object->original_flags = originals + source_len + 1; object->original_flags_len = flags_len;
    owner.set(Item{.map = object});
    if (!map_fill_reserve_data(object)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    StrBuf* escaped = strbuf_new();
    if (!escaped) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    spelling = source.get().get_string();
    if (!spelling->len) strbuf_append_str(escaped, "(?:)");
    for (uint32_t i = 0; i < spelling->len; i++) {
        char c = spelling->chars[i];
        if (c == '\\' && i + 1 < spelling->len) {
            strbuf_append_char(escaped, c); strbuf_append_char(escaped, spelling->chars[++i]);
        } else if (c == '/') strbuf_append_str(escaped, "\\/");
        else if (c == '\n') strbuf_append_str(escaped, "\\n");
        else if (c == '\r') strbuf_append_str(escaped, "\\r");
        else if ((unsigned char)c == 0xE2 && i + 2 < spelling->len &&
                (unsigned char)spelling->chars[i+1] == 0x80 &&
                ((unsigned char)spelling->chars[i+2] == 0xA8 || (unsigned char)spelling->chars[i+2] == 0xA9)) {
            strbuf_append_str(escaped, (unsigned char)spelling->chars[i+2] == 0xA8 ? "\\u2028" : "\\u2029"); i += 2;
        } else strbuf_append_char(escaped, c);
    }
    text.set(Item{.item = s2it(heap_strcpy(escaped->str, escaped->length))}); strbuf_free(escaped);
    // private Map fields are traced by the ordinary shape walker; the native payload has no Items.
    if (!map_shape_set(owner.get().map, state->regexp_source_key, text.get())) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    text.set(Item{.item = s2it(heap_strcpy(info.canonical_flags, info.canonical_flags_len))});
    if (!map_shape_set(owner.get().map, state->regexp_flags_key, text.get())) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item result = mvp_lmd_named_set(owner.get(), "lastIndex", Item{.item = i2it(0)},
        JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE);
    return item_is_error(result) ? result : owner.get();
}

static const JsRegExpIndicesOps regexp_indices_ops = {
    &regexp_ops,
    []() {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0));
        if (item_is_error(object.get())) return object.get();
        Item linked = mvp_lmd_set_prototype(object.get(), ItemNull);
        return item_is_error(linked) ? linked : object.get();
    },
    [](Item object, Item key, Item value) {
        return mvp_lmd_property_with_receiver(object, key, value, LMD_PROP_CREATE_DATA, object);
    }
};
static Item regexp_result(MvpLmdLibraryState* state, Item source, const MvpLmdRegExpInput* input,
        JsBtRegex* compiled, int* starts, int* ends, int count) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> original(roots, source), result(roots, mvp_lmd_array_new(0));
    Rooted<Item> value(roots, ItemNull), groups(roots, Item{.item = ITEM_JS_UNDEFINED});
    if (item_is_error(result.get())) return result.get();
    for (int i = 0; i < count; i++) {
        value.set(Item{.item = ITEM_JS_UNDEFINED});
        if (starts[i] >= 0) {
            size_t a = input->unit_offset(starts[i]);
            size_t b = input->unit_offset(ends[i]);
            value.set(mvp_lmd_string_range(original.get(), a, b, 1));
            if (item_is_error(value.get())) return value.get();
        }
        Item stored = mvp_lmd_array_store(result.get(), i, value.get());
        if (item_is_error(stored)) return stored;
    }
    int named = compiled ? js_bt_named_count(compiled) : 0;
    if (named) {
        groups.set(mvp_lmd_object_new(state->object_shape, 0));
        if (item_is_error(groups.get())) return groups.get();
        Item linked = mvp_lmd_set_prototype(groups.get(), ItemNull);
        if (item_is_error(linked)) return linked;
        for (int i = 0; i < named; i++) {
            int length; const char* key = js_bt_named_name(compiled, i, &length);
            String* name = heap_create_name(key, length);
            if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item stored = mvp_lmd_property_with_receiver(groups.get(), Item{.item = s2it(name)},
                result.get().array->items[js_bt_named_index(compiled, i)], LMD_PROP_CREATE_DATA, groups.get());
            if (item_is_error(stored)) return stored;
        }
    }
    Item stored = mvp_lmd_named_set(result.get(), "index", Item{.item = i2it(input->unit_offset(starts[0]))}, state->regexp_protocol_unobserved ? -1 : 0);
    if (item_is_error(stored)) return stored;
    stored = mvp_lmd_named_set(result.get(), "input", original.get(), state->regexp_protocol_unobserved ? -1 : 0);
    if (item_is_error(stored)) return stored;
    stored = mvp_lmd_named_set(result.get(), "groups", groups.get(), state->regexp_protocol_unobserved ? -1 : 0);
    return item_is_error(stored) ? stored : result.get();
}
// keep /d scratch out of the ordinary native exec frame.
__attribute__((noinline))
static Item regexp_indices(Item result, const MvpLmdRegExpInput* input,
        JsBtRegex* compiled, const int* matches, int count) {
    // only /d enters this adapter; ordinary native matches reserve no offset scratch.
    JsRegexScratch<JsRegExpIndexSpan> spans(count);
    int named = js_bt_named_count(compiled);
    JsRegexScratch<JsRegExpIndexName> names(named);
    if (spans.count != count || names.count != named) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    for (int i = 0; i < count; i++) spans.slots[i] = matches[i] < 0 ? JsRegExpIndexSpan{-1, -1} :
        JsRegExpIndexSpan{(int64_t)input->unit_offset(matches[i]), (int64_t)input->unit_offset(matches[count + i])};
    for (int i = 0; i < named; i++) {
        names.slots[i].chars = js_bt_named_name(compiled, i, &names.slots[i].length);
        names.slots[i].capture = js_bt_named_index(compiled, i);
    }
    return js_regexp_attach_indices_with_ops(result, spans.slots, count,
        names.slots, named, &regexp_indices_ops);
}
static Item regexp_exec(MvpLmdLibraryState* state, Item regex, Item string, bool test,
        const MvpLmdRegExpInput* prepared) {
    MvpLmdRegExp* object = regexp_record(regex, state);
    if (!object) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, regex), source(roots, string), result(roots, ItemNull);
    source.set(mvp_lmd_primitive_to_string(source.get()));
    if (item_is_error(source.get())) return source.get();
    ShapeEntry* field = typemap_hash_lookup((TypeMap*)object->type, "lastIndex", 9);
    Item number = mvp_lmd_primitive_to_number(map_shape_field_to_item(object->data, field));
    if (item_is_error(number)) return number;
    double start = it2d(number);
    bool stateful = object->flags.global || object->flags.sticky;
    start = !stateful || isnan(start) || start < 0 ? 0 : floor(start);
    MvpLmdRegExpInput local(prepared ? NULL : source.get().get_string(), object->flags.unicode);
    const MvpLmdRegExpInput* input = prepared ? prepared : &local;
    if (!input->view.str) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int count = js_bt_group_count(object->compiled) + 1;
    JsRegexScratch<int> scratch(count * 2);
    if (scratch.count < count * 2) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int* matches = scratch.slots;
    int found = 0;
    if (start <= input->units) found = js_bt_exec(object->compiled, input->view.str, input->view.length,
        input->byte_offset((size_t)start), object->flags.sticky,
        matches, matches + count, count);
    if (found < 0) result.set(mvp_lmd_fail(found == JS_BT_EXEC_ALLOCATION_FAILURE ? LMD_MVP_MEMORY : LMD_MVP_RANGE, 0));
    else {
        if (stateful) result.set(regexp_ops.set_last_index(held.get(), Item{.item = i2it(found ?
            input->unit_offset(matches[count]) : 0)}));
        if (!item_is_error(result.get())) result.set(!found ? (test ? Item{.item = ITEM_FALSE} : ItemNull) :
            test ? Item{.item = ITEM_TRUE} : regexp_result(state, source.get(), input, object->compiled, matches, matches + count, count));
        if (!test && found && !item_is_error(result.get()) && object->flags.has_indices)
            result.set(regexp_indices(result.get(), input, object->compiled, matches, count));
    }
    return result.get();
}
static Item regexp_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    Item pattern = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    Item flags = count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED};
    return js_regexp_construct_with_ops(pattern, flags, self, target, &regexp_constructor_ops);
}
Item mvp_lmd_regexp_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    state->regexp_source_key = name_pool_create_unique_private(context->name_pool, {"MVP [[RegExpSource]]", 20});
    state->regexp_flags_key = name_pool_create_unique_private(context->name_pool, {"MVP [[RegExpFlags]]", 19});
    if (!state->regexp_source_key || !state->regexp_flags_key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item result = mvp_lmd_library_class_initialize(program, state, &state->regexp,
        LMD_LIBRARY_REGEXP, "RegExp", regexp_construct, 2, true);
    if (item_is_error(result)) return result;
    ((MvpLmdCallable*)state->regexp.values[0].function)->custom_new_target = true;
    state->regexp_accessor_count = 0;
    for (int i = 0; ; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
        if (!method) break;
        if (method->owner == JS_BUILTIN_OWNER_REGEXP_ACCESSOR_INTERNAL) state->regexp_accessor_count++;
    }
    state->regexp_accessors = (MvpLmdRegExpAccessor*)pool_calloc(context->pool,
        state->regexp_accessor_count * sizeof(MvpLmdRegExpAccessor));
    if (!state->regexp_accessors) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (state->regexp_iterator) {
        MvpLmdClass* cls = state->regexp_iterator;
        result = mvp_lmd_iterator_prototype_initialize(program, cls, state->regexp_iterator_values, "RegExp String Iterator");
        if (item_is_error(result)) return result;
        const char* names[] = {"RegExp iterator matcher", "RegExp iterator string", "RegExp iterator global",
            "RegExp iterator unicode", "RegExp iterator done"};
        for (int i = 0; i < 5; i++) {
            state->regexp_iterator_keys[i] = name_pool_create_unique_private(context->name_pool, {names[i], strlen(names[i])});
            if (!state->regexp_iterator_keys[i]) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
    }
    int accessor = 0;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, ItemNull);
    for (int i = 0; ; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
        if (!method) break;
        bool symbol = method->owner == JS_BUILTIN_OWNER_REGEXP_SYMBOL_INTERNAL;
        bool iterator = state->regexp_iterator && method->owner == JS_BUILTIN_OWNER_REGEXP_ITERATOR_INTERNAL;
        if (method->owner != JS_BUILTIN_OWNER_REGEXP_PROTOTYPE_METHOD &&
                method->owner != JS_BUILTIN_OWNER_REGEXP_ACCESSOR_INTERNAL && !symbol && !iterator) continue;
        if (symbol && !regexp_symbol_id(method->builtin_id)) continue;
        function.set(mvp_lmd_native_function(program, (void*)(intptr_t)method->builtin_id,
            regexp_protocol, method->param_count, method->display_name ? method->display_name : method->name));
        if (item_is_error(function.get())) return function.get();
        if (method->builtin_id == JS_BUILTIN_REGEXP_EXEC) state->values[LMD_LIBRARY_REGEXP_EXEC] = function.get();
        if (method->property_kind == JS_BUILTIN_PROPERTY_ACCESSOR) {
            String* key = heap_create_name(method->name, method->len);
            if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            state->regexp_accessors[accessor++] = {method, key, NULL};
            result = mvp_lmd_define_accessor(state->regexp.values[1], Item{.item = s2it(key)},
                function.get(), JSPD_NON_ENUMERABLE << 8);
        } else if (symbol) {
            JsPropertyDescriptor descriptor = {};
            descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
                JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            descriptor.value = function.get(); js_pd_set_configurable(&descriptor, true);
            result = mvp_lmd_define_own_property(state->regexp.values[1], Item{.item = s2it(well_known_name_ref(
                regexp_symbol_id(method->builtin_id)))}, &descriptor);
        } else result = mvp_lmd_named_set(iterator ? state->regexp_iterator_values[1] : state->regexp.values[1],
            method->name, function.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    function.set(mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0, "get [Symbol.species]"));
    if (item_is_error(function.get())) return function.get();
    result = mvp_lmd_define_accessor(state->regexp.values[0], Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))},
        function.get(), JSPD_NON_ENUMERABLE << 8);
    if (item_is_error(result)) return result;
    // pool-owned layout metadata borrows no Map data buffer or getter Item across GC.
    state->regexp_accessor_shape = (TypeMap*)state->regexp.values[1].map->type;
    for (int i = 0; i < state->regexp_accessor_count; i++) {
        MvpLmdRegExpAccessor& entry = state->regexp_accessors[i];
        entry.field = typemap_hash_lookup_key(state->regexp_accessor_shape, entry.key);
    }
    return result;
}

static Item regexp_property(Item owner, const char* key) {
    TypeMap* type = get_type_id(owner) == LMD_TYPE_ARRAY ? (TypeMap*)owner.array->type : (TypeMap*)owner.map->type;
    ShapeEntry* field = typemap_hash_lookup(type, key, strlen(key));
    return field ? map_shape_field_to_item(owner.map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
}
static Item regexp_replacement(Item source, Item match, Item replacement) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> original(roots, source), record(roots, match), replace(roots, replacement);
    Rooted<Item> piece(roots, ItemNull), groups(roots, regexp_property(match, "groups"));
    int count = match.array->length;
    int64_t start = it2d(regexp_property(match, "index"));
    String* full = match.array->items[0].get_string();
    if (mvp_lmd_is_callable(replacement)) {
        RootSpan arguments(count + 3);
        if (!arguments.valid()) return ItemError;
        for (int i = 0; i < count; i++) arguments.items()[i] = record.get().array->items[i];
        arguments.items()[count] = Item{.item = i2it(start)};
        arguments.items()[count + 1] = original.get();
        arguments.items()[count + 2] = groups.get();
        piece.set(mvp_lmd_class_invoke(replace.get(), Item{.item = ITEM_JS_UNDEFINED}, arguments.items(),
            count + (groups.get().item == ITEM_JS_UNDEFINED ? 2 : 3), Item{.item = ITEM_JS_UNDEFINED}));
        return item_is_error(piece.get()) ? piece.get() : mvp_lmd_primitive_to_string(piece.get());
    }
    StrBuf* buffer = strbuf_new();
    if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    String* text = replace.get().get_string();
    piece.set(js_apply_replacement_from_items(buffer, text->chars, text->len,
        original.get().get_string()->chars, original.get().get_string()->len, start,
        full->chars, full->len, record.get().array->items + 1, count - 1, groups.get(),
        &regexp_ops, original.get(), !original.get().get_string()->is_ascii));
    if (!item_is_error(piece.get())) {
        String* value = heap_strcpy(buffer->str, buffer->length);
        piece.set(value ? Item{.item = s2it(value)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0));
    }
    strbuf_free(buffer); return piece.get();
}
static Item regexp_string_native(MvpLmdLibraryState* state, Item source, Item pattern,
        Item replacement, int64_t replace) {
    RootFrame roots(7);
    if (!roots.valid()) return ItemError;
    Rooted<Item> original(roots, source), regex(roots, pattern), substitution(roots, replacement);
    Rooted<Item> records(roots, ItemNull), match(roots, ItemNull), result(roots, ItemNull), piece(roots, ItemNull);
    if (!replace && !regexp_record(regex.get(), state)) {
        regex.set(regexp_create(regex.get(), Item{.item = ITEM_JS_UNDEFINED}));
        if (item_is_error(regex.get())) return regex.get();
    }
    if (replace && !mvp_lmd_is_callable(substitution.get())) {
        substitution.set(mvp_lmd_primitive_to_string(substitution.get()));
        if (item_is_error(substitution.get())) return substitution.get();
    }
    records.set(mvp_lmd_array_new(0));
    if (item_is_error(records.get())) return records.get();
    MvpLmdRegExp* compiled = regexp_record(regex.get(), state);
    bool global = compiled && compiled->flags.global;
    MvpLmdRegExpInput input(original.get().get_string(), compiled && compiled->flags.unicode);
    if (!input.view.str) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (global) {
        result.set(regexp_ops.set_last_index(regex.get(), Item{.item = i2it(0)}));
        if (item_is_error(result.get())) return result.get();
    }
    do {
        if (compiled) match.set(regexp_exec(state, regex.get(), original.get(), false, &input));
        else {
            piece.set(mvp_lmd_primitive_to_string(regex.get()));
            if (item_is_error(piece.get())) return piece.get();
            String* haystack = original.get().get_string(), *needle = piece.get().get_string();
            int64_t start = utf16_find(haystack->chars, haystack->len, needle->chars, needle->len, 0);
            if (start < 0) break;
            int a = input.byte_offset(start);
            int b = input.byte_offset(start + utf8_to_utf16_length(needle->chars, needle->len));
            match.set(regexp_result(state, original.get(), &input, NULL, &a, &b, 1));
        }
        if (item_is_error(match.get())) return match.get();
        if (match.get().item == ITEM_NULL) break;
        if (!replace && !global) return match.get();
        result.set(mvp_lmd_array_store(records.get(), records.get().array->length, match.get()));
        if (item_is_error(result.get())) return result.get();
        if (global && !match.get().array->items[0].get_string()->len) {
            int64_t cursor = it2d(regexp_property(regex.get(), "lastIndex"));
            String* string = original.get().get_string();
            int64_t next = js_regex_advance_string_index_units(string, cursor, compiled->flags.unicode);
            result.set(regexp_ops.set_last_index(regex.get(), push_d((double)next)));
            if (item_is_error(result.get())) return result.get();
        }
    } while (global);
    if (!replace) {
        if (!records.get().array->length) return ItemNull;
        result.set(mvp_lmd_array_new(0));
        if (item_is_error(result.get())) return result.get();
        for (int64_t i = 0; i < records.get().array->length; i++) {
            Item value = records.get().array->items[i].array->items[0];
            Item stored = mvp_lmd_array_store(result.get(), i, value);
            if (item_is_error(stored)) return stored;
        }
        return result.get();
    }
    StrBuf* buffer = strbuf_new();
    if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int64_t cursor = 0;
    for (int64_t i = 0; i < records.get().array->length; i++) {
        match.set(records.get().array->items[i]);
        int64_t start = it2d(regexp_property(match.get(), "index"));
        piece.set(mvp_lmd_string_range(original.get(), cursor, start, 1));
        if (item_is_error(piece.get())) break;
        String* text = piece.get().get_string(); strbuf_append_str_n(buffer, text->chars, text->len);
        text = match.get().array->items[0].get_string(); cursor = start + utf8_to_utf16_length(text->chars, text->len);
        piece.set(regexp_replacement(original.get(), match.get(), substitution.get()));
        if (item_is_error(piece.get())) break;
        text = piece.get().get_string(); strbuf_append_str_n(buffer, text->chars, text->len);
    }
    if (!item_is_error(piece.get())) {
        piece.set(mvp_lmd_string_range(original.get(), cursor, INFINITY, 1));
        if (!item_is_error(piece.get())) {
            String* text = piece.get().get_string(); strbuf_append_str_n(buffer, text->chars, text->len);
            text = heap_strcpy(buffer->str, buffer->length);
            piece.set(text ? Item{.item = s2it(text)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0));
        }
    }
    strbuf_free(buffer); return piece.get();
}

extern "C" Item mvp_lmd_regexp_string_method(MvpLmdLibraryState* state, Item source, Item pattern,
        Item replacement, int64_t operation) {
    if (source.item == ITEM_NULL || source.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (operation == 4) return js_regexp_string_match_all_with_ops(source, pattern, &regexp_ops);
    const int protocols[] = {JS_BUILTIN_REGEXP_SYMBOL_MATCH, JS_BUILTIN_REGEXP_SYMBOL_REPLACE,
        JS_BUILTIN_REGEXP_SYMBOL_SEARCH, JS_BUILTIN_REGEXP_SYMBOL_SPLIT};
    if (operation < 0 || operation >= 4) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // already-converted ordinary inputs keep the original bulk loop without observable coercions.
    if ((operation == 0 || (operation == 1 && get_type_id(replacement) == LMD_TYPE_STRING)) &&
            get_type_id(source) == LMD_TYPE_STRING && regexp_ordinary(state, pattern, protocols[operation]))
        return regexp_string_native(state, source, pattern, replacement, operation);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    uint64_t source_home = 0, pattern_home = 0, replacement_home = 0;
    Rooted<Item> string(roots, lambda_item_adopt_scalar_home(source, &source_home));
    Rooted<Item> regex(roots, lambda_item_adopt_scalar_home(pattern, &pattern_home));
    Rooted<Item> replace(roots, lambda_item_adopt_scalar_home(replacement, &replacement_home)), method(roots, ItemNull);
    NameRef key = well_known_name_ref(regexp_symbol_id(protocols[operation]));
    // String protocol handoff applies only to object patterns, including when primitive prototypes change.
    if (mvp_lmd_is_object(regex.get())) {
        method.set(mvp_lmd_property_with_receiver(regex.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, regex.get()));
        if (item_is_error(method.get())) return method.get();
        if (method.get().item != ITEM_NULL && method.get().item != ITEM_JS_UNDEFINED) {
            if (!mvp_lmd_is_callable(method.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item arguments[] = {string.get(), replace.get()};
            return mvp_lmd_class_invoke(method.get(), regex.get(), arguments,
                operation == 1 || operation == 3 ? 2 : 1, Item{.item = ITEM_JS_UNDEFINED});
        }
    }
    // GetMethod precedes receiver conversion; fallback string operations retain argument order.
    string.set(mvp_lmd_primitive_to_string(string.get()));
    if (item_is_error(string.get())) return string.get();
    if (operation == 1) {
        regex.set(mvp_lmd_primitive_to_string(regex.get()));
        return item_is_error(regex.get()) ? regex.get() : regexp_string_native(state, string.get(), regex.get(), replace.get(), 1);
    }
    if (operation == 3) {
        method.set(replace.get().item == ITEM_JS_UNDEFINED ? Item{.item = i2it(UINT32_MAX)} :
            mvp_lmd_primitive_to_number(replace.get()));
        if (item_is_error(method.get())) return method.get();
        uint32_t limit = (uint32_t)js_to_int32(it2d(method.get()));
        // separator coercion is observable even when the converted limit is zero.
        if (regex.get().item != ITEM_JS_UNDEFINED) regex.set(mvp_lmd_primitive_to_string(regex.get()));
        if (item_is_error(regex.get())) return regex.get();
        if (!limit) return mvp_lmd_array_new(0);
        return mvp_lmd_string_split(string.get(), regex.get(), limit);
    }
    regex.set(regexp_create(regex.get(), Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(regex.get())) return regex.get();
    method.set(mvp_lmd_property_with_receiver(regex.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, regex.get()));
    if (item_is_error(method.get())) return method.get();
    if (!mvp_lmd_is_callable(method.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item argument = string.get();
    return mvp_lmd_class_invoke(method.get(), regex.get(), &argument, 1, Item{.item = ITEM_JS_UNDEFINED});
}
