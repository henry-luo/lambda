#include "concurrency_js.h"

#include "concurrency.h"
#include "../lambda-data.hpp"
#include "lambda-error.h"
#include "lambda-root-frame.hpp"
#include "../js/js_runtime.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"

#include <string.h>

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

typedef struct LambdaPromiseObserver {
    Item promise;
} LambdaPromiseObserver;

static Item js_undefined_item(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item js_error_message(const char* message) {
    return js_new_error((Item){.item = s2it(heap_strcpy(
        message, (int64_t)strlen(message)))});
}

static Item lambda_error_to_js(Item result) {
    const char* message = "Lambda procedure failed";
    LambdaError* error = it2err(result);
    if (error && error->message) {
        message = error->message;
    }
    return js_error_message(message);
}

static Item js_rejection_to_lambda(Item reason) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> reason_root(roots, reason);
    if (get_type_id(reason) == LMD_TYPE_ERROR) return reason;
    Item text = js_to_string(reason_root.get());
    Rooted<Item> text_root(roots, text);
    const char* message = "JavaScript Promise rejected";
    String* string = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
    if (string) {
        message = string->chars;
    }
    return err2it(err_create_heap(ERR_RUNTIME_ERROR, message, NULL));
}

static void lambda_promise_observer_destroy(void* data) {
    LambdaPromiseObserver* observer = (LambdaPromiseObserver*)data;
    if (!observer) return;
    heap_unregister_gc_root(&observer->promise.item);
    mem_free(observer);
}

static void lambda_promise_observer_complete(
    LambdaTask* task, Item result, void* data) {
    (void)task;
    LambdaPromiseObserver* observer = (LambdaPromiseObserver*)data;
    if (!observer) return;
    RootFrame roots((Context*)context, 3);
    Rooted<Item> promise_root(roots, observer->promise);
    Rooted<Item> result_root(roots, result);
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        Item error = lambda_error_to_js(result_root.get());
        Rooted<Item> error_root(roots, error);
        js_promise_reject_existing(promise_root.get(), error_root.get());
    } else {
        js_promise_fulfill_existing(promise_root.get(), result_root.get());
    }
}

static Item lambda_handle_to_js_promise(Item handle) {
    RootFrame roots((Context*)context, 2);
    Rooted<Item> handle_root(roots, handle);
    LambdaTask* task = lambda_task_from_handle(handle_root.get());
    if (!task) return js_promise_reject(
        js_error_message("invalid Lambda task handle"));
    Item promise = js_promise_create_pending();
    Rooted<Item> promise_root(roots, promise);
    LambdaPromiseObserver* observer = (LambdaPromiseObserver*)mem_calloc(
        1, sizeof(LambdaPromiseObserver), MEM_CAT_EVAL);
    if (!observer) return js_promise_reject(
        js_error_message("Promise observer allocation failed"));
    observer->promise = promise_root.get();
    heap_register_gc_root(&observer->promise.item);
    if (!lambda_task_on_complete(task, lambda_promise_observer_complete,
            observer, lambda_promise_observer_destroy)) {
        lambda_promise_observer_destroy(observer);
        return js_promise_reject(
            js_error_message("Promise observer registration failed"));
    }
    return promise_root.get();
}

static Item lambda_promise_fulfilled(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    LambdaTask* waiter = env ? lambda_task_from_handle(env[0]) : NULL;
    if (waiter) lambda_task_resume_external(waiter, value);
    return js_undefined_item();
}

static Item lambda_promise_rejected(Item env_item, Item reason) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    LambdaTask* waiter = env ? lambda_task_from_handle(env[0]) : NULL;
    if (waiter) lambda_task_resume_external(waiter, js_rejection_to_lambda(reason));
    return js_undefined_item();
}

