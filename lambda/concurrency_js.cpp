#include "concurrency_js.h"

#include "concurrency.h"
#include "lambda-data.hpp"
#include "lambda-error.h"
#include "js/js_runtime.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

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
    if (get_type_id(reason) == LMD_TYPE_ERROR) return reason;
    Item text = js_to_string(reason);
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
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        js_promise_reject_existing(observer->promise, lambda_error_to_js(result));
    } else {
        js_promise_fulfill_existing(observer->promise, result);
    }
}

static Item lambda_handle_to_js_promise(Item handle) {
    LambdaTask* task = lambda_task_from_handle(handle);
    if (!task) return js_promise_reject(
        js_error_message("invalid Lambda task handle"));
    Item promise = js_promise_create_pending();
    LambdaPromiseObserver* observer = (LambdaPromiseObserver*)mem_calloc(
        1, sizeof(LambdaPromiseObserver), MEM_CAT_EVAL);
    if (!observer) return js_promise_reject(
        js_error_message("Promise observer allocation failed"));
    observer->promise = promise;
    heap_register_gc_root(&observer->promise.item);
    if (!lambda_task_on_complete(task, lambda_promise_observer_complete,
            observer, lambda_promise_observer_destroy)) {
        lambda_promise_observer_destroy(observer);
        return js_promise_reject(
            js_error_message("Promise observer registration failed"));
    }
    return promise;
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
    if (!waiter || !js_promise_is(promise)) {
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
    Item* rejected_env = js_alloc_env(1);
    if (!fulfilled_env || !rejected_env) {
        return err2it(err_create_heap(ERR_OUT_OF_MEMORY,
            "Promise reaction allocation failed", NULL));
    }
    fulfilled_env[0] = lambda_task_handle(waiter);
    rejected_env[0] = lambda_task_handle(waiter);
    Item on_fulfilled = js_new_closure(
        (void*)lambda_promise_fulfilled, 1, fulfilled_env, 1);
    Item on_rejected = js_new_closure(
        (void*)lambda_promise_rejected, 1, rejected_env, 1);
    js_promise_then(promise, on_fulfilled, on_rejected);
    lambda_task_park(waiter);
    log_debug("concurrency JS: parked Lambda task on Promise");
    return (Item){.item = ITEM_TASK_SUSPENDED};
}

static Item lambda_js_procedure_call(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item function = env ? env[0] : ItemNull;
    if (get_type_id(function) != LMD_TYPE_FUNC) {
        return js_promise_reject(js_error_message("invalid Lambda procedure"));
    }
    List* args = list();
    int64_t count = get_type_id(rest_args) == LMD_TYPE_ARRAY
        ? js_array_length(rest_args) : 0;
    for (int64_t i = 0; i < count; i++) {
        list_push(args, js_array_get_int(rest_args, i));
    }
    Item handle = lambda_task_start_function(function, args);
    if (!lambda_task_handle_is(handle)) {
        return js_promise_reject(lambda_error_to_js(handle));
    }
    log_debug("concurrency JS: dispatched Lambda procedure as task");
    return lambda_handle_to_js_promise(handle);
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
    js_set_formal_length(wrapper, arity);
    if (name) {
        js_set_function_name(wrapper,
            (Item){.item = s2it(heap_create_name(name))});
    }
    return wrapper;
}
