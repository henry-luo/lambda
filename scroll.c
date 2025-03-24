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
    int content_width, int content_height) {
    dzlog_debug("render scroller content size: %d x %d\n", content_width, content_height);
    sp->content_width = content_width;  sp->content_height = content_height;
    
    int view_x = block_bound->x, view_y = block_bound->y;
    int view_width = block_bound->width, view_height = block_bound->height;

    // Vertical scrollbar
    Tvg_Paint* v_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(v_scrollbar, view_x + view_width - SCROLLBAR_SIZE, 
        view_y, SCROLLBAR_SIZE, view_height, 0, 0);
    tvg_shape_set_fill_color(v_scrollbar, BAR_COLOR, BAR_COLOR, BAR_COLOR, 255);

    Tvg_Paint* v_scroll_handle = tvg_shape_new();
    tvg_shape_set_fill_color(v_scroll_handle, HANDLE_COLOR, HANDLE_COLOR, HANDLE_COLOR, 255);
    if (content_height > 0) {
        sp->v_max_scroll = content_height > view_height ? content_height - view_height : 0;
        int bar_height = view_height - SCROLLBAR_SIZE - SCROLL_BORDER_MAIN * 2;
        int v_ratio = view_height * 100 / content_height;
        int v_handle_height = (v_ratio * bar_height) / 100;
        v_handle_height = max(MIN_HANDLE_SIZE, v_handle_height);
        int v_handle_y = SCROLL_BORDER_MAIN + (sp->v_max_scroll > 0 ? 
                        (sp->v_scroll_position * (bar_height - v_handle_height)) / sp->v_max_scroll : 0);
        int v_scroll_x = view_x + view_width - SCROLLBAR_SIZE + SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(v_scroll_handle, v_scroll_x, view_y + v_handle_y, 
            SCROLLBAR_SIZE - SCROLL_BORDER_CROSS * 2, v_handle_height, HANDLE_RADIUS, HANDLE_RADIUS);
    }
    
    // Horizontal scrollbar
    Tvg_Paint* h_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(h_scrollbar, view_x, 
        view_y + view_height - SCROLLBAR_SIZE, view_width, SCROLLBAR_SIZE, 0, 0);
    tvg_shape_set_fill_color(h_scrollbar, BAR_COLOR, BAR_COLOR, BAR_COLOR, 255);

    Tvg_Paint* h_scroll_handle = tvg_shape_new();
    tvg_shape_set_fill_color(h_scroll_handle, HANDLE_COLOR, HANDLE_COLOR, HANDLE_COLOR, 255);
    if (content_width > 0) {
        sp->h_max_scroll = content_width > view_width ? content_width - view_width : 0;
        int bar_width = view_width - SCROLLBAR_SIZE - SCROLL_BORDER_MAIN * 2;
        int h_ratio = view_width * 100 / content_width;
        int h_handle_width = (h_ratio * bar_width) / 100;
        h_handle_width = max(MIN_HANDLE_SIZE, h_handle_width);
        int h_handle_x = SCROLL_BORDER_MAIN + (sp->h_max_scroll > 0 ? 
                        (sp->h_scroll_position * (bar_width - h_handle_width)) / sp->h_max_scroll : 0);
        int h_scroll_y = view_y + view_height - SCROLLBAR_SIZE + SCROLL_BORDER_CROSS;
        tvg_shape_append_rect(h_scroll_handle, view_x + h_handle_x, h_scroll_y, 
            h_handle_width, SCROLLBAR_SIZE - SCROLL_BORDER_CROSS * 2, HANDLE_RADIUS, HANDLE_RADIUS);
    }

    if (sp->content_height > view_height) {
        tvg_canvas_push(canvas, v_scrollbar);
        tvg_canvas_push(canvas, v_scroll_handle);
    }
    if (sp->content_width > view_width) {
        tvg_canvas_push(canvas, h_scrollbar);
        tvg_canvas_push(canvas, h_scroll_handle);
    }
    tvg_canvas_update(canvas);
}

