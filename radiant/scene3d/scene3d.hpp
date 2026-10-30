#pragma once
#include "../../lambda/lambda-data.hpp"
#include "../gl_core.hpp"
#include <stddef.h>
#include <stdint.h>

struct DomDocument;
struct DomElement;
struct UiContext;
struct ImageSurface;
struct ViewBlock;
struct RasterRenderContext;
struct PaintList;
struct Scene3dRuntime;

enum Scene3dTickPhase { SCENE3D_TICK_NAVIGATION, SCENE3D_TICK_ANIMATION, SCENE3D_TICK_PHYSICS, SCENE3D_TICK_PHASES };
enum Scene3dTickResult { SCENE3D_TICK_IDLE, SCENE3D_TICK_ACTIVE, SCENE3D_TICK_FAILED };
typedef Scene3dTickResult (*Scene3dTickFn)(void* owner, double timestep);
// internal main-owner source admission, before sampling; never a script callback.
typedef const char* (*Scene3dAdmitFn)(void* owner);
struct Scene3dRuntimeStats {
    uint64_t ticks, presentations, generation;
    double timestep, time, debt, interpolation, time_scale;
    unsigned active_services;
    bool paused, faulted;
};
// native service plumbing only; scripts use the existing radiant-dom command boundary.
Scene3dRuntime* scene3d_runtime_create(DomElement* root);
bool scene3d_runtime_bind_admission(Scene3dRuntime* runtime, void* owner, Scene3dAdmitFn admit);
void scene3d_runtime_destroy(Scene3dRuntime* runtime);
void scene3d_runtime_follow_rebuild(Scene3dRuntime* runtime, struct DomNode* old_root, struct DomNode* new_root);
bool scene3d_runtime_bind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, Scene3dTickFn tick);
bool scene3d_runtime_replace(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* previous, void* owner,
                             Scene3dTickFn tick);
bool scene3d_runtime_enable(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled);
void scene3d_runtime_unbind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner);
bool scene3d_runtime_configure(Scene3dRuntime* runtime, double timestep, unsigned max_substeps);
bool scene3d_runtime_can_configure(const Scene3dRuntime* runtime, double timestep, unsigned max_substeps);
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
    uint32_t environment_builds;    // environment captures and prefilters since the viewport was created
    uint32_t shadow_passes;         // shadow maps rendered since the viewport was created
    uint32_t shader_links;          // custom GLSL programs linked since viewport creation
    uint32_t material_links;        // retained built-in material programs linked
    uint32_t shader_programs;       // shader-material programs currently retained
    uint32_t simulation_steps;      // GPU simulation steps since the viewport was created
    uint32_t transmission_captures; // one shared opaque image per changed glass frame
    uint32_t postprocess_passes;    // color passes, excluding the common output conversion
    uint64_t visibility_tests, culled_draws, lod_skips; // decisions across presentation, capture and shadow views
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
// commands reconcile the current source before controlling playback; metrics use the observer above.
Scene3dAnimationState* scene3d_prepare_animations(DomElement* root);
Item scene3d_physics_control(DomElement* root, Item request);
struct Scene3dPhysics;
Scene3dPhysics* scene3d_physics(DomElement* root);
struct Scene3dNavigation;
Scene3dNavigation* scene3d_navigation(DomElement* root);
struct Scene3dNavigationStats {
    uint64_t compilations, invalidations, queries;
    size_t assets, nodes, edges, vertices, storage_nodes, raw_cells, source_triangles;
};
// one document-owned sequence survives viewport release/recreation and distinguishes its assets.
uint64_t* scene3d_navigation_sequence(DomDocument* document);
uint64_t* scene3d_physics_sequence(DomDocument* document);
bool scene3d_navigation_tag(DomElement* node);
Scene3dNavigation* scene3d_navigation_create(DomElement* root, Scene3dRuntime* runtime);
void scene3d_navigation_destroy(Scene3dNavigation* state);
bool scene3d_navigation_prepare(Scene3dNavigation* state, DomElement* root, char* diagnostic, size_t capacity);
void scene3d_navigation_follow_rebuild(Scene3dNavigation* state, struct DomNode* old_root, struct DomNode* new_root);
Item scene3d_navigation_operate(Scene3dNavigation* state, DomElement* root, Item request);
bool scene3d_navigation_stats(Scene3dNavigation* state, Scene3dNavigationStats* stats);
bool scene3d_navigation_generation(Scene3dNavigation* state, const char* id, uint64_t* generation);
bool scene3d_navigation_has_agents(Scene3dNavigation* state);
bool scene3d_seek(DomElement* root, size_t ticks);
