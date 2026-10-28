#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../gl_core.hpp"
#include "../../lambda/lambda-data.hpp"

struct DomDocument;
struct DomElement;
struct UiContext;
struct ImageSurface;
struct ViewBlock;
struct RasterRenderContext;
struct PaintList;
struct Scene3dRuntime;

enum Scene3dTickPhase { SCENE3D_TICK_ANIMATION, SCENE3D_TICK_PHYSICS, SCENE3D_TICK_PHASES };
enum Scene3dTickResult { SCENE3D_TICK_IDLE, SCENE3D_TICK_ACTIVE, SCENE3D_TICK_FAILED };
typedef Scene3dTickResult (*Scene3dTickFn)(void* owner, double timestep);
struct Scene3dRuntimeStats {
    uint64_t ticks, presentations, generation;
    double timestep, time, debt, interpolation, time_scale;
    unsigned active_services;
    bool paused, faulted;
};
// native service plumbing only; scripts use the existing radiant-dom command boundary.
Scene3dRuntime* scene3d_runtime_create(DomElement* root);
void scene3d_runtime_destroy(Scene3dRuntime* runtime);
void scene3d_runtime_follow_rebuild(Scene3dRuntime* runtime, struct DomNode* old_root, struct DomNode* new_root);
bool scene3d_runtime_bind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, Scene3dTickFn tick);
bool scene3d_runtime_enable(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled);
void scene3d_runtime_unbind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner);
bool scene3d_runtime_configure(Scene3dRuntime* runtime, double timestep, unsigned max_substeps);
bool scene3d_runtime_pause(Scene3dRuntime* runtime, bool paused);
bool scene3d_runtime_time_scale(Scene3dRuntime* runtime, double scale);
bool scene3d_runtime_advance(Scene3dRuntime* runtime, double elapsed);
bool scene3d_runtime_step(Scene3dRuntime* runtime, size_t ticks);
bool scene3d_runtime_reset_clock(Scene3dRuntime* runtime, uint64_t tick);
bool scene3d_runtime_stats(Scene3dRuntime* runtime, Scene3dRuntimeStats* stats);
const char* scene3d_runtime_diagnostic(Scene3dRuntime* runtime);
Item scene3d_runtime_operate(Scene3dRuntime* runtime, Item request);
Item scene3d_runtime_snapshot(Scene3dRuntime* runtime);
Scene3dRuntime* scene3d_runtime(DomElement* root);
Item scene3d_control(DomElement* root, Item request);

struct Scene3dStats {
    NativeGlStats graphics;
    Scene3dRuntimeStats runtime;
    uint64_t projection_generation, snapshot_generation;
    uint64_t projection_bytes, projection_reserved_bytes, snapshot_bytes;
    uint32_t meshes, geometries, textures;
    uint32_t environment_builds; // environment captures and prefilters since the viewport was created
    uint32_t shadow_passes;      // shadow maps rendered since the viewport was created
    uint32_t shader_links;       // custom GLSL programs linked since viewport creation
    uint32_t material_links;     // retained built-in material programs linked
    uint32_t shader_programs;    // shader-material programs currently retained
    uint32_t simulation_steps;   // GPU simulation steps since the viewport was created
    uint32_t transmission_captures; // one shared opaque image per changed glass frame
    uint32_t postprocess_passes; // color passes, excluding the common output conversion
    uint64_t visibility_tests,culled_draws,lod_skips; // decisions across presentation, capture and shadow views
    uint64_t lod_state_bytes; // document-owned hysteresis, separate from the disposable projection
    uint32_t lods;
    unsigned raster_width, raster_height;
    float camera_aspect;
};
// all retained data is document/native-owned (D4.5.2); the DOM is borrowed only during projection.
ImageSurface* scene3d_snapshot(DomElement* root, UiContext* ui, float width, float height, float raster_scale);
// the 16 x 16 RGBA split-sum DFG table standard materials sample (rows n·v, columns roughness)
const float* scene3d_dfg_table();
const char* scene3d_diagnostic(DomElement* root);
bool scene3d_stats(DomElement* root, Scene3dStats* stats);
void scene3d_collect(DomDocument* document);
void scene3d_release_subtree(struct DomNode* root);
void scene3d_follow_rebuild(DomDocument* document, struct DomNode* old_root, struct DomNode* new_root);
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
