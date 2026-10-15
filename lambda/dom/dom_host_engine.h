#pragma once

#include "dom_provider.hpp"
#include "dom_svg_context.h"
#include "../lambda-data.hpp"
#include "../input/css/css_style.hpp"
#include <math.h>

struct DomElement;
struct DomDocument;
struct DomNode;
struct UiContext;
struct DomText;
struct DocState;
struct DomSelection;

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
    OP(bool, document_tick_animation, (void* document, double now, void* dirty_tracker), (document, now, dirty_tracker), false) \
    OP(void, svg_animation_pause, (DomElement* element, bool paused), (element, paused), (void)0) \
    OP(const char*, svg_animation_value, (DomElement* element, const char* name, bool css_only), (element, name, css_only), nullptr) \
    OP(bool, svg_animation_paused, (DomElement* element), (element), false) \
    OP(void, svg_animation_set_time, (DomElement* element, double seconds), (element, seconds), (void)0) \
    OP(const char*, svg_animation_attribute, (DomElement* element, const char* name), (element, name), nullptr) \
    OP(bool, svg_animation_begin_end, (DomElement* element, bool end, double offset), (element, end, offset), false) \
    OP(void, svg_animation_use_event, (DomElement* host, DomElement* source, const char* type, bool bubbles, double detail), (host, source, type, bubbles, detail), (void)0) \
    OP(bool, svg_animation_start_time, (DomElement* element, double* seconds), (element, seconds), false) \
    OP(DomElement*, svg_animation_use_source, (DomElement* element), (element), nullptr) \
    OP(bool, svg_animation_is_sampling, (DomDocument* document), (document), false) \
    OP(double, svg_animation_current_time, (DomElement* element), (element), 0.0) \
    OP(DomElement*, svg_animation_target_element, (DomElement* element), (element), nullptr) \
    OP(double, svg_animation_simple_duration, (DomElement* element), (element), NAN) \
    OP(bool, dispatch_touch, (UiContext* context, DomNode* target, const char* type, double x, double y, int mods, bool active, double timestamp), (context, target, type, x, y, mods, active, timestamp), false) \
    OP(void, svg_source_enter, (DomDocument* document, DomElement* instance, DomElement* host, DomSvgSourceContext* previous), (document, instance, host, previous), (void)0) \
    OP(void, svg_source_leave, (const DomSvgSourceContext* previous), (previous), (void)0) \
    OP(void, node_document_origin, (DomNode* node, float* x, float* y), (node, x, y), (void)0) \
    OP(void, child_content_origin, (DomNode* node, float parent_x, float parent_y, float* x, float* y), (node, parent_x, parent_y, x, y), (void)0) \
    OP(bool, text_contains_point, (DomNode* node, float x, float y, float parent_x, float parent_y), (node, x, y, parent_x, parent_y), false) \
    OP(bool, foreign_object_matrix, (DomNode* node, void* matrix, bool include_self), (node, matrix, include_self), false) \
    OP(void, visual_bounds, (DomNode* node, float* x, float* y, float* width, float* height), (node, x, y, width, height), (void)0) \
    OP(DomNode*, svg_style_parent, (DomNode* node, const void* scope), (node, scope), nullptr) \
    OP(const char*, svg_property, (DomElement* element, const char* name, bool inherits, char* buffer, size_t capacity, bool* from_css, char** owned, DomElement** declaring, const void* scope), (element, name, inherits, buffer, capacity, from_css, owned, declaring, scope), nullptr) \
    OP(bool, svg_append_shape, (DomElement* element, RdtPath* path, const void* lengths), (element, path, lengths), false) \
    OP(RdtPath*, svg_text_path, (DomElement* element, const void* lengths, void* fonts), (element, lengths, fonts), nullptr) \
    OP(bool, svg_clip_contains, (DomElement* element, const void* lengths, void* fonts, const RdtMatrix* frame, SvgPathContainsFn contains, const RdtLogicalPoint* point, const void* scope), (element, lengths, fonts, frame, contains, point, scope), false) \
    OP(void*, web_animation_create, (DomElement* element, void* keyframes, double duration, const void* timing, Pool* pool), (element, keyframes, duration, timing, pool), nullptr) \
    OP(void, range_adopt_subtree, (DocState* source, DocState* destination, DomNode* subtree), (source, destination, subtree), (void)0) \
    OP(void, range_refresh_lifecycle_pins, (DomDocument* document), (document), (void)0) \
    OP(bool, selection_collapse, (DomSelection* selection, DomNode* node, uint32_t offset, const char** exception), (selection, node, offset, exception), false) \
    OP(DomText*, text_split_at, (DocState* state, DomText* text, uint32_t offset), (state, text, offset), nullptr)

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

class DomSvgSourceScope {
    DomSvgSourceContext previous = {};
    const DomHostEngineOps* operations;
public:
    explicit DomSvgSourceScope(DomDocument* document, DomElement* instance = nullptr,
            DomElement* host = nullptr) : operations(dom_host_engine_ops()) {
        if (operations) operations->svg_source_enter(document, instance, host, &previous);
    }
    ~DomSvgSourceScope() {
        // leave through the same provider that entered, including absence before registration.
        if (operations) operations->svg_source_leave(&previous);
    }
    DomSvgSourceScope(const DomSvgSourceScope&) = delete;
    DomSvgSourceScope& operator=(const DomSvgSourceScope&) = delete;
};
