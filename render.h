#include "view.h"

typedef struct {
    int x, y;  // abs x, y relative to entire canvas/screen
} BlockBlot;

typedef struct {
    UiContext* ui_context;
    BlockBlot block;
    FT_Library library;
    FT_Face face;   // current font face 
} RenderContext;