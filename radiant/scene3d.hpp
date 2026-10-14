#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gl_core.hpp"
#include "../lambda/lambda-data.hpp"

struct DomDocument;
struct DomElement;
struct UiContext;
struct ImageSurface;
struct ViewBlock;
struct RasterRenderContext;
struct PaintList;

struct Scene3dStats {
    NativeGlStats graphics;
    uint64_t projection_generation, snapshot_generation;
    uint64_t projection_bytes, projection_reserved_bytes, snapshot_bytes;
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
bool scene3d_paint_content(PaintList* paint, UiContext* ui, ViewBlock* view, float x, float y);

void scene3d_prepare_document(DomDocument* document, UiContext* ui, float raster_scale);
struct Scene3dAnimationState;
// document-owned playback is separate from authored scene values and disposable GPU projections.
Scene3dAnimationState* scene3d_animations(DomElement* root);
Item scene3d_physics_control(DomElement* root,Item request);
struct Scene3dPhysics;
Scene3dPhysics* scene3d_physics(DomElement* root);
