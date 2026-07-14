#include "event.hpp"
#include "render.hpp"
#include "layout.hpp"
#include "../lib/log.h"

struct ScrollConfig {
    float SCROLLBAR_SIZE;
    float MIN_HANDLE_SIZE;
    float HANDLE_RADIUS;
    float SCROLL_BORDER_MAIN;
    float SCROLL_BORDER_CROSS;
    float BAR_COLOR = 0xF6;
    float HANDLE_COLOR = 0xC0;
};
ScrollConfig sc;

void scroll_config_init(int pixel_ratio) {
    sc.SCROLLBAR_SIZE = 12 * pixel_ratio;
    sc.MIN_HANDLE_SIZE = 16 * pixel_ratio;
    sc.HANDLE_RADIUS = 4 * pixel_ratio;
    sc.SCROLL_BORDER_MAIN = 1 * pixel_ratio;
    sc.SCROLL_BORDER_CROSS = 2 * pixel_ratio;
}


void ScrollPane::reset() {
    DocState* state = state_ref;
    memset(this, 0, sizeof(ScrollPane));
    state_ref = state;
}

void scrollpane_render(RenderContext* rdcon, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    DocState* state, View* view,
    bool show_hz_scroll, bool show_vt_scroll) {
    log_info("SCROLLPANE: content size: %.1f x %.1f, view bounds: %.1f x %.1f",
        content_width, content_height, block_bound->width, block_bound->height);
    log_debug("render scroller content size: %f x %f, blk bounds: %f x %f",
        content_width, content_height, block_bound->width, block_bound->height);

    float view_x = block_bound->x, view_y = block_bound->y;
    float view_width = block_bound->width, view_height = block_bound->height;
    float h_scroll = 0.0f, v_scroll = 0.0f, h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, view, sp, &h_scroll, &v_scroll, &h_max, &v_max);

    // clip to visible bounds
    RdtPath* clip_path = rdt_path_new();
    rdt_path_add_rect(clip_path, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_path, nullptr);
    rdt_path_free(clip_path);

    // vertical scrollbar — only render if vertical overflow exists
    if (show_vt_scroll) {
        Color bar_color = {0}; bar_color.r = (uint8_t)sc.BAR_COLOR; bar_color.g = (uint8_t)sc.BAR_COLOR; bar_color.b = (uint8_t)sc.BAR_COLOR; bar_color.a = 255;
        rc_fill_rect(rdcon, view_x + view_width - sc.SCROLLBAR_SIZE,
            view_y, sc.SCROLLBAR_SIZE, view_height, bar_color);
        log_debug("v_scrollbar rect: x %f, y %f, wd %f, hg %f",
            view_x + view_width - sc.SCROLLBAR_SIZE, view_y, sc.SCROLLBAR_SIZE, view_height);

        if (content_height > 0) {
            Color handle_color = {0}; handle_color.r = (uint8_t)sc.HANDLE_COLOR; handle_color.g = (uint8_t)sc.HANDLE_COLOR; handle_color.b = (uint8_t)sc.HANDLE_COLOR; handle_color.a = 255;
            float bar_height = view_height - sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_MAIN * 2;
            log_debug("bar height: %f", bar_height);
            float v_ratio = min(view_height * 100 / content_height, 100.0f);
            float v_handle_height_phys = (v_ratio * bar_height) / 100;
            v_handle_height_phys = max(sc.MIN_HANDLE_SIZE, v_handle_height_phys);
            float scroll_ratio = (v_max > 0) ? (v_scroll / v_max) : 0;
            float v_handle_y_phys = sc.SCROLL_BORDER_MAIN + scroll_ratio * (bar_height - v_handle_height_phys);
            sp->v_handle_height = v_handle_height_phys / scale;
            sp->v_handle_y = v_handle_y_phys / scale;
            float v_scroll_x = view_x + view_width - sc.SCROLLBAR_SIZE + sc.SCROLL_BORDER_CROSS;
            rc_fill_rounded_rect(rdcon, v_scroll_x, view_y + v_handle_y_phys,
                sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, v_handle_height_phys, sc.HANDLE_RADIUS, sc.HANDLE_RADIUS, handle_color);
            log_debug("v_scroll_handle rect: x %f, y %f, wd %f, hg %f",
                v_scroll_x, view_y + v_handle_y_phys, sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, v_handle_height_phys);
        }
    }

    // horizontal scrollbar — only render if horizontal overflow exists
    if (show_hz_scroll) {
        Color bar_color = {0}; bar_color.r = (uint8_t)sc.BAR_COLOR; bar_color.g = (uint8_t)sc.BAR_COLOR; bar_color.b = (uint8_t)sc.BAR_COLOR; bar_color.a = 255;
        rc_fill_rect(rdcon, view_x,
            view_y + view_height - sc.SCROLLBAR_SIZE, view_width, sc.SCROLLBAR_SIZE, bar_color);
        log_debug("h_scrollbar rect: %f, %f, %f, %f",
            view_x, view_y + view_height - sc.SCROLLBAR_SIZE, view_width, sc.SCROLLBAR_SIZE);

        if (content_width > 0) {
            Color handle_color = {0}; handle_color.r = (uint8_t)sc.HANDLE_COLOR; handle_color.g = (uint8_t)sc.HANDLE_COLOR; handle_color.b = (uint8_t)sc.HANDLE_COLOR; handle_color.a = 255;
            log_debug("h_max_scroll: %f (content_width=%.1f, view_width=%.1f)", h_max, content_width, view_width);
            float bar_width = view_width - sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_MAIN * 2;
            log_debug("bar width: %f", bar_width);
            float h_ratio = min(view_width * 100 / content_width, 100.0f);
            float h_handle_width_phys = (h_ratio * bar_width) / 100;
            h_handle_width_phys = max(sc.MIN_HANDLE_SIZE, h_handle_width_phys);
            float scroll_ratio = (h_max > 0) ? (h_scroll / h_max) : 0;
            float h_handle_x_phys = sc.SCROLL_BORDER_MAIN + scroll_ratio * (bar_width - h_handle_width_phys);
            sp->h_handle_width = h_handle_width_phys / scale;
            sp->h_handle_x = h_handle_x_phys / scale;
            float h_scroll_y = view_y + view_height - sc.SCROLLBAR_SIZE + sc.SCROLL_BORDER_CROSS;
            rc_fill_rounded_rect(rdcon, view_x + h_handle_x_phys, h_scroll_y,
                h_handle_width_phys, sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, sc.HANDLE_RADIUS, sc.HANDLE_RADIUS, handle_color);
        }
    }

    rc_pop_clip(rdcon);
    log_debug("finished rendering scroller");
}

