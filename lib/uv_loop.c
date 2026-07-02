/**
 * @file uv_loop.c
 * @brief Unified libuv event loop wrapper implementation
 */

#include "uv_loop.h"
#include "memtrack.h"
#include "log.h"
#include <stdlib.h>

static uv_loop_t *g_loop = NULL;
static uv_prepare_t g_prepare;
static uv_check_t g_check;
static void (*g_microtask_drain)(void) = NULL;
static int g_prepare_active = 0;
static int g_check_active = 0;

static void drain_task_checkpoint(void) {
    if (g_microtask_drain) {
        g_microtask_drain();
    }
}

static void prepare_cb(uv_prepare_t *handle) {
    (void)handle;
    // drain tasks before libuv enters poll
    drain_task_checkpoint();
}

static void check_cb(uv_check_t *handle) {
    (void)handle;
    // drain tasks after poll callbacks complete
    drain_task_checkpoint();
}

uv_loop_t* lambda_uv_loop(void) {
    return g_loop;
}

int lambda_uv_init(void) {
    if (g_loop) return 0; // already initialized

    g_loop = (uv_loop_t *)mem_alloc(sizeof(uv_loop_t), MEM_CAT_TEMP);
    if (!g_loop) {
        log_error("uv_loop: failed to allocate loop");
        return -1;
    }

    int r = uv_loop_init(g_loop);
    if (r != 0) {
        log_error("uv_loop: uv_loop_init failed: %s", uv_strerror(r));
        mem_free(g_loop);
        g_loop = NULL;
        return r;
    }

    // install prepare handle for JS task draining
    uv_prepare_init(g_loop, &g_prepare);
    uv_prepare_start(&g_prepare, prepare_cb);
    // unref so the prepare handle alone doesn't keep the loop alive
    uv_unref((uv_handle_t *)&g_prepare);
    g_prepare_active = 1;

    // install check handle so callbacks that resolve promises during poll get
    // a same-iteration JS task checkpoint.
    uv_check_init(g_loop, &g_check);
    uv_check_start(&g_check, check_cb);
    uv_unref((uv_handle_t *)&g_check);
    g_check_active = 1;

    log_debug("uv_loop: initialized");
    return 0;
}

int lambda_uv_run(void) {
    if (!g_loop) return -1;
    return uv_run(g_loop, UV_RUN_DEFAULT);
}

void lambda_uv_stop(void) {
    if (g_loop) uv_stop(g_loop);
}

static void close_walk_cb(uv_handle_t *handle, void *arg) {
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

void lambda_uv_cleanup(void) {
    if (!g_loop) return;

    if (g_prepare_active) {
        uv_prepare_stop(&g_prepare);
        g_prepare_active = 0;
    }
    if (g_check_active) {
        uv_check_stop(&g_check);
        g_check_active = 0;
    }

    // close all remaining handles
    uv_walk(g_loop, close_walk_cb, NULL);
    uv_run(g_loop, UV_RUN_DEFAULT);

    uv_loop_close(g_loop);
    mem_free(g_loop);
    g_loop = NULL;
    g_microtask_drain = NULL;

    log_debug("uv_loop: cleaned up");
}

void lambda_uv_abandon(void) {
    if (!g_loop) return;

    // A signal-aborted JS run can leave libuv queues half-mutated; walking or
    // closing handles would re-enter corrupted queue links during recovery.
    mem_free(g_loop);
    g_loop = NULL;
    g_prepare_active = 0;
    g_check_active = 0;
    g_microtask_drain = NULL;
    log_error("uv_loop: abandoned unsafe loop without closing handles");
}

void lambda_uv_set_microtask_drain(void (*drain_fn)(void)) {
    g_microtask_drain = drain_fn;
}
