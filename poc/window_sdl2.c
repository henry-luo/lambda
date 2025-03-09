#include "layout.h"
#include <stdio.h>
#include <stdbool.h>

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
    SDL_GetRendererOutputSize(ui_context.renderer, &pixel_w, &pixel_h); // Actual clear pixel size

    SDL_Rect rect = {0, 0, ui_context.surface->w, ui_context.surface->h}; 
    printf("Repainting window: %dx%d, logic: %dx%d, actual: %dx%d\n", 
        ui_context.window_width, ui_context.window_height, logical_w, logical_h, pixel_w, pixel_h);
    SDL_RenderCopy(ui_context.renderer, ui_context.texture, &rect, &rect);
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
    if (uicon->surface) SDL_FreeSurface(uicon->surface);
    // creates the surface for rendering, 32-bits per pixel, RGBA format
    // SDL_PIXELFORMAT_RGBA8888 pack order is SDL_PACKEDORDER_RGBA, high bit -> low bit
    uicon->surface = SDL_CreateRGBSurfaceWithFormat(0, 
        pixel_width, pixel_height, 32, SDL_PIXELFORMAT_RGBA8888);
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

StrBuf* wnd_msg_buf;
static int resizingEventWatcher(void* data, SDL_Event* event) {
    if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_RESIZED || 
        event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        SDL_Window* win = (SDL_Window*)data;
        if (ui_context.window_width == event->window.data1 * ui_context.pixel_ratio && 
            ui_context.window_height == event->window.data2 * ui_context.pixel_ratio) {
            printf("No change in size\n");  return 0;  // consumes the event
        }
        // just update ui_context.window size here and let the main loop to handle the rest
        ui_context.window_width = event->window.data1 * ui_context.pixel_ratio; 
        ui_context.window_height = event->window.data2 * ui_context.pixel_ratio;

        // SDL_Renderer needs to be destroyed and recreated on window resize 
        SDL_DestroyRenderer(ui_context.renderer);
        SDL_SetWindowSize(ui_context.window, event->window.data1, event->window.data2);
        ui_context.renderer = SDL_CreateRenderer(ui_context.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        SDL_RenderSetIntegerScale(ui_context.renderer, SDL_TRUE);
        // fill the window initially with white color
        SDL_SetRenderDrawColor(ui_context.renderer, 255, 255, 255, 255); // white background
        SDL_RenderClear(ui_context.renderer);
        // SDL_Rect viewport = {0, 0, ui_context.window_width, ui_context.window_height};
        // SDL_RenderSetLogicalSize(ui_context.renderer, event->window.data1, event->window.data2);
        // SDL_RenderSetViewport(ui_context.renderer, &viewport);

        strbuf_append_format(wnd_msg_buf, "Window %d resized to %dx%d\n", event->window.windowID, 
            ui_context.window_width, ui_context.window_height);
        char title[256];
        snprintf(title, sizeof(title), "Window Size: %dx%d", (int)ui_context.window_width, (int)ui_context.window_height);
        SDL_SetWindowTitle(ui_context.window, title);

        // resize the surface
        ui_context_create_surface(&ui_context, ui_context.window_width, ui_context.window_height);
        // reflow the document
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);      
        }
        // render the texture to the screen
        repaint_window(); 

        // printf("%s", wnd_msg_buf->s);
        // return event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ? 1 : 0;
        return 0;  // consume the event
    }
    return 1;  // continue to process the event
  }

int ui_context_init(UiContext* uicon, int width, int height) {
    memset(uicon, 0, sizeof(UiContext));
    wnd_msg_buf = strbuf_new(256);
    // init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Disable filtering for pixel-perfect scaling

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
    // switched from SDL_AddEventWatch to SDL_SetEventFilter for event handling
    SDL_SetEventFilter(resizingEventWatcher, uicon->window);
    uicon->renderer = SDL_CreateRenderer(uicon->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetIntegerScale(uicon->renderer, SDL_TRUE);
    // fill the window initially with white color
    SDL_SetRenderDrawColor(uicon->renderer, 255, 255, 255, 255); // white background
    SDL_RenderClear(uicon->renderer);    

    // get logical and actual pixel ratio
    int logical_w, logical_h, pixel_w, pixel_h;
    SDL_GetWindowSize(uicon->window, &logical_w, &logical_h);       // Logical size
    SDL_GetRendererOutputSize(uicon->renderer, &pixel_w, &pixel_h); // Actual clear pixel size
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
    SDL_FreeSurface(uicon->surface);
    SDL_DestroyTexture(uicon->texture);
    SDL_DestroyRenderer(uicon->renderer);

    if (uicon->mouse_state.sdl_cursor) {
        SDL_FreeCursor(uicon->mouse_state.sdl_cursor);
    }
    SDL_DestroyWindow(uicon->window);
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    ui_context_init(&ui_context, 400, 600);

    ui_context.document = show_html_doc("test/sample.html");

    bool running = true;
    uint32_t frameStart = SDL_GetTicks();
    uint32_t frameDelay = 1000 / 60;  // 60 fps
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {  // handles events
            switch (event.type) {
            case SDL_QUIT:  running = false;  break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
                break;
            case SDL_WINDOWEVENT:
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
                else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    SDL_RenderPresent(ui_context.renderer);  // refresh the screen
                }   
                break;
            case SDL_MOUSEMOTION:
                printf("Mouse moved to (%d, %d)\n", event.motion.x, event.motion.y);
                if (ui_context.mouse_state.is_mouse_down) {
                    printf("Mouse dragging: (%f, %f) -> (%d, %d)\n", ui_context.mouse_state.down_x, 
                        ui_context.mouse_state.down_y, event.motion.x, event.motion.y);
                }
                handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    ui_context.mouse_state.is_mouse_down = 1;
                    ui_context.mouse_state.down_x = event.button.x;
                    ui_context.mouse_state.down_y = event.button.y;
                    printf("Mouse button down at (%d, %d)\n", event.button.x, event.button.y);
                }
                handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    ui_context.mouse_state.is_mouse_down = 0;
                    printf("Mouse button up at (%d, %d)\n", event.button.x, event.button.y);
                }
                // handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
                break;
            }
        }

        // repaint with a given frame rate
        uint32_t frameTime = SDL_GetTicks();
        if (frameTime - frameStart < frameDelay) {
            SDL_Delay(frameDelay - (frameTime - frameStart));
        } else {
            frameStart = frameTime;
            // render the texture to the screen
            repaint_window();
        }
    }
    
    ui_context_cleanup(&ui_context);
    return 0;
}