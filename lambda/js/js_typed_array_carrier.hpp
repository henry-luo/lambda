#pragma once

#include "../lambda-data.hpp"
#include "js_typed_array.h"

// Shared with MIR guards; the map kind certifies this trailing payload.
struct JsTypedArrayMapCarrier {
    Map base;
    JsTypedArray payload;
};
