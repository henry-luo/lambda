#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wchar.h>
#include <locale.h>
#include <assert.h>
#include <stdbool.h>
#include "layout.h"

void render(GLFWwindow* window);
void render_html_doc(UiContext* uicon, View* root_view);
void parse_html_doc(Document* doc, const char* doc_path);
View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void view_pool_destroy(ViewTree* tree);
void handle_event(UiContext* uicon, Document* doc, RdtEvent* event);
void fontface_cleanup(UiContext* uicon);
void image_cache_cleanup(UiContext* uicon);

// Window dimensions
int WINDOW_WIDTH = 800;
int WINDOW_HEIGHT = 600;
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

ImageSurface* image_surface_create(int pixel_width, int pixel_height) {
    if (pixel_width <= 0 || pixel_height <= 0) {
        fprintf(stderr, "Error: Invalid image surface dimensions.\n");
        return NULL;
    }
    ImageSurface* img_surface = calloc(1, sizeof(ImageSurface));
    img_surface->width = pixel_width;  img_surface->height = pixel_height;
    img_surface->pixels = calloc(pixel_width * pixel_height * 4, sizeof(uint32_t));
    if (!img_surface->pixels) {
        fprintf(stderr, "Error: Could not allocate memory for the image surface.\n");
        free(img_surface);
        return NULL;
    }
    return img_surface;
}

void image_surface_destroy(ImageSurface* img_surface) {
    if (img_surface) {
        if (img_surface->pixels) free(img_surface->pixels);
        free(img_surface);
    }
}

void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height) {
    // re-creates the surface for rendering, 32-bits per pixel, RGBA format
    if (uicon->surface) image_surface_destroy(uicon->surface);
    uicon->surface = image_surface_create(pixel_width, pixel_height);
    if (!uicon->surface) {
        fprintf(stderr, "Error: Could not create image surface.\n");
        return;
    }
    tvg_swcanvas_set_target(uicon->canvas, uicon->surface->pixels, 
        pixel_width, pixel_width, pixel_height, TVG_COLORSPACE_ABGR8888);          
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
    printf("Cursor position: (%.2f, %.2f)\n", xpos, ypos);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        printf("Right mouse button pressed\n");
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
        printf("Right mouse button released\n");
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        printf("Left mouse button pressed\n");
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        printf("Left mouse button released\n");
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    printf("Scroll offset: (%.2f, %.2f)\n", xoffset, yoffset);
}

// Callback function to handle window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    printf("Window resized to: %d x %d\n", width, height);
    WINDOW_WIDTH = width / 2;
    WINDOW_HEIGHT = height / 2;
    do_redraw = 1;
}

void window_refresh_callback(GLFWwindow *window) {
    render(window);
    do_redraw = 0;
}

void fill_rect(unsigned char *bitmap, int x, int y, int width, int height) {
    // file a rect in the bitmap at x, y with width and height
    for (int i = y; i < y + height; i++) {
        for (int j = x; j < x + width; j++) {
            int index = (i * WINDOW_WIDTH + j) * 4;
            bitmap[index + 0] = x; // Red
            bitmap[index + 1] = y; // Green
            bitmap[index + 2] = 0; // Blue
            bitmap[index + 3] = 255; // Alpha
        }
    }
}

#define RECT_WIDTH 120
#define RECT_HEIGHT 30
#define GAP 15
#define NUM_RECTANGLES 50

void render_rectangles(unsigned char *bitmap, int width) {
    int x = GAP, y = GAP;
    for (int i = 0; i < NUM_RECTANGLES; i++) {
        if (x + RECT_WIDTH > width) { // Wrap to next row if out of bounds using width parameter
            x = GAP;
            y += RECT_HEIGHT + GAP;
        }
        fill_rect(bitmap, x, y, RECT_WIDTH, RECT_HEIGHT);
        x += RECT_WIDTH + GAP;
    }
}

void render_to_screen(GLFWwindow *window) {
    int canvas_width = WINDOW_WIDTH, canvas_height = WINDOW_HEIGHT;
    // Create a large buffer for the bitmap
    unsigned char *bitmap = (unsigned char *)calloc(canvas_width * canvas_height * 4, sizeof(unsigned char));
    if (!bitmap) {
        fprintf(stderr, "Error: Could not allocate memory for the big bitmap.\n");
        return;
    }

    // render to the bitmap
    render_rectangles(bitmap, WINDOW_WIDTH);

    // generate a texture from the bitmap
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas_width, canvas_height, 0, 
        GL_RGBA, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // render the texture as a quad
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
    free(bitmap);
    glDeleteTextures(1, &texture);
}

void render(GLFWwindow* window) {
    double start = glfwGetTime();

    // get window size
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    assert(width == WINDOW_WIDTH && height == WINDOW_HEIGHT);

    render_to_screen(window);

    double end = glfwGetTime();
    // printf("Render time: %.4f ms\n", (end - start) * 1000);

    // Swap front and back buffers
    glfwSwapBuffers(window);
    glFinish(); // important, this waits until rendering result is actually visible, thus making resizing less ugly
}

int main() {
    setlocale(LC_ALL, "");  // Set locale to support Unicode (input)

    if (!glfwInit()) {
        fprintf(stderr, "Error: Could not initialize GLFW.\n");
        return -1;
    }

    // create a window and its OpenGL context
    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FreeType and GLFW Text Rendering", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Error: Could not create GLFW window.\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // receive the state of the Caps Lock and Num Lock keys
    glfwSetKeyCallback(window, key_callback);  // receive raw keyboard input
    glfwSetCharCallback(window, character_callback);  // receive character input
    // glfwSetCursorPosCallback(window, cursor_position_callback);  // receive cursor position
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

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
