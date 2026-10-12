// The weak half of the engine seams declared in dom_engine.h. Each answers the
// "no engine linked" case: absence, zero, or nothing done. RADIANT_INPUT_VALUE_
// TEXT is 0, so the input-kind default reads as a plain text control, which is
// the benign branch at every call site.

#include "dom_engine.h"
#include "dom_graphics.h"
#include "dom_style.h"

// Native providers belong to the lower runtime; native module registration
// publishes immutable process-lifetime tables without an upward link (D7.1.3).
static const DomCanvasEngineOps* canvas_engine_operations = nullptr;
static const DomWebGlEngineOps* webgl_engine_operations = nullptr;
static const DomStyleEngineOps* style_engine_operations = nullptr;

template<class Operations, class... Fields>
static bool dom_provider_register(const Operations** slot, const Operations* operations, Fields... fields) {
    if (!operations || !(... && (operations->*fields))) return false;
    const Operations* expected = nullptr;
    return __atomic_compare_exchange_n(slot, &expected, operations, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
           expected == operations;
}

template<class Operations>
static const Operations* dom_provider_load(const Operations* const* slot) {
    return __atomic_load_n(slot, __ATOMIC_ACQUIRE);
}

#define DOM_PROVIDER_MEMBER(result, name, parameters) , &Operations::name
extern "C" bool dom_canvas_register_engine(const DomCanvasEngineOps* operations) {
    using Operations = DomCanvasEngineOps;
    return dom_provider_register(&canvas_engine_operations, operations
        DOM_CANVAS_ENGINE_OPERATIONS(DOM_PROVIDER_MEMBER));
}

extern "C" bool dom_webgl_register_engine(const DomWebGlEngineOps* operations) {
    using Operations = DomWebGlEngineOps;
    return dom_provider_register(&webgl_engine_operations, operations
        DOM_WEBGL_ENGINE_OPERATIONS(DOM_PROVIDER_MEMBER));
}

extern "C" bool dom_style_register_engine(const DomStyleEngineOps* operations) {
    using Operations = DomStyleEngineOps;
    return dom_provider_register(&style_engine_operations, operations
        DOM_STYLE_ENGINE_OPERATIONS(DOM_PROVIDER_MEMBER));
}
#undef DOM_PROVIDER_MEMBER

extern "C" const DomCanvasEngineOps* dom_canvas_engine_ops(void) {
    return dom_provider_load(&canvas_engine_operations);
}

extern "C" const DomWebGlEngineOps* dom_webgl_engine_ops(void) {
    return dom_provider_load(&webgl_engine_operations);
}

extern "C" const DomStyleEngineOps* dom_style_engine_ops(void) {
    return dom_provider_load(&style_engine_operations);
}

#define DOM_ENGINE_WEAK extern "C" __attribute__((weak))

DOM_ENGINE_WEAK bool dom_engine_author_template_event_live(const char* n) { (void)n; return false; }
DOM_ENGINE_WEAK void dom_engine_dispatch_author_template_participant(void* d, Item e, const char* n) {
    (void)d; (void)e; (void)n;
}
DOM_ENGINE_WEAK DocState* dom_engine_document_ensure_state(DomDocument* d, const char* o) {
    (void)d; (void)o; return nullptr;
}
DOM_ENGINE_WEAK void dom_engine_reset_wrapper_cache(void) {}
DOM_ENGINE_WEAK bool dom_engine_window_is_closed(void* u) { (void)u; return true; }
DOM_ENGINE_WEAK bool dom_engine_event_cascade_active(void) { return false; }
DOM_ENGINE_WEAK bool dom_engine_exec_command(void* d, const char* c, const char* v) {
    (void)d; (void)c; (void)v; return false;
}
DOM_ENGINE_WEAK bool dom_engine_history_initialize(DomDocument* d) { (void)d; return false; }
DOM_ENGINE_WEAK int dom_engine_history_length(DomDocument* d) { (void)d; return 0; }
DOM_ENGINE_WEAK const char* dom_engine_history_scroll_restoration(DomDocument* d) { (void)d; return "auto"; }
DOM_ENGINE_WEAK bool dom_engine_history_go(DomDocument* d, int delta, RadiantHistoryTraversal* t) {
    (void)d; (void)delta; (void)t; return false;
}
DOM_ENGINE_WEAK bool dom_engine_history_set_location(DomDocument* d, const char* u, RadiantHistoryTraversal* t) {
    (void)d; (void)u; (void)t; return false;
}
DOM_ENGINE_WEAK int dom_engine_input_value_kind(const char* type) { (void)type; return 0; }
DOM_ENGINE_WEAK const char* dom_engine_input_live_value(DomElement* e) { (void)e; return nullptr; }
DOM_ENGINE_WEAK bool dom_engine_input_value_as_number(const char* t, const char* v,
                                                       double* out) {
    (void)t; (void)v; (void)out; return false;
}
DOM_ENGINE_WEAK bool dom_engine_input_set_live_value(DomElement* e, const char* v) {
    (void)e; (void)v; return false;
}
DOM_ENGINE_WEAK void dom_engine_input_reset_live_value(DomElement* e) { (void)e; }
DOM_ENGINE_WEAK bool dom_engine_input_value_sanitize(const char* t, const char* v, char* out, size_t n) {
    (void)t; (void)v; (void)out; (void)n; return false;
}
DOM_ENGINE_WEAK void dom_engine_input_value_validate(const char* t, const char* v, const char* mn,
                                                     const char* mx, const char* st,
                                                     RadiantInputValidity* out) {
    (void)t; (void)v; (void)mn; (void)mx; (void)st; (void)out;
}
DOM_ENGINE_WEAK void dom_engine_reconcile_dom_mutations(UiContext* u, DomDocument* d) { (void)u; (void)d; }
DOM_ENGINE_WEAK void* dom_engine_element_from_point(DomDocument* d, float x, float y) {
    (void)d; (void)x; (void)y; return nullptr;
}
DOM_ENGINE_WEAK void* dom_engine_subtree_element_from_point(DomElement* root, float x, float y) {
    (void)root; (void)x; (void)y; return nullptr;
}
DOM_ENGINE_WEAK bool dom_engine_hit_test_skips_subtree(DomElement* element) {
    (void)element; return false;
}
DOM_ENGINE_WEAK bool dom_engine_content_hidden_for_user_agent(DomElement* element) {
    (void)element; return false;
}
DOM_ENGINE_WEAK bool dom_engine_content_is_skipped(DomElement* element, bool include_self) {
    (void)element; (void)include_self; return false;
}
DOM_ENGINE_WEAK char* dom_engine_element_inner_text(DomElement* element) {
    (void)element; return nullptr;
}
DOM_ENGINE_WEAK void dom_engine_svg_timing_event(DomElement* target, const char* type, bool bubbles, double detail) {
    (void)target; (void)type; (void)bubbles; (void)detail;
}
DOM_ENGINE_WEAK void dom_engine_svg_timing_key(DomElement* target, const char* key) {
    (void)target; (void)key;
}
DOM_ENGINE_WEAK bool dom_engine_layout_active(DomDocument* d) { (void)d; return false; }
DOM_ENGINE_WEAK bool dom_engine_element_css_boxes(DomElement* e, DomCssBoxSizes* s) {
    (void)e; (void)s; return false;
}
DOM_ENGINE_WEAK bool dom_engine_element_viewport_origin(DomElement* e, float* x, float* y) {
    (void)e;
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    return false;
}
DOM_ENGINE_WEAK bool dom_engine_attribute_has_relational_css_dependency(DomDocument* d, const char* n) {
    (void)d; (void)n; return true;
}
DOM_ENGINE_WEAK void dom_engine_sync_pseudo_state(void* v, uint32_t f, bool set) {
    (void)v; (void)f; (void)set;
}
DOM_ENGINE_WEAK bool dom_engine_set_image_source(DomElement* e, const char* s) {
    (void)e; (void)s; return false;
}
DOM_ENGINE_WEAK bool dom_engine_image_natural_size(DomElement* e, int* w, int* h) {
    (void)e;
    if (w) *w = 0;
    if (h) *h = 0;
    return false;
}
DOM_ENGINE_WEAK bool dom_engine_image_rendered_size(DomElement* e,int* w,int* h) {
    (void)e;(void)w;(void)h;return false;
}
DOM_ENGINE_WEAK void dom_engine_install_animation_globals(void) {}
DOM_ENGINE_WEAK void dom_engine_animation_frame_prepare(void* d,double t) {(void)d;(void)t;}
DOM_ENGINE_WEAK uint64_t dom_engine_frame_request(void* n, const char* e) {
    (void)n; (void)e; return 0;
}
DOM_ENGINE_WEAK bool dom_engine_frame_cancel(void* n, uint64_t t) {
    (void)n; (void)t; return false;
}
DOM_ENGINE_WEAK bool dom_engine_frame_tick(DomDocument* d, double t) {
    (void)d; (void)t; return false;
}

DOM_ENGINE_WEAK int dom_engine_pointer_capture(DomElement*,int32_t,unsigned operation) { return operation==2?0:-1; }
