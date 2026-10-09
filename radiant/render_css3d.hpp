#ifndef RADIANT_RENDER_CSS3D_HPP
#define RADIANT_RENDER_CSS3D_HPP

#include "render.hpp"
#include "../lib/arraylist.hpp"
#include "../lib/scratch_arena.h"

// Homogeneous positions and original plane coordinates survive clipping and
// splitting. Vertex storage is borrowed from the caller's render scratch scope
// (D4.5.1v4); fragments retain no Lambda values or document ownership.
struct Css3dVertex {
    float x, y, z, w;
    float u, v;
};

struct Css3dPolygon {
    lam::Up<const Css3dVertex> vertices;
    size_t count;
    size_t fragment;
    uint64_t paint_order;
};

bool css3d_project_quad(const RdtMatrix4* matrix, Rect rect, size_t fragment,
    uint64_t paint_order, ScratchScope* scratch, Css3dPolygon* polygon);
bool css3d_order_planes(const Css3dPolygon* polygons, size_t count,
    ScratchScope* scratch, lam::ArrayList<Css3dPolygon>* ordered);
bool css3d_clip_to_viewport(Css3dPolygon* polygon, const RdtMatrix* paint,
    Rect viewport, ScratchScope* scratch);

struct Css3dPaintPlane {
    lam::Up<ViewBlock> view;
    RdtMatrix4 depth;
    RdtMatrix paint;
    Rect rect;
};

struct Css3dPaintRun {
    Css3dPaintPlane plane;
    int start, end;
    int packet_start, packet_end;
};

// One stack-owned collector spans a preserved context. Flattened descendants
// are captured as indivisible runs; nested contexts compose before that run.
struct Css3dPaintContext {
    lam::Up<RasterRenderContext> rdcon;
    lam::Up<ViewBlock> root;
    lam::ArrayList<Css3dPaintRun> runs;
    Css3dPaintPlane current;
    int start, run_start;
    bool failed;
    Css3dPaintContext(RasterRenderContext* context, ViewBlock* block);
    Css3dPaintPlane enter(ViewBlock* block);
    void restore(const Css3dPaintPlane& plane);
    void geometry(ViewBlock* block);
    void flush();
    bool compose();
};

#endif
