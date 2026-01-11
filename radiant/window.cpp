#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wchar.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include "../lib/log.h"
#include "layout.hpp"
#include "font_face.h"
#include "state_store.hpp"
#include "event_sim.hpp"
extern "C" {
#include "../lib/url.h"
}

void render(GLFWwindow* window);
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
// load_html_doc is declared in view.hpp (via layout.hpp)
DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio);
View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);

int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

// Document format detection
typedef enum {
    DOC_FORMAT_UNKNOWN,
    DOC_FORMAT_HTML,
    DOC_FORMAT_MARKDOWN,
    DOC_FORMAT_LATEX,
    DOC_FORMAT_XML,
    DOC_FORMAT_RST,
    DOC_FORMAT_WIKI,
    DOC_FORMAT_LAMBDA_SCRIPT,
    DOC_FORMAT_PDF,
    DOC_FORMAT_SVG,
    DOC_FORMAT_IMAGE,  // PNG, JPG, JPEG, GIF
    DOC_FORMAT_TEXT    // JSON, YAML, TOML, TXT, etc.
} DocFormat;

// Detect document format from file extension
static DocFormat detect_doc_format(const char* filename) {
    if (!filename) return DOC_FORMAT_UNKNOWN;

    const char* ext = strrchr(filename, '.');
    if (!ext) return DOC_FORMAT_UNKNOWN;

    ext++; // skip the '.'

    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
        return DOC_FORMAT_HTML;
    } else if (strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0) {
        return DOC_FORMAT_MARKDOWN;
    } else if (strcasecmp(ext, "tex") == 0 || strcasecmp(ext, "latex") == 0) {
        return DOC_FORMAT_LATEX;
    } else if (strcasecmp(ext, "xml") == 0) {
        return DOC_FORMAT_XML;
    } else if (strcasecmp(ext, "rst") == 0) {
        return DOC_FORMAT_RST;
    } else if (strcasecmp(ext, "wiki") == 0) {
        return DOC_FORMAT_WIKI;
    } else if (strcasecmp(ext, "ls") == 0) {
        return DOC_FORMAT_LAMBDA_SCRIPT;
    } else if (strcasecmp(ext, "pdf") == 0) {
        return DOC_FORMAT_PDF;
    } else if (strcasecmp(ext, "svg") == 0) {
        return DOC_FORMAT_SVG;
    } else if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 ||
               strcasecmp(ext, "jpeg") == 0 || strcasecmp(ext, "gif") == 0) {
        return DOC_FORMAT_IMAGE;
    } else if (strcasecmp(ext, "json") == 0 || strcasecmp(ext, "yaml") == 0 ||
               strcasecmp(ext, "yml") == 0 || strcasecmp(ext, "toml") == 0 ||
               strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "csv") == 0 ||
               strcasecmp(ext, "ini") == 0 || strcasecmp(ext, "conf") == 0 ||
               strcasecmp(ext, "cfg") == 0 || strcasecmp(ext, "log") == 0) {
        return DOC_FORMAT_TEXT;
    }

    return DOC_FORMAT_UNKNOWN;
}

