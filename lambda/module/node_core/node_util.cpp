// node_util.cpp -- node-core's util namespace (promisify, format, inspect).
#include "node_util.hpp"
#include "../../js/js_runtime.h"
#include "../../js/js_runtime_state.hpp"
#include "../../js/js_class.h"
#include "../../js/js_host_hooks.h"

static int js_util_service_append(char* buffer, int length, int capacity,
                                  const char* text, int text_length) {
    if (!buffer || !text || length >= capacity || text_length <= 0) return length;
    int available = capacity - length - 1;
    if (text_length > available) text_length = available;
    if (text_length > 0) memcpy(buffer + length, text, (size_t)text_length);
    return length + text_length;
}

static int js_util_service_append_string_item(char* buffer, int length, int capacity,
                                              Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return length;
    String* string = it2s(value);
    return string ? js_util_service_append(buffer, length, capacity,
        string->chars, (int)string->len) : length;
}

static Item js_util_service_format(Item rest_args) {
    RootFrame roots(3);
    Rooted<Item> args_root(roots, rest_args);
    Rooted<Item> first_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    int64_t count = js_array_length(args_root.get());
    if (count <= 0) return js_make_string("");
    first_root.set(js_elements_get_int(args_root.get(), 0));
    char buffer[8192] = {};
    int length = 0;
    int64_t argument = 1;
    if (get_type_id(first_root.get()) == LMD_TYPE_STRING) {
        String* format = it2s(first_root.get());
        for (int index = 0; format && index < (int)format->len && length < (int)sizeof(buffer) - 1;
                index++) {
            char current = format->chars[index];
            if (current != '%' || index + 1 >= (int)format->len) {
                buffer[length++] = current;
                continue;
            }
            char conversion = format->chars[index + 1];
            if (conversion == '%') {
                buffer[length++] = conversion;
                index++;
                continue;
            }
            if (argument >= count || (conversion != 's' && conversion != 'd' &&
                    conversion != 'i' && conversion != 'f' && conversion != 'j')) {
                buffer[length++] = current;
                continue;
            }
            value_root.set(js_elements_get_int(args_root.get(), argument++));
            if (conversion == 'j') value_root.set(js_json_stringify(value_root.get()));
            else value_root.set(js_to_string(value_root.get()));
            if (item_is_error(value_root.get())) return value_root.get();
            length = js_util_service_append_string_item(buffer, length, (int)sizeof(buffer),
                                                        value_root.get());
            index++;
        }
    } else {
        value_root.set(js_to_string(first_root.get()));
        if (item_is_error(value_root.get())) return value_root.get();
        length = js_util_service_append_string_item(buffer, length, (int)sizeof(buffer),
                                                    value_root.get());
    }
    for (; argument < count && length < (int)sizeof(buffer) - 1; argument++) {
        value_root.set(js_to_string(js_elements_get_int(args_root.get(), argument)));
        if (item_is_error(value_root.get())) return value_root.get();
        if (length > 0) buffer[length++] = ' ';
        length = js_util_service_append_string_item(buffer, length, (int)sizeof(buffer),
                                                    value_root.get());
    }
    return js_make_string_len(buffer, length);
}

static Item js_util_service_inspect(Item value, Item options) {
    (void)options;
    return js_to_string(value);
}

static Item js_util_service_inherits(Item constructor, Item super_constructor) {
    if (!js_is_callable(constructor) || !js_is_callable(super_constructor)) {
        return js_throw_invalid_arg_type("constructor", "function", constructor);
    }
    RootFrame roots(4);
    Rooted<Item> constructor_root(roots, constructor);
    Rooted<Item> super_root(roots, super_constructor);
    Rooted<Item> prototype_root(roots,
        js_get_key_cstr(constructor_root.get(), "prototype"));
    Rooted<Item> super_prototype_root(roots,
        js_get_key_cstr(super_root.get(), "prototype"));
    js_set_prototype(constructor_root.get(), super_root.get());
    if (get_type_id(prototype_root.get()) == LMD_TYPE_MAP &&
            get_type_id(super_prototype_root.get()) == LMD_TYPE_MAP) {
        js_set_prototype(prototype_root.get(), super_prototype_root.get());
    }
    return make_js_undefined();
}

static Item js_util_service_types_is_array(Item value) {
    return (Item){.item = b2it(js_is_js_array(value))};
}

static Item js_util_service_types_is_date(Item value) {
    return (Item){.item = b2it(js_class_id(value) == JS_CLASS_DATE)};
}

static Item js_util_service_custom_symbol(void) {
    return js_symbol_for(make_string_item("nodejs.util.promisify.custom"));
}

