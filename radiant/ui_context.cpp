#include "view.hpp"
#include "rdt_vector.hpp"
#include "render.hpp"
#include "animation.h"
#include "rdt_video.h"
#include "webview.h"
#include <locale.h>

#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/memtrack.h"
#include "../lambda/input/css/dom_element.hpp"  // For dom_document_destroy
#include "../radiant/script_runner.h"  // For script_runner_cleanup_js_state

void view_pool_destroy(ViewTree* tree);
void fontface_cleanup(UiContext* uicon);
void image_cache_cleanup(UiContext* uicon);
char* load_font_path(FontContext *font_ctx, const char* font_name);
void scroll_config_init(int pixel_ratio);

char *fallback_fonts[] = {
    "Noto Color Emoji",  // Emoji — Linux / cross-platform (before text fonts
                         // so emoji codepoints get color glyphs, not mono outlines)
    "Apple Color Emoji", // Emoji — macOS native
    "Segoe UI Emoji",    // Emoji — Windows
    "PingFang SC", // Chinese (Simplified), partial Japanese and Korean - macOS native
    "Heiti SC", // Chinese (Simplified) additional fallback
    "Hiragino Sans", // Japanese font with good Unicode coverage
    "Helvetica Neue", // Latin, Cyrillic, Greek, Vietnamese, Turkish
    "Arial Unicode MS", // Broad Unicode coverage including checkmarks, crosses, etc. (late fallback)
    "Times New Roman", // for Arabic
    NULL
};

void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height) {
    // re-creates the surface for rendering, 32-bits per pixel, RGBA format
    if (uicon->surface) image_surface_destroy(uicon->surface);
    uicon->surface = image_surface_create(pixel_width, pixel_height);
    if (!uicon->surface) {
        log_error("Error: Could not create image surface.");
    }
}

