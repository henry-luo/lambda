#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wchar.h>
#include <assert.h>
#include <stdbool.h>
#include "layout.h"

void render(GLFWwindow* window);
void render_html_doc(UiContext* uicon, View* root_view);
void parse_html_doc(Document* doc, const char* doc_path);
View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void handle_event(UiContext* uicon, Document* doc, RdtEvent* event);

int ui_context_init(UiContext* uicon);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

// Window dimensions
bool do_redraw = false;
UiContext ui_context;

Document* show_html_doc(char* doc_filename) {
    Document* doc = calloc(1, sizeof(Document));
    parse_html_doc(doc, doc_filename);
    
    // layout html doc 
    if (doc->dom_tree) {
        layout_html_doc(&ui_context, doc, false);
    }
    // render html doc
    if (doc->view_tree && doc->view_tree->root) { 
        render_html_doc(&ui_context, doc->view_tree->root); 
    }
    return doc;
}

void reflow_html_doc(Document* doc) {
    if (!doc || !doc->dom_tree) {
        printf("No document to reflow\n");
        return;
    }
    layout_html_doc(&ui_context, doc, true);
    // render html doc
    if (doc->view_tree->root) {
        render_html_doc(&ui_context, doc->view_tree->root);
    }
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
    // codepoint in UFT32
    if (codepoint > 127) {
        // wchar_t unicodeChar = codepoint;
        wprintf(L"Unicode codepoint: %u, %lc\n", codepoint, codepoint);
    } else {
        printf("Character entered: %u, %c\n", codepoint, codepoint);
    }
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    RdtEvent event;
    printf("Cursor position: (%.2f, %.2f)\n", xpos, ypos);
    event.mouse_motion.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_motion.timestamp = glfwGetTime();
    event.mouse_motion.x = xpos * ui_context.pixel_ratio;
    event.mouse_motion.y = ypos * ui_context.pixel_ratio;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    RdtEvent event;
    event.mouse_button.type = action == GLFW_PRESS ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = glfwGetTime();
    event.mouse_button.button = button;

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        printf("Right mouse button pressed\n");
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
        printf("Right mouse button released\n");
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        printf("Left mouse button pressed\n");
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        printf("Mouse position: (%.2f, %.2f)\n", xpos, ypos);
        ui_context.mouse_state.is_mouse_down = 1;
        ui_context.mouse_state.down_x = event.mouse_button.x = xpos * ui_context.pixel_ratio;
        ui_context.mouse_state.down_y = event.mouse_button.y = ypos * ui_context.pixel_ratio;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        printf("Left mouse button released\n");

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    printf("Scroll offset: (%.2f, %.2f)\n", xoffset, yoffset);
}

// Callback function to handle window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    printf("Window resized to: %d x %d\n", width, height);
    do_redraw = 1;
}

void window_refresh_callback(GLFWwindow *window) {
    render(window);
    do_redraw = 0;
}

void to_repaint() {
    do_redraw = 1;
}

void repaint_window() {
    // test rendering
    // render_rectangles(ui_context.surface->pixels, ui_context.surface->width);

    // generate a texture from the bitmap
    printf("creating rendering texture\n");
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ui_context.surface->width, ui_context.surface->height, 0, 
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ui_context.surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // render the texture as a quad
    printf("rendering texture\n");
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1, -1);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1, -1);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1, 1);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // cleanup
    glDeleteTextures(1, &texture);
}

void render(GLFWwindow* window) {
    double start = glfwGetTime();

    // get window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    if (width != ui_context.window_width || height != ui_context.window_height) {
        double start_time = glfwGetTime();
        ui_context.window_width = width;  ui_context.window_height = height;
        // resize the surface
        ui_context_create_surface(&ui_context, width, height);
        // reflow the document
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);   
        }
        printf("Reflow time: %.2f ms\n", (glfwGetTime() - start_time) * 1000);
    }

    repaint_window();

    double end = glfwGetTime();
    // printf("Render time: %.4f ms\n", (end - start) * 1000);

    // Swap front and back buffers
    glfwSwapBuffers(window);
    glFinish(); // important, this waits until rendering result is actually visible, thus making resizing less ugly
}

int main() {
    ui_context_init(&ui_context);
    GLFWwindow* window = ui_context.window;
    if (!window) {
        ui_context_cleanup(&ui_context);
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // receive the state of the Caps Lock and Num Lock keys
    glfwSetKeyCallback(window, key_callback);  // receive raw keyboard input
    glfwSetCharCallback(window, character_callback);  // receive character input
    glfwSetCursorPosCallback(window, cursor_position_callback);  // receive cursor/mouse position
    glfwSetMouseButtonCallback(window, mouse_button_callback);  // receive mouse button input
    glfwSetScrollCallback(window, scroll_callback);  // receive mouse/touchpad scroll input    
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);

    glClearColor(0.8f, 0.8f, 0.8f, 1.0f); // Light grey color

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback(window, width, height);

    // set up the FPS
    double lastTime = glfwGetTime();
    double deltaTime = 0.0;
    int frames = 0;

    ui_context.document = show_html_doc("test/sample.html"); // 

    // main loop
    while (!glfwWindowShouldClose(window)) {
        // calculate deltaTime
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // poll for new events
        glfwPollEvents();

        // only redraw if we need to
        if (do_redraw) {
            window_refresh_callback(window);
        }

        // limit to 60 FPS
        if (deltaTime < (1.0 / 60.0)) {
            glfwWaitEventsTimeout((1.0 / 60.0) - deltaTime);
        }
        frames++;
    }

    ui_context_cleanup(&ui_context);
    return 0;
}
