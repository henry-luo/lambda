#pragma once
#include "../../dom/dom_graphics.h"

// Radiant implements the runtime-owned native operation contract (D7.1.3).
#define RADIANT_WEBGL_DECLARE(result, name, parameters) extern "C" result radiant_webgl_##name parameters;
DOM_WEBGL_ENGINE_OPERATIONS(RADIANT_WEBGL_DECLARE)
#undef RADIANT_WEBGL_DECLARE

struct NativeGlStats;
extern "C" bool radiant_webgl_stats(void* canvas, NativeGlStats* stats);
