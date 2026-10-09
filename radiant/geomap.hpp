#pragma once
#include "render.hpp"

struct DomElement;
struct DomNode;
struct Input;
struct Pool;
struct GeoMapIndexNode {
    float left, top, right, bottom;
    int first, second, shape;
};
struct GeoMapShape {
    RdtPath* path;
    Color color;
    float stroke_width;
    RdtFillRule rule;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
    const char *source, *layer;
    Item feature;
    int64_t feature_index, record;
};

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

// immutable projection owns its Input and paths; consumers explicitly retain historical frames (D4.5.2).
struct GeoMapFrame {
    Pool* pool;
    Input* input;
    ArrayList* shapes;
    ArrayList* styles;
    GeoMapIndexNode* index;
    unsigned index_count, references;
    GeoMapCamera camera;
    uint64_t revision;
};
GeoMapFrame* geomap_plan(Element* model, float width, float height, char* diagnostic, size_t capacity);
GeoMapFrame* geomap_displayed_frame(DomElement* root);
void geomap_frame_retain(GeoMapFrame* frame);
void geomap_frame_release(GeoMapFrame* frame);
void geomap_frame_paint(PaintList* paint, const GeoMapFrame* frame, Rect viewport);
StrBuf* geomap_frame_svg(const GeoMapFrame* frame);
void geomap_release_subtree(DomNode* root);
bool geomap_shape_hit(const GeoMapShape* shape, Rect query, bool rectangle, float tolerance = 0);

// emits owned, realm-neutral paths; no borrowed Mark/DOM values survive this call (D4.5.2).
bool geomap_paint(PaintList* paint, DomElement* root, Rect viewport,
    char* diagnostic = nullptr, size_t diagnostic_capacity = 0);
StrBuf* geomap_svg(Element* model, float width, float height);
void render_geomap_content(RasterRenderContext* context, ViewBlock* view);
