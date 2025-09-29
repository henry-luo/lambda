#pragma once

#include "view.hpp"
#include "font_face.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct UiContext;
struct FontBox;
struct FontProp;

// Font face entry management
typedef struct FontfaceEntry {
    char* name;
    FT_Face face;
} FontfaceEntry;

// Function declarations
int fontface_compare(const void *a, const void *b, void *udata);
void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop);
bool fontface_entry_free(const void *item, void *udata);
void fontface_cleanup(UiContext* uicon);

#ifdef __cplusplus
}
#endif
