#pragma once
#include "render.hpp"
#include "../lambda/core/mark_reader.hpp"

struct GeoMapExpression;
enum GeoMapLayerKind { GEOMAP_BACKGROUND, GEOMAP_FILL, GEOMAP_LINE, GEOMAP_CIRCLE };
enum GeoMapStyleDependency { GEOMAP_STYLE_FEATURE = 1, GEOMAP_STYLE_ZOOM = 2 };

// compiled nodes borrow the description owned by their frame, or a rooted one-shot caller (D4.5.2).
struct GeoMapStyle {
    Arena* arena;
    GeoMapExpression *color, *opacity, *size, *filter;
    GeoMapExpression *outline, *stroke_color, *stroke_width, *stroke_opacity;
    GeoMapLayerKind kind;
    const char* id;
    double minzoom, maxzoom;
    unsigned dependencies;
    bool hidden;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
};
struct GeoMapStyleResult {
    Color color, outline, stroke_color;
    float size, stroke_width;
    unsigned warnings;
    bool visible, has_outline;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
};
bool geomap_style_compile(ElementReader layer, GeoMapStyle* style, char* diagnostic, size_t capacity);
void geomap_style_evaluate(const GeoMapStyle* style, ItemReader feature, double zoom, GeoMapStyleResult* result);
void geomap_style_destroy(GeoMapStyle* style);
