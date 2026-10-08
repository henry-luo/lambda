#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gl_core.hpp"

struct DomDocument;
struct DomElement;
struct UiContext;
struct ImageSurface;
struct ViewBlock;
struct RasterRenderContext;

struct Scene3dStats {
    NativeGlStats graphics;
    uint64_t projection_generation, snapshot_generation;
    uint32_t meshes, geometries, textures;
    unsigned raster_width, raster_height;
    float camera_aspect;
};
// all retained data is document/native-owned (D4.5.2); the DOM is borrowed only during projection.
ImageSurface* scene3d_snapshot(DomElement* root, UiContext* ui, float width, float height, float raster_scale);
const char* scene3d_diagnostic(DomElement* root);
bool scene3d_stats(DomElement* root, Scene3dStats* stats);
void scene3d_collect(DomDocument* document);
void scene3d_release_subtree(struct DomNode* root);
void scene3d_context_lost(DomElement* root);
void render_scene3d_content(RasterRenderContext* context, ViewBlock* view);
