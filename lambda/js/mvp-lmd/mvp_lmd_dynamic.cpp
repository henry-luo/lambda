#include "mvp_lmd_dynamic.h"
#include "../js_interp.hpp"
#include "../js_interp_env.h"
#include "../js_function.hpp"
#include "../js_props.h"
#include "../js_runtime_state.hpp"
#include "../../runtime/ast-core.hpp"
#include "../../runtime/context_capsule.h"
#include "../../runtime/heap_api.h"
#include "../../runtime/root_vector.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/side_stack.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/runtime-state.h"
#include "../../input/input.hpp"
#include "../../core/lambda-decimal.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/arraylist.h"
#include <limits.h>

struct MvpLmdDynamic {
    MvpLmdProgram* program;
    // Only the global eval intrinsic is rooted by the Context. Each internal
    // function owns its wrapper, so unreachable callback cycles can collect.
    RootVector functions;
    NameRef wrapper_key;
    NameRef source_key;
    JsInterpHostOps ops;
};
static constexpr uint32_t MVP_LMD_DYNAMIC_OWNER = 0x4d564459;
static Item dynamic_call(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
static Item dynamic_string(Item);

static Item dynamic_fail(int kind) { return mvp_lmd_fail(kind, 0); }
static Item dynamic_raise(Item value) { return mvp_lmd_throw(value, 0); }
static Item dynamic_caught(Item value) {
    return mvp_lmd_caught_value(mvp_lmd_program_library(), value);
}
static Item dynamic_global(void* state, Item key, Item value, int operation, bool strict) {
    return mvp_lmd_dynamic_global(((MvpLmdDynamic*)state)->program, key, value, operation, strict);
}
static Item dynamic_global_this(void* state) {
    return mvp_lmd_dynamic_global_this(((MvpLmdDynamic*)state)->program);
}
static Item dynamic_property(void*, Item owner, Item key, Item value, int operation, bool strict, Item receiver = Item{.item = 0}) {
    if (!receiver.item) receiver = owner;
    int64_t mode = operation == JS_INTERP_HOST_GET ? LMD_PROP_GET :
        operation == JS_INTERP_HOST_HAS ? LMD_PROP_HAS :
        operation == JS_INTERP_HOST_SET ? LMD_PROP_SET :
        operation == JS_INTERP_HOST_DELETE ? LMD_PROP_DELETE : LMD_PROP_CREATE_DATA;
    return mvp_lmd_property_with_receiver(owner, key, value,
        mode | (strict ? LMD_PROP_STRICT : 0), receiver);
}
static Item dynamic_invoke(void*, Item function, Item receiver, Item* args, int count, Item target) {
    return mvp_lmd_class_invoke(function, receiver, args, count, target);
}
static Item dynamic_object(void* state) {
    MvpLmdLibraryState* library = mvp_lmd_program_library(((MvpLmdDynamic*)state)->program);
    return library ? mvp_lmd_object_new(library->object_shape, 0) : dynamic_fail(LMD_MVP_CAPABILITY);
}
static Item dynamic_own_enumerability(Item object, Item key) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, object), name(roots, key);
    JsPropertyDescriptor descriptor = {};
    Item found = mvp_lmd_get_own_property(owner.get(), name.get(), &descriptor);
    if (item_is_error(found)) return found;
    return found.item == ITEM_FALSE ? make_js_undefined()
        : Item{.item = (descriptor.flags & JS_PD_ENUMERABLE) ? ITEM_TRUE : ITEM_FALSE};
}
static Item dynamic_for_in_key_is_live(Item object, Item key) {
    static const JsForInOps ops = {
        [](Item value) { return mvp_lmd_box_primitive(value); },
        mvp_lmd_get_prototype, dynamic_own_enumerability};
    return js_for_in_key_is_live_with_ops(object, key, &ops);
}
static Item dynamic_class(void* opaque, AstClassNode* ast, JsScript* script, Item base, Item constructor) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    RootFrame roots(2);
    Rooted<Item> function(roots, mvp_lmd_dynamic_class(state->program, ast, base, constructor));
    Rooted<Item> source(roots, ItemNull);
    if (item_is_error(function.get())) return function.get();
    SourceSpan span = ast->source_span;
    if (!script || !script->source || span.end_byte < span.start_byte ||
            span.end_byte > script->source_length) return dynamic_fail(LMD_MVP_CAPABILITY);
    String* text = heap_strcpy(script->source + span.start_byte, span.end_byte - span.start_byte);
    if (!text) return dynamic_fail(LMD_MVP_MEMORY);
    source.set(Item{.item = s2it(text)});
    Map* properties = mvp_lmd_property_map(function.get(), true);
    return properties && map_shape_set(properties, state->source_key, source.get())
        ? function.get() : dynamic_fail(LMD_MVP_MEMORY);
}
static Item dynamic_define_method(Item owner, Item key, Item function,
        int prefix, int attributes, bool private_name) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, owner), name(roots, key), value(roots, function);
    Item named = mvp_lmd_function_set_name(value.get(), name.get(), prefix);
    if (item_is_error(named)) return named;
    return prefix ? mvp_lmd_define_accessor(target.get(), name.get(), value.get(),
            (attributes << 8) | (prefix == 2))
        : private_name ? mvp_lmd_private_element(target.get(), name.get(), value.get(), LMD_PRIVATE_METHOD)
        : mvp_lmd_property_with_receiver(target.get(), name.get(), value.get(),
            LMD_PROP_CREATE_DATA | ((attributes & JSPD_NON_ENUMERABLE) ? LMD_PROP_NON_ENUMERABLE : 0),
            target.get());
}
static Item dynamic_class_method(void*, Item owner, AstMethodNode* method, Item key, Item function) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> cls(roots, owner);
    MvpLmdClass* metadata = mvp_lmd_class_record(cls.get());
    if (!metadata) return dynamic_fail(LMD_MVP_CAPABILITY);
    bool private_name = property_key_kind(key.get_string()) == NAME_KEY_PRIVATE;
    int prefix = method->kind == AstMethodNode::JS_METHOD_GET ? 1 :
        method->kind == AstMethodNode::JS_METHOD_SET ? 2 : 0;
    Item target = private_name ? metadata->field_keys[metadata->field_count + (method->static_method ? 1 : 0)] :
        mvp_lmd_class_values(metadata)[method->static_method ? 0 : 1];
    return dynamic_define_method(target, key, function, prefix, JSPD_NON_ENUMERABLE, private_name);
}
static Item dynamic_object_property(void*, Item owner, JsPropertyNode* property, Item key, Item value) {
    if (property->method || property->is_getter || property->is_setter) {
        int prefix = property->is_getter ? 1 : property->is_setter ? 2 : 0;
        return dynamic_define_method(owner, key, value, prefix, 0, false);
    }
    // the AST calls this branch only for the prototype literal production;
    // primitive values are ignored, while computed/shorthand keys stay own.
    return value.item == ITEM_NULL || mvp_lmd_is_object(value)
        ? mvp_lmd_set_prototype(owner, value) : owner;
}
static Item dynamic_class_field(void*, Item owner, AstClassFieldNode* field, Item key, Item value,
        uint32_t field_index, uint32_t initializer_index, bool initialize) {
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (!cls || field_index >= cls->field_count) return dynamic_fail(LMD_MVP_CAPABILITY);
    if (!initialize) {
        // the constructor environment owns every recipe before later key evaluation.
        cls->field_keys[field_index] = key;
        if (!field->is_static) {
            if (initializer_index >= cls->initializer_count) return dynamic_fail(LMD_MVP_CAPABILITY);
            cls->initializers[initializer_index] = value;
        }
        return owner;
    }
    return field->is_private ? mvp_lmd_private_element(owner, key, value, LMD_PRIVATE_ADD)
        : mvp_lmd_property_with_receiver(owner, key, value, LMD_PROP_CREATE_DATA, owner);
}
static Item dynamic_private_element(Item owner, Item key, Item value, int64_t operation) {
    static const int modes[] = {LMD_PRIVATE_NAME, LMD_PRIVATE_GET, LMD_PRIVATE_SET,
        LMD_PRIVATE_HAS, LMD_PRIVATE_ADD, LMD_PRIVATE_METHOD};
    if (operation < 0 || operation >= (int64_t)(sizeof(modes) / sizeof(modes[0])))
        return dynamic_fail(LMD_MVP_CAPABILITY);
    return mvp_lmd_private_element(owner, key, value, modes[operation]);
}
static Item dynamic_array_append(Item array, Item value, bool hole) {
    int64_t index = array.array->length;
    if (index >= UINT32_MAX) return dynamic_fail(LMD_MVP_RANGE);
    return hole ? mvp_lmd_array_resize(array, index + 1)
        : mvp_lmd_array_store(array, (uint32_t)index, value);
}
static Item dynamic_iterator_get(void* opaque, Item source) {
    return mvp_lmd_iterator_get(((MvpLmdDynamic*)opaque)->program, source);
}
static Item dynamic_iterator_step(Item record, bool read_value) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> iterator(roots, record), result(roots,
        mvp_lmd_iterator_step(record, Item{.item = ITEM_JS_UNDEFINED}, 0));
    if (item_is_error(result.get())) return result.get();
    Item done = dynamic_property(NULL, result.get(), Item{.item = s2it(heap_create_name("done", 4))},
        ItemNull, JS_INTERP_HOST_GET, false);
    if (item_is_error(done)) return done;
    if (mvp_lmd_truthy(done)) return Item{.item = JS_ITER_DONE_SENTINEL};
    // array-pattern elisions consume a step without observing its value getter.
    if (!read_value) return Item{.item = ITEM_JS_UNDEFINED};
    return js_retain_scalar(dynamic_property(NULL, result.get(), Item{.item = s2it(heap_create_name("value", 5))},
        ItemNull, JS_INTERP_HOST_GET, false));
}
static Item dynamic_iterator_collect_rest(void* opaque, Item record) {
    MvpLmdProgram* program = ((MvpLmdDynamic*)opaque)->program;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> iterator(roots, record), output(roots, mvp_lmd_array_new(0));
    if (item_is_error(output.get())) return output.get();
    Item absent = {.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_iterator_collect_with_method(program, output.get(), ItemNull,
        absent, absent, ItemError, -1, false, iterator.get());
}
static Item dynamic_array_spread(void* opaque, Item target, Item source) {
    Item absent = {.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_iterator_collect(((MvpLmdDynamic*)opaque)->program,
        target, source, absent, absent);
}
static Item dynamic_object_copy(void* opaque, Item target, Item source, Item* excluded, int count) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), keys(roots, ItemNull);
    if (output.get().item == ITEM_NULL) output.set(dynamic_object(opaque));
    if (item_is_error(output.get())) return output.get();
    if (count) {
        keys.set(mvp_lmd_array_new(count));
        if (item_is_error(keys.get())) return keys.get();
        for (int i = 0; i < count; i++) {
            Item stored = mvp_lmd_array_store(keys.get(), (uint32_t)i, excluded[i]);
            if (item_is_error(stored)) return stored;
        }
    }
    return mvp_lmd_object_copy(output.get(), input.get(), 0, keys.get());
}
static Item dynamic_parameter_cell(Item cell, Item value, bool write) {
    // receiver cells also retain immutable new.target at index one.
    if (get_type_id(cell) != LMD_TYPE_ARRAY || cell.array->length < 1)
        return dynamic_fail(LMD_MVP_CAPABILITY);
    return write ? mvp_lmd_array_store(cell, 0, value)
        : js_retain_scalar(cell.array->items[0]);
}
static Item dynamic_arguments(void*, Item callee, Item* args, int count,
        AstFuncNode* function, JsInterpEnv* env, bool mapped) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, callee), aliases(roots, ItemNull), cell(roots, ItemNull);
    if (mapped) {
        aliases.set(mvp_lmd_array_new(ast_linked_node_count(function->params)));
        if (item_is_error(aliases.get())) return aliases.get();
        // each unique formal's existing environment slot owns its canonical
        // cell; the separate alias table can unlink an index without losing it.
        for (NameEntry* entry = function->vars ? function->vars->first : NULL;
                entry; entry = entry->next) {
            if (!entry->is_parameter) continue;
            if (entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count)
                return dynamic_fail(LMD_MVP_CAPABILITY);
            cell.set(mvp_lmd_array_new(1));
            if (item_is_error(cell.get())) return cell.get();
            js_interp_env_slot_store(env, entry->slot, cell.get());
        }
        int index = 0;
        for (AstNode* parameter = function->params; parameter; parameter = parameter->next, index++) {
            JsIdentifierNode* identifier = js_ast_parameter_binding_identifier(parameter);
            NameEntry* entry = identifier ? identifier->entry : NULL;
            if (!entry && identifier) entry = name_scope_lookup_name(function->vars, identifier->name);
            if (!entry || !entry->is_parameter || entry->scope != env->scope ||
                    entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count)
                return dynamic_fail(LMD_MVP_CAPABILITY);
            cell.set(js_interp_env_slot_read(env, entry->slot, false));
            // only the final occurrence of a duplicate formal aliases; keep
            // this independent of argc; arguments_new removes omitted indices.
            for (int previous = 0; previous < index; previous++) {
                if (aliases.get().array->items[previous].item == cell.get().item)
                    aliases.get().array->items[previous] = ItemNull;
            }
            Item stored = mvp_lmd_array_store(aliases.get(), (uint32_t)index, cell.get());
            if (item_is_error(stored)) return stored;
        }
    }
    return mvp_lmd_arguments_new(owner.get(), args, (uint64_t)count, aliases.get(), mapped ? 0 : 1);
}
static Item dynamic_bind_this(void* state, Item receiver, bool strict) {
    if (strict || mvp_lmd_is_object(receiver)) return receiver;
    TypeId type = get_type_id(receiver);
    return type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED
        ? dynamic_global_this(state) : mvp_lmd_box_primitive(receiver);
}
static JsFunction* dynamic_unwrap(Item function) {
    Item underlying = mvp_lmd_dynamic_unwrap_function(function);
    return get_type_id(underlying) == LMD_TYPE_FUNC &&
        function_has_abi(underlying.function, FN_ENTRY_ABI_JS_FUNCTION)
        ? (JsFunction*)underlying.function : NULL;
}
static Item dynamic_wrap(void* state, Item function) {
    return mvp_lmd_dynamic_wrap_function(((MvpLmdDynamic*)state)->program, function);
}
static Item dynamic_name(Item function, Item name) {
    RootFrame roots(2);
    Rooted<Item> owner(roots, function), key(roots, name);
    Item existing = mvp_lmd_property_with_receiver(owner.get(),
        Item{.item = s2it(heap_create_name("name", 4))}, ItemNull, LMD_PROP_GET, owner.get());
    if (item_is_error(existing)) return existing;
    if (get_type_id(existing) == LMD_TYPE_STRING && existing.get_string()->len) return owner.get();
    return mvp_lmd_function_set_name(owner.get(), key.get(), 0);
}

