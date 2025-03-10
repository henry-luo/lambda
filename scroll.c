#include "render.h"

#define SCROLLBAR_SIZE 20
#define MIN_HANDLE_SIZE 30
#define HANDLE_RADIUS 10

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

ScrollPane* scrollpane_create(int x, int y, int width, int height) {
    ScrollPane* sp = (ScrollPane*)calloc(1, sizeof(ScrollPane));
    if (!sp) return NULL;

    sp->view_x = x;  sp->view_y = y;
    sp->view_width = width;  sp->view_height = height;
    
    // Vertical scrollbar
    sp->v_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(sp->v_scrollbar, x + width - SCROLLBAR_SIZE, y, SCROLLBAR_SIZE, height, 0, 0);
    tvg_shape_set_fill_color(sp->v_scrollbar, 200, 200, 200, 255);
    sp->v_scroll_handle = tvg_shape_new();
    tvg_shape_set_fill_color(sp->v_scroll_handle, 100, 100, 100, 255);
    
    // Horizontal scrollbar
    sp->h_scrollbar = tvg_shape_new();
    tvg_shape_append_rect(sp->h_scrollbar, x, y + height - SCROLLBAR_SIZE, width, SCROLLBAR_SIZE, 0, 0);
    printf("hz scrollbar bounds: %d, %d, %d, %d\n", x, y + height - SCROLLBAR_SIZE, width, SCROLLBAR_SIZE);
    tvg_shape_set_fill_color(sp->h_scrollbar, 200, 200, 200, 255);
    sp->h_scroll_handle = tvg_shape_new();
    tvg_shape_set_fill_color(sp->h_scroll_handle, 100, 100, 100, 255);
    return sp;
}

void scrollpane_set_content_size(ScrollPane* sp, int content_width, int content_height) {
    if (!sp) return;
    printf("set content size: %d x %d\n", content_width, content_height);
    sp->content_width = content_width;  sp->content_height = content_height;
    
    // Vertical scrollbar
    printf("create v_scroll_handle\n");
    tvg_shape_reset(sp->v_scroll_handle);
    if (content_height > 0) {
        sp->v_max_scroll = content_height > sp->view_height ? content_height - sp->view_height : 0;
        int v_ratio = sp->view_height * 100 / content_height;
        int v_handle_height = (v_ratio * sp->view_height) / 100;
        v_handle_height = v_handle_height < MIN_HANDLE_SIZE ? MIN_HANDLE_SIZE : v_handle_height;
        int v_handle_y = sp->v_max_scroll > 0 ? 
                        (sp->v_scroll_position * (sp->view_height - v_handle_height)) / sp->v_max_scroll : 0;
        int v_scroll_x = sp->view_x + sp->view_width - SCROLLBAR_SIZE;
        tvg_shape_append_rect(sp->v_scroll_handle,
            v_scroll_x, sp->view_y, SCROLLBAR_SIZE, v_handle_height, HANDLE_RADIUS, HANDLE_RADIUS);
    }
    
    // Horizontal scrollbar
    printf("create h_scroll_handle\n");
    tvg_shape_reset(sp->h_scroll_handle);
    if (content_width > 0) {
        sp->h_max_scroll = content_width > sp->view_width ? content_width - sp->view_width : 0;
        int h_ratio = sp->view_width * 100 / content_width;
        int h_handle_width = (h_ratio * sp->view_width) / 100;
        h_handle_width = h_handle_width < MIN_HANDLE_SIZE ? MIN_HANDLE_SIZE : h_handle_width;
        int h_handle_x = sp->h_max_scroll > 0 ? 
                        (sp->h_scroll_position * (sp->view_width - h_handle_width)) / sp->h_max_scroll : 0;
        int h_scroll_y = sp->view_y + sp->view_height - SCROLLBAR_SIZE;
        tvg_shape_append_rect(sp->h_scroll_handle,
            sp->view_x, h_scroll_y, h_handle_width, SCROLLBAR_SIZE, HANDLE_RADIUS, HANDLE_RADIUS);
    }
}

void scrollpane_update(Tvg_Canvas* canvas, ScrollPane* sp) {
    printf("update scrollpane\n");
    if (sp->content_height > sp->view_height) {
        tvg_canvas_push(canvas, sp->v_scrollbar);
        tvg_canvas_push(canvas, sp->v_scroll_handle);
    }
    if (sp->content_width > sp->view_width) {
        tvg_canvas_push(canvas, sp->h_scrollbar);
        tvg_canvas_push(canvas, sp->h_scroll_handle);
    }
    tvg_canvas_update(canvas);
}

