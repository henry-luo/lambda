#include "view.hpp"
#include <locale.h>

#include "../lib/log.h"
void view_pool_destroy(ViewTree* tree);
void fontface_cleanup(UiContext* uicon);
void image_cache_cleanup(UiContext* uicon);
char* load_font_path(FcConfig *font_config, const char* font_name);
void scroll_config_init(int pixel_ratio);

char *fallback_fonts[] = {
    "PingFang SC", // Chinese, partial Japanese and Korean
    "Helvetica Neue", // Latin, Cyrillic, Greek, Vietnamese, Turkish
    "Times New Roman", // for Arabic
    NULL
};

void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height) {
    // re-creates the surface for rendering, 32-bits per pixel, RGBA format
    if (uicon->surface) image_surface_destroy(uicon->surface);
    uicon->surface = image_surface_create(pixel_width, pixel_height);
    if (!uicon->surface) {
        fprintf(stderr, "Error: Could not create image surface.\n");
        return;
    }
}

int ui_context_init(UiContext* uicon, bool headless) {
    memset(uicon, 0, sizeof(UiContext));
    // inital window width and height - match browser test viewport
    int window_width = 1200, window_height = 800;

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

    if (headless) {
        // Headless mode: no window creation
        printf("Running in headless mode (no window)\n");
        uicon->window = NULL;
        uicon->pixel_ratio = 1.0;  // Default pixel ratio for headless
        uicon->window_width = window_width;
        uicon->window_height = window_height;
    } else {
        // GUI mode: create window
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
    }

    // load default fonts
    uicon->default_font = (FontProp){"Arial", (float)(16 * uicon->pixel_ratio),
        LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NONE};
    uicon->fallback_fonts = fallback_fonts;

    // init ThorVG engine
    tvg_engine_init(TVG_ENGINE_SW, 1);
    // load font for tvg to render text later
    char* font_path = load_font_path(uicon->font_config, "Arial");
    if (font_path) {
        tvg_font_load(font_path);  free(font_path);
    }
    // creates the surface for rendering
    ui_context_create_surface(uicon, uicon->window_width, uicon->window_height);
    scroll_config_init(uicon->pixel_ratio);

    return EXIT_SUCCESS;
}

void free_document(Document* doc) {
    if (doc->dom_tree) {
        lxb_html_document_destroy(doc->dom_tree);
    }
    if (doc->view_tree) {
        view_pool_destroy(doc->view_tree);
        free(doc->view_tree);
    }
    if (doc->url) {
        lxb_url_destroy(doc->url);
    }
    free(doc);
}

void ui_context_cleanup(UiContext* uicon) {
    log_debug("cleaning up UI context");
    if (uicon->document) {
        free_document(uicon->document);
    }

    log_debug("cleaning up font resources");
    fontface_cleanup(uicon);  // free font cache
    FT_Done_FreeType(uicon->ft_library);
    FcConfigDestroy(uicon->font_config);

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
