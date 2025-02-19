#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"
#include <stdio.h>
#include <stdbool.h>

UiContext ui_context;

void render_html_doc(UiContext* uicon, View* root_view);
StrBuf* readTextFile(const char *filename);
lxb_html_document_t* parse_html_doc(const char *html_source);
View* layout_html_doc(UiContext* uicon, lxb_html_document_t *doc);
void view_pool_destroy(ViewTree* tree);

static int resizingEventWatcher(void* data, SDL_Event* event) {
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        SDL_Window* win = (SDL_Window*)data;
        ui_context.window_width = event->window.data1; 
        ui_context.window_height = event->window.data2;
        printf("Window %d resized to %fx%f\n", event->window.windowID,  
            ui_context.window_width, ui_context.window_height);

        // layout html doc 
        if (ui_context.document) {
            view_pool_destroy(ui_context.view_tree);
            layout_html_doc(&ui_context, ui_context.document);
            // render html doc
            if (ui_context.view_tree->root) {
                render_html_doc(&ui_context, ui_context.view_tree->root);
                SDL_UpdateTexture(ui_context.texture, NULL, ui_context.surface->pixels, ui_context.surface->pitch);
                // render the texture to the screen
                SDL_Rect rect = {0, 0, ui_context.surface->w, ui_context.surface->h}; 
                SDL_RenderCopy(ui_context.renderer, ui_context.texture, NULL, &rect);
                SDL_RenderPresent(ui_context.renderer);                
            }            
        }        
    }
    return 0;
  }

int ui_context_init(UiContext* uicon, int width, int height) {
    memset(uicon, 0, sizeof(UiContext));
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
    view_pool_destroy(uicon->view_tree);
    free(uicon->view_tree);
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
    ui_context_init(&ui_context, 400, 600);
    
    // load sample HTML source
    StrBuf* source_buf = readTextFile("sample.html");
    ui_context.document = parse_html_doc(source_buf->b);
    strbuf_free(source_buf);

    // layout html doc 
    if (ui_context.document) {
        ui_context.view_tree = calloc(1, sizeof(ViewTree));
        layout_html_doc(&ui_context, ui_context.document);
    }
    // render html doc
    if (ui_context.view_tree->root) { render_html_doc(&ui_context, ui_context.view_tree->root); }
    ui_context.texture = SDL_CreateTextureFromSurface(ui_context.renderer, ui_context.surface); 
    SDL_RenderClear(ui_context.renderer);

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
                    SDL_SetWindowTitle(ui_context.window, title);
                }
                else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    printf("Window is being resized: %dx%d\n", event.window.data1, event.window.data2);
                }
                else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    printf("Window is being dragged.\n");
                }      
            }
        }

        // render the texture to the screen
        SDL_Rect rect = {0, 0, ui_context.surface->w, ui_context.surface->h}; 
        SDL_RenderCopy(ui_context.renderer, ui_context.texture, NULL, &rect);
        SDL_RenderPresent(ui_context.renderer);

        SDL_Delay(300);  // Pause for 300ms after each rendering
    }
    
    ui_context_cleanup(&ui_context);
    return 0;
}