void setup_scroller(RenderContext* rdcon, ViewBlock* block) {
    float s = rdcon->scale;
    if (block->scroller->has_clip) {
        // Inset clip by border widths for padding-box clipping (CSS spec: overflow clips to padding edge)
        float bl = 0, bt = 0, br = 0, bb = 0;
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            bl = border->width.left;
            bt = border->width.top;
            br = border->width.right;
            bb = border->width.bottom;
        }
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            block->scroller->clip.left, block->scroller->clip.top, block->scroller->clip.right, block->scroller->clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + (block->scroller->clip.left + bl) * s);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + (block->scroller->clip.top + bt) * s);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + (block->scroller->clip.right - br) * s);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + (block->scroller->clip.bottom - bb) * s);

        // Copy border-radius for rounded corner clipping when overflow:hidden (scale radius)
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            // resolve percentage border-radius if not yet resolved
            resolve_border_radius_percentages(&border->radius, block->width, block->height);
            if (corner_has_radius(&border->radius)) {
                rdcon->block.has_clip_radius = true;
                // Use inner radius (outer minus border width) for padding-box clipping
                rdcon->block.clip_radius.top_left = fmaxf(0, border->radius.top_left - bl) * s;
                rdcon->block.clip_radius.top_right = fmaxf(0, border->radius.top_right - br) * s;
                rdcon->block.clip_radius.bottom_left = fmaxf(0, border->radius.bottom_left - bl) * s;
                rdcon->block.clip_radius.bottom_right = fmaxf(0, border->radius.bottom_right - br) * s;
                rdcon->block.clip_radius.top_left_y = fmaxf(0, border->radius.top_left_y - bt) * s;
                rdcon->block.clip_radius.top_right_y = fmaxf(0, border->radius.top_right_y - bt) * s;
                rdcon->block.clip_radius.bottom_left_y = fmaxf(0, border->radius.bottom_left_y - bb) * s;
                rdcon->block.clip_radius.bottom_right_y = fmaxf(0, border->radius.bottom_right_y - bb) * s;
                constrain_corner_radii(&rdcon->block.clip_radius,
                    rdcon->block.clip.right - rdcon->block.clip.left,
                    rdcon->block.clip.bottom - rdcon->block.clip.top);
                log_debug("setup rounded clip: tl=%f, tr=%f, bl=%f, br=%f",
                    rdcon->block.clip_radius.top_left, rdcon->block.clip_radius.top_right,
                    rdcon->block.clip_radius.bottom_left, rdcon->block.clip_radius.bottom_right);
            }
        }
    }
    if (block->scroller->pane) {
        DocState* state = block->doc ? block->doc->state : NULL;
        float scroll_x = 0.0f, scroll_y = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroller->pane,
                                           &scroll_x, &scroll_y, NULL, NULL);
        rdcon->block.x -= scroll_x * s;
        rdcon->block.y -= scroll_y * s;
    }
}

