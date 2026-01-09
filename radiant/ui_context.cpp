#include "view.hpp"
#include <locale.h>
#include <freetype/ftlcdfil.h>  // For FT_Library_SetLcdFilter

#include "../lib/log.h"
#include "../lib/font_config.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/memtrack.h"
#include "../lambda/input/css/dom_element.hpp"  // For dom_document_destroy
void view_pool_destroy(ViewTree* tree);
void fontface_cleanup(UiContext* uicon);
void image_cache_cleanup(UiContext* uicon);
char* load_font_path(FontDatabase *font_db, const char* font_name);
void scroll_config_init(int pixel_ratio);

char *fallback_fonts[] = {
    "Apple Color Emoji", // Emoji - macOS native (must be first for color emoji support)
    "PingFang SC", // Chinese (Simplified), partial Japanese and Korean - macOS native
    "Heiti SC", // Chinese (Simplified) additional fallback
    "Hiragino Sans", // Japanese font with good Unicode coverage
    "Helvetica Neue", // Latin, Cyrillic, Greek, Vietnamese, Turkish
    "Arial Unicode MS", // Broad Unicode coverage including checkmarks, crosses, etc. (late fallback)
    "Times New Roman", // for Arabic
    NULL
};

// Configure FreeType for optimal sub-pixel rendering
void configure_freetype_subpixel(FT_Library library) {
    if (!library) {
        log_error("Invalid FreeType library handle");
        return;
    }
    // Enable LCD filtering for sub-pixel rendering
    FT_Error error = FT_Library_SetLcdFilter(library, FT_LCD_FILTER_DEFAULT);
    if (error) {
        log_info("Failed to set LCD filter: %d", error);
    } else {
        log_debug("LCD filter enabled for sub-pixel rendering");
    }
    log_info("FreeType configured for sub-pixel rendering (basic mode)");
}

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

    // init FreeType with sub-pixel rendering configuration
    if (FT_Init_FreeType(&uicon->ft_library)) {
        fprintf(stderr, "Could not initialize FreeType library\n");
        return EXIT_FAILURE;
    }

    // Configure sub-pixel rendering for better text quality
    configure_freetype_subpixel(uicon->ft_library);

    // Configure FreeType for better sub-pixel rendering
    FT_Error lcd_error = FT_Library_SetLcdFilter(uicon->ft_library, FT_LCD_FILTER_DEFAULT);
    if (lcd_error) {
        log_debug("Could not set LCD filter (FreeType version may not support it)");
    } else {
        log_debug("LCD filter enabled for sub-pixel rendering");
    }
    // Use global font database singleton for performance
    uicon->font_db = font_database_get_global();
    if (!uicon->font_db) {
        fprintf(stderr, "Failed to initialize global font database\n");
        return EXIT_FAILURE;
    }

    if (headless) {
        // Headless mode: no window creation
        printf("Running in headless mode (no window)\n");
        uicon->window = NULL;
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
        printf("Scale Factor: %.2f x %.2f\n", scale_x, scale_y);
        printf("ui_context_init: framebuffer size: %d x %d\n", pixel_w, pixel_h);
        uicon->pixel_ratio = scale_x;
        uicon->window_width = pixel_w;  uicon->window_height = pixel_h;
        // viewport_width/height store the intended CSS viewport (for vh/vw units)
        // These are the logical (CSS) pixels we requested, not the actual framebuffer size
        uicon->viewport_width = window_width;   // CSS pixels (e.g., 1200)
        uicon->viewport_height = window_height; // CSS pixels (e.g., 800)
        printf("ui_context_init: viewport=%dx%d (CSS), framebuffer=%dx%d (physical)\n",
               (int)uicon->viewport_width, (int)uicon->viewport_height,
               (int)uicon->window_width, (int)uicon->window_height);
    }

    // set default fonts
    // Browsers use serif (Times/Times New Roman) as the default font when no font-family is specified
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 CSS pixels - layout operates in CSS logical pixels
    uicon->default_font = (FontProp){"Times New Roman", 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    uicon->legacy_default_font = (FontProp){"Times", 16.0f, // 16px (CSS logical pixels)
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NONE};
    uicon->fallback_fonts = fallback_fonts;

    // init ThorVG engine
    tvg_engine_init(TVG_ENGINE_SW, 1);
    // load default font for tvg to render text later
    char* font_path = load_font_path(uicon->font_db, "Times New Roman");
    if (!font_path) {
        font_path = load_font_path(uicon->font_db, "Times");  // Fallback to Times if Times New Roman not found
    }
    if (font_path) {
        tvg_font_load(font_path);  mem_free(font_path);
    }
    // creates the surface for rendering
    ui_context_create_surface(uicon, uicon->window_width, uicon->window_height);
    scroll_config_init(uicon->pixel_ratio);

    return EXIT_SUCCESS;
}

void free_document(DomDocument* doc) {
    if (!doc) return;
    if (doc->view_tree) {
        // Note: view_pool_destroy destroys the pool that contains all view allocations
        // including the ViewTree itself (if it was pool-allocated).
        // Do NOT call free(doc->view_tree) after this - it would double-free.
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
    if (uicon->document) {
        free_document(uicon->document);
    }

    log_debug("cleaning up font resources");
    fontface_cleanup(uicon);  // free font cache
    FT_Done_FreeType(uicon->ft_library);
    font_database_destroy(uicon->font_db);

    log_debug("cleaning up media resources");
    image_cache_cleanup(uicon);  // cleanup image cache
    tvg_engine_term(TVG_ENGINE_SW);
    image_surface_destroy(uicon->surface);
    if (uicon->mouse_state.sys_cursor) {
        glfwDestroyCursor(uicon->mouse_state.sys_cursor);
    }

    glfwDestroyWindow(uicon->window);
    glfwTerminate();
}
