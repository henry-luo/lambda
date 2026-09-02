/**
 * js_node_trace_seam.cpp — weak defaults for the Node trace-events provider.
 *
 * `js_runtime.cpp` lives in the `lambda-rt` link target but calls two Node
 * trace-events entry points that are compiled by the `node-core` and `lambda`
 * targets above it. Every binary that links the runtime archive without
 * node-core -- the validator, null-vs-missing and validator-input gtests among
 * them -- therefore failed to link once `js_runtime.o` was rebuilt; the defect
 * has been latent since the calls were added (f501e1b2f, 2026-08-20) and was
 * hidden only by a stale archive member.
 *
 * These are the weak defaults for that provider seam, the same pattern
 * `dom_lifecycle.cpp` uses for the view-tree and range hooks: a binary that
 * links node-core gets the real implementations, and one that does not gets a
 * no-op instead of an unresolved symbol. Tracing is a Node facility, so a
 * runtime built without Node emitting nothing is the correct behaviour.
 *
 * The seam covers every entry point the provider header declares, not only the
 * three the linker happened to name first: a partial seam just moves the same
 * failure to the next binary that links a different subset.
 */

#include "../module/node_core/node_trace_events.hpp"

__attribute__((weak)) Item node_trace_events_internal_binding(void) {
    return ItemNull;
}

__attribute__((weak)) void node_trace_events_emit_async_hooks_init(
        const char* type_chars, int type_len, int64_t async_id, int64_t trigger_id) {
    (void)type_chars; (void)type_len; (void)async_id; (void)trigger_id;
}

__attribute__((weak)) Item node_trace_events_namespace(void) {
    return ItemNull;
}

__attribute__((weak)) int node_trace_events_init(const JubeHostAPI* host) {
    (void)host;
    return 0;   // no provider linked: initialising "no tracing" succeeds
}

__attribute__((weak)) void node_trace_events_shutdown(void) {}

__attribute__((weak)) void node_trace_events_runtime_reset(void* session) {
    (void)session;
}

__attribute__((weak)) void node_trace_events_runtime_detach(void* session) {
    (void)session;
}

__attribute__((weak)) void node_trace_events_flush(void) {}
