// The weak half of the engine seams declared in dom_engine.h. Each answers the
// "no engine linked" case: absence, zero, or nothing done. RADIANT_INPUT_VALUE_
// TEXT is 0, so the input-kind default reads as a plain text control, which is
// the benign branch at every call site.

#include "dom_engine.h"

#define DOM_ENGINE_WEAK extern "C" __attribute__((weak))

DOM_ENGINE_WEAK bool dom_engine_author_template_event_live(const char* n) { (void)n; return false; }
DOM_ENGINE_WEAK void dom_engine_dispatch_author_template_participant(void* d, Item e, const char* n) {
    (void)d; (void)e; (void)n;
}
DOM_ENGINE_WEAK DocState* dom_engine_document_ensure_state(DomDocument* d, const char* o) {
    (void)d; (void)o; return nullptr;
}
DOM_ENGINE_WEAK void dom_engine_reset_wrapper_cache(void) {}
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
DOM_ENGINE_WEAK void dom_engine_sync_pseudo_state(void* v, uint32_t f, bool set) {
    (void)v; (void)f; (void)set;
}
