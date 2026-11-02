#include "document_clock.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/runtime/transpiler.hpp"
#include <uv.h>

bool radiant_document_clock(DomDocument* document, double* now_ms) {
    Runtime* runtime = dom_document_script_runtime(document);
    if (!runtime) {
        bool virtual_clock = js_event_loop_virtual_clock_enabled();
        if (now_ms) *now_ms = virtual_clock ? js_event_loop_virtual_clock_now_ms() : js_performance_monotonic_now_ms();
        return virtual_clock;
    }
    // another document may be thread-active; its virtual clock cannot time this owner.
    JsRuntimeState* state = js_runtime_state_for(runtime_get_eval_context(runtime));
    bool virtual_clock = state && state->event_loop && state->event_loop->virtual_clock_enabled;
    if (now_ms) {
        if (state && state->performance.frame_clock_active) *now_ms = state->performance.frame_clock_ms;
        else if (virtual_clock) *now_ms = state->event_loop->virtual_clock_ms;
        // libuv uses the host uptime epoch; another realm's Performance clock may be virtual.
        else *now_ms = (double)uv_hrtime() / 1e6;
    }
    return virtual_clock;
}
