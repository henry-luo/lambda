#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "layout.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int iterations;
} AppState;

UiContext ui_context;

void render_html_doc(UiContext* uicon, View* root_view);
void parse_html_doc(Document* doc, const char* doc_path);
View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void view_pool_destroy(ViewTree* tree);
void handle_event(UiContext* uicon, Document* doc, RdtEvent* event);
void fontface_cleanup(UiContext* uicon);
void image_cache_cleanup(UiContext* uicon);

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

void repaint_window() {
    SDL_UpdateTexture(ui_context.texture, NULL, ui_context.surface->pixels, ui_context.surface->pitch);
    // render the texture to the screen
    assert(ui_context.window_width == ui_context.surface->w && ui_context.window_height == ui_context.surface->h);

    int logical_w, logical_h, pixel_w, pixel_h;
    SDL_GetWindowSize(ui_context.window, &logical_w, &logical_h);       // Logical size
    SDL_GetCurrentRenderOutputSize(ui_context.renderer, &pixel_w, &pixel_h); // Actual clear pixel size
    printf("Repainting window: %dx%d, logic: %dx%d, actual: %dx%d\n", 
        ui_context.window_width, ui_context.window_height, logical_w, logical_h, pixel_w, pixel_h);

    SDL_FRect rect = {0, 0, ui_context.surface->w, ui_context.surface->h};
    SDL_RenderTexture(ui_context.renderer, ui_context.texture, &rect, &rect);
    SDL_RenderPresent(ui_context.renderer);
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

void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height) {
    // re-create the surface
    if (uicon->surface) SDL_DestroySurface(uicon->surface);
    // creates the surface for rendering, 32-bits per pixel, RGBA format
    // SDL_PIXELFORMAT_RGBA8888 pack order is SDL_PACKEDORDER_RGBA, high bit -> low bit
    uicon->surface = SDL_CreateSurface(pixel_width, pixel_height, SDL_PIXELFORMAT_RGBA8888);
    tvg_swcanvas_set_target(uicon->canvas, uicon->surface->pixels, 
        pixel_width, pixel_width, pixel_height, TVG_COLORSPACE_ABGR8888);
        
    // re-create the texture
    if (uicon->texture) SDL_DestroyTexture(uicon->texture);
    // don't know why SDL_CreateTextureFromSurface failed to create the texture following the same pixel format as the surface
    // have to manually create the texture with explicit pixel format
    // uicon->texture = SDL_CreateTextureFromSurface(uicon->renderer, uicon->surface); 
    uicon->texture = SDL_CreateTexture(uicon->renderer, SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STATIC, uicon->surface->w, uicon->surface->h);
    if (uicon->texture == NULL) {
        printf("Error creating texture: %s\n", SDL_GetError());
    }
}

int ui_context_init(AppState *state, UiContext* uicon, int width, int height) {
    memset(uicon, 0, sizeof(UiContext));
    // wnd_msg_buf = strbuf_new(256);

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

    state->window = uicon->window = SDL_CreateWindow("SDL2 Window", 
        width, height, SDL_WINDOW_RESIZABLE);
    state->renderer = uicon->renderer = SDL_CreateRenderer(state->window, NULL);

    // get logical and actual pixel ratio
    int logical_w, logical_h, pixel_w, pixel_h;
    SDL_GetWindowSize(uicon->window, &logical_w, &logical_h);       // Logical size
    SDL_GetCurrentRenderOutputSize(uicon->renderer, &pixel_w, &pixel_h); // Actual clear pixel size
    float scale_x = (float)pixel_w / logical_w;
    float scale_y = (float)pixel_h / logical_h;
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
    SDL_DestroySurface(uicon->surface);
    SDL_DestroyTexture(uicon->texture);
    SDL_DestroyRenderer(uicon->renderer);

    if (uicon->mouse_state.sdl_cursor) {
        SDL_DestroyCursor(uicon->mouse_state.sdl_cursor);
    }
    SDL_DestroyWindow(uicon->window);
    SDL_Quit();
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    AppState *state = (AppState *)malloc(sizeof(AppState));
    *appstate = state;  state->iterations = 0;
    ui_context_init(state, &ui_context, 400, 600);

    return state->window && state->renderer ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *state = (AppState *)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS; // Signal to quit
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = (AppState *)appstate;
    state->iterations++;

    // Render
    SDL_SetRenderDrawColor(state->renderer, 0, 100, 0, 255);
    SDL_RenderClear(state->renderer);
    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    ui_context_cleanup(&ui_context);
    AppState *state = (AppState *)appstate;
    free(state);
}

// clang -o window3 window3.c -lSDL3 -I/opt/homebrew/Cellar/sdl3/3.2.8/include -L/opt/homebrew/Cellar/sdl3/3.2.8/lib