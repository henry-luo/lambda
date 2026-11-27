#pragma once

#include "lambda/lambda.h"

// Publish the WebIDL method on HTMLCanvasElement after its prototype exists.
extern "C" void dom_canvas_install_html_interface(Item html_canvas_prototype);

// ImageData uses private branded records over owned typed-array storage.
extern "C" Item dom_image_data_call(Item, Item, Item*, int, uint64_t*);
extern "C" Item dom_image_data_construct(Item, Item*, int, Item, uint64_t*);
extern "C" void dom_canvas_install_pixel_interface(Item constructor,
    Item image_data_prototype, Item context_prototype);
extern "C" void dom_canvas_install_gradient_interface(Item constructor,
    Item prototype, Item context_constructor, Item context_prototype);

// Width/height reset native 2D state as well as the retained bitmap.
extern "C" void dom_canvas_reset_context(Item canvas);

// OffscreenCanvas is available without a bound document, so its constructor
// installs the same method during global initialization.
extern "C" void js_canvas_install_offscreen_canvas_interface(Item constructor);
