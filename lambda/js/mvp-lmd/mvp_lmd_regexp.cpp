#include "mvp_lmd_runtime.h"
#include "../js_regexp_compile.h"
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
extern "C" Item mvp_lmd_regexp_new(MvpLmdLibraryState* state, Item pattern, Item flags) {
    if (!state) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, pattern), options(roots, flags), owner(roots, ItemNull), text(roots, ItemNull);
    MvpLmdRegExp* old = regexp_record(pattern, state);
    if (old) {
        ShapeEntry* field = typemap_hash_lookup((TypeMap*)old->type, "source", 6);
        source.set(map_shape_field_to_item(old->data, field));
        if (flags.item == ITEM_JS_UNDEFINED) {
            field = typemap_hash_lookup((TypeMap*)old->type, "flags", 5);
            options.set(map_shape_field_to_item(old->data, field));
        }
    }
    source.set(source.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_strcpy("", 0))} :
        mvp_lmd_primitive_to_string(source.get()));
    if (item_is_error(source.get())) return source.get();
    options.set(options.get().item == ITEM_JS_UNDEFINED ? Item{.item = s2it(heap_strcpy("", 0))} :
        mvp_lmd_primitive_to_string(options.get()));
    if (item_is_error(options.get())) return options.get();
    JsRegExpCompileInfo info = {};
    String* spelling = source.get().get_string(), *option = options.get().get_string();
    if (!js_regexp_compile_frontend(spelling->chars, spelling->len, option->chars, option->len, &info))
        return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    if (info.unicode_sets || info.has_indices) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    StrBuf* input = regexp_buffer(spelling, info.unicode);
    if (!input) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    JsBtFlags matching = {info.ignore_case, info.multiline, info.dot_all, info.unicode, info.sticky};
    JsBtRegex* compiled = js_bt_compile(input->str, input->length, matching, context->pool);
    strbuf_free(input);
    if (!compiled) return mvp_lmd_fail(LMD_MVP_SYNTAX, 0);
    // Shared Map storage owns the fields; the extra payload contains no GC references.
    MvpLmdRegExp* object = (MvpLmdRegExp*)heap_calloc(sizeof(MvpLmdRegExp), LMD_TYPE_MAP);
    if (!object) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    object->type_id = LMD_TYPE_MAP; object->type = &state->regexp.shape;
    object->compiled = compiled; object->flags = info; owner.set(Item{.map = object});
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
    Item result = mvp_lmd_named_set(owner.get(), "source", text.get());
    if (item_is_error(result)) return result;
    text.set(Item{.item = s2it(heap_strcpy(info.canonical_flags, info.canonical_flags_len))});
    result = mvp_lmd_named_set(owner.get(), "flags", text.get());
    if (item_is_error(result)) return result;
    const char* names[] = {"global", "ignoreCase", "multiline", "dotAll", "unicode", "sticky"};
    bool values[] = {info.global, info.ignore_case, info.multiline, info.dot_all, info.unicode, info.sticky};
    for (int i = 0; i < 6; i++) {
        result = mvp_lmd_named_set(owner.get(), names[i], Item{.item = b2it(values[i])});
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_named_set(owner.get(), "lastIndex", Item{.item = i2it(0)});
    return item_is_error(result) ? result : owner.get();
}

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
        for (int i = 0; i < named; i++) {
            int length; const char* key = js_bt_named_name(compiled, i, &length);
            String* name = heap_create_name(key, length);
            if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item stored = mvp_lmd_property_set(groups.get(), Item{.item = s2it(name)},
                result.get().array->items[js_bt_named_index(compiled, i)]);
            if (item_is_error(stored)) return stored;
        }
    }
    Item stored = mvp_lmd_named_set(result.get(), "index", Item{.item = i2it(input->unit_offset(starts[0]))});
    if (item_is_error(stored)) return stored;
    stored = mvp_lmd_named_set(result.get(), "input", original.get());
    if (item_is_error(stored)) return stored;
    stored = mvp_lmd_named_set(result.get(), "groups", groups.get());
    return item_is_error(stored) ? stored : result.get();
}
static Item regexp_exec(MvpLmdLibraryState* state, Item regex, Item string, bool test,
        const MvpLmdRegExpInput* prepared = NULL) {
    MvpLmdRegExp* object = regexp_record(regex, state);
    if (!object) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, regex), source(roots, mvp_lmd_primitive_to_string(string)), result(roots, ItemNull);
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
        if (stateful) result.set(mvp_lmd_named_set(held.get(), "lastIndex", Item{.item = i2it(found ?
            input->unit_offset(matches[count]) : 0)}));
        if (!item_is_error(result.get())) result.set(!found ? (test ? Item{.item = ITEM_FALSE} : ItemNull) :
            test ? Item{.item = ITEM_TRUE} : regexp_result(state, source.get(), input, object->compiled, matches, matches + count, count));
    }
    return result.get();
}
static Item regexp_construct(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    Item pattern = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    Item flags = count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED};
    if (target.item == ITEM_JS_UNDEFINED && flags.item == ITEM_JS_UNDEFINED && regexp_record(pattern, state)) return pattern;
    return mvp_lmd_regexp_new(state, pattern, flags);
}
static Item regexp_method(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    return regexp_exec(state, receiver, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, false);
}
static Item regexp_string(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, receiver), source(roots, ItemNull), flags(roots, ItemNull);
    const char* names[] = {"source", "flags"};
    Rooted<Item>* values[] = {&source, &flags};
    for (int i = 0; i < 2; i++) {
        String* key = heap_create_name(names[i], strlen(names[i]));
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        values[i]->set(mvp_lmd_class_property(owner.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, NULL));
        if (item_is_error(values[i]->get())) return values[i]->get();
        values[i]->set(mvp_lmd_primitive_to_string(values[i]->get()));
        if (item_is_error(values[i]->get())) return values[i]->get();
    }
    StrBuf* buffer = strbuf_new();
    if (!buffer) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    for (Rooted<Item>* value : values) {
        strbuf_append_char(buffer, '/');
        String* text = value->get().get_string(); strbuf_append_str_n(buffer, text->chars, text->len);
    }
    String* text = heap_strcpy(buffer->str, buffer->length); strbuf_free(buffer);
    return text ? Item{.item = s2it(text)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
Item mvp_lmd_regexp_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    Item result = mvp_lmd_library_class_initialize(program, state, &state->regexp,
        LMD_LIBRARY_REGEXP, "RegExp", regexp_construct, 2);
    if (item_is_error(result)) return result;
    // Distinct native entries share matching and preserve each function's public length.
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_REGEXP_PROTOTYPE], "exec",
        mvp_lmd_native_function(program, state, regexp_method, 1));
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_REGEXP_PROTOTYPE], "test",
        mvp_lmd_native_function(program, state, [](Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) -> Item {
            return regexp_exec((MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state, receiver,
                count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, true);
        }, 1));
    if (item_is_error(result)) return result;
    return item_is_error(result) ? result : mvp_lmd_named_set(state->values[LMD_LIBRARY_REGEXP_PROTOTYPE], "toString",
        mvp_lmd_native_function(program, state, regexp_string, 0));
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
    int64_t end = start + utf8_to_utf16_length(full->chars, full->len);
    if (get_type_id(replacement) == LMD_TYPE_FUNC) {
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
    String* text = replacement.get_string();
    for (uint32_t i = 0; i < text->len; i++) {
        char c = text->chars[i];
        if (c != '$' || i + 1 == text->len) { strbuf_append_char(buffer, c); continue; }
        char token = text->chars[i+1];
        piece.set(Item{.item = ITEM_JS_UNDEFINED});
        if (token == '$') { strbuf_append_char(buffer, '$'); i++; continue; }
        if (token == '&') piece.set(record.get().array->items[0]);
        else if (token == '`' || token == '\'') piece.set(mvp_lmd_string_range(original.get(),
            token == '`' ? 0 : end, token == '`' ? start : INFINITY, 1));
        else if (token >= '0' && token <= '9') {
            int index = token - '0';
            if (i + 2 < text->len && text->chars[i+2] >= '0' && text->chars[i+2] <= '9') {
                int two = index * 10 + text->chars[i+2] - '0';
                if (two > 0 && two < count) { index = two; i++; }
            }
            if (!index || index >= count) { strbuf_append_char(buffer, '$'); continue; }
            piece.set(record.get().array->items[index]);
        } else if (token == '<' && groups.get().item != ITEM_JS_UNDEFINED) {
            uint32_t close = i + 2;
            while (close < text->len && text->chars[close] != '>') close++;
            if (close == text->len) { strbuf_append_char(buffer, '$'); continue; }
            String* key = heap_create_name(text->chars + i + 2, close - i - 2);
            if (!key) { piece.set(mvp_lmd_fail(LMD_MVP_MEMORY, 0)); break; }
            piece.set(mvp_lmd_property_get(groups.get(), Item{.item = s2it(key)}, 0));
            i = close - 1;
        } else { strbuf_append_char(buffer, '$'); continue; }
        i++;
        if (item_is_error(piece.get())) break;
        if (piece.get().item != ITEM_JS_UNDEFINED) {
            String* value = piece.get().get_string(); strbuf_append_str_n(buffer, value->chars, value->len);
        }
    }
    if (!item_is_error(piece.get())) {
        String* value = heap_strcpy(buffer->str, buffer->length);
        piece.set(value ? Item{.item = s2it(value)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0));
    }
    strbuf_free(buffer); return piece.get();
}
extern "C" Item mvp_lmd_regexp_string_method(MvpLmdLibraryState* state, Item source, Item pattern,
        Item replacement, int64_t replace) {
    RootFrame roots(7);
    if (!roots.valid()) return ItemError;
    Rooted<Item> original(roots, source), regex(roots, pattern), substitution(roots, replacement);
    Rooted<Item> records(roots, ItemNull), match(roots, ItemNull), result(roots, ItemNull), piece(roots, ItemNull);
    if (!replace && !regexp_record(regex.get(), state)) {
        regex.set(mvp_lmd_regexp_new(state, regex.get(), Item{.item = ITEM_JS_UNDEFINED}));
        if (item_is_error(regex.get())) return regex.get();
    }
    if (replace && get_type_id(substitution.get()) != LMD_TYPE_FUNC) {
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
        result.set(mvp_lmd_named_set(regex.get(), "lastIndex", Item{.item = i2it(0)}));
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
            int64_t advance = 1;
            if (compiled->flags.unicode) {
                Utf16Iterator it = {(const unsigned char*)string->chars, string->len, 0, -1};
                uint16_t a = 0, b = 0;
                for (int64_t i = 0; i <= cursor && utf16_iterator_next(&it, &a); i++) {}
                if (utf_is_high_surrogate(a) && utf16_iterator_next(&it, &b) && utf_is_low_surrogate(b)) advance = 2;
            }
            result.set(mvp_lmd_named_set(regex.get(), "lastIndex", Item{.item = i2it(cursor + advance)}));
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
