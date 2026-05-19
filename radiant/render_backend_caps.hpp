#pragma once

#include "rdt_vector.hpp"

typedef struct FilterProp FilterProp;
typedef RdtVectorCaps RenderBackendCaps;

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec);
bool render_backend_has_native_blur(const RenderBackendCaps* caps);
bool render_backend_has_native_color_filters(const RenderBackendCaps* caps);
bool render_backend_supports_filter_chain(const RenderBackendCaps* caps,
                                          const FilterProp* filter);
