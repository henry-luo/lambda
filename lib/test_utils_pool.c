// test_utils_pool.c — pool-only fixture helpers for tests that need Pool.
//
// Kept separate from lib/test_utils.c so file/string/process-only tests can
// link pure helpers without dragging in log/mempool symbols.

#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "mempool.h"

Pool* tu_setup_pool(void) {
    log_init(NULL);
    Pool* pool = pool_create();
    if (!pool) {
        // No gtest dependency here — abort so the failure surfaces immediately
        // rather than crashing later on a NULL pool deref.
        fprintf(stderr, "tu_setup_pool: pool_create() failed\n");
        abort();
    }
    return pool;
}

void tu_teardown_pool(Pool* pool) {
    if (pool) pool_destroy(pool);
}
