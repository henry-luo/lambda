// test_utils_runtime.cpp — Pool+Heap+EvalContext fixture for tests that need
// the Lambda runtime. Split from lib/test_utils.c so tests using only the
// plain-C helpers (temp dir, process spawn, file/string utils) can avoid
// pulling in lambda/transpiler.hpp and the MIR JIT compile cost.
//
// See lib/test_utils.h for the API contract.

#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "mempool.h"

// Pulls in MIR + lambda-data + ast. Heavy but unavoidable for the runtime
// fixture; tests that don't need it should not list this .cpp under
// additional_sources.
#include "../lambda/runtime/transpiler.hpp"

// Defined in lambda/runner.cpp; we just need to flip the pointer.
extern __thread EvalContext* context;

// Provided by lambda/path.c.
extern "C" void path_init(void);

static Pool* test_path_pool_provider(void) {
    return ::context && ::context->heap ? ::context->heap->pool : nullptr;
}

void tu_setup_runtime(Pool** out_pool, Heap* heap, EvalContext* ctx) {
    log_init(NULL);

    Pool* pool = pool_create();
    if (!pool) {
        // No gtest dependency here — abort so the failure surfaces in test
        // output immediately rather than crashing later on a NULL pool.
        fprintf(stderr, "tu_setup_runtime: pool_create() failed\n");
        abort();
    }

    heap->pool = pool;
    heap->gc = nullptr;

    memset(ctx, 0, sizeof(*ctx));
    ctx->heap = heap;
    ctx->pool = pool;

    ::context = ctx;
    path_register_pool_provider(test_path_pool_provider);
    path_init();

    if (out_pool) *out_pool = pool;
}

void tu_teardown_runtime(Pool* pool) {
    ::context = nullptr;
    if (pool) pool_destroy(pool);
}
