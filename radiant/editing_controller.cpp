#include "event.hpp"

#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"

#include <string.h>

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event);
void event_context_cleanup(EventContext* evcon);

static DomDocument* editing_controller_document(EventContext* evcon) {
    if (!evcon) return nullptr;
    return evcon->target_document
        ? evcon->target_document
        : (evcon->ui_context ? evcon->ui_context->document : nullptr);
}

static DocState* editing_controller_doc_state(EventContext* evcon) {
    DomDocument* doc = editing_controller_document(evcon);
    return doc ? (DocState*)doc->state : nullptr;
}

static void editing_controller_selection_snapshot(
        EventContext* evcon,
        DocState* state,
        const EditingControllerHooks* hooks,
        View* focus_view,
        const char* operation) {
    if (hooks && hooks->selection_snapshot) {
        hooks->selection_snapshot(evcon, state, focus_view, operation, nullptr,
                                  hooks->user);
    }
}

static bool editing_controller_form_selection_extend(
        EventContext* evcon,
        DomElement* elem,
        DocState* state,
        View* target,
        int anchor_offset,
        int focus_offset,
        const EditingControllerHooks* hooks,
        const char* operation) {
    if (!hooks || !hooks->form_selection_extend) return false;
    return hooks->form_selection_extend(evcon, elem, state, target,
                                        anchor_offset, focus_offset, operation,
                                        hooks->user);
}

static bool editing_controller_is_history_intent(InputIntentType input_type) {
    return input_type == INPUT_INTENT_HISTORY_UNDO ||
        input_type == INPUT_INTENT_HISTORY_REDO;
}

static void editing_controller_composition_anchor(
        DocState* state,
        const EditingSurface* surface,
        View** out_view,
        int* out_offset) {
    if (out_view) *out_view = surface ? surface->view : nullptr;
    if (out_offset) *out_offset = 0;
    if (!state || !surface) return;

    View* caret_view = nullptr;
    int caret_offset = 0;
    if (caret_get_position(state, &caret_view, &caret_offset) && caret_view) {
        if (out_view) *out_view = caret_view;
        if (out_offset) *out_offset = caret_offset;
        return;
    }

    if (editing_surface_is_text_control(surface) && surface->owner &&
        surface->owner->form) {
        if (out_view) *out_view = surface->view
            ? surface->view : static_cast<View*>(surface->owner);
        if (out_offset) {
            *out_offset = (int)surface->owner->form->selection_start; // INT_CAST_OK: StateStore projection exposes selection offsets as int.
        }
    }
}

static uint32_t editing_controller_text_len(const char* text) {
    return text ? (uint32_t)strlen(text) : 0; // INT_CAST_OK: event payload length is bounded by in-memory string size.
}

bool editing_controller_dispatch_history(EventContext* evcon,
                                         const EditingSurface* surface,
                                         InputIntentType input_type,
                                         const EditingControllerHooks* hooks) {
    if (!evcon || !surface || !editing_controller_is_history_intent(input_type)) {
        return false;
    }
    if (!editing_surface_is_text_control(surface) &&
        !editing_surface_is_rich(surface)) {
        return false;
    }
    if (!hooks || !hooks->history_dispatch) return false;
    DocState* state = editing_controller_doc_state(evcon);
    editing_interaction_set_active_surface(state, surface);
    return hooks->history_dispatch(evcon, surface, input_type, hooks->user);
}