// Only primitive operands reach shared LJS scalar algorithms. Object coercion,
// BigInt errors and all user reentry retain the MVP property/call policy.
static Item dynamic_binary(int operation, Item left, Item right) {
    RootFrame roots(2);
    uint64_t homes[2] = {};
    Rooted<Item> a(roots, lambda_item_adopt_scalar_home(left, &homes[0]));
    Rooted<Item> b(roots, lambda_item_adopt_scalar_home(right, &homes[1]));
    auto store = [](Rooted<Item>& slot, Item value, uint64_t* home) {
        slot.set(lambda_item_adopt_scalar_home(value, home));
    };
    if (operation == OPERATOR_IN) {
        if (!mvp_lmd_is_object(b.get())) return dynamic_fail(LMD_MVP_TYPE);
        store(a, mvp_lmd_property_key(a.get()), &homes[0]);
        return item_is_error(a.get()) ? a.get() : mvp_lmd_property_with_receiver(
            b.get(), a.get(), ItemNull, LMD_PROP_HAS, b.get());
    }
    if (operation == OPERATOR_JS_INSTANCEOF) return mvp_lmd_instanceof(a.get(), b.get());
    bool strict = operation == OPERATOR_JS_STRICT_EQ || operation == OPERATOR_JS_STRICT_NE;
    bool equal = strict || operation == OPERATOR_EQ || operation == OPERATOR_NE;
    bool invert = operation == OPERATOR_NE || operation == OPERATOR_JS_STRICT_NE;
    if (strict) {
        Item result = mvp_lmd_is_object(a.get()) || mvp_lmd_is_object(b.get())
            ? Item{.item = b2it(a.get().item == b.get().item)} : js_strict_equal(a.get(), b.get());
        return invert ? Item{.item = b2it(!mvp_lmd_truthy(result))} : result;
    }
    if (equal) {
        bool ao = mvp_lmd_is_object(a.get()), bo = mvp_lmd_is_object(b.get());
        TypeId at = get_type_id(a.get()), bt = get_type_id(b.get());
        if ((ao && bo) || ((ao || bo) && (at == LMD_TYPE_NULL || bt == LMD_TYPE_NULL ||
                at == LMD_TYPE_UNDEFINED || bt == LMD_TYPE_UNDEFINED))) {
            bool same = a.get().item == b.get().item;
            return Item{.item = b2it(invert ? !same : same)};
        }
    }
    bool addition = operation == OPERATOR_ADD;
    int hint = addition || equal ? 0 : 1;
    if (mvp_lmd_is_object(a.get())) store(a, mvp_lmd_to_primitive(a.get(), hint), &homes[0]);
    if (item_is_error(a.get())) return a.get();
    if (mvp_lmd_is_object(b.get())) store(b, mvp_lmd_to_primitive(b.get(), hint), &homes[1]);
    if (item_is_error(b.get())) return b.get();
    if (addition && (get_type_id(a.get()) == LMD_TYPE_STRING || get_type_id(b.get()) == LMD_TYPE_STRING)) {
        store(a, mvp_lmd_primitive_to_string(a.get()), &homes[0]);
        if (item_is_error(a.get())) return a.get();
        store(b, mvp_lmd_primitive_to_string(b.get()), &homes[1]);
        return item_is_error(b.get()) ? b.get() : mvp_lmd_string_concat(a.get(), b.get());
    }
    if ((get_type_id(a.get()) == LMD_TYPE_DECIMAL && get_type_id(b.get()) == LMD_TYPE_SYMBOL) ||
            (get_type_id(b.get()) == LMD_TYPE_DECIMAL && get_type_id(a.get()) == LMD_TYPE_SYMBOL))
        return equal ? Item{.item = b2it(invert)} : dynamic_fail(LMD_MVP_TYPE);
    if (get_type_id(a.get()) == LMD_TYPE_DECIMAL || get_type_id(b.get()) == LMD_TYPE_DECIMAL)
        return mvp_lmd_bigint_binary(a.get(), b.get(), operation == OPERATOR_POW ? OPERATOR_JS_EXP : operation);
    if (equal) {
        Item result = js_equal(a.get(), b.get());
        return item_is_error(result) || !invert ? result : Item{.item = b2it(!mvp_lmd_truthy(result))};
    }
    bool compare = operation == OPERATOR_LT || operation == OPERATOR_LE ||
        operation == OPERATOR_GT || operation == OPERATOR_GE;
    if (compare && get_type_id(a.get()) == LMD_TYPE_STRING && get_type_id(b.get()) == LMD_TYPE_STRING) {
        int64_t order = mvp_lmd_string_compare(a.get(), b.get());
        return Item{.item = b2it(operation == OPERATOR_LT ? order < 0 : operation == OPERATOR_LE ? order <= 0 :
            operation == OPERATOR_GT ? order > 0 : order >= 0)};
    }
    store(a, mvp_lmd_primitive_to_number(a.get()), &homes[0]);
    if (item_is_error(a.get())) return a.get();
    store(b, mvp_lmd_primitive_to_number(b.get()), &homes[1]);
    if (item_is_error(b.get())) return b.get();
    switch (operation) {
    case OPERATOR_ADD: return js_add(a.get(), b.get());
    case OPERATOR_SUB: return js_subtract(a.get(), b.get());
    case OPERATOR_MUL: return js_multiply(a.get(), b.get());
    case OPERATOR_DIV: return js_divide(a.get(), b.get());
    case OPERATOR_MOD: return js_modulo(a.get(), b.get());
    case OPERATOR_POW: case OPERATOR_JS_EXP: return js_power(a.get(), b.get());
    case OPERATOR_LT: return js_less_than(a.get(), b.get());
    case OPERATOR_LE: return js_compare(2, a.get(), b.get());
    case OPERATOR_GT: return js_greater_than(a.get(), b.get());
    case OPERATOR_GE: return js_compare(3, a.get(), b.get());
    case OPERATOR_JS_BIT_AND: return js_bitwise_and(a.get(), b.get());
    case OPERATOR_JS_BIT_OR: return js_bitwise_or(a.get(), b.get());
    case OPERATOR_JS_BIT_XOR: return js_bitwise_xor(a.get(), b.get());
    case OPERATOR_JS_LSHIFT: return js_left_shift(a.get(), b.get());
    case OPERATOR_JS_RSHIFT: return js_right_shift(a.get(), b.get());
    case OPERATOR_JS_URSHIFT: return js_unsigned_right_shift(a.get(), b.get());
    default: return dynamic_fail(LMD_MVP_CAPABILITY);
    }
}
static Item dynamic_unary(int operation, Item value) {
    if (operation == OPERATOR_NOT) return Item{.item = b2it(!mvp_lmd_truthy(value))};
    if (operation == OPERATOR_JS_VOID) return Item{.item = ITEM_JS_UNDEFINED};
    if (operation == OPERATOR_JS_TYPEOF) {
        if (mvp_lmd_is_object(value)) return js_make_string(mvp_lmd_is_callable(value) ? "function" : "object");
        return js_typeof(value);
    }
    RootFrame roots(1);
    uint64_t home = 0;
    Rooted<Item> number(roots, lambda_item_adopt_scalar_home(value, &home));
    if (mvp_lmd_is_object(number.get())) number.set(lambda_item_adopt_scalar_home(mvp_lmd_to_primitive(number.get(), 1), &home));
    if (item_is_error(number.get())) return number.get();
    if (get_type_id(number.get()) == LMD_TYPE_DECIMAL) {
        if (operation == -1) return number.get();
        return operation == OPERATOR_POS ? dynamic_fail(LMD_MVP_TYPE)
            : mvp_lmd_bigint_binary(number.get(), ItemNull, operation);
    }
    number.set(lambda_item_adopt_scalar_home(mvp_lmd_primitive_to_number(number.get()), &home));
    if (item_is_error(number.get())) return number.get();
    switch (operation) {
    // this value must outlive the local scalar home in every build profile.
    case -1: case OPERATOR_POS: return js_retain_scalar(number.get());
    case OPERATOR_NEG: return js_unary_minus(number.get());
    case OPERATOR_JS_BIT_NOT: return js_bitwise_not(number.get());
    case OPERATOR_JS_INCREMENT: return js_increment_numeric(number.get());
    case OPERATOR_JS_DECREMENT: return js_decrement_numeric(number.get());
    default: return dynamic_fail(LMD_MVP_CAPABILITY);
    }
}
static bool dynamic_retain_script(void* opaque, JsScript* script) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    return mvp_lmd_dynamic_retain_script(state->program, script);
}
static void dynamic_release(void* opaque) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    root_vector_clear(&state->functions);
}
static void dynamic_destroy(void* opaque) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    root_vector_destroy(&state->functions);
    mem_free(state);
}
static Item dynamic_template_object(void*, JsScript* script, JsTemplateLiteralNode* literal,
        Item* cooked, Item* raw, int count) {
    // retained scripts outlive the realm registry; equal source in fresh parses is a new site.
    return js_build_template_object_cached_with_ops(cooked, raw, count,
        literal->source_span.start_byte, mvp_lmd_template_object_ops(), script);
}
static Item dynamic_intrinsic_eval(void* opaque) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    Item* intrinsic = root_vector_at(&state->functions, 0);
    return intrinsic ? *intrinsic : dynamic_fail(LMD_MVP_MEMORY);
}
static Item dynamic_generator_create(void* opaque, Item internal, Item wrapper, Item arguments, Item receiver, bool asynchronous) {
    return mvp_lmd_generator_create_ast(((MvpLmdDynamic*)opaque)->program, internal, wrapper, arguments, receiver, asynchronous);
}
static bool dynamic_is_derived_constructor(Item function) {
    MvpLmdClass* cls = mvp_lmd_class_record(function);
    return cls && cls->ast && cls->ast->superclass;
}
static MvpLmdDynamic* dynamic_state(MvpLmdProgram* program) {
    if (!context) return NULL;
    program = mvp_lmd_program_realm(program);
    MvpLmdDynamic* state = (MvpLmdDynamic*)context_capsule_extension(context, MVP_LMD_DYNAMIC_OWNER, 0);
    if (state) return state->program == program ? state : NULL;
    state = (MvpLmdDynamic*)mem_calloc(1, sizeof(MvpLmdDynamic), MEM_CAT_EVAL);
    if (!state) return NULL;
    state->program = program;
    state->wrapper_key = name_pool_create_unique_private(context->name_pool,
        {"MVP [[HostWrapper]]", 19});
    state->source_key = name_pool_create_unique_private(context->name_pool,
        {"MVP [[DynamicSource]]", 21});
    if (!state->wrapper_key || !state->source_key) { mem_free(state); return NULL; }
    root_vector_init(&state->functions, context, "MVP dynamic function identity");
    if (!root_vector_push(&state->functions, Item{.item = ITEM_JS_UNDEFINED})) {
        dynamic_destroy(state); return NULL;
    }
    state->ops = {state, dynamic_global, dynamic_global_this, dynamic_property,
        mvp_lmd_property_key, dynamic_invoke, dynamic_object, dynamic_wrap, dynamic_unwrap,
        dynamic_name, dynamic_binary, dynamic_unary, dynamic_fail, dynamic_raise,
        dynamic_caught, dynamic_bind_this, dynamic_retain_script, dynamic_class,
        mvp_lmd_array_new, dynamic_array_append, dynamic_arguments, dynamic_parameter_cell,
        dynamic_string, dynamic_iterator_get, dynamic_iterator_step, mvp_lmd_iterator_close,
        dynamic_iterator_collect_rest, dynamic_array_spread, dynamic_object_copy, dynamic_template_object, dynamic_intrinsic_eval, dynamic_generator_create,
        dynamic_class_method, dynamic_class_field, mvp_lmd_class_initialize_instance,
        dynamic_private_element, mvp_lmd_get_prototype, mvp_lmd_constructor_result, dynamic_is_derived_constructor,
        dynamic_object_property,
        mvp_lmd_for_in_keys, dynamic_for_in_key_is_live};
    static const ContextCapsuleOps capsule = {"js-mvp-dynamic", CONTEXT_CAPSULE_LIFETIME_REALM,
        sizeof(MvpLmdDynamic), NULL, dynamic_release, dynamic_destroy};
    if (!context_capsule_extension_install(context, MVP_LMD_DYNAMIC_OWNER, 0, state, &capsule)) {
        dynamic_destroy(state); return NULL;
    }
    return state;
}

