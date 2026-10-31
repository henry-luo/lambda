#pragma once
#include "../../lambda/lambda-data.hpp"
#include "../gl_core.hpp"
#include "scene3d_math.hpp"
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
static constexpr unsigned SCENE3D_NODE_LIMIT = 8192, SCENE3D_INSTANCE_LIMIT = 16384;

enum Scene3dTickPhase {
    SCENE3D_TICK_NAVIGATION,
    SCENE3D_TICK_ANIMATION,
    SCENE3D_TICK_PHYSICS,
    SCENE3D_TICK_PARTICLES,
    SCENE3D_TICK_PHASES
};
enum Scene3dTickResult { SCENE3D_TICK_IDLE, SCENE3D_TICK_ACTIVE, SCENE3D_TICK_FAILED };
typedef Scene3dTickResult (*Scene3dTickFn)(void* owner, double timestep);
// internal main-owner source admission, before sampling; never a script callback.
typedef const char* (*Scene3dAdmitFn)(void* owner);
typedef const char* (*Scene3dCommandFn)(void* owner, const void* payload);
static constexpr unsigned SCENE3D_COMMAND_BATCH_LIMIT = 32;
struct Scene3dCommandBatch {
    uint64_t session, generation, service_generation, sequence, tick;
    Scene3dTickPhase phase;
    void* payload;
    void (*release)(void* payload);
};
struct Scene3dRuntimeStats {
    uint64_t ticks, presentations, generation, session;
    uint64_t service_generations[SCENE3D_TICK_PHASES];
    uint64_t command_sequence, commands_applied, commands_rejected, last_command_sequence, last_command_tick;
    uint64_t next_command_tick;
    double timestep, time, debt, interpolation, time_scale;
    unsigned active_services, pending_commands;
    bool paused, faulted, last_command_applied;
    char command_diagnostic[192];
};
// native service plumbing only; scripts use the existing radiant-dom command boundary.
Scene3dRuntime* scene3d_runtime_create(DomElement* root);
bool scene3d_runtime_bind_admission(Scene3dRuntime* runtime, void* owner, Scene3dAdmitFn admit);
void scene3d_runtime_destroy(Scene3dRuntime* runtime);
void scene3d_runtime_follow_rebuild(Scene3dRuntime* runtime, struct DomNode* old_root, struct DomNode* new_root);
bool scene3d_runtime_bind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, Scene3dTickFn tick);
bool scene3d_runtime_replace(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* previous, void* owner,
                             Scene3dTickFn tick, Scene3dCommandFn commands = nullptr);
bool scene3d_runtime_bind_commands(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner,
                                   Scene3dCommandFn apply);
bool scene3d_runtime_command_wake(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled);
bool scene3d_runtime_revise(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner);
// ownership transfers only on success; payloads contain copied native data and never script values/callbacks.
bool scene3d_runtime_submit(Scene3dRuntime* runtime, const Scene3dCommandBatch& batch, const char** error);
bool scene3d_runtime_enable(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled);
void scene3d_runtime_unbind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner);
bool scene3d_runtime_configure(Scene3dRuntime* runtime, double timestep, unsigned max_substeps);
bool scene3d_runtime_can_configure(const Scene3dRuntime* runtime, double timestep, unsigned max_substeps);
bool scene3d_runtime_pause(Scene3dRuntime* runtime, bool paused);
bool scene3d_runtime_time_scale(Scene3dRuntime* runtime, double scale);
bool scene3d_runtime_advance(Scene3dRuntime* runtime, double elapsed);
bool scene3d_runtime_step(Scene3dRuntime* runtime, size_t ticks);
bool scene3d_runtime_reset_clock(Scene3dRuntime* runtime, uint64_t tick);
bool scene3d_runtime_tick_index(const Scene3dRuntime* runtime, double seconds, size_t* tick);
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
bool scene3d_navigation_body_owned(Scene3dNavigation* state, DomElement* body);
bool scene3d_seek(DomElement* root, size_t ticks);

struct Scene3dParticles;
struct Scene3dParticleGpu;
struct Scene3dParticleGpuOps {
    void *owner = nullptr;
    bool (*acquire)(void *owner, NativeGlContext **graphics, NativeGlResource *triangle,
                    NativeGlResource *program) = nullptr;
    Scene3dParticleGpu **registry = nullptr; // includes private replay candidates until graphics-owner retirement
};
struct Scene3dEmitterView {
    const Scene3dMatrix *matrices; // owner-borrowed; reacquire after source/replay changes
    unsigned capacity, count, live;
    uint64_t sequence, spawned, dropped, recycled, storage_bytes;
    double time;
    Scene3dVec lo, hi;
    bool gpu;
    const float *slots;
    NativeGlResource gpu_positions, gpu_births;
    float max_size;
    uint64_t gpu_readbacks, gpu_passes, gpu_recoveries, gpu_bytes;
    uint64_t collisions, killed, collision_queries, collision_work, collision_triangles, pending_events;
    bool world, mirrored, rotating;
    uint64_t surface_versions, surface_triangles, surface_bytes;
};
struct Scene3dParticleSample {
    uint64_t sequence;
    double age, lifetime;
    Scene3dVec position, velocity;
    float size;
    Scene3dVec rotation; // local Euler XYZ radians, before the retained affine birth basis
};
Scene3dParticles *scene3d_particles_create(DomElement *root, Scene3dRuntime *runtime,
                                           const Scene3dParticleGpuOps &gpu = {});
void scene3d_particles_destroy(Scene3dParticles *state);
bool scene3d_particles_prepare(Scene3dParticles *state, DomElement *root, bool automatic, char *diagnostic,
                               size_t capacity);
void scene3d_particles_follow_rebuild(Scene3dParticles *state, struct DomNode *old_root, struct DomNode *new_root);
uint64_t scene3d_particles_generation(Scene3dParticles *state);
bool scene3d_particles_view(Scene3dParticles *state, DomElement *emitter, Scene3dEmitterView *view);
bool scene3d_particles_sample(Scene3dParticles *state, DomElement *emitter, unsigned index,
                              Scene3dParticleSample *sample);
bool scene3d_particles_samples(Scene3dParticles *state, DomElement *emitter, unsigned first, unsigned count,
                               Scene3dParticleSample *samples);
Item scene3d_particles_snapshot(Scene3dParticles *state);
Item scene3d_particles_events(Scene3dParticles *state, Item request);
bool scene3d_particles_needs_world(Scene3dParticles *state);
const char *scene3d_particles_diagnostic(Scene3dParticles *state);
bool scene3d_particles_replay_attach(Scene3dParticles *state, Scene3dRuntime *runtime, struct Scene3dPhysics *physics,
                                     struct Scene3dAnimationState *animation);
void scene3d_particles_replay_detach(Scene3dParticles *state);

Scene3dParticles *scene3d_particles(DomElement *root);
// authored local-space reconstruction is disposable until the scene seek succeeds.
Scene3dParticles *scene3d_particles_replay(Scene3dParticles *state, size_t ticks, double timestep, char *diagnostic,
                                           size_t capacity);
// collision replay candidates publish through coordinated scene seek, alongside their query world.
bool scene3d_particles_adopt(Scene3dParticles *state, Scene3dParticles *replay);
