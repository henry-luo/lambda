#include "render_backend_caps.hpp"

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec) {
    return rdt_vector_get_caps(vec);
}
