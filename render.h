#include "view.h"

typedef struct {
    BlockBlot block;
    FontProp* font; // current font style
    FT_Face face;   // current font face
    float space_width;
    FT_Library library;
    UiContext* ui_context;
} RenderContext;