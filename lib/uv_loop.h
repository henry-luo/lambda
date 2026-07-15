/**
 * @file uv_loop.h
 * @brief Unified libuv event loop wrapper for Lambda
 *
 * Provides a global event loop shared by the JS runtime,
 * HTTP server, and async I/O subsystems.
 */

#ifndef LAMBDA_UV_LOOP_H
#define LAMBDA_UV_LOOP_H

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

// global event loop (one per process)
uv_loop_t* lambda_uv_loop(void);

// lifecycle
int  lambda_uv_init(void);       // create loop, return 0 on success
int  lambda_uv_run(void);        // run until no active handles/requests
void lambda_uv_stop(void);       // stop loop (from signal handler etc.)
void lambda_uv_cleanup(void);    // close all handles, free loop
void lambda_uv_abandon(void);    // free an unsafe loop without walking handles

// JS task integration — called at event-loop phase checkpoints
void lambda_uv_set_microtask_drain(void (*drain_fn)(void));
void lambda_uv_set_task_drain(void (*drain_fn)(void));

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_UV_LOOP_H
