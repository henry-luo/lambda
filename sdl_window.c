#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <thorvg_capi.h>
#include <stdbool.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

void renderText(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y) {
    SDL_Color color = {255, 255, 255, 255};
    SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dstRect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dstRect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void drawTriangle(Tvg_Canvas* canvas) {
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, 400, 100);
    tvg_shape_line_to(shape, 600, 500);
    tvg_shape_line_to(shape, 200, 500);
    tvg_shape_close(shape);
    tvg_shape_set_fill_color(shape, 255, 0, 0, 255); // Red color
    tvg_canvas_push(canvas, shape);
}

void renderTriangle(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawLine(renderer, 400, 150, 300, 450);
    SDL_RenderDrawLine(renderer, 300, 450, 500, 450);
    SDL_RenderDrawLine(renderer, 500, 450, 400, 150);
}

int main(int argc, char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_Window *window = SDL_CreateWindow("SDL2 Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font *font = TTF_OpenFont("lato.ttf", 24);
    if (!font) {
        printf("Failed to load font: %s\n", TTF_GetError());
        return -1;
    }

    uint32_t *buffer = (uint32_t *)malloc(WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint32_t));
    if (!buffer) {
        printf("Failed to allocate buffer\n");
        return 1;
    }
    memset(buffer, 0, WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint32_t));    

    // tvg_engine_init(TVG_ENGINE_SW, 1);
    // Tvg_Canvas* canvas = tvg_swcanvas_create();
    // tvg_swcanvas_set_target(canvas, buffer, WINDOW_WIDTH, WINDOW_WIDTH, WINDOW_HEIGHT, TVG_COLORSPACE_ARGB8888);
    // drawTriangle(canvas);
    // tvg_canvas_draw(canvas, true);
    // tvg_canvas_sync(canvas);

    bool running = true;
    SDL_Event event;

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }
        
        // SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        // SDL_RenderClear(renderer);
        
        // renderTriangle(renderer);
        renderText(renderer, font, "Hello, SDL2!", 50, 50);

        // tvg_canvas_update(canvas);
        SDL_UpdateTexture(texture, NULL, buffer, WINDOW_WIDTH * sizeof(uint32_t));
        
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    
    free(buffer);
    // tvg_canvas_destroy(canvas);
    tvg_engine_term(TVG_ENGINE_SW);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}


/*
clang sdl_window.c -o sdl_app $(sdl2-config --cflags --libs) \
-I/opt/homebrew/opt/sdl2/include -L/opt/homebrew/opt/sdl2/lib -lSDL2 \
-I/opt/homebrew/opt/sdl2_ttf/include -L/opt/homebrew/opt/sdl2_ttf/lib -lSDL2_ttf \
-I/opt/homebrew/include -L/opt/homebrew/lib -lThorVG
*/