bool editing_controller_handle_composition(EventContext* evcon,
                                           DocState* state,
                                           const CompositionEvent* comp_event,
                                           const EditingControllerHooks* hooks) {
    if (!evcon || !state || !comp_event || !hooks ||
        !hooks->composition_dispatch) {
        return false;
    }

    EditingIntent intent;
    if (!input_intent_from_composition_event(comp_event, &intent)) {
        return false;
    }

    EditingSurface surface;
    editing_surface_clear(&surface);
    if (intent.type != INPUT_INTENT_COMPOSITION_START &&
        state->editing.composition.active &&
        state->editing.composition.surface.kind != EDIT_SURFACE_NONE) {
        surface = state->editing.composition.surface;
    } else {
        View* focused = focus_get(state);
        View* target = focused ? focused : caret_get_view(state);
        if (!target) return false;
        if (!editing_surface_from_target(target, &surface)) return false;
    }
    if (!editing_surface_is_text_control(&surface) &&
        !editing_surface_is_rich(&surface)) {
        return false;
    }

    editing_interaction_set_active_surface(state, &surface);
    if (intent.type == INPUT_INTENT_COMPOSITION_START) {
        View* anchor_view = nullptr;
        int anchor_offset = 0;
        editing_controller_composition_anchor(state, &surface,
                                              &anchor_view, &anchor_offset);
        editing_interaction_begin_composition(state, &surface,
                                              anchor_view, anchor_offset);
    } else if (intent.type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
        editing_interaction_update_composition(state, &surface,
            editing_controller_text_len(intent.data),
            intent.composition_caret);
    }

    bool handled = hooks->composition_dispatch(evcon, &surface, comp_event,
                                               &intent, hooks->user);

    if (intent.type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
        intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT) {
        bool canceled = intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT;
        editing_interaction_end_composition(state, &surface,
            canceled ? 0 : editing_controller_text_len(intent.data),
            canceled);
    }
    return handled;
}

bool editing_controller_undo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks) {
    return editing_controller_dispatch_history(evcon, surface,
                                              INPUT_INTENT_HISTORY_UNDO,
                                              hooks);
}

bool editing_controller_redo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks) {
    return editing_controller_dispatch_history(evcon, surface,
                                              INPUT_INTENT_HISTORY_REDO,
                                              hooks);
}

bool editing_undo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks) {
    return editing_controller_undo(evcon, surface, hooks);
}

bool editing_redo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks) {
    return editing_controller_redo(evcon, surface, hooks);
}

static void editing_controller_log_autoscroll(
        DocState* state,
        const EditingSurface* surface,
        const char* operation,
        float dx,
        float dy,
        float velocity_x,
        float velocity_y,
        const EditingControllerHooks* hooks) {
    if (!hooks || !hooks->autoscroll_log) return;
    hooks->autoscroll_log(state, surface, operation, dx, dy,
                          velocity_x, velocity_y, hooks->user);
}

