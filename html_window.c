#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"
#include <stdio.h>
#include <stdbool.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

void render_html_doc(UiContext* uicon, View* root_view);
StrBuf* readTextFile(const char *filename);
lxb_html_document_t* parse_html_doc(const char *html_source);
View* layout_html_doc(UiContext* uicon, lxb_html_document_t *doc);

int ui_context_init(UiContext* uicon) {
    // init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s", SDL_GetError());
        return EXIT_FAILURE;
    }
    // init SDL_image
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("IMG_Init failed: %s", IMG_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

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

    // init ThorVG engine
    tvg_engine_init(TVG_ENGINE_SW, 1);

    // creates the surface for rendering, 32-bits per pixel, RGBA format
    // should be the size of the window/viewport
    uicon->surface = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    uicon->canvas = tvg_swcanvas_create();
    tvg_swcanvas_set_target(uicon->canvas, uicon->surface->pixels, 
        WINDOW_WIDTH, WINDOW_WIDTH, WINDOW_HEIGHT, TVG_COLORSPACE_ARGB8888);
    return EXIT_SUCCESS; 
}

void ui_context_cleanup(UiContext* uicon) {
    FT_Done_FreeType(uicon->ft_library);
    FcConfigDestroy(uicon->font_config);
    tvg_canvas_destroy(uicon->canvas);
    tvg_engine_term(TVG_ENGINE_SW);
    SDL_FreeSurface(uicon->surface);
    IMG_Quit();
    SDL_Quit();
}

static lxb_status_t serialize_callback(const lxb_char_t *data, size_t len, void *ctx) {
    // Append data to string buffer
    lxb_char_t **output = (lxb_char_t **)ctx;
    size_t old_len = *output ? strlen((char *)*output) : 0;
    *output = realloc(*output, old_len + len + 1);
    if (*output == NULL) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }
    
    memcpy(*output + old_len, data, len);
    (*output)[old_len + len] = '\0';
    
    return LXB_STATUS_OK;
}

int main(int argc, char *argv[]) {
    UiContext uicon;
    ui_context_init(&uicon);
    
    SDL_Window *window = SDL_CreateWindow("SDL2 Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Get logical and actual pixel size
    int logical_w, logical_h, pixel_w, pixel_h;
    SDL_GetWindowSize(window, &logical_w, &logical_h);       // Logical size
    SDL_GetRendererOutputSize(renderer, &pixel_w, &pixel_h); // Actual pixel size
    float scale_x = (float)pixel_w / logical_w;
    float scale_y = (float)pixel_h / logical_h;
    printf("Logical Size: %d x %d\n", logical_w, logical_h);
    printf("Actual Pixel Size: %d x %d\n", pixel_w, pixel_h);
    printf("Scale Factor: %.2f x %.2f\n", scale_x, scale_y);
    // Scale rendering
    SDL_RenderSetScale(renderer, scale_x, scale_y);

    // load sample HTML source
    View* root_view = NULL;
    StrBuf* source_buf = readTextFile("sample.html");
    lxb_html_document_t* document = parse_html_doc(source_buf->b);
    strbuf_free(source_buf);

    // Serialize document to string
    lxb_char_t *output = NULL;
    lxb_dom_document_t *dom_document = &document->dom_document;
    lxb_status_t status = lxb_html_serialize_tree_cb(dom_document, serialize_callback, &output);
    if (status != LXB_STATUS_OK || output == NULL) {
        fprintf(stderr, "Failed to serialize document\n");
        return EXIT_FAILURE;
    }
    // Print serialized output
    printf("Serialized HTML:\n%s\n", output);

    // layout html doc 
    if (document) { root_view = layout_html_doc(&uicon, document); }
    // render html doc
    if (root_view) { render_html_doc(&uicon, root_view); }

    bool running = true;
    SDL_Event event;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, uicon.surface);
    while (running) {
        while (SDL_PollEvent(&event)) {  // handles events
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_RenderClear(renderer);
        // render the texture to the screen
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay(400);  // Pause for 400ms after each rendering
    }
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    ui_context_cleanup(&uicon);
    return 0;
}