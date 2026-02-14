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

// Function declarations
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
void fontface_cleanup(UiContext* uicon);

#ifdef __cplusplus
}
#endif
