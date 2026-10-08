#pragma once
#include "../lambda-data.hpp"
extern "C" void dom_webgl_register_static(void);
extern "C" void dom_webgl_install_globals(void);
Item dom_webgl_context_for(Item canvas, Item options);
