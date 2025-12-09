#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wchar.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../lib/log.h"
#include "layout.hpp"

void render(GLFWwindow* window);
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
DomDocument* load_html_doc(Url* base, char* doc_filename);
View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);

int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

// Forward declaration for internal function
int window_main_with_file(const char* html_file);

// Window dimensions
bool do_redraw = false;
UiContext ui_context;

DomDocument* show_html_doc(Url* base, char* doc_url) {
    log_debug("Showing HTML document %s", doc_url);
    DomDocument* doc = load_html_doc(base, doc_url);
    ui_context.document = doc;
    // layout html doc
    if (doc->root) {
        layout_html_doc(&ui_context, doc, false);
    }
    // render html doc
    if (doc && doc->view_tree) {
        log_debug("html version: %d", doc->view_tree->html_version);
        render_html_doc(&ui_context, doc->view_tree, NULL);
    }
    return doc;
}

void reflow_html_doc(DomDocument* doc) {
    if (!doc || !doc->root) {
        log_debug("No document to reflow");
        return;
    }
    layout_html_doc(&ui_context, doc, true);
    // render html doc
    if (doc->view_tree) {
        render_html_doc(&ui_context, doc->view_tree, NULL);
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
        // wprintf(L"Unicode codepoint: %u, %lc\n", codepoint, codepoint);
        log_debug("Unicode character entered: %u", codepoint);
    } else {
        log_debug("Character entered: %u, %c", codepoint, codepoint);
    }
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    RdtEvent event;
    log_debug("Cursor position: (%.1f, %.1f)", xpos, ypos);
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = glfwGetTime();
    event.mouse_position.x = xpos * ui_context.pixel_ratio;
    event.mouse_position.y = ypos * ui_context.pixel_ratio;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    RdtEvent event;
    event.mouse_button.type = action == GLFW_PRESS ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = glfwGetTime();
    event.mouse_button.button = button;

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        log_debug("Right mouse button pressed");
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        log_debug("Right mouse button released");
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        log_debug("Left mouse button pressed");
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        log_debug("Mouse position: (%.2f, %.2f)", xpos, ypos);
        ui_context.mouse_state.is_mouse_down = 1;
        ui_context.mouse_state.down_x = event.mouse_button.x = xpos * ui_context.pixel_ratio;
        ui_context.mouse_state.down_y = event.mouse_button.y = ypos * ui_context.pixel_ratio;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        log_debug("Left mouse button released");

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
    }
}

// handles mouse/touchpad scroll input
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    RdtEvent event;
    log_debug("Scroll_callback");
    log_enter();
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = glfwGetTime();
    event.scroll.xoffset = xoffset * ui_context.pixel_ratio;
    event.scroll.yoffset = yoffset * ui_context.pixel_ratio;
    log_debug("Scroll offset: (%.1f, %.1f)", xoffset, yoffset);
    assert(xoffset != 0 || yoffset != 0);
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    log_debug("Mouse position: (%.1f, %.1f)", xpos, ypos);
    event.scroll.x = xpos * ui_context.pixel_ratio;
    event.scroll.y = ypos * ui_context.pixel_ratio;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
    log_leave();
}

// Callback function to handle window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    log_debug("Window resized to: %d x %d", width, height);
    do_redraw = 1;
}

void window_refresh_callback(GLFWwindow *window) {
    render(window);
    do_redraw = 0;
}

void to_repaint() {
    log_debug("Requesting repaint");
    do_redraw = 1;
}

void repaint_window() {
    // test rendering
    // render_rectangles(ui_context.surface->pixels, ui_context.surface->width);

    // generate a texture from the bitmap
    log_debug("creating rendering texture");
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ui_context.surface->width, ui_context.surface->height, 0,
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ui_context.surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // render the texture as a quad
    log_debug("rendering texture");
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

    // reflow the document if window size has changed
    if (width != ui_context.window_width || height != ui_context.window_height) {
        double start_time = glfwGetTime();
        ui_context.window_width = width;  ui_context.window_height = height;
        // resize the surface
        ui_context_create_surface(&ui_context, width, height);
        // reflow the document
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);
        }
        log_debug("Reflow time: %.2f ms", (glfwGetTime() - start_time) * 1000);
    }
    // rerender if the document is dirty
    if (ui_context.document->state && ui_context.document->state->is_dirty) {
        render_html_doc(&ui_context, ui_context.document->view_tree, NULL);
    }

    // repaint to screen
    repaint_window();

    double end = glfwGetTime();
    // log_debug("Render time: %.4f ms", (end - start) * 1000);

    // Swap front and back buffers
    glfwSwapBuffers(window);
    glFinish(); // important, this waits until rendering result is actually visible, thus making resizing less ugly
}

