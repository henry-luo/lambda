#include "loader_runtime_adapter.hpp"
#include "script_loop_adapter.hpp"
#include "radiant/script_loop_host.hpp"
#include "radiant/event.hpp"
#include "radiant/radiant.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/concurrency.h"
#include "../../runtime/activation.h"
#include "../../js/js_runtime_state.hpp"
#include "../../js/js_runtime.h"
#include "../../js/js_event_loop.h"
#include "../../dom/dom.h"

extern __thread Context* input_context;
extern __thread EvalContext* context;
extern "C" void js_batch_reset(void);
extern "C" void js_globals_batch_reset(void);

static bool script_loop_begin(UiContext* ui, DomDocument* doc, RadiantScriptLoopTurn* turn) {
    Runtime* runtime = dom_document_script_runtime(doc);
    if (!runtime) return true;
    EvalContext* pump_ctx = runtime_get_eval_context(runtime);
    if (!pump_ctx || !runtime_heap(runtime) || !runtime_name_pool(runtime)) return false;
    Context* saved_input = input_context;
    // A frame can leave its evaluator bound; a host turn must use its own document.
    if (!radiant_eval_context_switch(pump_ctx) ||
            !runtime_context_bind_retained(runtime, pump_ctx)) return false;
    input_context = nullptr;
    if ((js_runtime_state_for(pump_ctx) && !js_runtime_state_init(pump_ctx)) ||
            !js_runtime_state_ensure_input(pump_ctx) ||
            !radiant_bind_document_script_host(ui, doc)) {
        input_context = saved_input;
        return false;
    }
    turn->saved_input = saved_input;
    turn->active = true;
    return true;
}

static bool script_loop_pump(bool active, bool advance, int wait_ms,
                             double delta_ms, int frame_steps) {
    if (advance) {
        if (!active || js_event_loop_virtual_clock_enabled())
            return js_event_loop_advance_virtual_time(delta_ms, frame_steps) > 0;
        // Interactive replay drains one bounded turn on the native clock.
        js_event_loop_pump_nowait();
        double timestamp_ms = js_performance_monotonic_now_ms();
        bool pumped = js_animation_frame_flush(timestamp_ms) > 0;
        return dom_tick_headless_animation_frame() || pumped;
    }
    if (wait_ms >= 0) return js_event_loop_pump_wait(wait_ms);
    js_event_loop_pump_nowait();
    return active;
}

static void script_loop_end(RadiantScriptLoopTurn* turn) {
    if (turn->active) input_context = static_cast<Context*>(turn->saved_input);
}

static bool script_window_bind_document(DomDocument* doc) {
    Runtime* runtime = dom_document_script_runtime(doc);
    EvalContext* owner = runtime ? runtime_get_eval_context(runtime) : nullptr;
    return !owner || radiant_eval_context_switch(owner);
}

static void script_window_cancel_document(DomDocument* doc) {
    runtime_request_satellite_cancel(dom_document_script_runtime(doc));
}

static bool script_window_quiescent() {
    return !activation_current() && (!context || !context->execution_depth);
}

static void script_window_detach() {
    dom_set_ui_context(nullptr);
    dom_set_host_driven_loop(false);
}

static void script_window_cleanup_batch(bool reset_dom_cache) {
    if (!script_runner_js_batch_cleanup_unsafe()) {
        js_event_loop_shutdown();
        // The JS batch reset owns the DOM reset and releases its range wrappers.
        js_batch_reset();
        // Layout batches also clear the weak cache when no JS capsule survived.
        if (reset_dom_cache) dom_batch_reset();
        js_globals_batch_reset();
        script_runner_cleanup_heap();
    }
}

static void script_window_reset_clock() {
    js_event_loop_set_virtual_clock(false, 0.0);
}

static bool script_window_animation_frame(double time_ms, bool* callbacks_ran) {
    if (!js_animation_frame_has_pending()) return false;
    int callbacks = js_animation_frame_flush(time_ms);
    if (callbacks_ran) *callbacks_ran = callbacks > 0;
    return js_animation_frame_has_pending() != 0;
}

static void script_window_configure(bool retain, bool static_snapshot, bool external_scripts) {
    script_runner_set_retain_js_state(retain);
    script_runner_set_static_headless_snapshot(static_snapshot);
    script_runner_set_execute_external_scripts(external_scripts);
}

static bool script_window_edit_transform(bool source, const LambdaDocumentTransformConfig** transform,
                                         const LambdaDocumentTransformOption** options, int* option_count) {
    const LambdaDocumentTransformConfig* edit = lambda_document_transform_for_input_type("edit");
    if (!edit || !transform || !options || !option_count) return false;
    static const LambdaDocumentTransformOption edit_options[] = {
        {"source", LAMBDA_DOCUMENT_TRANSFORM_OPTION_BOOL, nullptr, true},
    };
    *transform = edit;
    *options = source ? edit_options : nullptr;
    *option_count = source ? 1 : 0;
    return true;
}

bool lambda_script_loop_host_register() {
    static const RadiantScriptLoopHostOps operations = {
        script_loop_begin, script_loop_pump, script_loop_end, script_window_bind_document,
        script_window_cancel_document, script_window_quiescent, script_window_detach,
        script_window_cleanup_batch, script_window_reset_clock, script_window_animation_frame,
        script_window_configure, lambda_scheduler_progress_generation, script_window_edit_transform,
    };
    return radiant_script_loop_host_register(&operations);
}
