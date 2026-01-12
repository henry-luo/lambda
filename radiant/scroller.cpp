#include "render.hpp"
#include "handler.hpp"
#include "state_store.hpp"
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


void tvg_shape_get_bounds(Tvg_Paint shape, int* x, int* y, int* width, int* height) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    *x = p[0].x;  *y = p[0].y;
    *width = p[2].x - p[0].x;  *height = p[2].y - p[0].y;
}

float tvg_shape_get_w(Tvg_Paint shape) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    return p[2].x - p[0].x;
}

float tvg_shape_get_h(Tvg_Paint shape) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    return p[2].y - p[0].y;
}

void ScrollPane::reset() {
    memset(this, 0, sizeof(ScrollPane));
}

void scrollpane_render(Tvg_Canvas canvas, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip) {
    log_info("SCROLLPANE: content size: %.1f x %.1f, view bounds: %.1f x %.1f",
        content_width, content_height, block_bound->width, block_bound->height);
    log_debug("render scroller content size: %f x %f, blk bounds: %f x %f",
        content_width, content_height, block_bound->width, block_bound->height);

    float view_x = block_bound->x, view_y = block_bound->y;
    float view_width = block_bound->width, view_height = block_bound->height;

    tvg_canvas_remove(canvas, NULL);  // clear any existing shapes
    // clip shape
    Tvg_Paint clip_rect = tvg_shape_new();
    tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0, true);
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill

    // vertical scrollbar
    Tvg_Paint v_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(v_scrollbar, view_x + view_width - sc.SCROLLBAR_SIZE,
        view_y, sc.SCROLLBAR_SIZE, view_height, 0, 0, true);
    log_debug("v_scrollbar rect: x %f, y %f, wd %f, hg %f",
        view_x + view_width - sc.SCROLLBAR_SIZE, view_y, sc.SCROLLBAR_SIZE, view_height);
    tvg_shape_set_fill_color(v_scrollbar, sc.BAR_COLOR, sc.BAR_COLOR, sc.BAR_COLOR, 255);
    tvg_paint_set_mask_method(v_scrollbar, clip_rect, TVG_MASK_METHOD_ALPHA);

    Tvg_Paint v_scroll_handle = tvg_shape_new();
    if (content_height > 0) {
        tvg_shape_set_fill_color(v_scroll_handle, sc.HANDLE_COLOR, sc.HANDLE_COLOR, sc.HANDLE_COLOR, 255);
        // NOTE: Do NOT recalculate v_max_scroll here!
        // v_max_scroll is set by update_scroller() in CSS pixels, and is used by scroll event handlers
        // which compare against scroll_position (also in CSS pixels).
        // content_height and view_height here are in physical pixels (for rendering).
        float bar_height = view_height - sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_MAIN * 2;
        log_debug("bar height: %f", bar_height);
        float v_ratio = min(view_height * 100 / content_height, 100.0f);
        sp->v_handle_height = (v_ratio * bar_height) / 100;
        sp->v_handle_height = max(sc.MIN_HANDLE_SIZE, sp->v_handle_height);
        // v_scroll_position and v_max_scroll are in CSS pixels, calculate scroll ratio (0..1)
        float scroll_ratio = (sp->v_max_scroll > 0) ? (sp->v_scroll_position / sp->v_max_scroll) : 0;
        sp->v_handle_y = sc.SCROLL_BORDER_MAIN + scroll_ratio * (bar_height - sp->v_handle_height);
        float v_scroll_x = view_x + view_width - sc.SCROLLBAR_SIZE + sc.SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(v_scroll_handle, v_scroll_x, view_y + sp->v_handle_y,
            sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, sp->v_handle_height, sc.HANDLE_RADIUS, sc.HANDLE_RADIUS, true);
        log_debug("v_scroll_handle rect: x %f, y %f, wd %f, hg %f",
            v_scroll_x, view_y + sp->v_handle_y, sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, sp->v_handle_height);
        tvg_paint_set_mask_method(v_scroll_handle, clip_rect, TVG_MASK_METHOD_ALPHA);
    }

    // horizontal scrollbar
    Tvg_Paint h_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(h_scrollbar, view_x,
        view_y + view_height - sc.SCROLLBAR_SIZE, view_width, sc.SCROLLBAR_SIZE, 0, 0, true);
    log_debug("h_scrollbar rect: %f, %f, %f, %f",
        view_x, view_y + view_height - sc.SCROLLBAR_SIZE, view_width, sc.SCROLLBAR_SIZE);
    tvg_shape_set_fill_color(h_scrollbar, sc.BAR_COLOR, sc.BAR_COLOR, sc.BAR_COLOR, 255);
    tvg_paint_set_mask_method(h_scrollbar, clip_rect, TVG_MASK_METHOD_ALPHA);

    Tvg_Paint h_scroll_handle = tvg_shape_new();
    if (content_width > 0) {
        tvg_shape_set_fill_color(h_scroll_handle, sc.HANDLE_COLOR, sc.HANDLE_COLOR, sc.HANDLE_COLOR, 255);
        // NOTE: Do NOT recalculate h_max_scroll here! Same reason as v_max_scroll above.
        // h_max_scroll is set by update_scroller() in CSS pixels.
        log_debug("h_max_scroll: %f (content_width=%.1f, view_width=%.1f)", sp->h_max_scroll, content_width, view_width);
        float bar_width = view_width - sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_MAIN * 2;
        log_debug("bar width: %f", bar_width);
        float h_ratio = min(view_width * 100 / content_width, 100.0f);
        sp->h_handle_width = (h_ratio * bar_width) / 100;
        sp->h_handle_width = max(sc.MIN_HANDLE_SIZE, sp->h_handle_width);
        // h_scroll_position and h_max_scroll are in CSS pixels, calculate scroll ratio (0..1)
        float scroll_ratio = (sp->h_max_scroll > 0) ? (sp->h_scroll_position / sp->h_max_scroll) : 0;
        sp->h_handle_x = sc.SCROLL_BORDER_MAIN + scroll_ratio * (bar_width - sp->h_handle_width);
        int h_scroll_y = view_y + view_height - sc.SCROLLBAR_SIZE + sc.SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(h_scroll_handle, view_x + sp->h_handle_x, h_scroll_y,
            sp->h_handle_width, sc.SCROLLBAR_SIZE - sc.SCROLL_BORDER_CROSS * 2, sc.HANDLE_RADIUS, sc.HANDLE_RADIUS, true);
        tvg_paint_set_mask_method(h_scroll_handle, clip_rect, TVG_MASK_METHOD_ALPHA);
    }

    // as clip_rect is shared, can only push the shapes after they are all setup
    tvg_canvas_push(canvas, v_scrollbar);
    tvg_canvas_push(canvas, v_scroll_handle);
    tvg_canvas_push(canvas, h_scrollbar);
    tvg_canvas_push(canvas, h_scroll_handle);

    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
    tvg_canvas_remove(canvas, NULL);  // IMPORTANT: clear shapes after rendering
    log_debug("finished rendering scroller");
}