static void editing_controller_view_abs_xy(View* view,
                                           float* out_x,
                                           float* out_y) {
    float x = 0.0f;
    float y = 0.0f;
    for (View* cur = view; cur; cur = cur->parent) {
        if (cur->view_type == RDT_VIEW_BLOCK ||
            cur->view_type == RDT_VIEW_INLINE_BLOCK ||
            cur->view_type == RDT_VIEW_LIST_ITEM) {
            x += cur->x;
            y += cur->y;
        }
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static bool editing_controller_velocity_for_rect(float pointer_x,
                                                 float pointer_y,
                                                 float left,
                                                 float top,
                                                 float width,
                                                 float height,
                                                 bool allow_x,
                                                 bool allow_y,
                                                 float* out_vx,
                                                 float* out_vy) {
    const float edge = 24.0f;
    const float max_step = 36.0f;
    float vx = 0.0f;
    float vy = 0.0f;

    if (allow_x) {
        if (pointer_x < left + edge) {
            vx = -max_step * (left + edge - pointer_x) / edge;
        } else if (pointer_x > left + width - edge) {
            vx = max_step * (pointer_x - (left + width - edge)) / edge;
        }
    }
    if (allow_y) {
        if (pointer_y < top + edge) {
            vy = -max_step * (top + edge - pointer_y) / edge;
        } else if (pointer_y > top + height - edge) {
            vy = max_step * (pointer_y - (top + height - edge)) / edge;
        }
    }
    if (vx < -max_step) vx = -max_step;
    if (vx > max_step) vx = max_step;
    if (vy < -max_step) vy = -max_step;
    if (vy > max_step) vy = max_step;
    if (out_vx) *out_vx = vx;
    if (out_vy) *out_vy = vy;
    return vx != 0.0f || vy != 0.0f;
}

static float editing_controller_text_control_content_width(ViewBlock* block,
                                                           DomElement* elem) {
    if (!block || !elem || !elem->form) return 0.0f;
    bool is_textarea = elem->form->control_type == FORM_CONTROL_TEXTAREA;
    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (is_textarea ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float width = block->width - 2.0f * (border + padding);
    return width > 0.0f ? width : 0.0f;
}

static float editing_controller_text_control_content_height(ViewBlock* block,
                                                            DomElement* elem) {
    if (!block || !elem || !elem->form) return 0.0f;
    bool is_textarea = elem->form->control_type == FORM_CONTROL_TEXTAREA;
    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (is_textarea ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float height = block->height - 2.0f * (border + padding);
    return height > 0.0f ? height : 0.0f;
}

static float editing_controller_text_control_max_line_width(UiContext* uicon,
                                                            DomElement* elem) {
    if (!uicon || !elem || !elem->form) return 0.0f;
    ViewBlock* block = lam::view_require_block(elem);
    tc_ensure_init(elem);
    const char* value = elem->form->current_value ? elem->form->current_value : elem->form->value;
    uint32_t value_len = elem->form->current_value_len;
    if (!value) value = "";

    bool is_textarea = elem->form->control_type == FORM_CONTROL_TEXTAREA;
    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (is_textarea ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);

    float max_width = 0.0f;
    uint32_t line_end = 0;
    while (line_end <= value_len) {
        if (line_end == value_len || value[line_end] == '\n') {
            EditingCaretRect caret_rect;
            if (editing_geometry_text_control_caret_rect(uicon, elem,
                    line_end, &caret_rect)) {
                float line_width = caret_rect.x - block->x - border - padding;
                if (line_width > max_width) max_width = line_width;
            }
            if (line_end == value_len) break;
        }
        line_end++;
    }
    return max_width;
}

static float editing_controller_textarea_content_height(DomElement* elem) {
    if (!elem || !elem->form) return 0.0f;
    ViewBlock* block = lam::view_require_block(elem);
    tc_ensure_init(elem);
    const char* value = elem->form->current_value ? elem->form->current_value : elem->form->value;
    uint32_t value_len = elem->form->current_value_len;
    float font_size = block->font ? block->font->font_size : 13.333f;
    float line_height = font_size * 1.4f;
    uint32_t lines = 1;
    if (value) {
        for (uint32_t i = 0; i < value_len; i++) {
            if (value[i] == '\n') lines++;
        }
    }
    return (float)lines * line_height;
}

static void editing_controller_extend_selection_after_scroll(
        EventContext* evcon,
        DocState* state,
        View* surface_target,
        float pointer_x,
        float pointer_y,
        const EditingControllerHooks* hooks) {
    if (!evcon || !state || !surface_target) return;

    View* anchor_view = nullptr;
    int anchor_offset = 0;
    if (!selection_get_pointer_anchor(state, &anchor_view, &anchor_offset)) return;

    EditingSurface surface;
    if (!editing_surface_from_target(surface_target, &surface)) return;

    if (editing_surface_is_text_control(&surface) && surface.owner) {
        uint32_t hit_offset = 0;
        if (editing_geometry_text_control_offset_for_point(evcon->ui_context,
                surface.owner, pointer_x, pointer_y, &hit_offset)) {
            int focus_offset = (int)hit_offset; // INT_CAST_OK: StateStore selection API uses int offsets.
            editing_controller_form_selection_extend(evcon, surface.owner, state,
                static_cast<View*>(surface.owner), anchor_offset, focus_offset,
                hooks, "autoscrollExtend");
        }
        return;
    }

    DomDocument* doc = editing_controller_document(evcon);
    if (!editing_surface_is_rich(&surface) || !evcon->ui_context ||
        !doc || !doc->view_tree) {
        return;
    }

    EditingBoundary hit_boundary;
    if (!editing_geometry_hit_test_boundary(evcon->ui_context,
            doc->view_tree->root, &surface,
            pointer_x, pointer_y, EDITING_CLAMP_SKIP_TEXT_CONTROLS,
            &hit_boundary) ||
        !hit_boundary.dom.node ||
        hit_boundary.dom.node->node_type != DOM_NODE_TEXT) {
        return;
    }

    View* focus_view = static_cast<View*>(hit_boundary.dom.node);
    int focus_offset = (int)hit_boundary.offset; // INT_CAST_OK: editor selection offsets are byte-index ints.
    state_store_selection_extend_to_view(state, focus_view, focus_offset);
    update_caret_visual_position(evcon->ui_context, state);
    editing_controller_selection_snapshot(evcon, state, hooks, focus_view,
                                          "autoscrollExtend");
}

static bool editing_controller_text_control_drag_autoscroll(
        EventContext* evcon,
        DocState* state,
        DomElement* elem,
        float pointer_x,
        float pointer_y,
        const EditingControllerHooks* hooks) {
    if (!evcon || !evcon->ui_context || !state || !elem ||
        !tc_is_text_control(elem) || !elem->form) {
        return false;
    }

    ViewBlock* block = lam::view_require_block(elem);
    bool is_textarea = elem->form->control_type == FORM_CONTROL_TEXTAREA;
    float abs_x = 0.0f;
    float abs_y = 0.0f;
    editing_controller_view_abs_xy(static_cast<View*>(block), &abs_x, &abs_y);

    bool has_css_border = block->bound && block->bound->border;
    float border = has_css_border ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (is_textarea ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float content_x = abs_x + border + padding;
    float content_y = abs_y + border + padding;
    float content_w = editing_controller_text_control_content_width(block, elem);
    float content_h = editing_controller_text_control_content_height(block, elem);

    float vx = 0.0f;
    float vy = 0.0f;
    if (!editing_controller_velocity_for_rect(pointer_x, pointer_y,
            content_x, content_y, content_w, content_h, true, is_textarea,
            &vx, &vy)) {
        editing_controller_drag_autoscroll_stop(state, hooks);
        return false;
    }

    float max_x = editing_controller_text_control_max_line_width(
        evcon->ui_context, elem) - content_w;
    if (max_x < 0.0f) max_x = 0.0f;
    float max_y = is_textarea
        ? editing_controller_textarea_content_height(elem) - content_h : 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;

    float old_x = elem->form->scroll_x;
    float old_y = elem->form->scroll_y;
    float next_x = old_x + vx;
    float next_y = old_y + vy;
    if (next_x < 0.0f) next_x = 0.0f;
    if (next_y < 0.0f) next_y = 0.0f;
    if (next_x > max_x) next_x = max_x;
    if (next_y > max_y) next_y = max_y;
    float dx = next_x - old_x;
    float dy = next_y - old_y;
    if (dx == 0.0f && dy == 0.0f) return false;

    EditingSurface surface;
    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(static_cast<View*>(elem), &surface)) {
        surface_ptr = &surface;
    }
    if (!state->editing_autoscroll_active) {
        editing_controller_log_autoscroll(state, surface_ptr, "start", dx, dy,
                                          vx, vy, hooks);
        editing_interaction_set_autoscroll(state, true, static_cast<View*>(elem),
                                           pointer_x, pointer_y);
    } else {
        editing_interaction_set_autoscroll(state, true,
                                           state->editing_autoscroll_surface,
                                           pointer_x, pointer_y);
        editing_controller_log_autoscroll(state, surface_ptr, "tick", dx, dy,
                                          vx, vy, hooks);
    }

    elem->form->scroll_x = next_x;
    elem->form->scroll_y = next_y;
    editing_controller_extend_selection_after_scroll(evcon, state,
        static_cast<View*>(elem), pointer_x, pointer_y, hooks);
    evcon->need_repaint = true;
    doc_state_request_repaint(state);
    return true;
}

static ViewBlock* editing_controller_root_block(EventContext* evcon) {
    DomDocument* doc = editing_controller_document(evcon);
    if (!evcon || !evcon->ui_context || !doc ||
        !doc->view_tree ||
        !doc->view_tree->root ||
        doc->view_tree->root->view_type != RDT_VIEW_BLOCK) {
        return nullptr;
    }
    return lam::view_require_block(doc->view_tree->root);
}

static ViewBlock* editing_controller_nearest_scroll_block(DocState* state,
                                                          View* surface_target,
                                                          ViewBlock* root_block) {
    for (View* cur = surface_target; cur; cur = cur->parent) {
        if (cur->view_type != RDT_VIEW_BLOCK &&
            cur->view_type != RDT_VIEW_INLINE_BLOCK &&
            cur->view_type != RDT_VIEW_LIST_ITEM) {
            continue;
        }
        ViewBlock* block = lam::view_require_block(cur);
        if (!block || block == root_block || !block->scroller || !block->scroller->pane) {
            continue;
        }
        float h_max = 0.0f;
        float v_max = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block),
            block->scroller->pane, nullptr, nullptr, &h_max, &v_max);
        if (h_max > 0.0f || v_max > 0.0f) return block;
    }
    return root_block;
}

static bool editing_controller_password_reveal_tick(DocState* state,
                                                    double delta) {
    if (!state || delta <= 0.0) return false;
    View* focused = focus_get(state);
    if (!focused || !focused->is_element()) return false;
    DomElement* elem = lam::dom_require_element(focused);
    if (!tc_is_text_control(elem) || !elem->form ||
        !elem->form->input_type ||
        strcmp(elem->form->input_type, "password") != 0 ||
        !elem->form->password_reveal_active) {
        return false;
    }

    elem->form->password_reveal_elapsed += delta;
    if (elem->form->password_reveal_elapsed < 1.0) return false;

    te_password_reveal_clear(elem);
    doc_state_request_repaint(state);
    return true;
}

static bool editing_controller_password_reveal_active_for_focus(DocState* state) {
    if (!state) return false;
    View* focused = focus_get(state);
    if (!focused || !focused->is_element()) return false;
    DomElement* elem = lam::dom_require_element(focused);
    return tc_is_text_control(elem) && elem->form &&
        elem->form->input_type &&
        strcmp(elem->form->input_type, "password") == 0 &&
        elem->form->password_reveal_active != 0;
}

bool editing_controller_drag_autoscroll(EventContext* evcon,
                                        DocState* state,
                                        View* surface_target,
                                        float pointer_x,
                                        float pointer_y,
                                        const EditingControllerHooks* hooks) {
    DomDocument* doc = editing_controller_document(evcon);
    if (!evcon || !evcon->ui_context || !doc ||
        !state || !surface_target) {
        return false;
    }

    EditingSurface surface;
    if (editing_surface_from_target(surface_target, &surface) &&
        editing_surface_is_text_control(&surface) && surface.owner) {
        if (state->editing_autoscroll_active) {
            editing_interaction_set_autoscroll(state, true,
                                               state->editing_autoscroll_surface,
                                               pointer_x, pointer_y);
        }
        return editing_controller_text_control_drag_autoscroll(evcon, state,
            surface.owner, pointer_x, pointer_y, hooks);
    }

    ViewBlock* root_block = editing_controller_root_block(evcon);
    if (!root_block) return false;

    ViewBlock* scroll_block = editing_controller_nearest_scroll_block(
        state, surface_target, root_block);
    if (!scroll_block || !scroll_block->scroller || !scroll_block->scroller->pane) {
        return false;
    }

    bool is_viewport = scroll_block == root_block;
    float rect_x = 0.0f;
    float rect_y = 0.0f;
    float rect_w = scroll_block->width;
    float rect_h = scroll_block->height;
    if (is_viewport) {
        rect_w = evcon->ui_context->viewport_width > 0
            ? (float)evcon->ui_context->viewport_width : root_block->width;
        rect_h = evcon->ui_context->viewport_height > 0
            ? (float)evcon->ui_context->viewport_height : root_block->height;
    } else {
        editing_controller_view_abs_xy(static_cast<View*>(scroll_block),
            &rect_x, &rect_y);
    }
    if (rect_w <= 0.0f || rect_h <= 0.0f) return false;

    if (state->editing_autoscroll_active) {
        editing_interaction_set_autoscroll(state, true,
                                           state->editing_autoscroll_surface,
                                           pointer_x, pointer_y);
    }

    float vx = 0.0f;
    float vy = 0.0f;
    if (!editing_controller_velocity_for_rect(pointer_x, pointer_y,
            rect_x, rect_y, rect_w, rect_h, true, true, &vx, &vy)) {
        editing_controller_drag_autoscroll_stop(state, hooks);
        return false;
    }

    float h = 0.0f, v = 0.0f, h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, static_cast<View*>(scroll_block),
                                       scroll_block->scroller->pane,
                                       &h, &v, &h_max, &v_max);
    float next_h = h + vx;
    float next_v = v + vy;
    if (next_h < 0.0f) next_h = 0.0f;
    if (next_v < 0.0f) next_v = 0.0f;
    if (next_h > h_max) next_h = h_max;
    if (next_v > v_max) next_v = v_max;
    float dx = next_h - h;
    float dy = next_v - v;
    if (dx == 0.0f && dy == 0.0f) return false;

    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(surface_target, &surface)) {
        surface_ptr = &surface;
    }
    if (!state->editing_autoscroll_active) {
        editing_controller_log_autoscroll(state, surface_ptr, "start",
                                          dx, dy, vx, vy, hooks);
        editing_interaction_set_autoscroll(state, true, surface_target,
                                           pointer_x, pointer_y);
    } else {
        editing_interaction_set_autoscroll(state, true,
                                           state->editing_autoscroll_surface,
                                           pointer_x, pointer_y);
        editing_controller_log_autoscroll(state, surface_ptr, "tick",
                                          dx, dy, vx, vy, hooks);
    }

    scroll_state_set_position_for_view(state, static_cast<View*>(scroll_block),
                                       scroll_block->scroller->pane,
                                       next_h, next_v, is_viewport);
    if (is_viewport) doc_state_sync_viewport_scroll(state, doc, next_h, next_v);
    editing_controller_extend_selection_after_scroll(evcon, state, surface_target,
                                                     pointer_x, pointer_y, hooks);
    evcon->need_repaint = true;
    return true;
}

