#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/side_stack.h"
#include "../../../lib/mem.h"

Item mvp_lmd_function_forward(Item function, Item receiver, Item prefix, Item* arguments, uint64_t count, int skip, MvpLmdNativeEntry entry, Item target) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callee(roots, function), self(roots, receiver), head(roots, prefix), new_target(roots, target);
    Rooted<Item> copied(roots, mvp_lmd_array_new(0));
    if (item_is_error(copied.get())) return copied.get();
    uint64_t first = prefix.item == ITEM_JS_UNDEFINED ? 0 : prefix.array->length - skip;
    if (count > UINT32_MAX || first > UINT32_MAX - count) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    for (uint64_t i = 0; i < first + count; i++) {
        Item value = i < first ? head.get().array->items[i + skip] : arguments[i - first];
        if (value.item == ITEM_JS_DELETED_SENTINEL) value.item = ITEM_JS_UNDEFINED;
        Item stored = mvp_lmd_array_store(copied.get(), i, value);
        if (item_is_error(stored)) return stored;
    }
    RootSpan span(first + count ? first + count : 1);
    if (!span.valid()) return ItemError;
    uint64_t* watermark = context->side_number_top;
    if (!lambda_side_stack_ensure_tls(0, first + count)) return ItemError;
    uint64_t* homes = context->side_number_top;
    context->side_number_top += first + count;
    // Heap data can move at the callee's first safepoint; arguments and scalar homes must be stable.
    for (uint64_t i = 0; i < first + count; i++)
        span.items()[i] = lambda_item_adopt_scalar_home(copied.get().array->items[i], &homes[i]);
    MvpLmdCallable* callable = (MvpLmdCallable*)callee.get().function;
    Item result = entry ? entry(context, callable->program, span.items(), first + count,
        callee.get(), self.get(), Item{.item = ITEM_JS_UNDEFINED}) :
        mvp_lmd_class_invoke(callee.get(), self.get(), span.items(), first + count, new_target.get());
    result = lambda_item_resolve_pending_slot(result);
    bool number = get_type_id(result) == LMD_TYPE_FLOAT;
    double scalar = number ? it2d(result) : 0;
    // Activation abandonment releases these side-stack homes without native destructors.
    context->side_number_top = watermark;
    return number ? push_d(scalar) : result;
}
extern "C" Item mvp_lmd_call_array(Item function, Item receiver, Item arguments, Item target) {
    if (get_type_id(function) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return mvp_lmd_function_forward(function, receiver, arguments, NULL, 0, 0, NULL, target);
}
static Item function_bound(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Item* env = (Item*)self.function->closure_env;
    return mvp_lmd_function_forward(env[0], env[1].array->items[0], env[1], args, count, 1);
}
static Item function_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item receiver, Item) {
    if (get_type_id(receiver) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    Item absent = {.item = ITEM_JS_UNDEFINED}, this_arg = count ? args[0] : absent;
    if (self.item == state->values[LMD_LIBRARY_CALL].item)
        return mvp_lmd_function_forward(receiver, this_arg, absent, count ? args + 1 : NULL, count ? count - 1 : 0);
    if (self.item == state->values[LMD_LIBRARY_APPLY].item) {
        Item array = count > 1 ? args[1] : absent;
        if (array.item == ITEM_NULL || array.item == ITEM_JS_UNDEFINED)
            return mvp_lmd_function_forward(receiver, this_arg, absent, NULL, 0);
        if (get_type_id(array) != LMD_TYPE_ARRAY) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        // Retain the owner, not a borrowed items pointer, while the argument snapshot allocates.
        return mvp_lmd_function_forward(receiver, this_arg, array, NULL, 0);
    }
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callee(roots, receiver), object(roots, this_arg), bound(roots, ItemNull);
    Rooted<Item> arguments(roots, mvp_lmd_array_new(0));
    if (item_is_error(arguments.get())) return arguments.get();
    Item stored = mvp_lmd_array_store(arguments.get(), 0, object.get());
    if (item_is_error(stored)) return stored;
    for (uint64_t i = 1; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    int arity = callee.get().function->arity - (int)(count ? count - 1 : 0);
    bound.set(mvp_lmd_native_function(program, state, function_bound, arity > 0 ? arity : 0));
    if (item_is_error(bound.get())) return bound.get();
    Item* env = (Item*)heap_calloc_closure_env(2 * sizeof(Item));
    if (!env) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    bound.get().function->closure_env = env; bound.get().function->closure_field_count = 2;
    // The captured array owns scalar homes for both thisArg and the bound arguments.
    env[0] = callee.get(); env[1] = arguments.get();
    return bound.get();
}
Item mvp_lmd_function_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    for (int i = LMD_LIBRARY_CALL; i <= LMD_LIBRARY_BIND; i++) {
        state->values[i] = mvp_lmd_native_function(program, state, function_method, i == LMD_LIBRARY_APPLY ? 2 : 1);
        if (item_is_error(state->values[i])) return state->values[i];
    }
    return ItemNull;
}
Item mvp_lmd_function_method(Item owner, String* name) {
    if (property_key_requires_identity(name)) return Item{.item = ITEM_JS_UNDEFINED};
    Function* function = owner.function;
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context)
        return Item{.item = ITEM_JS_UNDEFINED};
    MvpLmdLibraryState* state = mvp_lmd_program_library(((MvpLmdCallable*)function)->program);
    if (!state) return Item{.item = ITEM_JS_UNDEFINED};
    const char* names[] = {"call", "apply", "bind"};
    for (int i = 0; i < 3; i++) if (name->len == strlen(names[i]) && !memcmp(name->chars, names[i], name->len))
        return state->values[LMD_LIBRARY_CALL + i];
    return Item{.item = ITEM_JS_UNDEFINED};
}
