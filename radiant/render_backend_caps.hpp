#pragma once

#include "rdt_vector.hpp"

typedef RdtVectorCaps RenderBackendCaps;

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec);
