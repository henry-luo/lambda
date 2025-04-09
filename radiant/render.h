#include "view.h"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

typedef struct {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color; 
    Tvg_Canvas* canvas;    // ThorVG canvas
    
    UiContext* ui_context;
} RenderContext;