void scrollpane_scroll(EventContext* evcon, ScrollPane* sp, ScrollEvent* event) {
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

// void _mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
//     ScrollPane* sp = (ScrollPane*)glfwGetWindowUserPointer(window);
//     if (!sp || button != GLFW_MOUSE_BUTTON_LEFT) return;
    
//     double xpos, ypos;
//     glfwGetCursorPos(window, &xpos, &ypos);
    
//     // Vertical scrollbar check
//     int vsx, vsy, vsw, vsh;
//     tvg_shape_get_bounds(sp->v_scroll_handle, &vsx, &vsy, &vsw, &vsh);
//     if (action == GLFW_PRESS && 
//         xpos >= vsx && xpos <= vsx + vsw && 
//         ypos >= vsy && ypos <= vsy + vsh) {
//         sp->v_is_dragging = true;
//         sp->drag_start_y = (int)ypos;
//         sp->v_drag_start_scroll = sp->v_scroll_position;
//         return;
//     }
    
//     // Horizontal scrollbar check
//     int hsx, hsy, hsw, hsh;
//     tvg_shape_get_bounds(sp->h_scroll_handle, &hsx, &hsy, &hsw, &hsh);
//     if (action == GLFW_PRESS && 
//         xpos >= hsx && xpos <= hsx + hsw && 
//         ypos >= hsy && ypos <= hsy + hsh) {
//         sp->h_is_dragging = true;
//         sp->drag_start_x = (int)xpos;
//         sp->h_drag_start_scroll = sp->h_scroll_position;
//         return;
//     }
    
//     if (action == GLFW_RELEASE) {
//         sp->v_is_dragging = false;
//         sp->h_is_dragging = false;
//     }
// }

// void _cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
//     ScrollPane* sp = (ScrollPane*)glfwGetWindowUserPointer(window);
//     if (!sp) return;
    
//     // Vertical dragging
//     if (sp->v_is_dragging) {
//         int sy, sh;
//         tvg_shape_get_bounds(sp->v_scrollbar, NULL, &sy, NULL, &sh);
//         int handle_h = tvg_shape_get_h(sp->v_scroll_handle);
        
//         int delta_y = (int)ypos - sp->drag_start_y;
//         int scroll_range = sh - handle_h;
//         int scroll_per_pixel = scroll_range > 0 ? sp->v_max_scroll / scroll_range : 0;
        
//         sp->v_scroll_position = sp->v_drag_start_scroll + (delta_y * scroll_per_pixel);
//         sp->v_scroll_position = sp->v_scroll_position < 0 ? 0 : 
//                                sp->v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : 
//                                sp->v_scroll_position;
//     }
    
//     // Horizontal dragging
//     if (sp->h_is_dragging) {
//         int sx, sw;
//         tvg_shape_get_bounds(sp->h_scrollbar, &sx, NULL, &sw, NULL);
//         int handle_w = tvg_shape_get_w(sp->h_scroll_handle);
        
//         int delta_x = (int)xpos - sp->drag_start_x;
//         int scroll_range = sw - handle_w;
//         int scroll_per_pixel = scroll_range > 0 ? sp->h_max_scroll / scroll_range : 0;
        
//         sp->h_scroll_position = sp->h_drag_start_scroll + (delta_x * scroll_per_pixel);
//         sp->h_scroll_position = sp->h_scroll_position < 0 ? 0 : 
//                                sp->h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : 
//                                sp->h_scroll_position;
//     }
    
//     if (sp->v_is_dragging || sp->h_is_dragging) {
//         evcon->need_repaint = true;
//     }
// }

void scrollpane_destroy(ScrollPane* sp) {
    if (sp) free(sp);
}

/*
int main() {
    glfwSetWindowUserPointer(window, sp);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
}
*/