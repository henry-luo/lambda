#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "radiant.hpp"
#include <locale.h>
#include <stdlib.h>

#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"  // For dom_document_destroy
#include "../lambda/js/js_event_loop.h"

void fontface_cleanup(UiContext* uicon);
char* load_font_path(FontContext *font_ctx, const char* font_name);
extern "C" void radiant_dom_invalidate_document(DomDocument* doc);

// F7: platform IME shims (radiant/ime_mac.mm, radiant/ime_win.cpp).
// Take an opaque GLFWwindow*; resolve focus/state through provided
// `state_provider` callback to keep them free of view.hpp / Cocoa
// header collisions (e.g. Carbon `Rect`).
extern "C" void radiant_ime_mac_attach(UiContext* uicon);
extern "C" void radiant_ime_win_attach(UiContext* uicon);

static char fallback_font_noto_color_emoji[] = "Noto Color Emoji";
static char fallback_font_apple_color_emoji[] = "Apple Color Emoji";
static char fallback_font_segoe_ui_emoji[] = "Segoe UI Emoji";
static char fallback_font_pingfang_sc[] = "PingFang SC";
static char fallback_font_heiti_sc[] = "Heiti SC";
static char fallback_font_hiragino_sans[] = "Hiragino Sans";
static char fallback_font_helvetica_neue[] = "Helvetica Neue";
static char fallback_font_arial_unicode_ms[] = "Arial Unicode MS";
static char fallback_font_times_new_roman[] = "Times New Roman";
static char default_font_times_new_roman[] = "Times New Roman";
static char default_font_times[] = "Times";

char *fallback_fonts[] = {
    fallback_font_noto_color_emoji,  // Emoji — Linux / cross-platform (before text fonts
                                     // so emoji codepoints get color glyphs, not mono outlines)
    fallback_font_apple_color_emoji, // Emoji — macOS native
    fallback_font_segoe_ui_emoji,    // Emoji — Windows
    fallback_font_pingfang_sc, // Chinese (Simplified), partial Japanese and Korean - macOS native
    fallback_font_heiti_sc, // Chinese (Simplified) additional fallback
    fallback_font_hiragino_sans, // Japanese font with good Unicode coverage
    fallback_font_helvetica_neue, // Latin, Cyrillic, Greek, Vietnamese, Turkish
    fallback_font_arial_unicode_ms, // Broad Unicode coverage including checkmarks, crosses, etc. (late fallback)
    fallback_font_times_new_roman, // for Arabic
    NULL
};

void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height) {
    if (!uicon) return;
    uicon->create_surface(pixel_width, pixel_height);
}

void UiContext::create_surface(int pixel_width, int pixel_height) {
    // re-creates the surface for rendering, 32-bits per pixel, RGBA format
    if (surface) image_surface_destroy(surface);
    surface = image_surface_create(pixel_width, pixel_height);
    if (!surface) {
        log_error("Error: Could not create image surface.");
    }
}

int ui_context_init(UiContext* uicon, bool headless) {
    if (!uicon) return EXIT_FAILURE;
    return uicon->init(headless);
}