Item mvp_lmd_dynamic_unwrap_function(Item function) {
    if (get_type_id(function) != LMD_TYPE_FUNC ||
            !function_has_abi(function.function, FN_ENTRY_ABI_MVP_LMD) ||
            function.function->ptr != (fn_ptr)dynamic_call || !function.function->closure_env)
        return function;
    return ((Item*)function.function->closure_env)[0];
}
Item mvp_lmd_dynamic_function_source(Item function) {
    Item underlying = mvp_lmd_dynamic_unwrap_function(function);
    if (underlying.item != function.item && get_type_id(underlying) == LMD_TYPE_FUNC &&
            !((MvpLmdCallable*)function.function)->constructor) {
        String* source = js_fn_source_text((JsFunction*)underlying.function);
        return source ? Item{.item = s2it(source)} : Item{.item = ITEM_JS_UNDEFINED};
    }
    MvpLmdDynamic* state = context ? (MvpLmdDynamic*)context_capsule_extension(
        context, MVP_LMD_DYNAMIC_OWNER, 0) : NULL;
    Map* properties = state ? mvp_lmd_property_map(function, false) : NULL;
    ShapeEntry* field = properties && properties->type
        ? typemap_hash_lookup_key((TypeMap*)properties->type, state->source_key) : NULL;
    return field ? map_shape_field_to_item(properties->data, field)
        : Item{.item = ITEM_JS_UNDEFINED};
}
Item mvp_lmd_dynamic_wrap_function(MvpLmdProgram* program, Item function) {
    if (get_type_id(function) != LMD_TYPE_FUNC ||
            !function_has_abi(function.function, FN_ENTRY_ABI_JS_FUNCTION)) return function;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, function), wrapper(roots, ItemNull), length(roots, ItemNull);
    MvpLmdDynamic* state = dynamic_state(program);
    if (!state) return dynamic_fail(LMD_MVP_MEMORY);
    JsFunction* internal = (JsFunction*)target.get().function;
    if (get_type_id(internal->properties_map) == LMD_TYPE_MAP) {
        Map* properties = internal->properties_map.map;
        ShapeEntry* cached = typemap_hash_lookup_key((TypeMap*)properties->type, state->wrapper_key);
        if (cached) return map_shape_field_to_item(properties->data, cached);
    }
    int arity = js_fn_formal_length(internal);
    if (arity < 0) arity = js_fn_param_count(internal);
    wrapper.set(mvp_lmd_native_function(program, state, dynamic_call,
        (uint8_t)(arity > 255 ? 255 : arity)));
    if (item_is_error(wrapper.get())) return wrapper.get();
    Item* environment = (Item*)heap_calloc_closure_env(sizeof(Item));
    if (!environment) return dynamic_fail(LMD_MVP_MEMORY);
    environment[0] = target.get();
    wrapper.get().function->closure_env = environment;
    wrapper.get().function->closure_field_count = 1;
    MvpLmdCallable* callable = (MvpLmdCallable*)wrapper.get().function;
    bool ast = js_fn_body_kind(internal) == JS_FUNCTION_BODY_AST;
    callable->def = ast ? (AstNode*)js_fn_ast_function(internal) : NULL;
    callable->non_constructible = ast ? (internal->flags & (JS_FUNC_FLAG_ARROW | JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_GENERATOR | JS_FUNC_FLAG_ASYNC | JS_FUNC_FLAG_ASYNC_GEN)) != 0
        : internal->construct == NULL;
    callable->native_constructor = !ast && internal->construct != NULL;
    callable->custom_new_target = callable->native_constructor;
    Item ready = mvp_lmd_function_prepare(wrapper.get());
    if (item_is_error(ready)) return ready;
    Item named = mvp_lmd_named_set(wrapper.get(), "name",
        internal->name ? Item{.item = s2it(internal->name)} : js_make_string(""),
        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(named)) return named;
    length.set(Item{.item = i2it(arity)});
    if (!ast) {
        // bound native code has a dispatch arity independent of its published length.
        JsPropertyDescriptor descriptor = {};
        if (js_get_own_property_descriptor(target.get(), "length", 6, &descriptor) &&
                (descriptor.flags & JS_PD_HAS_VALUE)) {
            length.set(js_retain_scalar(descriptor.value));
        }
    }
    Item defined = mvp_lmd_named_set(wrapper.get(), "length", length.get(),
        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(defined)) return defined;
    // This hidden slot uses only Lambda's shared map storage. In particular,
    // it must not initialize a second LJS realm for an AST-only host client.
    if (get_type_id(internal->properties_map) != LMD_TYPE_MAP) {
        TypeMap* shape = type_tree_map_root(runtime_shape_tree());
        if (!shape) return dynamic_fail(LMD_MVP_MEMORY);
        internal->properties_map = mvp_lmd_object_new(shape, 0);
        if (item_is_error(internal->properties_map)) return internal->properties_map;
        js_function_root_item_if_needed(internal, &internal->properties_map);
    }
    TypeMap* storage_type = (TypeMap*)internal->properties_map.map->type;
    if (storage_type->js_meta) {
        // Shared native functions already own LJS metadata storage. Lambda's
        // ordinary map writer intentionally rejects that legacy shape family.
        Item stored = js_define_own_key_storage(internal->properties_map,
            Item{.item = s2it(state->wrapper_key)}, wrapper.get());
        if (item_is_error(stored)) return stored;
    } else if (!map_shape_set(internal->properties_map.map, state->wrapper_key, wrapper.get())) {
        return dynamic_fail(LMD_MVP_MEMORY);
    }
    return wrapper.get();
}
static Item dynamic_call(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (count > INT_MAX) return dynamic_fail(LMD_MVP_RANGE);
    MvpLmdDynamic* state = (MvpLmdDynamic*)((MvpLmdNativeCallable*)self.function)->state;
    Item underlying = mvp_lmd_dynamic_unwrap_function(self);
    JsFunction* function = (JsFunction*)underlying.function;
    if (js_fn_body_kind(function) == JS_FUNCTION_BODY_AST)
        return js_interp_call_host_function(&state->ops, self, function, receiver, args, (int)count, target);
    // Native callbacks such as Promise resolving functions already belong to
    // this Context's shared service; they must never create another JS realm.
    if (!js_active_runtime_state || !js_runtime_state_thread_matches((EvalContext*)context))
        return dynamic_fail(LMD_MVP_CAPABILITY);
    RootFrame roots(1);
    Rooted<Item> result(roots, target.item == ITEM_JS_UNDEFINED
        ? js_call_function(underlying, receiver, args, (int)count)
        : js_construct_value(underlying, args, (int)count,
            mvp_lmd_dynamic_unwrap_function(target), NULL, true));
    return item_is_error(result.get()) ? result.get() : mvp_lmd_dynamic_wrap_function(program, result.get());
}

