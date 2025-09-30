#include "view.hpp"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

typedef struct {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color;
    Tvg_Canvas* canvas;   // ThorVG canvas

    UiContext* ui_context;
} RenderContext;

// Function declarations
void render_html_doc(UiContext* uicon, View* root_view, const char* output_file);
