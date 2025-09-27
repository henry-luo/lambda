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

lxb_url_t* get_current_dir_lexbor();
void render(GLFWwindow* window);
void render_html_doc(UiContext* uicon, View* root_view);
Document* load_html_doc(lxb_url_t *base, char* doc_filename);
View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void handle_event(UiContext* uicon, Document* doc, RdtEvent* event);

int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

// Window dimensions
bool do_redraw = false;
UiContext ui_context;

Document* show_html_doc(lxb_url_t *base, char* doc_url) {
    printf("Showing HTML document %s\n", doc_url);
    Document* doc = load_html_doc(base, doc_url);
    ui_context.document = doc;
    // layout html doc 
    if (doc->dom_tree) {
        layout_html_doc(&ui_context, doc, false);
    }    
    // render html doc
    if (doc && doc->view_tree && doc->view_tree->root) { 
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

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
    }
}

// handles mouse/touchpad scroll input 
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    RdtEvent event;
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = glfwGetTime();
    event.scroll.xoffset = xoffset * ui_context.pixel_ratio;
    event.scroll.yoffset = yoffset * ui_context.pixel_ratio;
    printf("Scroll offset: (%.2f, %.2f)\n", xoffset, yoffset);
    assert(xoffset != 0 || yoffset != 0);
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    printf("Mouse position: (%.2f, %.2f)\n", xpos, ypos);
    event.scroll.x = xpos * ui_context.pixel_ratio;
    event.scroll.y = ypos * ui_context.pixel_ratio;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
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
    printf("Requesting repaint\n");
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
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ui_context.surface->pixels);
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
        printf("Reflow time: %.2f ms\n", (glfwGetTime() - start_time) * 1000);
    }
    // rerender if the document is dirty
    if (ui_context.document->state && ui_context.document->state->is_dirty) {
        render_html_doc(&ui_context, ui_context.document->view_tree->root);
    }

    // repaint to screen
    repaint_window();

    double end = glfwGetTime();
    // printf("Render time: %.4f ms\n", (end - start) * 1000);

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

lxb_url_t* get_current_dir_lexbor() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }
    
    // Convert to file:// URL format
    char file_url[1200];
    snprintf(file_url, sizeof(file_url), "file://%s/", cwd);
    
    // Create memory pool for URL parsing
    lexbor_mraw_t *mraw = lexbor_mraw_create();
    if (!mraw) {
        return NULL;
    }
    
    lxb_status_t status = lexbor_mraw_init(mraw, 1024 * 16);
    if (status != LXB_STATUS_OK) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }
    
    lxb_url_parser_t *parser = lxb_url_parser_create();
    if (!parser) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }
    
    status = lxb_url_parser_init(parser, mraw);
    if (status != LXB_STATUS_OK) {
        lxb_url_parser_destroy(parser, true);
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }
    
    lxb_url_t *url = lxb_url_parse(parser, NULL, (const lxb_char_t*)file_url, strlen(file_url));
    
    lxb_url_parser_destroy(parser, false);
    // Note: don't destroy mraw here as url depends on it
    
    return url;
}

// Layout test function for headless testing
int run_layout_test(const char* html_file) {
    printf("Radiant Layout Test Mode\n");
    printf("========================\n");
    printf("Testing file: %s\n\n", html_file);
    
    // Initialize without GUI
    log_init_wrapper();
    
    // Initialize UI context properly in headless mode
    if (ui_context_init(&ui_context, true) != 0) {
        fprintf(stderr, "Error: Failed to initialize UI context\n");
        return 1;
    }
    
    // Create surface for layout calculations (no actual rendering)
    ui_context_create_surface(&ui_context, ui_context.window_width, ui_context.window_height);
    
    // Get current directory for relative path resolution
    lxb_url_t* cwd = get_current_dir_lexbor();
    if (!cwd) {
        fprintf(stderr, "Error: Could not get current directory\n");
        ui_context_cleanup(&ui_context);
        return 1;
    }
    
    // Load HTML document
    printf("Loading HTML document...\n");
    Document* doc = load_html_doc(cwd, (char*)html_file);
    if (!doc) {
        fprintf(stderr, "Error: Could not load HTML file: %s\n", html_file);
        lxb_url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }
    
    ui_context.document = doc;
    
    // Layout the document
    printf("Performing layout...\n");
    layout_html_doc(&ui_context, doc, false);
    
    printf("Layout completed successfully!\n\n");
    
    // Print view tree (existing functionality)
    if (doc->view_tree && doc->view_tree->root) {
        print_view_tree((ViewGroup*)doc->view_tree->root, ui_context.pixel_ratio);
    } else {
        printf("Warning: No view tree generated\n");
    }
    
    // Cleanup
    lxb_url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    log_cleanup();
    
    printf("\nLayout test completed successfully!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    // Check for layout sub-command
    if (argc >= 3 && strcmp(argv[1], "layout") == 0) {
        return run_layout_test(argv[2]);
    }
    
    // Check for help
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Radiant HTML/CSS Layout Engine\n");
        printf("Usage:\n");
        printf("  %s                    # Run GUI mode (default)\n", argv[0]);
        printf("  %s layout <file.html> # Run layout test on HTML file\n", argv[0]);
        printf("  %s --help            # Show this help\n", argv[0]);
        return 0;
    }
    
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

    lxb_url_t* cwd = get_current_dir_lexbor();
    if (cwd) {
        show_html_doc(cwd, "test/html/index.html");
        lxb_url_destroy(cwd);
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