static Item dynamic_eval(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    if (!count) return Item{.item = ITEM_JS_UNDEFINED};
    if (get_type_id(args[0]) != LMD_TYPE_STRING) return args[0];
    MvpLmdDynamic* state = (MvpLmdDynamic*)((MvpLmdNativeCallable*)self.function)->state;
    String* source = args[0].get_string();
    return js_interp_execute_host_source(&state->ops, source->chars, source->len, "<eval>", true);
}
Item mvp_lmd_dynamic_initialize(MvpLmdProgram* program) {
    MvpLmdDynamic* state = dynamic_state(program);
    if (!state) return dynamic_fail(LMD_MVP_MEMORY);
    Item* intrinsic = root_vector_at(&state->functions, 0);
    if (intrinsic->item == ITEM_JS_UNDEFINED)
        *intrinsic = mvp_lmd_native_function(program, state, dynamic_eval, 1, "eval");
    return *intrinsic;
}
static Item dynamic_string(Item value) { return mvp_lmd_primitive_to_string(value); }
static Item dynamic_syntax(void*, const char*) { return dynamic_fail(LMD_MVP_SYNTAX); }
static Item dynamic_source(void* opaque, const char* source, size_t length) {
    MvpLmdDynamic* state = (MvpLmdDynamic*)opaque;
    return js_interp_execute_host_source(&state->ops, source, length, "<new Function>", false);
}
static void dynamic_metadata(void*, Item function, Item name, Item source) {
    RootFrame roots(3);
    Rooted<Item> owner(roots, function), label(roots, name), text(roots, source);
    JsFunction* internal = dynamic_unwrap(owner.get());
    if (!internal) return;
    internal->name = it2s(label.get());
    js_fn_code_ensure(internal)->source_text = it2s(text.get());
    mvp_lmd_named_set(owner.get(), "name", label.get(), JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
}
Item mvp_lmd_dynamic_function(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item, Item, Item target) {
    return mvp_lmd_dynamic_function_kind(program, args, count, target, LMD_DYNAMIC_FUNCTION);
}
Item mvp_lmd_dynamic_function_kind(MvpLmdProgram* program, Item* args, uint64_t count,
        Item target, MvpLmdDynamicFunctionKind kind) {
    static const char* const prefixes[][2] = {
        {"function", "function anonymous"},
        {"function*", "function* anonymous"},
        {"async function", "async function anonymous"},
        {"async function*", "async function* anonymous"}
    };
    if ((unsigned)kind >= sizeof(prefixes) / sizeof(prefixes[0]))
        return dynamic_fail(LMD_MVP_CAPABILITY);
    if (count > INT_MAX) return dynamic_fail(LMD_MVP_RANGE);
    MvpLmdDynamic* state = dynamic_state(program);
    if (!state) return dynamic_fail(LMD_MVP_MEMORY);
    JsDynamicFunctionOps ops = {state, dynamic_string, dynamic_syntax, dynamic_source, dynamic_metadata};
    RootFrame roots(2);
    Rooted<Item> new_target(roots, target), result(roots, js_new_function_from_string_with_ops(args,
        (int)count, prefixes[kind][0], prefixes[kind][1], &ops));
    if (item_is_error(result.get()) || target.item == ITEM_JS_UNDEFINED) return result.get();
    Item prototype = mvp_lmd_property_with_receiver(new_target.get(),
        Item{.item = s2it(heap_create_name("prototype", 9))}, ItemNull, LMD_PROP_GET, new_target.get());
    if (item_is_error(prototype)) return prototype;
    if (mvp_lmd_is_object(prototype)) {
        Item linked = mvp_lmd_set_prototype(result.get(), prototype);
        if (item_is_error(linked)) return linked;
    }
    return result.get();
}

extern "C" JsInterpEnv* mvp_lmd_dynamic_environment(MvpLmdProgram*, NameScope* scope,
        JsInterpEnv* outer, int64_t flags) {
    JsInterpEnv* env = js_interp_env_create(scope, outer);
    if (env) {
        env->host_cells = (uint8_t)flags;
        env->binding_role = (uint8_t)(flags >> 8);
    }
    return env;
}

extern "C" Item mvp_lmd_dynamic_initialize_body(MvpLmdProgram* program,
        JsInterpEnv* body, JsInterpEnv* parameters) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<JsInterpEnv*> body_root(roots, body), parameters_root(roots, parameters);
    MvpLmdDynamic* state = dynamic_state(program);
    if (!state) return ItemError;
    JsInterpHostScope profile(&state->ops);
    return js_interp_initialize_body_vars(body_root.get(), parameters_root.get());
}