// Load document based on detected format
static DomDocument* load_doc_by_format(const char* filename, Url* base_url, int width, int height, Pool* pool) {
    DocFormat format = detect_doc_format(filename);

    switch (format) {
        case DOC_FORMAT_HTML:
            log_debug("Loading as HTML document");
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_MARKDOWN: {
            log_debug("Loading as Markdown document");
            Url* doc_url = url_parse_with_base(filename, base_url);
            if (!doc_url) {
                log_error("Failed to parse document URL: %s", filename);
                return NULL;
            }
            DomDocument* doc = load_markdown_doc(doc_url, width, height, pool);
            return doc;
        }

        case DOC_FORMAT_LATEX: {
            log_debug("Loading as LaTeX document");

            // Check environment variable to select pipeline
            // LAMBDA_TEX_PIPELINE=1 uses direct LaTeX→TeX→PDF pipeline
            // Default (unset or 0) uses LaTeX→HTML→CSS pipeline
            const char* use_tex = getenv("LAMBDA_TEX_PIPELINE");
            if (use_tex && strcmp(use_tex, "1") == 0) {
                log_info("Using TeX typesetting pipeline for LaTeX");
                Url* doc_url = url_parse_with_base(filename, base_url);
                if (!doc_url) {
                    log_error("Failed to parse document URL: %s", filename);
                    return NULL;
                }
                extern DomDocument* load_latex_doc_tex(Url*, int, int, Pool*, float);
                return load_latex_doc_tex(doc_url, width, height, pool, 1.0f);
            }

            // Default: use HTML conversion pipeline
            return load_html_doc(base_url, (char*)filename, width, height);
        }

        case DOC_FORMAT_XML:
            log_debug("Loading as XML document with CSS stylesheet");
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_RST:
            log_warn("RST format not yet implemented");
            return NULL;

        case DOC_FORMAT_LAMBDA_SCRIPT:
            log_debug("Loading as Lambda script document");
            // load_html_doc will detect .ls extension and route to load_lambda_script_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_WIKI: {
            log_debug("Loading as Wiki document");
            Url* doc_url = url_parse_with_base(filename, base_url);
            if (!doc_url) {
                log_error("Failed to parse document URL: %s", filename);
                return NULL;
            }
            DomDocument* doc = load_wiki_doc(doc_url, width, height, pool);
            return doc;
        }

        case DOC_FORMAT_PDF:
            log_debug("Loading as PDF document");
            // load_html_doc will detect .pdf extension and route to load_pdf_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_SVG:
            log_debug("Loading as SVG document");
            // load_html_doc will detect .svg extension and route to load_svg_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_IMAGE:
            log_debug("Loading as image document");
            // load_html_doc will detect image extensions and route to load_image_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_TEXT:
            log_debug("Loading as text document (source view)");
            // load_html_doc will detect text extensions and route to load_text_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        default:
            log_error("Unsupported document format for file: %s", filename);
            log_error("Supported formats: .html, .htm, .md, .markdown, .tex, .latex, .ls, .xml, .pdf, .svg, .png, .jpg, .jpeg, .gif, .json, .yaml, .yml, .toml, .txt, .csv, .ini, .conf, .cfg, .log");
            return NULL;
    }
}

// Get human-readable format name for window title
static const char* get_format_name(const char* filename) {
    DocFormat format = detect_doc_format(filename);
    switch (format) {
        case DOC_FORMAT_HTML: return "HTML";
        case DOC_FORMAT_MARKDOWN: return "Markdown";
        case DOC_FORMAT_LATEX: return "LaTeX";
        case DOC_FORMAT_XML: return "XML";
        case DOC_FORMAT_RST: return "RST";
        case DOC_FORMAT_WIKI: return "Wiki";
        case DOC_FORMAT_LAMBDA_SCRIPT: return "Lambda Script";
        case DOC_FORMAT_PDF: return "PDF";
        case DOC_FORMAT_SVG: return "SVG";
        case DOC_FORMAT_IMAGE: return "Image";
        case DOC_FORMAT_TEXT: return "Text";
        default: return "Document";
    }
}

// Window dimensions
bool do_redraw = false;
UiContext ui_context;

