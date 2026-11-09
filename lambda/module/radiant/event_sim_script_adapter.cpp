#include "loader_runtime_adapter.hpp"
#include "event_sim_script_adapter.hpp"
#include "radiant/event_sim_render.hpp"
#include "radiant/event.hpp"
#include "radiant/radiant.hpp"
#include "lambda/input/css/dom_lifecycle.hpp"
#include "../../dom/dom.h"
#include "../../dom/realm/dom_realm.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/gc/gc_heap.h"
#include "../../js/js_runtime.h"
#include "../../js/js_runtime_state.hpp"
#include "../../js/js_event_loop.h"
#include "lib/time_util.h"
#include <math.h>

extern "C" bool radiant_document_ensure_evaluator(DomDocument* document);
extern "C" void dom_window_dialog_push_response(const char* value);

static Runtime* bind_runtime(UiContext* ui) {
    if (ui && ui->document && !dom_document_script_runtime(ui->document))
        radiant_document_ensure_evaluator(ui->document);
    Runtime* runtime = ui && ui->document ? dom_document_script_runtime(ui->document) : nullptr;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!runtime || !radiant_eval_context_switch(owner)) return nullptr;
    // a script-less page's evaluator has no heap until its first script turn.
    bool reusing = false;
    if ((!runtime_heap(runtime) && !js_prepare_eval_context(runtime, true, &owner, &reusing)) ||
        !runtime_context_bind_retained(runtime, owner) || !js_runtime_state_ensure_input(owner) ||
        !radiant_bind_document_script_host(ui, ui->document)) return nullptr;
    return runtime;
}

bool lambda_event_sim_evaluate(UiContext* ui, const char* source, bool assertion) {
    Runtime* runtime = bind_runtime(ui);
    if (!runtime || !source) return false;
    // eval borrows the host; publishing it as an owned document would recurse during teardown.
    void* document = runtime->dom_doc;
    void* host = runtime->dom_ui_context;
    runtime->dom_doc = ui->document;
    runtime->dom_ui_context = ui;
    // the evaluated code and its return value remain precise roots across eval safepoints (D6.3.3).
    RootFrame roots(2);
    Rooted<Item> code(roots, make_string_item(source)), result(roots, ItemNull);
    result.set(js_builtin_eval(code.get()));
    runtime->dom_doc = document;
    runtime->dom_ui_context = host;
    if (item_is_error(result.get())) return false;
    return !assertion || (get_type_id(result.get()) == LMD_TYPE_BOOL && result.get().bool_val);
}

bool lambda_event_sim_collect_gc(UiContext* ui, double count) {
    Runtime* runtime = bind_runtime(ui);
    bool passed = runtime && count >= 1 && count <= 100 && count == floor(count);
    if (passed) for (unsigned i = 0; i < count; i++) gc_collect(runtime_heap(runtime)->gc, nullptr, 0);
    return passed;
}

bool lambda_event_sim_animation_frame(UiContext* ui, double time, int* callbacks) {
    if (!bind_runtime(ui) || !isfinite(time) || time < 0 || !callbacks) return false;
    *callbacks = js_animation_frame_flush(time);
    return true;
}

bool lambda_event_sim_measure_frames(UiContext* ui, const char* callback_name, const char* sample_name,
                                    size_t frames, double* durations, double* sampling) {
    RootFrame roots(4);
    Rooted<Item> global(roots, dom_realm_global());
    Rooted<Item> callback(roots, dom_realm_get_name(global.get(), callback_name));
    Rooted<Item> sample(roots, ItemNull), result(roots, ItemNull);
    // performance.now() must observe wall time inside the callback, not the fixture's frozen clock.
    bool virtual_clock = js_event_loop_virtual_clock_enabled();
    double virtual_ms = js_event_loop_virtual_clock_now_ms();
    bool document_clock = ui->document->js.virtual_clock_enabled;
    ui->document->js.virtual_clock_enabled = false;
    js_event_loop_set_virtual_clock(false, 0);
    bool passed = true;
    for (size_t i = 0; i < frames && passed; i++) {
        uint64_t start = time_now_ns();
        result.set(dom_realm_call(callback.get(), ItemNull, nullptr, 0));
        sample.set(dom_realm_get_name(global.get(), sample_name));
        passed = !item_is_error(result.get()) && item_try_to_double(sample.get(), &sampling[i]);
        if (passed) event_sim_render_surface(ui);
        passed &= !ui->render_failed;
        durations[i] = time_elapsed_ms_f(start, time_now_ns());
    }
    ui->document->js.virtual_clock_enabled = document_clock;
    js_event_loop_set_virtual_clock(virtual_clock, virtual_ms);
    return passed;
}

double lambda_event_sim_clock_ms(bool relative) {
    return relative ? js_performance_now_ms() : js_performance_monotonic_now_ms();
}

void lambda_event_sim_dialog_response(const char* value) {
    dom_window_dialog_push_response(value);
}

SelectorMatcher* lambda_event_sim_selector_matcher(DomDocument* document, Pool* pool) {
    return (SelectorMatcher*)dom_create_selector_matcher_bridge(document, pool);
}

DomElement* lambda_event_sim_element_from_point(DomDocument* document, float x, float y) {
    return (DomElement*)dom_document_element_from_point_native(document, x, y);
}
