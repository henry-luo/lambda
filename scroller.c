
#include "layout.h"

#define SCROLLBAR_SIZE 15.0f  // Scrollbar thickness
#define MIN_THUMB_SIZE 20.0f  // Minimum scrollbar thumb size

void scroller_get_hscroll_bounds(ScrollProp* pane, float* x, float* width);
void scroller_get_vscroll_bounds(ScrollProp* pane, float* y, float* height);
void scroller_update(ScrollProp* pane);

void scroller_scroll_callback(ScrollProp* pane, GLFWwindow* window, double xoffset, double yoffset) {
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    
    if (mouseX >= pane->x && mouseX <= pane->x + pane->width &&
        mouseY >= pane->y && mouseY <= pane->y + pane->height) {
        if (pane->has_hz_scroll && xoffset != 0) {
            pane->scrollX += xoffset * pane->scrollSpeed; // cast to int
        }
        if (pane->has_vt_scroll && yoffset != 0) {
            pane->scrollY -= yoffset * pane->scrollSpeed;
        }
        scroller_update(pane);
    }
}

// Mouse button callback for scrollbar dragging
void scroller_mouse_button_callback(ScrollProp* pane, GLFWwindow* window, int button, int action, int mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    
    if (action == GLFW_PRESS) {
        // Check horizontal scrollbar
        if (pane->has_hz_scroll) {
            float thumbX, thumbWidth;
            scroller_get_hscroll_bounds(pane, &thumbX, &thumbWidth);
            if (mouseX >= thumbX && mouseX <= thumbX + thumbWidth &&
                mouseY >= pane->y + pane->height - SCROLLBAR_SIZE &&
                mouseY <= pane->y + pane->height) {
                pane->draggingHScroll = true;
                pane->dragStartX = mouseX;
                pane->scrollStartX = pane->scrollX;
            }
        }
        
        // Check vertical scrollbar
        if (pane->has_vt_scroll) {
            float thumbY, thumbHeight;
            scroller_get_vscroll_bounds(pane, &thumbY, &thumbHeight);
            if (mouseX >= pane->x + pane->width - SCROLLBAR_SIZE &&
                mouseX <= pane->x + pane->width &&
                mouseY >= thumbY && mouseY <= thumbY + thumbHeight) {
                pane->draggingVScroll = true;
                pane->dragStartY = mouseY;
                pane->scrollStartY = pane->scrollY;
            }
        }
    } else if (action == GLFW_RELEASE) {
        pane->draggingHScroll = false;
        pane->draggingVScroll = false;
    }
}

// Mouse movement callback for dragging
void scroller_cursor_pos_callback(ScrollProp* pane, GLFWwindow* window, double xpos, double ypos) {
    if (pane->draggingHScroll) {
        float deltaX = xpos - pane->dragStartX;
        float scrollArea = pane->width - SCROLLBAR_SIZE * (pane->has_vt_scroll ? 2 : 1);
        float contentRange = pane->contentWidth - pane->width;
        pane->scrollX = pane->scrollStartX + (deltaX / scrollArea) * contentRange;
        scroller_update(pane);
    }
    if (pane->draggingVScroll) {
        float deltaY = (float)ypos - pane->dragStartY;
        float scrollArea = pane->height - SCROLLBAR_SIZE * (pane->has_hz_scroll ? 2 : 1);
        float contentRange = pane->contentHeight - pane->height;
        pane->scrollY = pane->scrollStartY + (deltaY / scrollArea) * contentRange;
        scroller_update(pane);
    }
}

// Get horizontal scrollbar thumb bounds
void scroller_get_hscroll_bounds(ScrollProp* pane, float* x, float* width) {
    float scrollArea = pane->width - (pane->has_vt_scroll ? SCROLLBAR_SIZE : 0);
    float thumbRatio = pane->width / pane->contentWidth;
    *width = fmaxf(MIN_THUMB_SIZE, scrollArea * thumbRatio);
    float scrollRange = scrollArea - *width;
    float scrollMax = pane->contentWidth - pane->width;
    *x = pane->x + (scrollRange * (pane->scrollX / scrollMax));
}

// Get vertical scrollbar thumb bounds
void scroller_get_vscroll_bounds(ScrollProp* pane, float* y, float* height) {
    float scrollArea = pane->height - (pane->has_hz_scroll ? SCROLLBAR_SIZE : 0);
    float thumbRatio = pane->height / pane->contentHeight;
    *height = fmaxf(MIN_THUMB_SIZE, scrollArea * thumbRatio);
    float scrollRange = scrollArea - *height;
    float scrollMax = pane->contentHeight - pane->height;
    *y = pane->y + (scrollRange * (pane->scrollY / scrollMax));
}

/*
int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(800, 600, "ScrollPane Example", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    
    ScrollPane pane = scroller_create(
        100.0f, 100.0f, 400.0f, 300.0f,
        800.0f, 600.0f, OVERFLOW_AUTO, OVERFLOW_AUTO
    );

    glfwSetScrollCallback(window, (GLFWscrollfun)scroller_scroll_callback, &pane);
    glfwSetMouseButtonCallback(window, 
        (GLFWmousebuttonfun)scroller_mouse_button_callback, &pane);
    glfwSetCursorPosCallback(window, 
        (GLFWcursorposfun)scroller_cursor_pos_callback, &pane);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render your content here with offsets -pane.scrollX, -pane.scrollY
        scroller_render(&pane);
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
*/