extern "C" Item mvp_lmd_dynamic_eval(MvpLmdProgram* program, Item source,
        const JsInterpHostCaller* caller) {
    if (!caller) return ItemError;
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<JsInterpEnv*> environment(roots, caller->env);
    Rooted<Item> method_home(roots, caller->home_object), constructor(roots, caller->class_constructor);
    uint64_t homes[3] = {};
    Rooted<Item> code(roots, lambda_item_adopt_scalar_home(source, &homes[0]));
    Rooted<Item> receiver(roots, lambda_item_adopt_scalar_home(caller->receiver, &homes[1]));
    Rooted<Item> target(roots, lambda_item_adopt_scalar_home(caller->new_target, &homes[2]));
    MvpLmdDynamic* state = dynamic_state(program);
    if (!state) return ItemError;
    JsInterpHostCaller rooted = *caller;
    rooted.env = environment.get(); rooted.receiver = receiver.get(); rooted.new_target = target.get();
    rooted.home_object = method_home.get(); rooted.class_constructor = constructor.get();
    return js_interp_execute_host_eval(&state->ops, &rooted, code.get());
}

extern "C" Item mvp_lmd_dynamic_reference(MvpLmdProgram* program,
        const JsInterpHostCaller* caller, JsInterpHostReference* reference,
        Item key, Item value, int64_t operation) {
    if (!caller) return ItemError;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<JsInterpEnv*> environment(roots, caller->env);
    uint64_t home = 0;
    Rooted<Item> name(roots, key), input(roots, lambda_item_adopt_scalar_home(value, &home));
    MvpLmdDynamic* state = dynamic_state(program);
    return state ? js_interp_host_reference(&state->ops, caller, reference,
        name.get(), input.get(), (int)operation) : ItemError;
}
