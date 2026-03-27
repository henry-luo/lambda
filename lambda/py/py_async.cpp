// lambda/py/py_async.cpp — Python async/await and asyncio support (Phase D)
// ==========================================================================
// Coroutines are specializations of generator objects: they carry
// FN_FLAG_IS_COROUTINE (0x08) in addition to FN_FLAG_IS_GENERATOR (0x04).
//
// await expr  compiles as  yield from expr + py_coro_get_return()
// async def   compiles as  generator (state machine) via pm_compile_generator
//
// asyncio event loop: single-threaded, cooperative.
//   asyncio.run(coro)          → py_coro_drive(coro)
//   asyncio.sleep(n)           → C-backed coroutine: optionally sleeps, returns None
//   asyncio.gather(c1, c2, …)  → drive each coroutine in order, return list of values
// ==========================================================================

#include "py_async.h"
#include "py_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.h"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>
#include <unistd.h>

// =========================================================================
// Coroutine object creation
// =========================================================================

extern "C" Item py_coro_create(void* resume_fn_ptr, int frame_size) {
    // Identical to py_gen_create but also sets FN_FLAG_IS_COROUTINE.
    Item gen = py_gen_create(resume_fn_ptr, frame_size);
    if (get_type_id(gen) == LMD_TYPE_FUNC && gen.function) {
        gen.function->flags |= FN_FLAG_IS_COROUTINE;
    }
    return gen;
}

extern "C" bool py_is_coroutine(Item x) {
    if (get_type_id(x) != LMD_TYPE_FUNC) return false;
    Function* fn = x.function;
    return fn && (fn->flags & FN_FLAG_IS_COROUTINE);
}

// =========================================================================
// Coroutine return-value side channel (safe in single-threaded mode)
// =========================================================================

static Item g_coro_return_value = {.item = ITEM_NULL};

extern "C" Item py_coro_set_return(Item value) {
    g_coro_return_value = value;
    return value;
}

extern "C" Item py_coro_get_return(void) {
    Item v = g_coro_return_value;
    g_coro_return_value = {.item = ITEM_NULL};
    return v;
}

// =========================================================================
// Coroutine driving
// =========================================================================

extern "C" Item py_coro_drive(Item coro) {
    if (get_type_id(coro) != LMD_TYPE_FUNC) {
        // Not a coroutine — return as-is (plain value passed to await)
        return coro;
    }
    // Drive via py_gen_send until StopIteration.
    Item sent = {.item = ITEM_NULL};
    while (true) {
        Item result = py_gen_send(coro, sent);
        if (py_is_stop_iteration(result)) {
            return py_coro_get_return();
        }
        // In a single-threaded model, ignore intermediate yielded values
        // and drive forward immediately.
        sent = {.item = ITEM_NULL};
    }
}

// =========================================================================
// asyncio.sleep — C-backed immediately-completing coroutine
// =========================================================================

// Resume function for the sleep coroutine.
// frame[0] = state  (0 = not yet run, -1 = exhausted)
// frame[1] = seconds as raw uint64_t bits (via memcpy from double)
static uint64_t py_sleep_resume(uint64_t* frame, uint64_t /*sent*/) {
    if (frame[0] == (uint64_t)-1) {
        // Already exhausted — return StopIteration again.
        return py_stop_iteration().item;
    }
    // Read requested seconds.
    double secs = 0.0;
    memcpy(&secs, &frame[1], sizeof(double));
    if (secs > 0.0) {
        // Actually pause for n seconds (blocking; acceptable for scripting use).
        long usecs = (long)(secs * 1000000.0);
        if (usecs > 0) usleep((unsigned int)usecs);
    }
    // Mark exhausted and return None via the side channel.
    frame[0] = (uint64_t)-1;
    py_coro_set_return({.item = ITEM_NULL});
    return py_stop_iteration().item;
}

extern "C" Item py_asyncio_sleep(Item seconds) {
    double secs = 0.0;
    TypeId t = get_type_id(seconds);
    if (t == LMD_TYPE_INT)   secs = (double)seconds.get_int56();
    else if (t == LMD_TYPE_FLOAT) secs = seconds.get_double();

    // frame_size=2: slot 0 = state, slot 1 = seconds bits
    Item coro = py_coro_create((void*)py_sleep_resume, 2);
    if (get_type_id(coro) == LMD_TYPE_FUNC && coro.function) {
        uint64_t* frame = (uint64_t*)coro.function->closure_env;
        if (frame) {
            memcpy(&frame[1], &secs, sizeof(double));
        }
    }
    return coro;
}

// =========================================================================
// asyncio.run — drive a top-level coroutine to completion
// =========================================================================

extern "C" Item py_asyncio_run(Item coro) {
    return py_coro_drive(coro);
}

// =========================================================================
// asyncio.gather — drive multiple coroutines, return list of results
// =========================================================================

extern "C" Item py_asyncio_gather(Item a0, Item a1, Item a2,
                                   Item a3, Item a4, Item a5) {
    Item coros[] = {a0, a1, a2, a3, a4, a5};
    Array* results = array();
    for (int i = 0; i < 6; i++) {
        if (get_type_id(coros[i]) == LMD_TYPE_NULL) break;
        Item val = py_coro_drive(coros[i]);
        array_push(results, val);
    }
    return (Item){.array = results};
}

// =========================================================================
// asyncio.create_task — in single-threaded mode, immediately drive the coro
// =========================================================================

extern "C" Item py_asyncio_create_task(Item coro) {
    // In a multi-tasking event loop this would schedule the coroutine.
    // For our single-threaded model: drive it to completion immediately and
    // return a "Task" object (just the result wrapped in a trivial completed coro).
    Item result = py_coro_drive(coro);
    // Return a dummy Map with 'result' field so callers can inspect it if needed.
    Item task = py_dict_new();
    py_dict_set(task, (Item){.item = s2it(heap_create_name("result"))}, result);
    py_dict_set(task, (Item){.item = s2it(heap_create_name("done"))},
                (Item){.item = b2it(true)});
    return task;
}

// =========================================================================
// asyncio module namespace
// =========================================================================

extern "C" Item py_stdlib_asyncio_init(void) {
    Item mod = py_dict_new();

    py_dict_set(mod, (Item){.item = s2it(heap_create_name("run"))},
        py_new_function((void*)py_asyncio_run, 1));
    py_dict_set(mod, (Item){.item = s2it(heap_create_name("sleep"))},
        py_new_function((void*)py_asyncio_sleep, 1));
    py_dict_set(mod, (Item){.item = s2it(heap_create_name("gather"))},
        py_new_function((void*)py_asyncio_gather, 6));
    py_dict_set(mod, (Item){.item = s2it(heap_create_name("create_task"))},
        py_new_function((void*)py_asyncio_create_task, 1));

    return mod;
}
