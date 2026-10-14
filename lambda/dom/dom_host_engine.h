#pragma once

#include "dom_provider.hpp"
#include "../lambda-data.hpp"
#include "../input/css/css_style.hpp"

// native host calls borrow Items/documents/text synchronously; publication has process lifetime
// (D4.5.1v4 / D4.1.4v5, D7.1.3 / D7.5.3).
#define DOM_HOST_ENGINE_OPERATIONS(OP) \
    OP(Item, request_frame, (Item owner, Item event_name), (owner, event_name), ItemNull) \
    OP(Item, cancel_frame, (Item owner, Item token), (owner, token), ItemNull) \
    OP(Item, viewport_size, (Item owner), (owner), ItemNull) \
    OP(Item, watch_window, (Item owner, Item event_name, Item enabled), (owner, event_name, enabled), ItemNull) \
    OP(Item, history_traverse, (Item owner, Item delta), (owner, delta), ItemNull) \
    OP(Item, media_info, (Item owner), (owner), ItemNull) \
    OP(Item, media_command, (Item owner, Item command, Item value), (owner, command, value), ItemNull) \
    OP(Item, clear_editing_focus, (Item owner), (owner), ItemNull) \
    OP(Item, edit_target, (Item owner, Item token), (owner, token), ItemNull) \
    OP(Item, query_command, (void* document, const char* kind, const char* command), (document, kind, command), ItemNull) \
    OP(bool, set_design_mode, (void* document, const char* value), (document, value), false) \
    OP(bool, state_bool, (void* state, void* node, const char* name), (state, node, name), false) \
    OP(void, document_request_repaint, (void* document), (document), (void)0) \
    OP(void, document_clear_reflow, (void* document), (document), (void)0) \
    OP(bool, layout_document, (void* ui_context, void* document, bool is_reflow), (ui_context, document, is_reflow), false) \
    OP(float, element_effective_zoom, (void* element), (element), 1.0f) \
    OP(bool, element_has_vertical_rl_flow, (void* element), (element), false) \
    OP(CssEnum, element_specified_keyword, (void* element, CssPropertyCode property, CssEnum fallback), (element, property, fallback), fallback) \
    OP(void, prepare_children_mutation, (void* element), (element), (void)0) \
    OP(bool, control_is_disabled, (void* state, void* element), (state, element), false) \
    OP(void, control_focus_programmatic, (void* state, void* element, bool from_keyboard), (state, element, from_keyboard), (void)0) \
    OP(bool, control_store_text, (void* state, void* element, const char* value, uint32_t bytes, uint32_t utf16_units), (state, element, value, bytes, utf16_units), false) \
    OP(void, control_invalidate_behavior, (void* state, void* element), (state, element), (void)0) \
    OP(void, control_open_dropdown, (void* state, void* element), (state, element), (void)0) \
    OP(bool, scroll_begin_smooth, (void* state, void* element, void* pane, float x, float y), (state, element, pane, x, y), false) \
    OP(void, scroll_cancel_smooth, (void* state, void* element), (state, element), (void)0) \
    OP(bool, document_tick_smooth_scroll, (void* document, double now), (document, now), false) \
    OP(bool, document_tick_animation, (void* document, double now, void* dirty_tracker), (document, now, dirty_tracker), false)

struct DomHostEngineOps {
#define DOM_HOST_FIELD(result, name, parameters, arguments, absence) result (*name) parameters;
    DOM_HOST_ENGINE_OPERATIONS(DOM_HOST_FIELD)
#undef DOM_HOST_FIELD
};

extern "C" bool dom_host_register_engine(const DomHostEngineOps* operations);
extern "C" const DomHostEngineOps* dom_host_engine_ops(void);

#define DOM_HOST_DECLARE(result, name, parameters, arguments, absence) extern "C" result dom_engine_##name parameters;
DOM_HOST_ENGINE_OPERATIONS(DOM_HOST_DECLARE)
#undef DOM_HOST_DECLARE