static Item js_util_service_custom_args_symbol(void) {
    return js_symbol_for(make_string_item("nodejs.util.promisify.customArgs"));
}

static Item js_util_service_result_from_args(Item custom_args, Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc <= 1) return make_js_undefined();

    if (get_type_id(custom_args) == LMD_TYPE_ARRAY) {
        int64_t names_length = js_array_length(custom_args);
        if (names_length > 0) {
            RootFrame roots(5);
            Rooted<Item> custom_args_root(roots, custom_args);
            Rooted<Item> rest_args_root(roots, rest_args);
            Rooted<Item> result_root(roots, js_new_object());
            Rooted<Item> name_root(roots, ItemNull);
            Rooted<Item> value_root(roots, ItemNull);
            int64_t value_count = argc - 1;
            int64_t count = names_length < value_count ? names_length : value_count;
            for (int64_t index = 0; index < count; index++) {
                name_root.set(js_elements_get_int(custom_args_root.get(), index));
                if (get_type_id(name_root.get()) == LMD_TYPE_STRING) {
                    value_root.set(js_elements_get_int(rest_args_root.get(), index + 1));
                    js_set_key_default(result_root.get(), name_root.get(), value_root.get());
                }
            }
            return result_root.get();
        }
    }

    return js_elements_get_int(rest_args, 1);
}

static Item js_util_service_promisify_callback(Item env_item, Item rest_args) {
    JS_ENV_OR_UNDEFINED(env, env_item);

    RootFrame roots(6);
    Rooted<Item> resolve_root(roots, env[0]);
    Rooted<Item> reject_root(roots, env[1]);
    Rooted<Item> custom_args_root(roots, env[2]);
    Rooted<Item> rest_args_root(roots, rest_args);
    Rooted<Item> error_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    int64_t argc = js_array_length(rest_args_root.get());
    error_root.set(argc > 0 ? js_elements_get_int(rest_args_root.get(), 0) : make_js_undefined());

    if (js_is_truthy(error_root.get())) {
        Item reject_args[1] = {error_root.get()};
        (void)js_call_function(reject_root.get(), make_js_undefined(), reject_args, 1);
        return make_js_undefined();
    }

    value_root.set(js_util_service_result_from_args(custom_args_root.get(), rest_args_root.get()));
    Item resolve_args[1] = {value_root.get()};
    (void)js_call_function(resolve_root.get(), make_js_undefined(), resolve_args, 1);
    return make_js_undefined();
}

static Item js_util_service_promisify_executor(Item env_item, Item resolve, Item reject) {
    JS_ENV_OR_UNDEFINED(env, env_item);

    RootFrame roots(9);
    Rooted<Item> original_root(roots, env[0]);
    Rooted<Item> this_root(roots, env[1]);
    Rooted<Item> call_args_root(roots, env[2]);
    Rooted<Item> custom_args_root(roots, env[3]);
    Rooted<Item> resolve_root(roots, resolve);
    Rooted<Item> reject_root(roots, reject);
    Rooted<Item> callback_root(roots, ItemNull);
    Rooted<Item> call_result_root(roots, ItemNull);
    Rooted<Item> error_root(roots, ItemNull);
    int64_t argc = js_array_length(call_args_root.get());
    if (argc < 0) argc = 0;

    Item* callback_env = js_alloc_env3(resolve_root.get(), reject_root.get(), custom_args_root.get());
    if (!callback_env) return ItemError;
    callback_root.set(js_new_native_closure(js_util_service_promisify_callback, -1, callback_env, 3));
    if (item_is_error(callback_root.get())) return callback_root.get();

    RootSpan argument_roots((size_t)argc + 1);
    Item* arguments = argument_roots.items();
    if (!arguments || !argument_roots.valid()) return ItemError;
    for (int64_t index = 0; index < argc; index++) {
        arguments[index] = js_elements_get_int(call_args_root.get(), index);
    }
    arguments[argc] = callback_root.get();

    call_result_root.set(js_call_function(original_root.get(), this_root.get(), arguments,
                                          (int)argc + 1));
    if (item_is_error(call_result_root.get())) {
        error_root.set(js_error_lane_payload(call_result_root.get()));
        Item reject_args[1] = {error_root.get()};
        (void)js_call_function(reject_root.get(), make_js_undefined(), reject_args, 1);
    }
    return make_js_undefined();
}

