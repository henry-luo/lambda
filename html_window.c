#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"
#include <stdio.h>
#include <stdbool.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

View* layout_style_tree(UiContext* uicon, StyleBlock* style_root);
void render_html_doc(UiContext* uicon, View* root_view);
int ui_context_init(UiContext* uicon);
void ui_context_cleanup(UiContext* uicon);
StrBuf* readTextFile(const char *filename);
lxb_html_document_t* parse_html_doc(const char *html_source);
View* layout_html_doc(UiContext* uicon, lxb_html_document_t *doc);

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