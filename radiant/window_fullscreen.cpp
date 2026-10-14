#include "window_fullscreen.hpp"
#include <math.h>

float radiant_fullscreen_overlap(RadiantWindowBounds a, RadiantWindowBounds b) {
    float width = fmaxf(0, fminf(a.x + a.width, b.x + b.width) - fmaxf(a.x, b.x));
    float height = fmaxf(0, fminf(a.y + a.height, b.y + b.height) - fmaxf(a.y, b.y));
    return width * height;
}

RadiantWindowBounds radiant_fullscreen_fit(RadiantWindowBounds bounds, RadiantWindowBounds work) {
    bounds.width = fminf(bounds.width, work.width);
    bounds.height = fminf(bounds.height, work.height);
    bounds.x = fmaxf(work.x, fminf(bounds.x, work.x + work.width - bounds.width));
    bounds.y = fmaxf(work.y, fminf(bounds.y, work.y + work.height - bounds.height));
    return bounds;
}

bool radiant_fullscreen_request(RadiantFullscreenState* state, bool enabled) {
    if (!state || !state->available) return false;
    state->request_generation++;
    state->requested = enabled;
    state->pending = state->requested != state->actual;
    return true;
}

static void fullscreen_observe(RadiantFullscreenState* state,
                               const RadiantWindowPlacement& placement) {
    if (state->actual != placement.fullscreen) {
        state->actual = placement.fullscreen;
        state->changed = true;
        if (!state->pending && !state->applying) state->requested = state->actual;
    }
    // maximized bounds are not the restore rectangle; retain the last normal one.
    if (!placement.fullscreen && !placement.maximized) {
        state->restore.bounds = placement.bounds;
        state->restore_valid = placement.bounds.width > 0 && placement.bounds.height > 0;
    }
}

bool radiant_fullscreen_update(RadiantFullscreenState* state,
    const RadiantFullscreenBackend* backend, void* context, bool commit) {
    if (!state || !backend || state->applying) return false;
    RadiantWindowPlacement before = {};
    state->available = backend->read(context, &before);
    if (!state->available) {
        state->failed |= state->pending;
        state->changed |= state->actual;
        state->actual = state->requested = state->pending = false;
        return state->changed || state->failed;
    }
    fullscreen_observe(state, before);
    if (!commit || !state->pending) return state->changed || state->failed;
    state->pending = false;
    if (state->requested == state->actual) return state->changed || state->failed;
    if (state->requested) {
        state->restore.maximized = before.maximized;
        state->restore.fullscreen = false;
        // initialization normally records a normal rectangle before maximization.
        if (!state->restore_valid) state->restore.bounds = before.bounds;
    }
    state->applying = true;
    bool requested = state->requested;
    uint64_t generation = state->request_generation;
    bool applied = backend->apply(context, requested, &state->restore);
    RadiantWindowPlacement after = {};
    bool observed = backend->read(context, &after);
    if (observed) fullscreen_observe(state, after);
    state->applying = false;
    state->failed |= !applied || !observed || state->actual != requested;
    // synchronous platform callbacks may queue a newer command during the switch.
    if (state->request_generation == generation) state->requested = state->actual;
    state->pending = state->requested != state->actual;
    return true;
}
