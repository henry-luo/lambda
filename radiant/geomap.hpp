#pragma once
#include "render.hpp"

struct DomElement;

// geographic calculations stay in double precision until rebased to the viewport.
struct GeoMapCamera {
    double longitude, latitude, zoom, bearing;
    double width, height;
};
bool geomap_camera_valid(const GeoMapCamera& camera);
bool geomap_project(const GeoMapCamera& camera, double longitude, double latitude,
    double* x, double* y);
bool geomap_unproject(const GeoMapCamera& camera, double x, double y,
    double* longitude, double* latitude);

// emits owned, realm-neutral paths; no borrowed Mark/DOM values survive this call (D4.5.2).
bool geomap_paint(PaintList* paint, DomElement* root, Rect viewport,
    char* diagnostic = nullptr, size_t diagnostic_capacity = 0);
StrBuf* geomap_svg(Element* model, float width, float height);
void render_geomap_content(RasterRenderContext* context, ViewBlock* view);