void editing_controller_drag_autoscroll_stop(DocState* state,
                                             const EditingControllerHooks* hooks) {
    if (!state || !state->editing_autoscroll_active) return;

    EditingSurface surface;
    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(state->editing_autoscroll_surface, &surface)) {
        surface_ptr = &surface;
    }
    editing_controller_log_autoscroll(state, surface_ptr, "stop",
                                      0.0f, 0.0f, 0.0f, 0.0f, hooks);
    editing_interaction_clear_autoscroll(state);
}

bool editing_controller_animation_active(DocState* state) {
    if (!state) return false;
    return caret_has_projection(state) ||
        state->editing_autoscroll_active ||
        editing_controller_password_reveal_active_for_focus(state);
}

bool editing_controller_animation_tick(UiContext* uicon,
                                       double timestamp,
                                       const EditingControllerHooks* hooks) {
    if (!uicon || !uicon->document || !uicon->document->state) return false;

    DocState* state = (DocState*)uicon->document->state;
    double delta = 0.0;
    if (state->editing_tick_last_time > 0.0 && timestamp >= state->editing_tick_last_time) {
        delta = timestamp - state->editing_tick_last_time;
        if (delta > 1.0) delta = 1.0;
    }
    editing_interaction_set_clock(state, timestamp,
                                  state->editing_caret_blink_elapsed);

    bool changed = false;
    if (editing_controller_password_reveal_tick(state, delta)) changed = true;
    const double caret_blink_interval = 0.5;
    if (caret_has_projection(state)) {
        state->editing_caret_blink_elapsed += delta;
        if (state->editing_caret_blink_elapsed >= caret_blink_interval) {
            state->editing_caret_blink_elapsed = 0.0;
            caret_toggle_blink(state);
            changed = true;
        }
    } else {
        state->editing_caret_blink_elapsed = 0.0;
    }
    editing_interaction_set_clock(state, state->editing_tick_last_time,
                                  state->editing_caret_blink_elapsed);

    if (!state->editing_autoscroll_active ||
        !state->editing_autoscroll_surface) {
        return changed;
    }

    RdtEvent event;
    memset(&event, 0, sizeof(event));
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = timestamp;
    event.mouse_position.x = (int)state->editing_autoscroll_pointer_x; // INT_CAST_OK: synthetic timer event stores viewport pixel coordinate
    event.mouse_position.y = (int)state->editing_autoscroll_pointer_y; // INT_CAST_OK: synthetic timer event stores viewport pixel coordinate

    EventContext evcon;
    event_context_init(&evcon, uicon, &event);

    EventStateLog* cascade_log = state->active_event_log
        ? state->active_event_log : uicon->event_log;
    uint64_t cascade_id = state_begin_event_cascade(state, cascade_log, "timer");

    bool scrolled = editing_controller_drag_autoscroll(
        &evcon, state, state->editing_autoscroll_surface,
        state->editing_autoscroll_pointer_x,
        state->editing_autoscroll_pointer_y, hooks);

    if (evcon.need_repaint) {
        doc_state_mark_dirty(state);
    }
    state_end_event_cascade(state, cascade_log, cascade_id);
    event_context_cleanup(&evcon);
    return changed || scrolled || evcon.need_repaint;
}

