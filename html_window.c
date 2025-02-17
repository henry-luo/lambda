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

static int resizingEventWatcher(void* data, SDL_Event* event) {
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        SDL_Window* win = (SDL_Window*)data;
        int width = event->window.data1;  int height = event->window.data2;
        printf("Window %d resized to %dx%d\n", event->window.windowID, width, height);
    }
    return 0;
  }

int ui_context_init(UiContext* uicon, int width, int height) {
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

    uicon->window = SDL_CreateWindow("SDL2 Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_AddEventWatch(resizingEventWatcher, uicon->window);
    uicon->renderer = SDL_CreateRenderer(uicon->window, -1, SDL_RENDERER_ACCELERATED);   
    // get logical and actual pixel ratio
    int logical_w, logical_h, pixel_w, pixel_h;
    SDL_GetWindowSize(uicon->window, &logical_w, &logical_h);       // Logical size
    uicon->window_width = logical_w;  uicon->window_height = logical_h;
    SDL_GetRendererOutputSize(uicon->renderer, &pixel_w, &pixel_h); // Actualclear pixel size
    float scale_x = (float)pixel_w / logical_w;
    float scale_y = (float)pixel_h / logical_h;
    printf("Scale Factor: %.2f x %.2f\n", scale_x, scale_y);
    // Scale rendering
    // SDL_RenderSetScale(renderer, scale_x, scale_y);
    uicon->pixel_ratio = scale_x;

    // creates the surface for rendering, 32-bits per pixel, RGBA format
    // should be the size of the window/viewport
    uicon->surface = SDL_CreateRGBSurfaceWithFormat(0, width * scale_x, height * scale_x, 32, SDL_PIXELFORMAT_ARGB8888);   
    // init ThorVG engine
    tvg_engine_init(TVG_ENGINE_SW, 1);    
    uicon->canvas = tvg_swcanvas_create();
    tvg_swcanvas_set_target(uicon->canvas, uicon->surface->pixels, 
        width * scale_x, width * scale_x, height * scale_x, TVG_COLORSPACE_ARGB8888);
    return EXIT_SUCCESS; 
}

void ui_context_cleanup(UiContext* uicon) {
    FT_Done_FreeType(uicon->ft_library);
    FcConfigDestroy(uicon->font_config);
    tvg_canvas_destroy(uicon->canvas);
    tvg_engine_term(TVG_ENGINE_SW);
    SDL_FreeSurface(uicon->surface);
    SDL_DestroyTexture(uicon->texture);
    SDL_DestroyRenderer(uicon->renderer);
    SDL_DestroyWindow(uicon->window);
    IMG_Quit();
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    UiContext uicon;
    ui_context_init(&uicon, WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // load sample HTML source
    View* root_view = NULL;
    StrBuf* source_buf = readTextFile("sample.html");
    lxb_html_document_t* document = parse_html_doc(source_buf->b);
    strbuf_free(source_buf);

    // layout html doc 
    if (document) { root_view = layout_html_doc(&uicon, document); }
    // render html doc
    if (root_view) { render_html_doc(&uicon, root_view); }
    uicon.texture = SDL_CreateTextureFromSurface(uicon.renderer, uicon.surface); 

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {  // handles events
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
            else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int newWidth = event.window.data1;
                    int newHeight = event.window.data2;
                    char title[256];
                    snprintf(title, sizeof(title), "Window Size: %dx%d", newWidth, newHeight);
                    SDL_SetWindowTitle(uicon.window, title);
                }
                else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    printf("Window is being resized: %dx%d\n", event.window.data1, event.window.data2);
                }
                else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    printf("Window is being dragged.\n");
                }      
            }
        }
        SDL_RenderClear(uicon.renderer);
        // render the texture to the screen
        SDL_Rect rect = {0, 0, uicon.surface->w, uicon.surface->h}; 
        SDL_RenderCopy(uicon.renderer, uicon.texture, NULL, &rect);
        SDL_RenderPresent(uicon.renderer);

        SDL_Delay(300);  // Pause for 300ms after each rendering
    }
    
    ui_context_cleanup(&uicon);
    return 0;
}