static Item lambda_wait_js_promise(Item promise, LambdaTask* waiter) {
    RootFrame roots((Context*)context, 4);
    Rooted<Item> promise_root(roots, promise);
    if (!waiter || !js_promise_is(promise_root.get())) {
        return err2it(err_create_heap(ERR_INVALID_OPERATION,
            "wait requires a Promise and a running Lambda task", NULL));
    }
    Item resumed = ItemNull;
    if (lambda_task_take_resume_value(waiter, &resumed)) return resumed;
    if (lambda_task_cancel_requested(waiter)) {
        return err2it(err_create_heap(ERR_CANCELLED,
            "task cancelled", NULL));
    }

    Item* fulfilled_env = js_alloc_env(1);
    if (!fulfilled_env) {
        return err2it(err_create_heap(ERR_OUT_OF_MEMORY,
            "Promise reaction allocation failed", NULL));
    }
    fulfilled_env[0] = lambda_task_handle(waiter);
    Item on_fulfilled = js_new_closure(
        (void*)lambda_promise_fulfilled, 1, fulfilled_env, 1);
    Rooted<Item> fulfilled_root(roots, on_fulfilled);
    Item* rejected_env = js_alloc_env(1);
    if (!rejected_env) {
        return err2it(err_create_heap(ERR_OUT_OF_MEMORY,
            "Promise reaction allocation failed", NULL));
    }
    rejected_env[0] = lambda_task_handle(waiter);
    Item on_rejected = js_new_closure(
        (void*)lambda_promise_rejected, 1, rejected_env, 1);
    Rooted<Item> rejected_root(roots, on_rejected);
    // These reactions are unowned until Promise.then publishes them.
    js_promise_then(promise_root.get(), fulfilled_root.get(), rejected_root.get());
    lambda_task_park(waiter);
    log_debug("concurrency JS: parked Lambda task on Promise");
    return (Item){.item = ITEM_TASK_SUSPENDED};
}

static Item lambda_js_procedure_call(Item env_item, Item rest_args) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> env_root(roots, env_item);
    Rooted<Item> args_root(roots, rest_args);
    Item* env = (Item*)(uintptr_t)env_root.get().item;
    Item function = env ? env[0] : ItemNull;
    if (get_type_id(function) != LMD_TYPE_FUNC) {
        return js_promise_reject(js_error_message("invalid Lambda procedure"));
    }
    List* args = list();
    int64_t count = get_type_id(args_root.get()) == LMD_TYPE_ARRAY
        ? js_array_length(args_root.get()) : 0;
    for (int64_t i = 0; i < count; i++) {
        list_push(args, js_array_get_int(args_root.get(), i));
    }
    Item handle = lambda_task_start_function(function, args);
    Rooted<Item> handle_root(roots, handle);
    if (!lambda_task_handle_is(handle)) {
        Item error = lambda_error_to_js(handle_root.get());
        return js_promise_reject(error);
    }
    log_debug("concurrency JS: dispatched Lambda procedure as task");
    return lambda_handle_to_js_promise(handle_root.get());
}

extern "C" void lambda_concurrency_js_init(void) {
    lambda_concurrency_set_promise_bridge(
        js_promise_is, lambda_wait_js_promise, lambda_handle_to_js_promise);
}

extern "C" Item lambda_js_wrap_procedure(
    Function* function, int arity, const char* name) {
    lambda_concurrency_js_init();
    Item* env = js_alloc_env(1);
    if (!env) return ItemNull;
    env[0] = (Item){.function = function};
    // A negative one-parameter signature gives the adapter one JS rest array,
    // preserving the procedure's source arity without fixed C trampolines.
    Item wrapper = js_new_closure((void*)lambda_js_procedure_call, -1, env, 1);
    RootFrame roots((Context*)context, 1);
    Rooted<Item> wrapper_root(roots, wrapper);
    js_set_formal_length(wrapper_root.get(), arity);
    if (name) {
        js_set_function_name(wrapper_root.get(),
            (Item){.item = s2it(heap_create_name(name))});
    }
    // The module namespace is the first persistent owner of this adapter.
    return wrapper_root.get();
}