int ui_context_init(UiContext* uicon, bool headless) {
    memset(uicon, 0, sizeof(UiContext));
    // inital window width and height - match browser test viewport
    int window_width = 1200, window_height = 800;

    setlocale(LC_ALL, "");  // Set locale to support Unicode (input)

    if (headless) {
        // Headless mode: create a hidden GLFW window so that native subsystems
        // (e.g. WKWebView) that require a parent window still function.
        #if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        #endif
        #ifdef __APPLE__
        // Prevent dock icon and menu bar creation in headless mode.
        // Must be set before glfwInit() — glfwInit() creates the NSApp dock icon.
        glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
        #endif
        if (!glfwInit()) {
            fprintf(stderr, "Error: Could not initialize GLFW for headless mode.\n");
            return EXIT_FAILURE;
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // hidden window
        uicon->window = glfwCreateWindow(window_width, window_height, "Lambda Headless", NULL, NULL);
        if (!uicon->window) {
            log_error("Headless: could not create hidden GLFW window, falling back to NULL window");
            uicon->window = NULL;
        } else {
            log_info("Running in headless mode (hidden GLFW window)");
        }
        uicon->pixel_ratio = 1.0;  // Default pixel ratio for headless
        uicon->window_width = window_width;
        uicon->window_height = window_height;
        uicon->viewport_width = window_width;   // CSS pixels
        uicon->viewport_height = window_height; // CSS pixels
    } else {
        // GUI mode: create window
        // Force X11 backend on Linux to ensure window visibility in mixed Wayland/XWayland environments
        #if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        #endif
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

        // ensure window is shown and focused (needed on some Wayland/XWayland setups)
        glfwShowWindow(uicon->window);
        glfwFocusWindow(uicon->window);

        // get logical and actual pixel ratio
        int pixel_w, pixel_h;
        glfwGetFramebufferSize(uicon->window, &pixel_w, &pixel_h);
        float scale_x = (float)pixel_w / window_width;
        float scale_y = (float)pixel_h / window_height;
        log_info("ui_context_init: scale factor: %.2f x %.2f, framebuffer size: %d x %d", scale_x, scale_y, pixel_w, pixel_h);
        uicon->pixel_ratio = scale_x;
        uicon->window_width = pixel_w;  uicon->window_height = pixel_h;
        // viewport_width/height store the intended CSS viewport (for vh/vw units)
        // These are the logical (CSS) pixels we requested, not the actual framebuffer size
        uicon->viewport_width = window_width;   // CSS pixels (e.g., 1200)
        uicon->viewport_height = window_height; // CSS pixels (e.g., 800)
        log_info("ui_context_init: viewport=%dx%d (CSS), framebuffer=%dx%d (physical)",
               (int)uicon->viewport_width, (int)uicon->viewport_height,
               (int)uicon->window_width, (int)uicon->window_height);
    }

    // Create unified font context — owns font database internally
    // Created after window so pixel_ratio is known
    FontContextConfig font_cfg = {};
    font_cfg.pixel_ratio = uicon->pixel_ratio;
    font_cfg.max_cached_faces = 64;
    font_cfg.enable_lcd_rendering = true;
    uicon->font_ctx = font_context_create(&font_cfg);

    // set default fonts
    // Browsers use serif (Times/Times New Roman) as the default font when no font-family is specified
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 CSS pixels - layout operates in CSS logical pixels
    uicon->default_font = (FontProp){"Times New Roman", 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    uicon->default_font.font_size_from_medium = true;
    uicon->legacy_default_font = (FontProp){"Times", 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    uicon->legacy_default_font.font_size_from_medium = true;
    uicon->fallback_fonts = fallback_fonts;

    // init vector rendering engine
    rdt_engine_init(1);
    // init animation timing presets (cubic-bezier ease, ease-in, ease-out, ease-in-out)
    timing_init_presets();
    // share font context with the vector backend so that picture-mode SVG
    // (file-based and data-URI) can resolve fonts via the same code path
    // used by inline <svg> in HTML body — including weight/style matching.
    rdt_set_font_context(uicon->font_ctx);
    // creates the surface for rendering
    ui_context_create_surface(uicon, uicon->window_width, uicon->window_height);
    scroll_config_init(uicon->pixel_ratio);

    return EXIT_SUCCESS;
}

// walk a view tree and destroy heap-allocated video resources
// must be called before view_pool_destroy to avoid leaking RdtVideo*
static void destroy_video_in_view(View* view) {
    if (!view) return;
    if (view->view_type >= RDT_VIEW_INLINE_BLOCK && view->is_element()) {
        ViewBlock* blk = (ViewBlock*)view;
        if (blk->embed && blk->embed->video) {
            rdt_video_destroy((RdtVideo*)blk->embed->video);
            blk->embed->video = nullptr;
        }
    }
    // recurse into children if this is an element
    if (view->is_element()) {
        DomElement* elem = view->as_element();
        DomNode* child = elem->first_child;
        while (child) {
            destroy_video_in_view(child);
            child = child->next_sibling;
        }
    }
}

static void destroy_video_resources(ViewTree* tree) {
    if (!tree || !tree->root) return;
    destroy_video_in_view(tree->root);
}

void free_document(DomDocument* doc) {
    if (!doc) return;

    // Clean up retained JS state (MIR context, event registry, runtime heap)
    // before destroying the document that owns the pointers.
    script_runner_cleanup_js_state(doc);

    if (doc->view_tree) {
        // destroy video resources before bulk-freeing the pool
        // (RdtVideo* and poster ImageSurface* are heap-allocated, not pool-allocated)
        destroy_video_resources(doc->view_tree);

        // Note: view_pool_destroy destroys the pool that contains all view allocations
        // including the ViewTree itself (if it was pool-allocated).
        // Do NOT call free(doc->view_tree) after this - it would double-free.

        // Check if doc->pool is the same as view_tree->pool to avoid double-free
        if (doc->pool == doc->view_tree->pool) {
            doc->pool = nullptr;  // Pool will be destroyed by view_pool_destroy
        }

        view_pool_destroy(doc->view_tree);
        // Don't free view_tree - it was allocated from the pool that was just destroyed
    }
    // Note: root (DomElement) is arena-allocated and will be freed with the arena
    // No need to explicitly free it here
    if (doc->url) {
        url_destroy(doc->url);
    }

    // Free DomDocument via dom_document_destroy (handles arena and pool)
    dom_document_destroy(doc);
}

void ui_context_cleanup(UiContext* uicon) {
    log_debug("cleaning up UI context");

    // destroy all webviews before tearing down the window
    if (uicon->webview_mgr) {
        webview_manager_destroy(uicon->webview_mgr);
        uicon->webview_mgr = nullptr;
    }

    if (uicon->document) {
        free_document(uicon->document);
    }

    log_debug("cleaning up font resources");
    fontface_cleanup(uicon);  // free font cache
    if (uicon->font_ctx) {
        font_context_destroy(uicon->font_ctx);
        uicon->font_ctx = NULL;
    }

    log_debug("cleaning up media resources");
    image_cache_cleanup(uicon);  // cleanup image cache
    render_pool_shutdown();  // destroy worker threads before ThorVG engine
    rdt_engine_term();
    image_surface_destroy(uicon->surface);

    // Only tear down GLFW if a window was created (i.e., non-headless mode).
    // Calling glfwTerminate() without a prior glfwInit() is undefined behavior.
    if (uicon->window) {
        if (uicon->mouse_state.sys_cursor) {
            glfwDestroyCursor(uicon->mouse_state.sys_cursor);
        }
        glfwDestroyWindow(uicon->window);
        glfwTerminate();
    }
}
