#include "render.hpp"

#include "view.hpp"

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec) {
    return rdt_vector_get_caps(vec);
}

static bool render_backend_has_native_blur(const RenderBackendCaps* caps) {
    return caps && caps->gaussian_blur;
}

static bool render_backend_has_native_color_filters(const RenderBackendCaps* caps) {
    return caps && caps->color_matrix_filters;
}

bool render_backend_supports_filter_chain(const RenderBackendCaps* caps,
                                          const FilterProp* filter) {
    if (!caps || !filter || !filter->functions) {
        return false;
    }

    const FilterFunction* func = filter->functions;
    while (func) {
        switch (func->type) {
            case FILTER_BLUR:
                if (!render_backend_has_native_blur(caps)) return false;
                break;
            case FILTER_BRIGHTNESS:
            case FILTER_CONTRAST:
            case FILTER_GRAYSCALE:
            case FILTER_HUE_ROTATE:
            case FILTER_INVERT:
            case FILTER_OPACITY:
            case FILTER_SATURATE:
            case FILTER_SEPIA:
                if (!render_backend_has_native_color_filters(caps)) return false;
                break;
            case FILTER_DROP_SHADOW:
            case FILTER_URL:
            case FILTER_NONE:
            default:
                return false;
        }
        func = func->next;
    }

    return true;
}