void scrollpane_scroll(EventContext* evcon, ScrollPane* sp) {
    ScrollEvent* event = &evcon->event.scroll;
    // GLFW gives scroll deltas that are pre-adjusted to match the user's OS scrolling preference
    // yoffset > 0 = Scroll up, yoffset < 0 = Scroll down
    log_debug("firing scroll event: %f, %f", event->xoffset, event->yoffset);
    float scroll_amount = 50;  // pixels to scroll per offset
    if (event->yoffset != 0 && sp->v_max_scroll > 0) {
        sp->v_scroll_position += -event->yoffset * scroll_amount;
        sp->v_scroll_position = sp->v_scroll_position < 0 ? 0 :
            sp->v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : sp->v_scroll_position;
    }
    if (event->xoffset != 0 && sp->h_max_scroll > 0) {
        sp->h_scroll_position += -event->xoffset * scroll_amount;
        sp->h_scroll_position = sp->h_scroll_position < 0 ? 0 :
            sp->h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : sp->h_scroll_position;
    }
    log_debug("updated scroll position: %f, %f", sp->h_scroll_position, sp->v_scroll_position);
    evcon->need_repaint = true;
    // todo: set invalidate_rect
}

bool scrollpane_target(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroller->pane;
    float bottom = evcon->block.y + block->height;  float right = evcon->block.x + block->width;
    if (block->scroller->has_hz_scroll) {
        if (evcon->block.x <= event->x && event->x < right &&
            bottom - sc.SCROLLBAR_SIZE <= event->y && event->y < bottom) {
            sp->is_h_hovered = true;
            return true;
        }
        else sp->is_h_hovered = false;
    }
    if (block->scroller->has_vt_scroll) {
        if (evcon->block.y <= event->y && event->y < bottom &&
            right - sc.SCROLLBAR_SIZE <= event->x && event->x < right) {
            sp->is_v_hovered = true;
            return true;
        }
        else sp->is_v_hovered = false;
    }
    return false;
}