void _scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    ScrollPane* sp = (ScrollPane*)glfwGetWindowUserPointer(window);
    if (!sp) return;
    
    int scroll_amount = 50;
    if (yoffset != 0 && sp->v_max_scroll > 0) {
        sp->v_scroll_position += (int)yoffset * scroll_amount;
        sp->v_scroll_position = sp->v_scroll_position < 0 ? 0 : 
                               sp->v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : 
                               sp->v_scroll_position;
    }
    if (xoffset != 0 && sp->h_max_scroll > 0) {
        sp->h_scroll_position += (int)xoffset * scroll_amount;
        sp->h_scroll_position = sp->h_scroll_position < 0 ? 0 : 
                               sp->h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : 
                               sp->h_scroll_position;
    }
    scrollpane_set_content_size(sp, sp->content_width, sp->content_height);
    // scrollpane_update(sp);
}

void _mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    ScrollPane* sp = (ScrollPane*)glfwGetWindowUserPointer(window);
    if (!sp || button != GLFW_MOUSE_BUTTON_LEFT) return;
    
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    // Vertical scrollbar check
    int vsx, vsy, vsw, vsh;
    tvg_shape_get_bounds(sp->v_scroll_handle, &vsx, &vsy, &vsw, &vsh);
    if (action == GLFW_PRESS && 
        xpos >= vsx && xpos <= vsx + vsw && 
        ypos >= vsy && ypos <= vsy + vsh) {
        sp->v_is_dragging = true;
        sp->drag_start_y = (int)ypos;
        sp->v_drag_start_scroll = sp->v_scroll_position;
        return;
    }
    
    // Horizontal scrollbar check
    int hsx, hsy, hsw, hsh;
    tvg_shape_get_bounds(sp->h_scroll_handle, &hsx, &hsy, &hsw, &hsh);
    if (action == GLFW_PRESS && 
        xpos >= hsx && xpos <= hsx + hsw && 
        ypos >= hsy && ypos <= hsy + hsh) {
        sp->h_is_dragging = true;
        sp->drag_start_x = (int)xpos;
        sp->h_drag_start_scroll = sp->h_scroll_position;
        return;
    }
    
    if (action == GLFW_RELEASE) {
        sp->v_is_dragging = false;
        sp->h_is_dragging = false;
    }
}

void _cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    ScrollPane* sp = (ScrollPane*)glfwGetWindowUserPointer(window);
    if (!sp) return;
    
    // Vertical dragging
    if (sp->v_is_dragging) {
        int sy, sh;
        tvg_shape_get_bounds(sp->v_scrollbar, NULL, &sy, NULL, &sh);
        int handle_h = tvg_shape_get_h(sp->v_scroll_handle);
        
        int delta_y = (int)ypos - sp->drag_start_y;
        int scroll_range = sh - handle_h;
        int scroll_per_pixel = scroll_range > 0 ? sp->v_max_scroll / scroll_range : 0;
        
        sp->v_scroll_position = sp->v_drag_start_scroll + (delta_y * scroll_per_pixel);
        sp->v_scroll_position = sp->v_scroll_position < 0 ? 0 : 
                               sp->v_scroll_position > sp->v_max_scroll ? sp->v_max_scroll : 
                               sp->v_scroll_position;
    }
    
    // Horizontal dragging
    if (sp->h_is_dragging) {
        int sx, sw;
        tvg_shape_get_bounds(sp->h_scrollbar, &sx, NULL, &sw, NULL);
        int handle_w = tvg_shape_get_w(sp->h_scroll_handle);
        
        int delta_x = (int)xpos - sp->drag_start_x;
        int scroll_range = sw - handle_w;
        int scroll_per_pixel = scroll_range > 0 ? sp->h_max_scroll / scroll_range : 0;
        
        sp->h_scroll_position = sp->h_drag_start_scroll + (delta_x * scroll_per_pixel);
        sp->h_scroll_position = sp->h_scroll_position < 0 ? 0 : 
                               sp->h_scroll_position > sp->h_max_scroll ? sp->h_max_scroll : 
                               sp->h_scroll_position;
    }
    
    if (sp->v_is_dragging || sp->h_is_dragging) {
        scrollpane_set_content_size(sp, sp->content_width, sp->content_height);
        // scrollpane_update(sp);
    }
}

void scrollpane_destroy(ScrollPane* sp) {
    if (!sp) return;
    tvg_paint_del(sp->v_scrollbar);
    tvg_paint_del(sp->v_scroll_handle);
    tvg_paint_del(sp->h_scrollbar);
    tvg_paint_del(sp->h_scroll_handle);
    free(sp);
}

/*
int main() {
    if (!glfwInit()) return -1;
    
    GLFWwindow* window = glfwCreateWindow(800, 600, "ScrollPane", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    
    tvg_engine_init(TVG_ENGINE_SW, 0);
    Tvg_Canvas* canvas = tvg_swcanvas_create();
    // Set canvas target here
    
    ScrollPane* sp = scrollpane_create(0, 0, 300, 400);
    sp->canvas = canvas;
    scrollpane_set_content_size(sp, 600, 800); // Wide and tall content
    
    glfwSetWindowUserPointer(window, sp);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    
    while (!glfwWindowShouldClose(window)) {
        scrollpane_update(sp);
        // Render canvas here
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    scrollpane_destroy(sp);
    tvg_canvas_destroy(canvas);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
*/