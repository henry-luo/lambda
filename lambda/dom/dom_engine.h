#pragma once

// Engine-provided operations the DOM core needs (ES40 provider seams).
//
// `lambda/dom/` is compiled into lambda-rt, whose declared dependencies do not
// include radiant; radiant depends on lambda-rt, not the reverse. So the core
// cannot call an engine function by its own name -- it declares one here, ships
// a weak default in dom_engine_seam.cpp, and the engine supplies the strong
// definition. A runtime linked without an engine still links, and answers
// absence rather than failing.
//
// This is the same shape as dom_engine_load_document_native and the ~66
// DOM_F_ENGINE catalog rows; these eighteen were reaching radiant directly.
// Only calls that traffic in *native* types belong here. An engine call that
// traffics in script values is the DOM calling the script runtime, which is a
// different API and does not exist yet.
//
// Residual: three signatures still name radiant types by pointer, forward
// declared below, and two call sites still need radiant's enum and pseudo-state
// constants. That is a compile-time dependency, not a link-time one, so the
// seam does its job; removing the includes is separate work.

#include "../lambda-data.hpp"
#include <stddef.h>

struct DomDocument;
struct DomElement;
struct DocState;
struct UiContext;
struct RadiantHistoryTraversal;
struct RadiantInputValidity;

#ifdef __cplusplus
extern "C" {
#endif

// author templates: whether an event name has live author-template participants,
// and the per-node participation callback
bool dom_engine_author_template_event_live(const char* event_name);
void dom_engine_dispatch_author_template_participant(void* dom_node, Item event,
                                                     const char* event_name);

// per-document engine state, wrapper cache, editing commands
DocState* dom_engine_document_ensure_state(DomDocument* document, const char* owner);
void dom_engine_reset_wrapper_cache(void);
bool dom_engine_exec_command(void* document, const char* command, const char* value);

// session history (browsing-context state, owned by the shell)
bool dom_engine_history_initialize(DomDocument* document);
int dom_engine_history_length(DomDocument* document);
const char* dom_engine_history_scroll_restoration(DomDocument* document);
bool dom_engine_history_go(DomDocument* document, int delta,
                           RadiantHistoryTraversal* traversal);
bool dom_engine_history_set_location(DomDocument* document, const char* url_text,
                                     RadiantHistoryTraversal* traversal);

// a form control's live value: interaction state, not DOM state
int dom_engine_input_value_kind(const char* type);
const char* dom_engine_input_live_value(DomElement* element);
bool dom_engine_input_set_live_value(DomElement* element, const char* value);
void dom_engine_input_reset_live_value(DomElement* element);
bool dom_engine_input_value_sanitize(const char* type, const char* value,
                                     char* output, size_t output_size);
void dom_engine_input_value_validate(const char* type, const char* value,
                                     const char* min_value, const char* max_value,
                                     const char* step_value,
                                     RadiantInputValidity* output);

// view tree: commit pending mutations, refresh a pseudo-class flag
void dom_engine_reconcile_dom_mutations(UiContext* uicon, DomDocument* doc);
void dom_engine_sync_pseudo_state(void* view, uint32_t pseudo_flag, bool set);

#ifdef __cplusplus
}
#endif