DomDocument* show_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height) {
    log_debug("Showing HTML document %s", doc_url);
    DomDocument* doc = load_html_doc(base, doc_url, viewport_width, viewport_height);
    if (!doc) return nullptr;

    // Set scale for window display: given_scale = 1.0, scale = pixel_ratio
    doc->given_scale = 1.0f;
    doc->scale = doc->given_scale * ui_context.pixel_ratio;

    ui_context.document = doc;

    // Create RadiantState for interactive state management (caret, selection, focus, etc.)
    if (!doc->state) {
        doc->state = radiant_state_create(doc->pool, STATE_MODE_IN_PLACE);
        log_debug("show_html_doc: created RadiantState for document");
    }

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

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
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    // Build keyboard event
    RdtEvent event;
    event.key.type = (action == GLFW_PRESS || action == GLFW_REPEAT) ? RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP;
    event.key.timestamp = glfwGetTime();
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.mods = 0;
    if (mods & GLFW_MOD_SHIFT) event.key.mods |= RDT_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) event.key.mods |= RDT_MOD_CTRL;
    if (mods & GLFW_MOD_ALT) event.key.mods |= RDT_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) event.key.mods |= RDT_MOD_SUPER;

    // Handle key events
    handle_event(&ui_context, ui_context.document, &event);
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
    // Build text input event
    RdtEvent event;
    event.text_input.type = RDT_EVENT_TEXT_INPUT;
    event.text_input.timestamp = glfwGetTime();
    event.text_input.codepoint = codepoint;

    if (codepoint > 127) {
        log_debug("Unicode character entered: U+%04X", codepoint);
    } else {
        log_debug("Character entered: %u '%c'", codepoint, codepoint);
    }

    handle_event(&ui_context, ui_context.document, &event);
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    RdtEvent event;
    log_debug("Cursor position: (%.1f, %.1f)", xpos, ypos);
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = glfwGetTime();
    // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
    event.mouse_position.x = xpos;
    event.mouse_position.y = ypos;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    log_info("MOUSE_BUTTON_CALLBACK: button=%d action=%d mods=%d", button, action, mods);
    RdtEvent event;
    event.mouse_button.type = action == GLFW_PRESS ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = glfwGetTime();
    event.mouse_button.button = button;

    // Get cursor position for all mouse button events
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    event.mouse_button.x = xpos;
    event.mouse_button.y = ypos;

    // Map GLFW modifiers to RDT modifiers
    event.mouse_button.mods = 0;
    if (mods & GLFW_MOD_SHIFT) event.mouse_button.mods |= RDT_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) event.mouse_button.mods |= RDT_MOD_CTRL;
    if (mods & GLFW_MOD_ALT) event.mouse_button.mods |= RDT_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) event.mouse_button.mods |= RDT_MOD_SUPER;

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        log_debug("Right mouse button pressed");
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        log_debug("Right mouse button released");
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        log_debug("Left mouse button pressed at (%.2f, %.2f)", xpos, ypos);
        ui_context.mouse_state.is_mouse_down = 1;
        // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
        ui_context.mouse_state.down_x = xpos;
        ui_context.mouse_state.down_y = ypos;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        log_debug("Left mouse button released");

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
        do_redraw = 1;  // trigger repaint after mouse click
    }
}

