#include "view.h"

typedef struct {
    float x, y;  // abs x, y relative to entire canvas/screen
} BlockBlot;

typedef struct {
    BlockBlot block;
    FontProp* font; // current font style
    FT_Face face;   // current font face
    FT_Library library;
    UiContext* ui_context;
} RenderContext;