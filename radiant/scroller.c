#include "render.h"
#include "handler.h"

#define SCROLLBAR_SIZE 24
#define MIN_HANDLE_SIZE 32
#define HANDLE_RADIUS 8
#define SCROLL_BORDER_MAIN 2
#define SCROLL_BORDER_CROSS 4
#define BAR_COLOR 0xF6
#define HANDLE_COLOR 0xC0

void tvg_shape_get_bounds(Tvg_Paint* shape, int* x, int* y, int* width, int* height) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    *x = p[0].x;  *y = p[0].y;
    *width = p[2].x - p[0].x;  *height = p[2].y - p[0].y;
}

float tvg_shape_get_w(Tvg_Paint* shape) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    return p[2].x - p[0].x;
}

float tvg_shape_get_h(Tvg_Paint* shape) {
    Tvg_Matrix m;
    tvg_paint_get_transform(shape, &m);
    Tvg_Point p[4];
    // tvg_paint_get_obb(shape, p);  // get oriented bounding box
    return p[2].y - p[0].y;
}

void scrollpane_render(Tvg_Canvas* canvas, ScrollPane* sp, Rect* block_bound, 
    int content_width, int content_height, Bound* clip) {
    printf("rendering scroller\n");
    dzlog_debug("render scroller content size: %d x %d\n", content_width, content_height);
    
    int view_x = block_bound->x, view_y = block_bound->y;
    int view_width = block_bound->width, view_height = block_bound->height;

    tvg_canvas_remove(canvas, NULL);  // clear any existing shapes
    // clip shape
    Tvg_Paint* clip_rect = tvg_shape_new();
    tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill

    // vertical scrollbar
    Tvg_Paint* v_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(v_scrollbar, view_x + view_width - SCROLLBAR_SIZE, 
        view_y, SCROLLBAR_SIZE, view_height, 0, 0);
    tvg_shape_set_fill_color(v_scrollbar, BAR_COLOR, BAR_COLOR, BAR_COLOR, 255);
    tvg_paint_set_mask_method(v_scrollbar, clip_rect, TVG_MASK_METHOD_ALPHA);
    
    Tvg_Paint* v_scroll_handle = tvg_shape_new();
    if (content_height > 0) {
        tvg_shape_set_fill_color(v_scroll_handle, HANDLE_COLOR, HANDLE_COLOR, HANDLE_COLOR, 255);
        sp->v_max_scroll = content_height > view_height ? content_height - view_height : 0;
        int bar_height = view_height - SCROLLBAR_SIZE - SCROLL_BORDER_MAIN * 2;
        int v_ratio = view_height * 100 / content_height;
        sp->v_handle_height = (v_ratio * bar_height) / 100;
        sp->v_handle_height = max(MIN_HANDLE_SIZE, sp->v_handle_height);
        sp->v_handle_y = SCROLL_BORDER_MAIN + (sp->v_max_scroll > 0 ? 
            (sp->v_scroll_position * (bar_height - sp->v_handle_height)) / sp->v_max_scroll : 0);
        int v_scroll_x = view_x + view_width - SCROLLBAR_SIZE + SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(v_scroll_handle, v_scroll_x, view_y + sp->v_handle_y, 
            SCROLLBAR_SIZE - SCROLL_BORDER_CROSS * 2, sp->v_handle_height, HANDLE_RADIUS, HANDLE_RADIUS);
        tvg_paint_set_mask_method(v_scroll_handle, clip_rect, TVG_MASK_METHOD_ALPHA);
    }

    // horizontal scrollbar
    Tvg_Paint* h_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(h_scrollbar, view_x, 
        view_y + view_height - SCROLLBAR_SIZE, view_width, SCROLLBAR_SIZE, 0, 0);
    tvg_shape_set_fill_color(h_scrollbar, BAR_COLOR, BAR_COLOR, BAR_COLOR, 255);
    tvg_paint_set_mask_method(h_scrollbar, clip_rect, TVG_MASK_METHOD_ALPHA);

    Tvg_Paint* h_scroll_handle = tvg_shape_new();
    if (content_width > 0) {
        tvg_shape_set_fill_color(h_scroll_handle, HANDLE_COLOR, HANDLE_COLOR, HANDLE_COLOR, 255);
        sp->h_max_scroll = content_width > view_width ? content_width - view_width : 0;
        int bar_width = view_width - SCROLLBAR_SIZE - SCROLL_BORDER_MAIN * 2;
        int h_ratio = view_width * 100 / content_width;
        sp->h_handle_width = (h_ratio * bar_width) / 100;
        sp->h_handle_width = max(MIN_HANDLE_SIZE, sp->h_handle_width);
        sp->h_handle_x = SCROLL_BORDER_MAIN + (sp->h_max_scroll > 0 ? 
            (sp->h_scroll_position * (bar_width - sp->h_handle_width)) / sp->h_max_scroll : 0);
        int h_scroll_y = view_y + view_height - SCROLLBAR_SIZE + SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(h_scroll_handle, view_x + sp->h_handle_x, h_scroll_y, 
            sp->h_handle_width, SCROLLBAR_SIZE - SCROLL_BORDER_CROSS * 2, HANDLE_RADIUS, HANDLE_RADIUS);
        tvg_paint_set_mask_method(h_scroll_handle, clip_rect, TVG_MASK_METHOD_ALPHA);
    }

    // as clip_rect is shared, can only push the shapes after they are all setup
    tvg_canvas_push(canvas, v_scrollbar);
    tvg_canvas_push(canvas, v_scroll_handle);
    tvg_canvas_push(canvas, h_scrollbar);
    tvg_canvas_push(canvas, h_scroll_handle);
    
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas); 
    printf("finished rendering scroller\n");
}