static Item js_util_service_promisified_function(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_promise_reject(make_string_item("promisified function missing target"));

    RootFrame roots(6);
    Rooted<Item> original_root(roots, env[0]);
    Rooted<Item> custom_args_root(roots, env[1]);
    Rooted<Item> this_root(roots, js_get_this());
    Rooted<Item> rest_args_root(roots, rest_args);
    Rooted<Item> call_args_root(roots, js_array_new(0));
    Rooted<Item> executor_root(roots, ItemNull);
    int64_t argc = js_array_length(rest_args_root.get());
    for (int64_t index = 0; index < argc; index++) {
        js_array_push(call_args_root.get(), js_elements_get_int(rest_args_root.get(), index));
    }

    Item* executor_env = js_alloc_env(4);
    if (!executor_env) return ItemError;
    executor_env[0] = original_root.get();
    executor_env[1] = this_root.get();
    executor_env[2] = call_args_root.get();
    executor_env[3] = custom_args_root.get();
    executor_root.set(js_new_native_closure(js_util_service_promisify_executor, 2, executor_env, 4));
    if (item_is_error(executor_root.get())) return executor_root.get();
    return js_promise_create(executor_root.get());
}

static Item js_util_service_promisify(Item function) {
    if (!js_is_callable(function)) {
        return js_throw_invalid_arg_type("original", "function", function);
    }

    RootFrame roots(6);
    Rooted<Item> function_root(roots, function);
    Rooted<Item> custom_key_root(roots, js_util_service_custom_symbol());
    Rooted<Item> custom_root(roots, js_get_key_default(function_root.get(), custom_key_root.get()));
    Rooted<Item> custom_args_root(roots, ItemNull);
    Rooted<Item> wrapper_root(roots, ItemNull);
    Rooted<Item> name_root(roots, ItemNull);
    if (item_is_error(custom_root.get())) return custom_root.get();
    if (custom_root.get().item != ITEM_NULL && custom_root.get().item != ITEM_JS_UNDEFINED &&
            get_type_id(custom_root.get()) != LMD_TYPE_UNDEFINED) {
        if (!js_is_callable(custom_root.get())) {
            return js_throw_invalid_arg_type("util.promisify.custom", "function", custom_root.get());
        }
        return custom_root.get();
    }

    custom_args_root.set(js_get_key_default(function_root.get(),
                                             js_util_service_custom_args_symbol()));
    if (item_is_error(custom_args_root.get())) return custom_args_root.get();
    Item* wrapper_env = js_alloc_env2(function_root.get(), custom_args_root.get());
    if (!wrapper_env) return ItemError;
    wrapper_root.set(js_new_native_closure(js_util_service_promisified_function, -1, wrapper_env, 2));
    if (item_is_error(wrapper_root.get())) return wrapper_root.get();
    js_set_key_default(wrapper_root.get(), custom_key_root.get(), wrapper_root.get());
    name_root.set(make_string_item("promisified"));
    js_set_function_name(wrapper_root.get(), name_root.get());
    return wrapper_root.get();
}

Item node_util_namespace(void) {
    Item* namespace_slot = js_active_runtime_state ? js_realm_slot(
        &js_runtime_state.realm_slots, JS_REALM_SLOT_UTIL_NAMESPACE) : NULL;
    if (!namespace_slot) return ItemError;
    js_host_hooks_set_console_format_hook(js_util_service_format);
    if (namespace_slot->item != 0) return *namespace_slot;

    *namespace_slot = js_new_object();
    if (item_is_error(*namespace_slot)) return *namespace_slot;
    RootFrame roots(6);
    Rooted<Item> namespace_root(roots, *namespace_slot);
    Rooted<Item> promisify_root(roots,
        js_install_native_method(namespace_root.get(), "promisify", js_util_service_promisify));
    Rooted<Item> symbol_root(roots, js_util_service_custom_symbol());
    Rooted<Item> custom_key_root(roots, make_string_item("custom"));
    Rooted<Item> default_key_root(roots, make_string_item("default"));
    Rooted<Item> types_root(roots, js_new_object());
    if (item_is_error(promisify_root.get())) return promisify_root.get();
    js_install_native_method(namespace_root.get(), "format", js_util_service_format, -1);
    js_install_native_method(namespace_root.get(), "inspect", js_util_service_inspect);
    js_install_native_method(namespace_root.get(), "inherits", js_util_service_inherits);
    js_install_native_method(types_root.get(), "isArray", js_util_service_types_is_array);
    js_install_native_method(types_root.get(), "isDate", js_util_service_types_is_date);
    js_set_key_default(promisify_root.get(), custom_key_root.get(), symbol_root.get());
    js_set_key_cstr(namespace_root.get(), "types", types_root.get());
    js_set_key_default(namespace_root.get(), default_key_root.get(), namespace_root.get());
    return namespace_root.get();
}