static void editing_controller_extend_to_moved_caret(
        EventContext* evcon,
        DocState* state,
        View* fallback_view,
        int* caret_offset,
        const EditingControllerHooks* hooks,
        const char* operation) {
    View* new_caret_view = fallback_view;
    caret_get_render_snapshot(state, &new_caret_view, caret_offset,
                              nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr);
    state_store_selection_extend_to_view(state, new_caret_view, *caret_offset);
    editing_controller_selection_snapshot(evcon, state, hooks, new_caret_view,
                                          operation);
}

bool editing_controller_handle_rich_navigation(EventContext* evcon,
                                               DocState* state,
                                               const KeyEvent* key_event,
                                               const EditingControllerHooks* hooks) {
    if (!evcon || !state || !key_event) return false;

    View* caret_view = nullptr;
    int caret_offset = 0;
    if (!caret_get_position(state, &caret_view, &caret_offset)) return false;
    if (!caret_view) return false;

    EditingSurface surface;
    if (editing_surface_from_target(caret_view, &surface) &&
        editing_surface_is_text_control(&surface)) {
        return false;
    }

    bool shift = (key_event->mods & RDT_MOD_SHIFT) != 0;
    bool ctrl = (key_event->mods & RDT_MOD_CTRL) != 0;
    bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;

    unsigned char* text_data = nullptr;
    if (caret_view->is_text()) {
        text_data = (static_cast<ViewText*>(caret_view))->text_data();
    }

    switch (key_event->key) {
        case RDT_KEY_LEFT:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                int new_offset = text_data
                    ? utf8_offset_by_chars(text_data, caret_offset, -1)
                    : caret_offset - 1;
                state_store_selection_extend_to_offset(state, new_offset);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      "extendBackward");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move(state, ctrl ? -10 : -1);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      ctrl ? "moveWordBackward"
                                                           : "moveBackward");
            }
            break;

        case RDT_KEY_RIGHT:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                int new_offset = text_data
                    ? utf8_offset_by_chars(text_data, caret_offset, 1)
                    : caret_offset + 1;
                state_store_selection_extend_to_offset(state, new_offset);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      "extendForward");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move(state, ctrl ? 10 : 1);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      ctrl ? "moveWordForward"
                                                           : "moveForward");
            }
            break;

        case RDT_KEY_UP:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                state_store_caret_move_line(state, -1, evcon->ui_context);
                editing_controller_extend_to_moved_caret(evcon, state, caret_view,
                                                         &caret_offset, hooks,
                                                         "extendLineBackward");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move_line(state, -1, evcon->ui_context);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      "moveLineBackward");
            }
            break;

        case RDT_KEY_DOWN:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                state_store_caret_move_line(state, 1, evcon->ui_context);
                editing_controller_extend_to_moved_caret(evcon, state, caret_view,
                                                         &caret_offset, hooks,
                                                         "extendLineForward");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move_line(state, 1, evcon->ui_context);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      "moveLineForward");
            }
            break;

        case RDT_KEY_HOME:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                state_store_caret_move_to_boundary(state, cmd ? 2 : 0);
                editing_controller_extend_to_moved_caret(evcon, state, caret_view,
                                                         &caret_offset, hooks,
                                                         cmd ? "extendDocumentStart"
                                                             : "extendLineStart");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move_to_boundary(state, cmd ? 2 : 0);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      cmd ? "moveDocumentStart"
                                                          : "moveLineStart");
            }
            break;

        case RDT_KEY_END:
            if (shift) {
                selection_begin_non_pointer_extend(state, caret_view, caret_offset);
                state_store_caret_move_to_boundary(state, cmd ? 3 : 1);
                editing_controller_extend_to_moved_caret(evcon, state, caret_view,
                                                         &caret_offset, hooks,
                                                         cmd ? "extendDocumentEnd"
                                                             : "extendLineEnd");
                selection_finish_active_gesture(state);
            } else {
                state_store_selection_clear(state);
                state_store_caret_move_to_boundary(state, cmd ? 3 : 1);
                editing_controller_selection_snapshot(evcon, state, hooks,
                                                      caret_view,
                                                      cmd ? "moveDocumentEnd"
                                                          : "moveLineEnd");
            }
            break;

        default:
            return false;
    }

    update_caret_visual_position(evcon->ui_context, state);
    evcon->need_repaint = true;
    return true;
}
