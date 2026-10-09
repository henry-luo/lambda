#pragma once
#include "render.hpp"
#include "../lambda/core/mark_reader.hpp"

struct GeoMapExpression;
enum GeoMapLayerKind { GEOMAP_BACKGROUND, GEOMAP_FILL, GEOMAP_LINE, GEOMAP_CIRCLE };
enum GeoMapStyleDependency { GEOMAP_STYLE_FEATURE = 1, GEOMAP_STYLE_ZOOM = 2 };

// compiled nodes borrow the rooted description only for a single paint/query call (D4.5.2).
struct GeoMapStyle {
    Arena* arena;
    GeoMapExpression *color, *opacity, *size, *filter;
    GeoMapLayerKind kind;
    const char* id;
    double minzoom, maxzoom;
    unsigned dependencies;
    bool hidden;
};
struct GeoMapStyleResult {
    Color color;
    float size;
    unsigned warnings;
    bool visible;
};
bool geomap_style_compile(ElementReader layer, GeoMapStyle* style, char* diagnostic, size_t capacity);
void geomap_style_evaluate(const GeoMapStyle* style, ItemReader feature, double zoom, GeoMapStyleResult* result);
void geomap_style_destroy(GeoMapStyle* style);
