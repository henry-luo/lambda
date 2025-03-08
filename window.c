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
int WINDOW_WIDTH = 400;
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
    img_surface->pitch = pixel_width * 4;
    img_surface->pixels = calloc(pixel_width * pixel_height * 4, sizeof(uint32_t));
    if (!img_surface->pixels) {
        fprintf(stderr, "Error: Could not allocate memory for the image surface.\n");
        free(img_surface);
        return NULL;
    }
    return img_surface;
}

ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels) {
    if (pixel_width <= 0 || pixel_height <= 0 || !pixels) {
        fprintf(stderr, "Error: Invalid image surface dimensions or pixels.\n");
        return NULL;
    }
    ImageSurface* img_surface = calloc(1, sizeof(ImageSurface));
    if (img_surface) {
        img_surface->width = pixel_width;  img_surface->height = pixel_height;
        img_surface->pitch = pixel_width * 4;
        img_surface->pixels = pixels;
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

int ui_context_init(UiContext* uicon, int window_width, int window_height) {
    memset(uicon, 0, sizeof(UiContext));

    setlocale(LC_ALL, "");  // Set locale to support Unicode (input)

    // init FreeType
    if (FT_Init_FreeType(&uicon->ft_library)) {
        fprintf(stderr, "Could not initialize FreeType library\n");
        return EXIT_FAILURE;
    }
    // init Fontconfig
    uicon->font_config = FcInitLoadConfigAndFonts();
    if (!uicon->font_config) {
        fprintf(stderr, "Failed to initialize Fontconfig\n");
        return EXIT_FAILURE;
    }

    if (!glfwInit()) {
        fprintf(stderr, "Error: Could not initialize GLFW.\n");
        return EXIT_FAILURE;
    }

    // create a window and its OpenGL context
    uicon->window = glfwCreateWindow(window_width, window_height, "FreeType and GLFW Text Rendering", NULL, NULL);
    if (!uicon->window) {
        fprintf(stderr, "Error: Could not create GLFW window.\n");
        return EXIT_FAILURE;
    }

    // get logical and actual pixel ratio
    int pixel_w, pixel_h;
    glfwGetFramebufferSize(uicon->window, &pixel_w, &pixel_h);
    float scale_x = (float)pixel_w / window_width;
    float scale_y = (float)pixel_h / window_height;
    printf("Scale Factor: %.2f x %.2f\n", scale_x, scale_y);
    uicon->pixel_ratio = scale_x;   
    uicon->window_width = pixel_w;  uicon->window_height = pixel_h;
    default_font_prop.font_size = 16 * uicon->pixel_ratio;

    // init ThorVG engine
    tvg_engine_init(TVG_ENGINE_SW, 1);    
    uicon->canvas = tvg_swcanvas_create();

    // creates the surface for rendering
    ui_context_create_surface(uicon, uicon->window_width, uicon->window_height);  
    return EXIT_SUCCESS; 
}

void ui_context_cleanup(UiContext* uicon) {
    printf("Cleaning up UI context\n");
    if (uicon->document) {
        if (uicon->document->dom_tree) {
            lxb_html_document_destroy(uicon->document->dom_tree);
        }
        if (uicon->document->view_tree) {
            view_pool_destroy(uicon->document->view_tree);
            free(uicon->document->view_tree);
        }
        free(uicon->document);
    }
    printf("Cleaning up fonts\n");
    fontface_cleanup(uicon);  // free font cache
    FT_Done_FreeType(uicon->ft_library);
    FcConfigDestroy(uicon->font_config);
    image_cache_cleanup(uicon);  // cleanup image cache
    
    tvg_canvas_destroy(uicon->canvas);
    tvg_engine_term(TVG_ENGINE_SW);
    image_surface_destroy(uicon->surface);

    //if (uicon->mouse_state.sdl_cursor) {
        // SDL_DestroyCursor(uicon->mouse_state.sdl_cursor);
    //}
    glfwDestroyWindow(uicon->window);
    glfwTerminate();
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
            int index = (i * WINDOW_WIDTH * 2 + j) * 4;
            bitmap[index + 0] = 255; // Alpha
            bitmap[index + 1] = 0; // Blue 
            bitmap[index + 2] = y; // Green
            bitmap[index + 3] = j - x; // Red
        }
    }
}

void render_rectangles(unsigned char *bitmap, int width) {
    int RECT_WIDTH = 120;
    int RECT_HEIGHT = 30;
    int GAP = 15;
    int NUM_RECTANGLES = 50;    
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
    assert(width == WINDOW_WIDTH * ui_context.pixel_ratio && height == WINDOW_HEIGHT * ui_context.pixel_ratio);

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

    render_to_screen(window);

    double end = glfwGetTime();
    // printf("Render time: %.4f ms\n", (end - start) * 1000);

    // Swap front and back buffers
    glfwSwapBuffers(window);
    glFinish(); // important, this waits until rendering result is actually visible, thus making resizing less ugly
}

int main() {
    ui_context_init(&ui_context, WINDOW_WIDTH, WINDOW_HEIGHT);
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

    ui_context.document = show_html_doc("test/sample.html");

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
