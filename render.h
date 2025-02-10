#include "view.h"

typedef struct {
    int x, y;  // abs x, y relative to entire canvas/screen
} BlockBlot;

typedef struct {
    BlockBlot block;
    FT_Face face;   // current font face
    FT_Library library;
    UiContext* ui_context;
} RenderContext;