void scrollpane_scroll(EventContext* evcon, ScrollPane* sp) {
    ScrollEvent* event = &evcon->event.scroll;
    printf("firing scroll event: %f, %f\n", event->xoffset, event->yoffset);
    int scroll_amount = 50;  // pixels to scroll per offset
    if (event->yoffset != 0 && sp->v_max_scroll > 0) {
        sp->v_scroll_position += event->yoffset * scroll_amount;
        sp->v_scroll_position = sp->v_scroll_position < 0 ? 0 : 
            sp->v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : sp->v_scroll_position;
    }
    if (event->xoffset != 0 && sp->h_max_scroll > 0) {
        sp->h_scroll_position += event->xoffset * scroll_amount;
        sp->h_scroll_position = sp->h_scroll_position < 0 ? 0 : 
            sp->h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : sp->h_scroll_position;
    }
    dzlog_debug("updated scroll position: %d, %d\n", sp->h_scroll_position, sp->v_scroll_position);
    evcon->need_repaint = true;
    // todo: set invalidate_rect
}

bool scrollpane_target(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroller->pane;
    int bottom = evcon->block.y + block->height;  int right = evcon->block.x + block->width;
    if (block->scroller->has_hz_scroll) {
        if (evcon->block.x <= event->x && event->x < right &&
            bottom - SCROLLBAR_SIZE <= event->y && event->y < bottom) {
            sp->is_h_hovered = true;
            return true;
        }
        else sp->is_h_hovered = false;
    }
    if (block->scroller->has_vt_scroll) {
        if (evcon->block.y <= event->y && event->y < bottom &&
            right - SCROLLBAR_SIZE <= event->x && event->x < right) {
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
        int handle_h = sp->v_handle_height;
        int delta_y = event->y - sp->drag_start_y;
        int scroll_range = block->height - handle_h;
        int scroll_per_pixel = scroll_range > 0 ? sp->v_max_scroll / scroll_range : 0;
        int v_scroll_position = sp->v_drag_start_scroll + (delta_y * scroll_per_pixel);
        v_scroll_position = v_scroll_position < 0 ? 0 : 
                               v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : 
                               v_scroll_position;
        if (v_scroll_position != sp->v_scroll_position) {
            sp->v_scroll_position = v_scroll_position;
            evcon->need_repaint = true;
        }
    }
    
    // Horizontal dragging
    if (sp->h_is_dragging) {
        int handle_w = sp->h_handle_width;
        int delta_x = event->x - sp->drag_start_x;
        int scroll_range = block->width - handle_w;
        int scroll_per_pixel = scroll_range > 0 ? sp->h_max_scroll / scroll_range : 0;
        int h_scroll_position = sp->h_drag_start_scroll + (delta_x * scroll_per_pixel);
        h_scroll_position = h_scroll_position < 0 ? 0 : 
                               h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : 
                               h_scroll_position;
        if (h_scroll_position != sp->h_scroll_position) {
            sp->h_scroll_position = h_scroll_position;
            evcon->need_repaint = true;
        }
    }
}

void scrollpane_destroy(ScrollPane* sp) {
    if (sp) free(sp);
}