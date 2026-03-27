#pragma once
// py_async.h — Python async/await and asyncio support (Phase D)
// Coroutines are generator objects with FN_FLAG_IS_COROUTINE set.
// The event loop is single-threaded and cooperative.

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// =========================================================================
// Coroutine creation and identification
// =========================================================================

// Create a coroutine object (like py_gen_create but sets FN_FLAG_IS_COROUTINE).
Item py_coro_create(void* resume_fn_ptr, int frame_size);

// Returns true if x is a coroutine (IS_GENERATOR + IS_COROUTINE flags).
bool py_is_coroutine(Item x);

// =========================================================================
// Coroutine return value side-channel
// =========================================================================

// Called by coroutine MIR code before exhaustion to store the return value.
// Returns the value passed in (so MIR call sites get a valid i64 result).
Item py_coro_set_return(Item value);

// Retrieve (and clear) the last stored coroutine return value.
Item py_coro_get_return(void);

// =========================================================================
// Coroutine driving
// =========================================================================

// Drive a coroutine/generator to completion; return its final return value.
// Equivalent to: while not StopIteration: send(None); return py_coro_get_return()
Item py_coro_drive(Item coro);

// =========================================================================
// asyncio module functions (registered in sys_func_registry)
// =========================================================================

// asyncio.run(coro) — drive top-level coroutine to completion.
Item py_asyncio_run(Item coro);

// asyncio.sleep(seconds) — returns an immediately-completing awaitable.
Item py_asyncio_sleep(Item seconds);

// asyncio.gather(c0, c1, c2, c3, c4, c5) — drive up to 6 coroutines,
// returns a list of their return values. Extra ItemNull args are ignored.
Item py_asyncio_gather(Item a0, Item a1, Item a2, Item a3, Item a4, Item a5);

// asyncio.create_task(coro) — schedule a coroutine (single-threaded: drives now).
Item py_asyncio_create_task(Item coro);

// =========================================================================
// asyncio module initializer
// =========================================================================

// Returns a Map representing the 'asyncio' module namespace.
Item py_stdlib_asyncio_init(void);

#ifdef __cplusplus
}
#endif