// handles mouse/touchpad scroll input
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    RdtEvent event;
    log_debug("Scroll_callback");
    log_enter();
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = glfwGetTime();
    // Scroll offset can stay as-is (relative motion)
    event.scroll.xoffset = xoffset;
    event.scroll.yoffset = yoffset;
    log_debug("Scroll offset: (%.1f, %.1f)", xoffset, yoffset);
    assert(xoffset != 0 || yoffset != 0);
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    log_debug("Mouse position: (%.1f, %.1f)", xpos, ypos);
    // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
    event.scroll.x = xpos;
    event.scroll.y = ypos;
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
    if (!ui_context.surface || !ui_context.surface->pixels) {
        log_error("repaint_window: surface or pixels is NULL");
        return;
    }

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

    // set up OpenGL viewport and projection for 2D rendering
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);  // normalized device coordinates for quad rendering
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // clear the framebuffer
    glClear(GL_COLOR_BUFFER_BIT);

    // reflow the document if window size has changed
    if (width != ui_context.window_width || height != ui_context.window_height) {
        log_debug("render: window size changed to %dx%d, reflowing", width, height);
        double start_time = glfwGetTime();
        ui_context.window_width = width;  ui_context.window_height = height;
        // CRITICAL: Update viewport dimensions (CSS logical pixels) for layout
        // This ensures vh/vw units and percentage heights use the correct window size
        ui_context.viewport_width = (int)(width / ui_context.pixel_ratio);
        ui_context.viewport_height = (int)(height / ui_context.pixel_ratio);
        log_debug("render: updated viewport to %dx%d CSS pixels",
                  (int)ui_context.viewport_width, (int)ui_context.viewport_height);
        // resize the surface
        ui_context_create_surface(&ui_context, width, height);
        // reflow the document
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);
        }
        log_debug("Reflow time: %.2f ms", (glfwGetTime() - start_time) * 1000);
    }

    // Check for incremental reflow due to state changes (pseudo-classes, etc.)
    RadiantState* state = ui_context.document ? ui_context.document->state : nullptr;
    if (state && state->needs_reflow) {
        log_debug("render: incremental reflow triggered by state change");
        double start_time = glfwGetTime();
        // Reflow the document (styles will be recalculated for marked elements)
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);
        }
        state->needs_reflow = false;
        log_debug("Incremental reflow time: %.2f ms", (glfwGetTime() - start_time) * 1000);
    }

    // rerender if the document is dirty or needs repaint (e.g., caret changed)
    if (ui_context.document && ui_context.document->state &&
        (ui_context.document->state->is_dirty || ui_context.document->state->needs_repaint)) {
        render_html_doc(&ui_context, ui_context.document->view_tree, NULL);
        ui_context.document->state->needs_repaint = false;
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

    // Create memory pool for document loading
    Pool* pool = pool_create();
    if (!pool) {
        log_error("Error: Failed to create memory pool");
        url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load document based on format (respects LAMBDA_TEX_PIPELINE for .tex files)
    // CSS media queries should use CSS pixels (logical pixels), not physical pixels
    int css_viewport_width = (int)(ui_context.window_width / ui_context.pixel_ratio);
    int css_viewport_height = (int)(ui_context.window_height / ui_context.pixel_ratio);
    log_debug("Loading document...");
    DomDocument* doc = load_doc_by_format(html_file, cwd, css_viewport_width, css_viewport_height, pool);
    if (!doc) {
        log_error("Error: Could not load file: %s", html_file);
        pool_destroy(pool);
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

// Unified document viewer supporting multiple formats (HTML, Markdown, XML, RST, etc.)
// event_file: optional JSON file with simulated events for automated testing
int view_doc_in_window_with_events(const char* doc_file, const char* event_file) {
    log_init_wrapper();
    log_info("VIEW_DOC_IN_WINDOW STARTED with file: %s, event_file: %s",
             doc_file ? doc_file : "NULL", event_file ? event_file : "NULL");
    ui_context_init(&ui_context, false);
    log_debug("view_doc_in_window: after ui_context_init: window_width=%.1f, window_height=%.1f, pixel_ratio=%.2f",
              ui_context.window_width, ui_context.window_height, ui_context.pixel_ratio);
    GLFWwindow* window = ui_context.window;
    if (!window) {
        ui_context_cleanup(&ui_context);
        return -1;
    }

    // Load event simulation if specified
    EventSimContext* sim_ctx = NULL;
    if (event_file) {
        sim_ctx = event_sim_load(event_file);
        if (!sim_ctx) {
            log_error("Failed to load event file: %s", event_file);
            // Continue without simulation
        } else {
            log_info("Event simulation loaded: %d events", sim_ctx->events->length);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // receive the state of the Caps Lock and Num Lock keys
    glfwSetKeyCallback(window, key_callback);  // receive raw keyboard input
    glfwSetCharCallback(window, character_callback);  // receive character input
    glfwSetCursorPosCallback(window, cursor_position_callback);  // receive cursor/mouse position
    glfwSetMouseButtonCallback(window, mouse_button_callback);  // receive mouse button input
    log_info("Mouse button callback registered: %p", mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);  // receive mouse/touchpad scroll input
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);

    glClearColor(0.8f, 0.8f, 0.8f, 1.0f); // Light grey color

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback(window, width, height);

    // CRITICAL: Update ui_context dimensions to match actual framebuffer size
    // This ensures the initial layout uses the correct viewport dimensions
    ui_context.window_width = width;
    ui_context.window_height = height;
    ui_context.viewport_width = (int)(width / ui_context.pixel_ratio);
    ui_context.viewport_height = (int)(height / ui_context.pixel_ratio);
    log_debug("view_doc_in_window: updated viewport to %dx%d CSS pixels (framebuffer %dx%d)",
              (int)ui_context.viewport_width, (int)ui_context.viewport_height, width, height);

    // Recreate surface with correct dimensions
    ui_context_create_surface(&ui_context, width, height);

    Url* cwd = get_current_dir();
    if (cwd) {
        // Use provided document file or default to test HTML file
        const char* file_to_load = doc_file ? doc_file : "test/html/index.html";

        log_debug("Loading document: %s", file_to_load);

        // Create memory pool for document loading
        Pool* pool = pool_create();
        if (!pool) {
            log_error("Failed to create memory pool for document");
            url_destroy(cwd);
            ui_context_cleanup(&ui_context);
            return -1;
        }

        // CSS media queries should use CSS pixels (logical pixels), not physical pixels
        int css_width = (int)(width / ui_context.pixel_ratio);
        int css_height = (int)(height / ui_context.pixel_ratio);

        // Load document based on file extension
        DomDocument* doc = load_doc_by_format(file_to_load, cwd, css_width, css_height, pool);
        if (!doc) {
            log_error("Failed to load document: %s", file_to_load);
            pool_destroy(pool);
            url_destroy(cwd);
            ui_context_cleanup(&ui_context);
            return -1;
        }

        // Set scale for window display: given_scale = 1.0, scale = pixel_ratio
        // For HTML documents, this updates the default; for PDF/SVG/Image, this was already set in loader
        if (doc->html_root) {
            // HTML documents need scale set for display (layout is in CSS pixels)
            doc->given_scale = 1.0f;
            doc->scale = doc->given_scale * ui_context.pixel_ratio;
        }
        // Note: PDF/SVG/Image documents already have scale set in their respective loaders

        ui_context.document = doc;

        // Create RadiantState for interactive state management (caret, selection, focus, etc.)
        if (!doc->state) {
            doc->state = radiant_state_create(doc->pool, STATE_MODE_IN_PLACE);
            log_debug("view_doc_in_window: created RadiantState for document");
        }

        // Process @font-face rules before layout
        process_document_font_faces(&ui_context, doc);

        // Layout document (for HTML-based documents)
        // PDF documents have pre-built view trees and skip this
        if (doc->root) {
            layout_html_doc(&ui_context, doc, false);
        }
        // PDF scaling now happens inside pdf_page_to_view_tree

        // Render document
        if (doc && doc->view_tree) {
            render_html_doc(&ui_context, doc->view_tree, NULL);
        }

        url_destroy(cwd);

        // Set custom window title with format name
        if (doc_file) {
            char title[512];
            const char* format_name = get_format_name(file_to_load);
            snprintf(title, sizeof(title), "Lambda %s Viewer - %s", format_name, file_to_load);
            glfwSetWindowTitle(window, title);
        }
    }

    // Check for auto-close after initial render (for testing/benchmarking)
    bool auto_close_enabled = (getenv("LAMBDA_AUTO_CLOSE") != NULL);
    if (auto_close_enabled) {
        log_info("First frame rendered, auto-closing window for testing");
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Initial render to screen - must call render() to set up OpenGL state and blit surface to screen
    do_redraw = 1;

    // Main loop
    double lastTime = glfwGetTime();
    double deltaTime = 0.0;
    double caretBlinkTime = 0.0;
    const double CARET_BLINK_INTERVAL = 0.5;  // 500ms blink interval
    int frames = 0;

    // Give the window a moment to render before starting simulation
    double sim_start_delay = sim_ctx ? 0.5 : 0.0;  // 500ms delay before starting simulation
    double sim_start_time = glfwGetTime() + sim_start_delay;

    while (!glfwWindowShouldClose(window)) {
        // calculate deltaTime
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // poll for new events
        glfwPollEvents();

        // Process simulated events if simulation is active
        if (sim_ctx && sim_ctx->is_running && currentTime >= sim_start_time) {
            bool sim_running = event_sim_update(sim_ctx, &ui_context, window, currentTime);
            if (!sim_running && sim_ctx->auto_close) {
                // Simulation complete, auto-close window
                log_info("Simulation complete, auto-closing window");
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            do_redraw = 1;  // redraw after each simulated event
        }

        // Handle caret blinking
        RadiantState* state = ui_context.document ? ui_context.document->state : nullptr;
        if (state && state->caret) {
            caretBlinkTime += deltaTime;
            if (caretBlinkTime >= CARET_BLINK_INTERVAL) {
                caretBlinkTime = 0.0;
                caret_toggle_blink(state);
                do_redraw = 1;  // trigger repaint for caret blink
            }
        }

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

    // Get simulation results before cleanup
    int sim_fail_count = 0;
    if (sim_ctx) {
        sim_fail_count = sim_ctx->fail_count;
        event_sim_free(sim_ctx);
    }

    log_info("End of document viewer");
    ui_context_cleanup(&ui_context);
    log_cleanup();

    // Return non-zero if simulation had failures
    return sim_fail_count > 0 ? 1 : 0;
}

// Wrapper for backward compatibility
int view_doc_in_window(const char* doc_file) {
    return view_doc_in_window_with_events(doc_file, NULL);
}

int window_main(int argc, char* argv[]) {
    // render the default index.html using unified viewer
    return view_doc_in_window(NULL);
}