void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block) {
    MouseButtonEvent *event = &evcon->event.mouse_button;
    ScrollPane* sp = block->scroller->pane;
    if (sp->is_h_hovered) {
        if (evcon->offset_x < sp->h_handle_x ) {
            sp->h_scroll_position -= block->width * 0.85;   // scroll 85% of the block width
            sp->h_scroll_position = max(0, sp->h_scroll_position);
            evcon->need_repaint = true;
        }
        else if (evcon->offset_x > sp->h_handle_x + sp->h_handle_width) { // page right
            sp->h_scroll_position += block->width * 0.85;   // scroll 85% of the block width
            sp->h_scroll_position = min(sp->h_scroll_position, sp->h_max_scroll);
            evcon->need_repaint = true;
        }
        else {
            sp->h_is_dragging = true; // start dragging the handle
            sp->drag_start_x = event->x; // capture the current mouse X position
            sp->h_drag_start_scroll = sp->h_scroll_position;
            evcon->ui_context->document->state->is_dragging = true;
            evcon->ui_context->document->state->drag_target = (View*)block;
        }
    }
    else if (sp->is_v_hovered) {
        if (evcon->offset_y < sp->v_handle_y) { // page up
            sp->v_scroll_position -= block->height * 0.85;   // scroll 85% of the block height
            sp->v_scroll_position = max(0, sp->v_scroll_position);
            evcon->need_repaint = true;
        }
        else if (evcon->offset_y > sp->v_handle_y + sp->v_handle_height) { // page down
            sp->v_scroll_position += block->height * 0.85;   // scroll 85% of the block height
            sp->v_scroll_position = min(sp->v_scroll_position, sp->v_max_scroll);
            evcon->need_repaint = true;
        }
        else {
            sp->v_is_dragging = true; // start dragging the handle
            sp->drag_start_y = event->y; // capture the current mouse Y position
            sp->v_drag_start_scroll = sp->v_scroll_position;
            evcon->ui_context->document->state->is_dragging = true;
            evcon->ui_context->document->state->drag_target = (View*)block;
        }
    }
}

void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block) {
    ScrollPane* sp = block->scroller->pane;
    if (sp->h_is_dragging || sp->v_is_dragging) {
        sp->h_is_dragging = false;
        sp->drag_start_x = 0;  sp->h_drag_start_scroll = 0;
        sp->v_is_dragging = false;
        sp->drag_start_y = 0;  sp->v_drag_start_scroll = 0;
        evcon->ui_context->document->state->is_dragging = false;
        evcon->ui_context->document->state->drag_target = NULL;
    }
}

void scrollpane_drag(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroller->pane;

    // Vertical dragging
    if (sp->v_is_dragging) {
        float handle_h = sp->v_handle_height;
        float delta_y = event->y - sp->drag_start_y;
        float scroll_range = block->height - handle_h;
        float scroll_per_pixel = scroll_range > 0 ? sp->v_max_scroll / scroll_range : 0;
        float v_scroll_position = sp->v_drag_start_scroll + (delta_y * scroll_per_pixel);
        v_scroll_position = v_scroll_position < 0 ? 0 :
            v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : v_scroll_position;
        if (v_scroll_position != sp->v_scroll_position) {
            sp->v_scroll_position = v_scroll_position;
            evcon->need_repaint = true;
        }
    }

    // Horizontal dragging
    if (sp->h_is_dragging) {
        float handle_w = sp->h_handle_width;
        float delta_x = event->x - sp->drag_start_x;
        float scroll_range = block->width - handle_w;
        float scroll_per_pixel = scroll_range > 0 ? sp->h_max_scroll / scroll_range : 0;
        float h_scroll_position = sp->h_drag_start_scroll + (delta_x * scroll_per_pixel);
        h_scroll_position = h_scroll_position < 0 ? 0 :
            h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : h_scroll_position;
        if (h_scroll_position != sp->h_scroll_position) {
            sp->h_scroll_position = h_scroll_position;
            evcon->need_repaint = true;
        }
    }
}

void update_scroller(ViewBlock* block, float content_width, float content_height) {
    if (!block->scroller) { return; }
    // handle horizontal overflow
    log_debug("update scroller for block:%s, content_width:%.1f, content_height:%.1f, block_width:%.1f, block_height:%.1f",
        block->node_name(), content_width, content_height, block->width, block->height);
    
    // Update scroll pane max values if pane exists
    if (block->scroller->pane) {
        block->scroller->pane->h_max_scroll = content_width > block->width ? content_width - block->width : 0;
        block->scroller->pane->v_max_scroll = content_height > block->height ? content_height - block->height : 0;
        // Clamp current scroll positions to new max values
        if (block->scroller->pane->h_scroll_position > block->scroller->pane->h_max_scroll) {
            block->scroller->pane->h_scroll_position = block->scroller->pane->h_max_scroll;
        }
        if (block->scroller->pane->v_scroll_position > block->scroller->pane->v_max_scroll) {
            block->scroller->pane->v_scroll_position = block->scroller->pane->v_max_scroll;
        }
        log_debug("update_scroller: h_max_scroll=%.1f, v_max_scroll=%.1f", 
            block->scroller->pane->h_max_scroll, block->scroller->pane->v_max_scroll);
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
        if (block->scroller->has_hz_scroll ||
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
