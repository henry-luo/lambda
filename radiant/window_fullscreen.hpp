#pragma once
#include <stdint.h>

// internal window presentation state; geometry stays in logical screen coordinates.
struct RadiantWindowBounds { float x, y, width, height; };
struct RadiantWindowPlacement {
    RadiantWindowBounds bounds;
    bool maximized;
    bool fullscreen;
};
struct RadiantFullscreenBackend {
    bool (*read)(void* context, RadiantWindowPlacement* placement);
    bool (*apply)(void* context, bool enabled, const RadiantWindowPlacement* restore);
};
struct RadiantFullscreenState {
    RadiantWindowPlacement restore;
    uint64_t request_generation;
    bool restore_valid;
    bool available;
    bool actual;
    bool requested;
    bool pending;
    bool applying;
    bool changed;
    bool failed;
    bool toggle_key_down;
    bool escape_key_down;
    // the document prevented the current Escape press: the host neither leaves
    // fullscreen nor closes the viewer for it (radiant_window_escape_default)
    bool escape_page_consumed;
};

float radiant_fullscreen_overlap(RadiantWindowBounds a, RadiantWindowBounds b);
RadiantWindowBounds radiant_fullscreen_fit(RadiantWindowBounds bounds, RadiantWindowBounds work);
bool radiant_fullscreen_request(RadiantFullscreenState* state, bool enabled);
bool radiant_fullscreen_update(RadiantFullscreenState* state,
    const RadiantFullscreenBackend* backend, void* context, bool commit);
