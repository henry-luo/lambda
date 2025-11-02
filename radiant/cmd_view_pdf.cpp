/**
 * PDF Viewer Command for Lambda
 *
 * Implements 'lambda view <file.pdf>' to open PDF in a window
 * Uses existing radiant window infrastructure
 */

#include "../radiant/view.hpp"
#include "../radiant/pdf/pdf_to_view.hpp"
#include "../lambda/input/input.h"
#include "../lib/log.h"
#include "../lib/mempool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External functions
void parse_pdf(Input* input, const char* pdf_data); // From input-pdf.cpp
int ui_context_init(UiContext* uicon, bool headless); // From window.cpp
void ui_context_cleanup(UiContext* uicon); // From window.cpp
void ui_context_create_surface(UiContext* uicon, int width, int height); // From window.cpp
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file); // From window.cpp
FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint, bool for_rendering); // From font.cpp
FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style); // From font.cpp

// External declarations
extern bool do_redraw;

// Helper function: render text string using FreeType
static void render_text_gl(UiContext* uicon, const char* text, float x, float y, float size, float r, float g, float b) {
    // Get or load a font face
    FontProp font_style = uicon->default_font;
    font_style.font_size = size;

    FT_Face face = load_styled_font(uicon, "Arial", &font_style);
    if (!face) {
        log_warn("No font face available for text rendering");
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor3f(r, g, b);

    float pen_x = x;
    float pen_y = y;

    for (const char* p = text; *p; p++) {
        // Load character glyph
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) {
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;

        // Create texture from glyph bitmap
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_ALPHA,
            glyph->bitmap.width,
            glyph->bitmap.rows,
            0,
            GL_ALPHA,
            GL_UNSIGNED_BYTE,
            glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        float xpos = pen_x + glyph->bitmap_left;
        float ypos = pen_y - glyph->bitmap_top;
        float w = glyph->bitmap.width;
        float h = glyph->bitmap.rows;

        // Render textured quad
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(xpos, ypos);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(xpos + w, ypos);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(xpos + w, ypos + h);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(xpos, ypos + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glDeleteTextures(1, &texture);

        // Advance cursor
        pen_x += (glyph->advance.x >> 6);
    }

    glDisable(GL_BLEND);
}// Callback implementations for PDF viewer
static void key_callback_pdf(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Handle ESC key to close window
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

static void cursor_position_callback_pdf(GLFWwindow* window, double xpos, double ypos) {
    // Handle mouse movement (for future panning/zoom)
    // Currently a no-op
}

static void character_callback_pdf(GLFWwindow* window, unsigned int codepoint) {
    // Handle character input (for future search/navigation)
    // Currently a no-op
}

static void mouse_button_callback_pdf(GLFWwindow* window, int button, int action, int mods) {
    // Handle mouse clicks (for future link navigation)
    // Currently a no-op
}

static void scroll_callback_pdf(GLFWwindow* window, double xoffset, double yoffset) {
    // Handle scrolling (for future zoom/pan)
    // Currently a no-op
}

static void framebuffer_size_callback_pdf(GLFWwindow* window, int width, int height) {
    // Update viewport when window is resized
    glViewport(0, 0, width, height);
    do_redraw = true;
}

static void window_refresh_callback_pdf(GLFWwindow* window) {
    // Get UI context from window user pointer
    UiContext* uicon = (UiContext*)glfwGetWindowUserPointer(window);
    if (!uicon) {
        log_warn("window_refresh_callback_pdf: missing context");
        return;
    }

    log_debug("Rendering frame...");

    // Get window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // For now, just render a simple colored screen
    // TODO: Enable full PDF rendering once parse_pdf is fixed

    // Clear with light blue background to show something is working
    glClearColor(0.85f, 0.90f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set up orthographic projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Draw a white "page" rectangle to show PDF viewer UI
    float page_width = 600;
    float page_height = 800;
    float x = (width - page_width) / 2;
    float y = (height - page_height) / 2;

    // Draw white page background
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    // Draw a border around the page
    glColor3f(0.3f, 0.3f, 0.3f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    // Draw some test content to show rendering is working

    // Title bar (blue)
    glColor3f(0.2f, 0.4f, 0.8f);
    glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + 60);
        glVertex2f(x, y + 60);
    glEnd();

    // Render title text in white
    render_text_gl(uicon, "Lambda PDF Viewer", x + 20, y + 40, 24, 1.0f, 1.0f, 1.0f);

    // Red rectangle (simulating an image or shape)
    glColor3f(0.9f, 0.2f, 0.2f);
    glBegin(GL_QUADS);
        glVertex2f(x + 50, y + 100);
        glVertex2f(x + 250, y + 100);
        glVertex2f(x + 250, y + 250);
        glVertex2f(x + 50, y + 250);
    glEnd();

    // Green rectangle
    glColor3f(0.2f, 0.8f, 0.3f);
    glBegin(GL_QUADS);
        glVertex2f(x + 300, y + 100);
        glVertex2f(x + 550, y + 100);
        glVertex2f(x + 550, y + 180);
        glVertex2f(x + 300, y + 180);
    glEnd();

    // Render actual text content instead of gray bars
    const char* sample_lines[] = {
        "This is a demonstration of text rendering.",
        "Lambda Script is a functional language for",
        "document processing and data transformation.",
        "",
        "Key Features:",
        "  - Pure functional programming",
        "  - JIT compilation via MIR",
        "  - Multi-format document support",
        "  - Advanced type system"
    };

    for (int i = 0; i < 9; i++) {
        float line_y = y + 320 + i * 35;
        render_text_gl(uicon, sample_lines[i], x + 50, line_y, 16, 0.2f, 0.2f, 0.2f);
    }

    // Bottom status bar (light gray)
    glColor3f(0.8f, 0.8f, 0.8f);
    glBegin(GL_QUADS);
        glVertex2f(x, y + page_height - 40);
        glVertex2f(x + page_width, y + page_height - 40);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    // Status bar text
    render_text_gl(uicon, "Page 1 of 1", x + 20, y + page_height - 15, 14, 0.3f, 0.3f, 0.3f);

    // Swap buffers
    glfwSwapBuffers(window);

    do_redraw = false;
}/**
 * Read file contents to string
 */
static char* read_pdf_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        log_error("Failed to open file: %s", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, size, file);
    content[bytes_read] = '\0';
    fclose(file);

    return content;
}

/**
 * View PDF file in window
 * Main entry point for 'lambda view <file.pdf>' command
 */
int view_pdf_in_window(const char* pdf_file) {
    log_info("Opening PDF file in viewer: %s", pdf_file);

    // TODO: Fix parse_pdf crash and integrate real PDF rendering
    // For now, just show a simple window with a page mockup
    log_info("Creating PDF viewer window (PDF parsing temporarily disabled)...");

    // Initialize UI context
    UiContext uicon;
    memset(&uicon, 0, sizeof(UiContext));

    if (ui_context_init(&uicon, false) != 0) {
        log_error("Failed to initialize UI context");
        return 1;
    }

    GLFWwindow* window = uicon.window;
    if (!window) {
        log_error("Failed to create window");
        ui_context_cleanup(&uicon);
        return 1;
    }    // Set up OpenGL context and callbacks (like window_main does)
    log_info("Setting up OpenGL context...");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    // Set window user pointer so callbacks can access uicon
    glfwSetWindowUserPointer(window, &uicon);

    // Set up event callbacks
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetKeyCallback(window, key_callback_pdf);
    glfwSetCharCallback(window, character_callback_pdf);
    glfwSetCursorPosCallback(window, cursor_position_callback_pdf);
    glfwSetMouseButtonCallback(window, mouse_button_callback_pdf);
    glfwSetScrollCallback(window, scroll_callback_pdf);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback_pdf);
    glfwSetWindowRefreshCallback(window, window_refresh_callback_pdf);

    // Set clear color
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f); // Light grey background

    // Initialize framebuffer
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback_pdf(window, width, height);

    log_info("OpenGL context initialized");

    // Set window title
    char title[512];
    snprintf(title, sizeof(title), "Lambda PDF Viewer - %s (Demo)", pdf_file);
    glfwSetWindowTitle(window, title);

    log_info("PDF viewer ready. Close window or press ESC to exit.");

    // Trigger initial draw
    do_redraw = true;    // Trigger initial draw
    do_redraw = true;

    // Main event loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (do_redraw) {
            window_refresh_callback_pdf(window);
        }

        // Limit FPS
        glfwWaitEventsTimeout(1.0 / 60.0);
    }

    // Cleanup
    log_info("Closing PDF viewer");
    ui_context_cleanup(&uicon);

    return 0;
}

/**
 * View HTML file in window (for compatibility)
 */
int view_html_in_window(const char* html_file) {
    log_info("HTML viewer not yet implemented");
    printf("HTML viewing not yet implemented. Use: ./radiant.exe\n");
    return 1;
}