void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    float s = rdcon->scale;
    rdcon->block.x = pa_block->x + block->x * s;  rdcon->block.y = pa_block->y + block->y * s;
    if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width * s, block->height * s};
        if (block->bound && block->bound->border) {
            BoxMetrics block_box = layout_box_metrics(block);
            rect.x += block->bound->border->width.left * s;
            rect.y += block->bound->border->width.top * s;
            rect.width -= block_box.border_h * s;
            rect.height -= block_box.border_v * s;
        }
        if (block->scroller->pane) {
            DocState* state = block->doc ? block->doc->state : NULL;
            scrollpane_render(rdcon, block->scroller->pane, &rect,
                block->content_width * s, block->content_height * s, &rdcon->block.clip, s,
                state, static_cast<View*>(block),
                block->scroller->has_hz_scroll, block->scroller->has_vt_scroll);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void scroll_apply_pending_element_scroll(ViewBlock* block) {
    if (!block || !block->scroller || !block->scroller->pane) return;
    DomElement* elem = static_cast<DomElement*>(block);
    if (!elem->has_pending_element_scroll_x &&
        !elem->has_pending_element_scroll_y) {
        return;
    }

    DocState* state = elem->doc ? (DocState*)elem->doc->state : nullptr;
    float current_x = 0.0f;
    float current_y = 0.0f;
    scroll_state_get_position_for_view(state, static_cast<View*>(block),
        block->scroller->pane, &current_x, &current_y, NULL, NULL);
    float target_x = elem->has_pending_element_scroll_x
        ? elem->pending_element_scroll_x : current_x;
    float target_y = elem->has_pending_element_scroll_y
        ? elem->pending_element_scroll_y : current_y;

    scroll_state_set_position_for_view(state, static_cast<View*>(block),
        block->scroller->pane, target_x, target_y, false);
    elem->has_pending_element_scroll_x = false;
    elem->has_pending_element_scroll_y = false;
}

void scrollpane_scroll(EventContext* evcon, ViewBlock* block, ScrollPane* sp) {
    ScrollEvent* event = &evcon->event.scroll;
    // GLFW gives scroll deltas that are pre-adjusted to match the user's OS scrolling preference
    // yoffset > 0 = Scroll up, yoffset < 0 = Scroll down
    log_debug("firing scroll event: %f, %f", event->xoffset, event->yoffset);

    DomDocument* doc = block && block->doc
        ? block->doc
        : (evcon && evcon->target_document
            ? evcon->target_document
            : (evcon && evcon->ui_context ? evcon->ui_context->document : nullptr));
    DocState* state = doc ? (DocState*)doc->state : nullptr;
    float h = 0.0f, v = 0.0f, h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, &h_max, &v_max);
    float scroll_amount = 50;  // pixels to scroll per offset

    if (event->yoffset != 0 && v_max > 0) {
        v += -event->yoffset * scroll_amount;
    }
    if (event->xoffset != 0 && h_max > 0) {
        h += -event->xoffset * scroll_amount;
    }

    // Centralized writer path for scroll mutations.
    scroll_state_set_position_for_view(state, (View*)block, sp, h, v, false);

    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, NULL, NULL);
    log_debug("updated scroll position: %f, %f", h, v);
    evcon->need_repaint = true;
    // todo: set invalidate_rect
}

static DocState* scrollpane_doc_state(EventContext* evcon, ViewBlock* block) {
    DomDocument* doc = block && block->doc
        ? block->doc
        : (evcon && evcon->target_document
            ? evcon->target_document
            : (evcon && evcon->ui_context ? evcon->ui_context->document : nullptr));
    return doc ? (DocState*)doc->state : nullptr;
}