int UiContext::init(bool next_headless) {
    memset(this, 0, sizeof(UiContext));
    headless = next_headless;
    // inital window width and height - match browser test viewport
    int window_width = 1200, window_height = 800;

    setlocale(LC_ALL, "");  // Set locale to support Unicode (input)

    if (next_headless) {
        // Headless automation runs entirely against the in-memory view tree.
        // Avoid creating a hidden native window by default: on macOS, GLFW's
        // hidden-window path can block in LaunchServices before the test runner
        // ever reaches document loading.  Webview/native integration tests that
        // truly need a parent window can opt into the old behavior.
        bool create_hidden_window = getenv("LAMBDA_HEADLESS_GLFW_WINDOW") != NULL;
        if (create_hidden_window) {
            #if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
            #endif
            #ifdef __APPLE__
            // Prevent dock icon and menu bar creation in headless mode.
            // Must be set before glfwInit() — glfwInit() creates the NSApp dock icon.
            glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
            #endif
            if (!glfwInit()) {
                log_error("GLFW init failed (headless mode)");
                return EXIT_FAILURE;
            }
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // hidden window
            window = glfwCreateWindow(window_width, window_height, "Lambda Headless", NULL, NULL);
            if (!window) {
                log_error("Headless: could not create hidden GLFW window, falling back to NULL window");
                glfwTerminate();
                window = NULL;
            } else {
                log_info("Running in headless mode (hidden GLFW window)");
            }
        } else {
            window = NULL;
            log_info("Running in headless mode (windowless)");
        }
        pixel_ratio = 1.0;  // Default pixel ratio for headless
        this->window_width = window_width;
        this->window_height = window_height;
        viewport_width = window_width;   // CSS pixels
        viewport_height = window_height; // CSS pixels
    } else {
        // GUI mode: create window
        // Force X11 backend on Linux to ensure window visibility in mixed Wayland/XWayland environments
        #if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        #endif
        if (!glfwInit()) {
            log_error("GLFW init failed");
            return EXIT_FAILURE;
        }

        // create a window and its OpenGL context
        window = glfwCreateWindow(window_width, window_height, "Lambda Radiant Text Rendering", NULL, NULL);
        if (!window) {
            log_error("GLFW window create failed");
            return EXIT_FAILURE;
        }

        // ensure window is shown and focused (needed on some Wayland/XWayland setups)
        glfwShowWindow(window);
        glfwFocusWindow(window);

        // get logical and actual pixel ratio
        int pixel_w, pixel_h;
        glfwGetFramebufferSize(window, &pixel_w, &pixel_h);
        float scale_x = (float)pixel_w / window_width;
        float scale_y = (float)pixel_h / window_height;
        log_info("ui_context_init: scale factor: %.2f x %.2f, framebuffer size: %d x %d", scale_x, scale_y, pixel_w, pixel_h);
        pixel_ratio = scale_x;
        this->window_width = pixel_w;  this->window_height = pixel_h;
        // viewport_width/height store the intended CSS viewport (for vh/vw units)
        // These are the logical (CSS) pixels we requested, not the actual framebuffer size
        viewport_width = window_width;   // CSS pixels (e.g., 1200)
        viewport_height = window_height; // CSS pixels (e.g., 800)
        log_info("ui_context_init: viewport=%dx%d (CSS), framebuffer=%dx%d (physical)",
               (int)viewport_width, (int)viewport_height,
               (int)this->window_width, (int)this->window_height);
    }

    // F7: install platform IME shims (no-op on platforms without one).
    radiant_ime_mac_attach(this);
    radiant_ime_win_attach(this);

    // Create unified font context — owns font database internally
    // Created after window so pixel_ratio is known
    FontContextConfig font_cfg = {};
    font_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "ui.font.pool");
    font_arena = font_pool
        ? mem_arena_create(NULL, font_pool, MEM_ROLE_RENDER, "ui.font.arena")
        : NULL;
    font_glyph_arena = font_pool
        ? mem_arena_create_sized(NULL, font_pool, 256 * 1024, 4 * 1024 * 1024,
                                 MEM_ROLE_RENDER, "ui.font.glyph_arena")
        : NULL;
    if (!font_pool || !font_arena || !font_glyph_arena) {
        log_error("ui_context_init: failed to create tracked font allocators");
        if (font_glyph_arena) mem_arena_destroy(font_glyph_arena);
        if (font_arena) mem_arena_destroy(font_arena);
        if (font_pool) mem_pool_destroy(font_pool);
        font_glyph_arena = NULL;
        font_arena = NULL;
        font_pool = NULL;
        return EXIT_FAILURE;
    }
    font_cfg.pool = font_pool;
    font_cfg.arena = font_arena;
    font_cfg.glyph_arena = font_glyph_arena;
    font_cfg.pixel_ratio = pixel_ratio;
    font_cfg.max_cached_faces = 64;
    font_cfg.enable_lcd_rendering = true;
    font_ctx = font_context_create(&font_cfg);
    if (!font_ctx) {
        log_error("ui_context_init: failed to initialize font context");
        mem_arena_destroy(font_glyph_arena);
        mem_arena_destroy(font_arena);
        mem_pool_destroy(font_pool);
        font_glyph_arena = NULL;
        font_arena = NULL;
        font_pool = NULL;
        return EXIT_FAILURE;
    }

    // set default fonts
    // Browsers use serif (Times/Times New Roman) as the default font when no font-family is specified
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 CSS pixels - layout operates in CSS logical pixels
    default_font = (FontProp){default_font_times_new_roman, 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    default_font.font_size_from_medium = true;
    legacy_default_font = (FontProp){default_font_times, 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    legacy_default_font.font_size_from_medium = true;
    fallback_fonts = ::fallback_fonts;

    // init vector rendering engine
    rdt_engine_init(1);
    // init animation timing presets (cubic-bezier ease, ease-in, ease-out, ease-in-out)
    timing_init_presets();
    // share font context with the vector backend so that picture-mode SVG
    // (file-based and data-URI) can resolve fonts via the same code path
    // used by inline <svg> in HTML body — including weight/style matching.
    rdt_set_font_context(font_ctx);
    // creates the surface for rendering
    create_surface(this->window_width, this->window_height);
    scroll_config_init(pixel_ratio);

    return EXIT_SUCCESS;
}

// F7: opaque accessors used by IME shims (ime_mac.mm / ime_win.cpp) so
// those translation units don't need to include view.hpp (which collides
// with Cocoa's `Rect`).
extern "C" GLFWwindow* radiant_ui_get_glfw_window(UiContext* uicon) {
    return uicon ? uicon->window : nullptr;
}
extern "C" DocState* radiant_ui_get_state(UiContext* uicon) {
    if (!uicon || !uicon->document) return nullptr;
    return (DocState*)uicon->document->state;
}

extern "C" void radiant_state_request_repaint(DocState* state) {
    if (state) {
        // Set both flags so render() actually rebuilds the display list.
        // `needs_repaint` alone only triggers a repaint when there is a
        // dirty region; paths that bypass the event pipeline (macOS IME,
        // async resource loaders) have no opportunity to mark dirty
        // regions, so flip `is_dirty` directly.
        doc_state_request_repaint(state);
    }
}

static void destroy_form_props_in_dom(DomNode* node) {
    if (!node || !node->is_element()) return;
    DomElement* elem = node->as_element();
    DomNode* child = elem->first_child;
    while (child) {
        destroy_form_props_in_dom(child);
        child = child->next_sibling;
    }

    form_control_release_prop(elem);
}

static void destroy_dom_owned_embed_images(DomNode* node) {
    if (!node || !node->is_element()) return;
    DomElement* elem = node->as_element();
    DomNode* child = elem->first_child;
    while (child) {
        destroy_dom_owned_embed_images(child);
        child = child->next_sibling;
    }

    release_dom_owned_embed_images(elem);
}

void free_document(DomDocument* doc) {
    if (!doc) return;

    // Module-owned DOM wrappers are non-owning; unroot wrappers before the
    // document arena destroys the native nodes they point at.
    radiant_dom_invalidate_document(doc);

    if (script_runner_js_batch_cleanup_unsafe()) {
        js_event_loop_abandon_document_timers(doc);
    } else {
        js_event_loop_cancel_document_timers(doc);
    }

    // Clean up retained JS state (MIR context, event registry, runtime heap)
    // before destroying the document that owns the pointers.
    script_runner_cleanup_js_state(doc);

    radiant_document_destroy_state(doc);

    destroy_dom_owned_embed_images((DomNode*)doc->root);

    if (!doc->view_tree) {
        destroy_form_props_in_dom((DomNode*)doc->root);
    }

    if (doc->view_tree) {
        // Some imported DOM/view fixtures alias the pools; the view-tree destroy path owns that shared pool.
        if (doc->document_pool == doc->view_tree->prop_pool) {
            doc->document_pool = nullptr;  // Pool will be destroyed by view_pool_destroy
        }

        view_pool_destroy(doc->view_tree);
        mem_free(doc->view_tree);
        doc->view_tree = nullptr;
    }
    // Note: root (DomElement) is arena-allocated and will be freed with the arena
    // No need to explicitly free it here
    if (doc->url) {
        if (doc->input && doc->input->url == doc->url) {
            doc->input->url = nullptr;
        }
        url_destroy(doc->url);
        doc->url = nullptr;
    }

    // Free DomDocument via dom_document_destroy (handles arena and pool)
    dom_document_destroy(doc);
}

void ui_context_cleanup(UiContext* uicon) {
    if (!uicon) return;
    uicon->destroy();
}

void UiContext::destroy_document() {
    if (document) {
        free_document(document);
        document = nullptr;
    }
}

void UiContext::destroy() {
    log_debug("cleaning up UI context");

    // destroy all webviews before tearing down the window
    if (webview_mgr) {
        webview_manager_destroy(webview_mgr);
        webview_mgr = nullptr;
    }

    destroy_document();

    log_debug("cleaning up font resources");
    fontface_cleanup(this);  // free font cache
    font_prop_release_handle(&default_font);
    font_prop_release_handle(&legacy_default_font);
    if (font_ctx) {
        font_context_destroy(font_ctx);
        font_ctx = NULL;
    }
    // FontContext borrows these tracked roots; destroy them after its caches release their handles.
    if (font_glyph_arena) {
        mem_arena_destroy(font_glyph_arena);
        font_glyph_arena = NULL;
    }
    if (font_arena) {
        mem_arena_destroy(font_arena);
        font_arena = NULL;
    }
    if (font_pool) {
        mem_pool_destroy(font_pool);
        font_pool = NULL;
    }

    log_debug("cleaning up media resources");
    image_cache_cleanup(this);  // cleanup image cache
    render_pool_shutdown();  // destroy worker threads before ThorVG engine
    rdt_engine_term();
    image_surface_destroy(surface);
    surface = nullptr;

    // Only tear down GLFW if a window was created (i.e., non-headless mode).
    // Calling glfwTerminate() without a prior glfwInit() is undefined behavior.
    if (window) {
        if (mouse_state.sys_cursor) {
            glfwDestroyCursor(mouse_state.sys_cursor);
            mouse_state.sys_cursor = nullptr;
        }
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
    }
}
