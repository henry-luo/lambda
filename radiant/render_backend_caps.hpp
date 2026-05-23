#pragma once

#include "rdt_vector.hpp"

typedef struct FilterProp FilterProp;
typedef RdtVectorCaps RenderBackendCaps;

typedef enum RenderExportTargetKind {
    RENDER_EXPORT_TARGET_SVG,
    RENDER_EXPORT_TARGET_PDF,
} RenderExportTargetKind;

typedef struct RenderExportTargetCaps {
    RenderExportTargetKind target;
    const char* target_name;
    bool rects;
    bool rounded_rects;
    bool paths;
    bool strokes;
    bool gradients;
    bool images;
    bool glyph_runs;
    bool clips;
    bool transforms;
    bool opacity_groups;
    bool blend_modes;
    bool filters;
    bool shadows;
} RenderExportTargetCaps;

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec);
bool render_backend_has_native_blur(const RenderBackendCaps* caps);
bool render_backend_has_native_color_filters(const RenderBackendCaps* caps);
bool render_backend_supports_filter_chain(const RenderBackendCaps* caps,
                                          const FilterProp* filter);

static inline const RenderExportTargetCaps* render_export_target_get_caps(RenderExportTargetKind target) {
    static const RenderExportTargetCaps svg_caps = {
        RENDER_EXPORT_TARGET_SVG,
        "svg",
        true,   // rects
        true,   // rounded_rects
        true,   // paths
        true,   // strokes
        true,   // gradients
        true,   // images
        true,   // glyph_runs
        true,   // clips
        true,   // transforms
        true,   // opacity_groups
        false,  // blend_modes
        false,  // filters
        false,  // shadows
    };

    static const RenderExportTargetCaps pdf_caps = {
        RENDER_EXPORT_TARGET_PDF,
        "pdf",
        true,   // rects
        true,   // rounded_rects
        true,   // paths
        true,   // strokes
        false,  // gradients
        true,   // images
        true,   // glyph_runs
        true,   // clips
        true,   // transforms
        false,  // opacity_groups
        false,  // blend_modes
        false,  // filters
        false,  // shadows
    };

    switch (target) {
    case RENDER_EXPORT_TARGET_PDF:
        return &pdf_caps;
    case RENDER_EXPORT_TARGET_SVG:
    default:
        return &svg_caps;
    }
}