bool scrollpane_target(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroller->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    float bottom = evcon->block.y + block->height;  float right = evcon->block.x + block->width;
    // sc.SCROLLBAR_SIZE is in physical pixels; convert to CSS pixels for event-coordinate comparisons
    float pixel_ratio = evcon->ui_context->pixel_ratio;
    float scrollbar_css = sc.SCROLLBAR_SIZE / pixel_ratio;
    bool h_hovered = false, v_hovered = false;
    if (block->scroller->has_hz_scroll) {
        if (evcon->block.x <= event->x && event->x < right &&
            bottom - scrollbar_css <= event->y && event->y < bottom) {
            h_hovered = true;
            scroll_state_set_hover_for_view(state, (View*)block, sp, h_hovered, v_hovered);
            return true;
        }
    }
    if (block->scroller->has_vt_scroll) {
        if (evcon->block.y <= event->y && event->y < bottom &&
            right - scrollbar_css <= event->x && event->x < right) {
            v_hovered = true;
            scroll_state_set_hover_for_view(state, (View*)block, sp, h_hovered, v_hovered);
            return true;
        }
    }
    scroll_state_set_hover_for_view(state, (View*)block, sp, false, false);
    return false;
}

void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block) {
    MouseButtonEvent *event = &evcon->event.mouse_button;
    ScrollPane* sp = block->scroller->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, (View*)block, &interaction);

    float h = 0.0f, v = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, NULL, NULL);

    if (interaction.h_hovered) {
        if (evcon->offset_x < sp->h_handle_x ) {
            float next_h = h - block->width * 0.85f;  // scroll 85% of the block width
            scroll_state_set_position_for_view(state, (View*)block, sp, next_h, v, false);
            evcon->need_repaint = true;
        }
        else if (evcon->offset_x > sp->h_handle_x + sp->h_handle_width) { // page right
            float next_h = h + block->width * 0.85f;  // scroll 85% of the block width
            scroll_state_set_position_for_view(state, (View*)block, sp, next_h, v, false);
            evcon->need_repaint = true;
        }
        else {
            scroll_state_begin_drag_for_view(state, (View*)block, sp, true, event->x, event->y, h, v);
            DragTransitionArgs drag_args = { .target = (View*)block, .dragging = true };
            drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);
        }
    }
    else if (interaction.v_hovered) {
        if (evcon->offset_y < sp->v_handle_y) { // page up
            float next_v = v - block->height * 0.85f;  // scroll 85% of the block height
            scroll_state_set_position_for_view(state, (View*)block, sp, h, next_v, false);
            evcon->need_repaint = true;
        }
        else if (evcon->offset_y > sp->v_handle_y + sp->v_handle_height) { // page down
            float next_v = v + block->height * 0.85f;  // scroll 85% of the block height
            scroll_state_set_position_for_view(state, (View*)block, sp, h, next_v, false);
            evcon->need_repaint = true;
        }
        else {
            scroll_state_begin_drag_for_view(state, (View*)block, sp, false, event->x, event->y, h, v);
            DragTransitionArgs drag_args = { .target = (View*)block, .dragging = true };
            drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);
        }
    }
}

void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block) {
    ScrollPane* sp = block->scroller->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    if (scroll_state_is_dragging_for_view(state, (View*)block)) {
        scroll_state_clear_drag_for_view(state, (View*)block, sp);
        DragTransitionArgs drag_args = { .target = NULL, .dragging = false };
        drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);
    }
}

void scrollpane_drag(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroller->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, (View*)block, &interaction);
    float h = 0.0f, v = 0.0f, h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, &h_max, &v_max);

    // Vertical dragging
    if (interaction.v_dragging) {
        float handle_h = sp->v_handle_height;  // CSS pixels
        float delta_y = event->y - interaction.drag_start_y;  // CSS pixels
        // scroll track length in CSS pixels = block height - scrollbar bottom strip - borders
        float pixel_ratio = evcon->ui_context->pixel_ratio;
        float scrollbar_css = sc.SCROLLBAR_SIZE / pixel_ratio;
        float border_css = sc.SCROLL_BORDER_MAIN / pixel_ratio;
        float scroll_track = block->height - scrollbar_css - border_css * 2;
        float scroll_range = scroll_track - handle_h;
        float scroll_per_pixel = scroll_range > 0 ? v_max / scroll_range : 0;
        float v_scroll_position = interaction.v_drag_start_scroll + (delta_y * scroll_per_pixel);
        if (v_scroll_position != v) {
            scroll_state_set_position_for_view(state, (View*)block, sp, h, v_scroll_position, false);
            evcon->need_repaint = true;
        }
    }

    // Horizontal dragging
    if (interaction.h_dragging) {
        float handle_w = sp->h_handle_width;  // CSS pixels
        float delta_x = event->x - interaction.drag_start_x;  // CSS pixels
        // scroll track length in CSS pixels = block width - scrollbar right strip - borders
        float pixel_ratio2 = evcon->ui_context->pixel_ratio;
        float scrollbar_css2 = sc.SCROLLBAR_SIZE / pixel_ratio2;
        float border_css2 = sc.SCROLL_BORDER_MAIN / pixel_ratio2;
        float scroll_track_h = block->width - scrollbar_css2 - border_css2 * 2;
        float scroll_range = scroll_track_h - handle_w;
        float scroll_per_pixel = scroll_range > 0 ? h_max / scroll_range : 0;
        float h_scroll_position = interaction.h_drag_start_scroll + (delta_x * scroll_per_pixel);
        if (h_scroll_position != h) {
            scroll_state_set_position_for_view(state, (View*)block, sp, h_scroll_position, v, false);
            evcon->need_repaint = true;
        }
    }
}