void log_init_wrapper() {
    // empty existing log file
    FILE *file = fopen("log.txt", "w");
    if (file ) { fclose(file); }
    log_parse_config_file("log.conf");
}
void log_cleanup() {
    log_finish();
}

// Layout test function for headless testing
int run_layout(const char* html_file) {
    log_debug("Radiant Layout Test Mode");
    log_debug("Testing file: %s", html_file);

    // Initialize without GUI
    log_init_wrapper();

    // Initialize UI context properly in headless mode
    if (ui_context_init(&ui_context, true) != 0) {
        log_error("Error: Failed to initialize UI context");
        return 1;
    }

    // Create surface for layout calculations (no actual rendering)
    ui_context_create_surface(&ui_context, ui_context.window_width, ui_context.window_height);

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_error("Error: Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load HTML document
    log_debug("Loading HTML document...");
    DomDocument* doc = load_html_doc(cwd, (char*)html_file);
    if (!doc) {
        log_error("Error: Could not load HTML file: %s", html_file);
        url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Layout the document
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);
    log_debug("Layout completed successfully!");

    // Cleanup
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    log_cleanup();
    return 0;
}

// Internal function that accepts an optional HTML file to display
int view_html_in_window(const char* html_file) {
    // Original GUI mode
    log_init_wrapper();
    ui_context_init(&ui_context, false);
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

    Url* cwd = get_current_dir();
    if (cwd) {
        // Use provided HTML file or default to test file
        const char* file_to_load = html_file ? html_file : "test/html/index.html";
        DomDocument* doc = show_html_doc(cwd, (char*)file_to_load);
        url_destroy(cwd);

        // Set custom window title if HTML file was provided
        if (html_file && doc) {
            char title[512];
            snprintf(title, sizeof(title), "Lambda HTML Viewer - %s", html_file);
            glfwSetWindowTitle(window, title);
        }
    }

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

    log_info("End of app");
    ui_context_cleanup(&ui_context);
    log_cleanup();
    return 0;
}

// Function to load and view markdown documents
// Similar to view_html_in_window but parses markdown first
int view_markdown_in_window(const char* markdown_file) {
    log_init_wrapper();
    ui_context_init(&ui_context, false);
    GLFWwindow* window = ui_context.window;
    if (!window) {
        ui_context_cleanup(&ui_context);
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, character_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);

    glClearColor(0.8f, 0.8f, 0.8f, 1.0f); // Light grey color

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback(window, width, height);

    Url* cwd = get_current_dir();
    if (cwd && markdown_file) {
        log_debug("Loading markdown file: %s", markdown_file);
        
        Pool* pool = pool_create();
        if (!pool) {
            log_error("Failed to create memory pool for markdown");
            url_destroy(cwd);
            ui_context_cleanup(&ui_context);
            return -1;
        }
        
        // Parse markdown file URL
        Url* markdown_url = url_parse_with_base(markdown_file, cwd);
        if (!markdown_url) {
            log_error("Failed to parse markdown URL: %s", markdown_file);
            pool_destroy(pool);
            url_destroy(cwd);
            ui_context_cleanup(&ui_context);
            return -1;
        }
        
        // Load markdown document (parses markdown, builds DOM, applies CSS)
        DomDocument* doc = load_markdown_doc(markdown_url, width, height, pool);
        if (!doc) {
            log_error("Failed to load markdown document: %s", markdown_file);
            pool_destroy(pool);
            url_destroy(cwd);
            ui_context_cleanup(&ui_context);
            return -1;
        }
        
        ui_context.document = doc;
        
        // Layout markdown doc
        if (doc->root) {
            layout_html_doc(&ui_context, doc, false);
        }
        
        // Render markdown doc
        if (doc && doc->view_tree) {
            render_html_doc(&ui_context, doc->view_tree, NULL);
        }
        
        url_destroy(cwd);
        
        // Set custom window title
        char title[512];
        snprintf(title, sizeof(title), "Lambda Markdown Viewer - %s", markdown_file);
        glfwSetWindowTitle(window, title);
        
        // Main loop
        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime();
            
            glfwPollEvents();
            
            if (do_redraw) {
                window_refresh_callback(window);
            }
            
            glfwWaitEventsTimeout(1.0 / 60.0);
        }
    }

    log_info("End of markdown viewer");
    ui_context_cleanup(&ui_context);
    log_cleanup();
    return 0;
}

int window_main(int argc, char* argv[]) {
    // render the default index.html
    return view_html_in_window(NULL);
}
