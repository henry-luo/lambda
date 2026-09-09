#pragma once

#include "../lambda.h"

// Publish the WebIDL method on HTMLCanvasElement after its prototype exists.
extern "C" void dom_canvas_install_html_interface(Item html_canvas_prototype);

// OffscreenCanvas is available without a bound document, so its constructor
// installs the same method during global initialization.
extern "C" void js_canvas_install_offscreen_canvas_interface(Item constructor);