void update_scroller(ViewBlock* block, float content_width, float content_height) {
    if (!block->scroller) { return; }
    // handle horizontal overflow
    log_debug("update scroller for block:%s, content_width:%.1f, content_height:%.1f, block_width:%.1f, block_height:%.1f",
        block->node_name(), content_width, content_height, block->width, block->height);

    block->scroller->has_hz_overflow = false;
    block->scroller->has_vt_overflow = false;
    block->scroller->has_hz_scroll = false;
    block->scroller->has_vt_scroll = false;
    block->scroller->has_clip = false;

    // Update scroll pane max values through centralized API.
    if (block->scroller->pane) {
        DocState* state = block->doc ? (DocState*)block->doc->state : nullptr;
        float h_max = content_width > block->width ? content_width - block->width : 0.0f;
        float v_max = content_height > block->height ? content_height - block->height : 0.0f;
        scroll_state_set_max_for_view(state, (View*)block, block->scroller->pane, h_max, v_max);
        scroll_apply_pending_element_scroll(block);
        scroll_state_get_position_for_view(state, (View*)block, block->scroller->pane,
                                           NULL, NULL, &h_max, &v_max);
        log_debug("update_scroller: h_max_scroll=%.1f, v_max_scroll=%.1f",
            h_max, v_max);
    }

    if (content_width > block->width) { // hz overflow
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == CSS_VALUE_VISIBLE) {}
        else if (block->scroller->overflow_x == CSS_VALUE_SCROLL ||
            block->scroller->overflow_x == CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll ||
            block->scroller->overflow_x == CSS_VALUE_CLIP ||
            block->scroller->overflow_x == CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
        }
    }
    else {
        block->scroller->has_hz_overflow = false;
    }
    // handle vertical overflow and determine block->height
    if (content_height > block->height) { // vt overflow
        block->scroller->has_vt_overflow = true;
        if (block->scroller->overflow_y == CSS_VALUE_VISIBLE) { }
        else if (block->scroller->overflow_y == CSS_VALUE_SCROLL || block->scroller->overflow_y == CSS_VALUE_AUTO) {
            block->scroller->has_vt_scroll = true;
        }
        if (block->scroller->has_vt_scroll ||
            block->scroller->overflow_y == CSS_VALUE_CLIP ||
            block->scroller->overflow_y == CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
        }
    }
    else {
        block->scroller->has_vt_overflow = false;
    }
    // Always clip when overflow is hidden/clip, even without actual overflow
    // This is needed for border-radius clipping to work correctly
    bool should_clip = block->scroller->has_vt_overflow || block->scroller->has_hz_overflow ||
                       block->scroller->overflow_x == CSS_VALUE_HIDDEN ||
                       block->scroller->overflow_x == CSS_VALUE_CLIP ||
                       block->scroller->overflow_y == CSS_VALUE_HIDDEN ||
                       block->scroller->overflow_y == CSS_VALUE_CLIP;
    if (should_clip) {
        block->scroller->has_clip = true;
        block->scroller->clip.left = block->bound->border ? block->bound->border->width.left : 0;
        block->scroller->clip.top = block->bound->border ? block->bound->border->width.top : 0;
        block->scroller->clip.right = block->width - (block->bound->border ? block->bound->border->width.right : 0);
        block->scroller->clip.bottom = block->height - (block->bound->border ? block->bound->border->width.bottom : 0);
    }
}
