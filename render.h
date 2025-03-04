#include "view.h"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

typedef struct {
    BlockBlot block;
    // FontProp* font; // current font style
    // FT_Face face;   // current font face
    // float space_width;
    FontBox font;  // current font style

    UiContext* ui_context;
} RenderContext;