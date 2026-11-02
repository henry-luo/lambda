#pragma once
#include "../../lib/arraylist.hpp"
#include "../../lib/navigation.h"
#include "scene3d_math.hpp"
#include "../gl_core.hpp"
#include "../../lib/physics/physics.h"
#include <stddef.h>
#include <stdint.h>
struct DomElement;
struct DomNode;
struct Pool;
struct Scene3dAnimationState;
class ItemReader;
struct DomNodeRef;
typedef bool (*Scene3dPoseOverrideFn)(void* owner, DomElement* node, Scene3dMatrix* world);

static constexpr unsigned SCENE3D_PARTICLE_CURVE_LIMIT = 16;
enum Scene3dParticleCurveKind {
    SCENE3D_PARTICLE_SIZE_CURVE, SCENE3D_PARTICLE_ROTATION_CURVE,
    SCENE3D_PARTICLE_COLOR_CURVE, SCENE3D_PARTICLE_OPACITY_CURVE,
    SCENE3D_PARTICLE_VELOCITY_CURVE, SCENE3D_PARTICLE_CURVES
};
static constexpr unsigned SCENE3D_PARTICLE_GPU_POSE_TEXELS = 3;
static constexpr unsigned SCENE3D_PARTICLE_SHEET_LOOP = 1, SCENE3D_PARTICLE_SHEET_RANDOM = 2,
                          SCENE3D_PARTICLE_SHEET_LIFETIME = 4;
struct Scene3dParticleSheet {
    unsigned columns, rows, first, count, flags;
    float rate, inset;
};
static constexpr size_t SCENE3D_PARTICLE_GPU_SLOT_BYTES =
    (9 + 1 + 2 * (SCENE3D_PARTICLE_GPU_POSE_TEXELS + 1)) * 4 * sizeof(float);
static constexpr unsigned SCENE3D_PARTICLE_ANALYTIC_LIMIT = 8, SCENE3D_PARTICLE_HISTORY_LIMIT = 4096;
struct Scene3dParticleNoise {
    float strength[3], frequency;
};
static inline bool scene3d_particle_noise_enabled(const Scene3dParticleNoise &noise) {
    return noise.strength[0] || noise.strength[1] || noise.strength[2];
}
struct Scene3dParticleAnalytic {
    float shapes[SCENE3D_PARTICLE_ANALYTIC_LIMIT][4]; // plane normal/offset or sphere center/radius
    float kinds[SCENE3D_PARTICLE_ANALYTIC_LIMIT];
    unsigned count, impacts, response;
    float radius, skin, restitution, friction, bounce_threshold;
};
static constexpr unsigned SCENE3D_PARTICLE_SURFACE_LIMIT = 4096, SCENE3D_PARTICLE_SURFACE_VERSIONS = 8,
                          SCENE3D_PARTICLE_SURFACE_COLUMNS = 128,
                          SCENE3D_PARTICLE_SURFACE_ROWS =
                              SCENE3D_PARTICLE_SURFACE_LIMIT / SCENE3D_PARTICLE_SURFACE_COLUMNS;
struct Scene3dParticleSurfaceTriangle {
    float vertices[3][4]; // the first vertex's w is the normalized area prefix
    float normal[4];
};
struct Scene3dParticleSurface {
    Scene3dParticleSurfaceTriangle *triangles;
    unsigned count, records, references;
    uint64_t hash;
    Scene3dVec lo, hi;
};
struct Scene3dParticleSurfaces {
    Scene3dParticleSurface *values[SCENE3D_PARTICLE_SURFACE_VERSIONS];
};
bool scene3d_particle_surface_read(DomElement *root, DomElement *emitter, Pool *pool, Scene3dParticleSurface **result,
                                   char *diagnostic, size_t capacity);
bool scene3d_particle_surface_retain(Scene3dParticleSurface *surface);
void scene3d_particle_surface_release(Scene3dParticleSurface *surface);
bool scene3d_particle_surface_same(const Scene3dParticleSurface *a, const Scene3dParticleSurface *b);
void scene3d_particle_surface_sample(const Scene3dParticleSurface *surface, double face, double u, double v,
                                     Scene3dVec *position, Scene3dVec *normal);

struct Scene3dParticleGpu;
static constexpr unsigned SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT = 64,
                          SCENE3D_PARTICLE_TRAIL_POINT_LIMIT = 1048576;
struct Scene3dParticleTrailPoint;
struct Scene3dParticleTrailDesc {
    unsigned samples;
    float lifetime, width;
    bool fade;
};
struct Scene3dParticleTrailState {
    // CPU staging rings or immutable GPU checkpoint anchors; ordinary GPU steps never write these arrays.
    Scene3dParticleTrailPoint *points[2];
    float *slots, *styles;
    unsigned capacity, samples, columns, rows, records, head;
    size_t bytes;
    struct Bounds { Scene3dVec lo, hi; bool valid; } bounds[2][SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT];
    bool gpu;
};
bool scene3d_particle_trail_read(DomElement *node, Scene3dParticleTrailDesc *description);
bool scene3d_particle_trail_same(const Scene3dParticleTrailDesc &a, const Scene3dParticleTrailDesc &b);
unsigned scene3d_particle_trail_records(unsigned capacity);
Scene3dParticleTrailState *scene3d_particle_trail_create(unsigned capacity, unsigned samples, bool gpu = false);
void scene3d_particle_trail_destroy(Scene3dParticleTrailState *trail);
void scene3d_particle_trail_begin(Scene3dParticleTrailState *trail);
void scene3d_particle_trail_append(Scene3dParticleTrailState *trail, unsigned slot, Scene3dVec position,
                                   bool newborn, double timestep);
void scene3d_particle_trail_commit(Scene3dParticleTrailState *trail);
unsigned scene3d_particle_trail_samples(const Scene3dParticleTrailState *trail, unsigned slot,
                                        Scene3dParticleTrailPoint *points);
struct Scene3dParticleSample;
struct Scene3dParticleGpuOps;
struct Scene3dParticleGpuMetadata {
    float age, lifetime, initial_size, active;
};
// nine RGBA texels per physical slot; sequence words are exact unsigned 16-bit values.
struct Scene3dParticleGpuBirth {
    float sequence[4], shape_extent[4], velocity_radius[4], cone_spread_anchor[4];
    float anchor_position_age[4], anchor_velocity[4];
    float frame[3][4]; // affine birth rows; identity for local simulation
};
struct Scene3dParticleGpuControls {
    float gravity[3], drag, seed[4], curves[SCENE3D_PARTICLE_CURVES][SCENE3D_PARTICLE_CURVE_LIMIT][4];
    unsigned curve_counts[SCENE3D_PARTICLE_CURVES];
    Scene3dParticleAnalytic collision;
    Scene3dParticleNoise noise;
    Scene3dParticleSheet sheet;
};
static inline bool scene3d_particle_velocity_enabled(const Scene3dParticleGpuControls &controls) {
    for (unsigned i = 0; i < controls.curve_counts[SCENE3D_PARTICLE_VELOCITY_CURVE]; i++)
        for (unsigned c = 1; c < 4; c++)
            if (controls.curves[SCENE3D_PARTICLE_VELOCITY_CURVE][i][c]) return true;
    return false;
}
static inline bool scene3d_particle_gpu_stateful(const Scene3dParticleGpuControls &controls) {
    return controls.collision.count || scene3d_particle_noise_enabled(controls.noise) ||
           scene3d_particle_velocity_enabled(controls);
}
struct Scene3dParticleGpuUpload {
    Scene3dParticleGpuMetadata *metadata;
    Scene3dParticleGpuBirth *births;
};
struct Scene3dParticleGpuStats {
    NativeGlResource positions, births;
    uint64_t readbacks, passes, recoveries, gpu_bytes, cpu_bytes, history_ticks;
    NativeGlResource trails;
};
const char *scene3d_particle_gpu_fragment();
Scene3dParticleGpu *scene3d_particle_gpu_create(unsigned capacity, const Scene3dParticleGpuOps &ops,
                                                const Scene3dParticleSurfaces *surfaces);
void scene3d_particle_gpu_surface_changed(Scene3dParticleGpu *gpu, unsigned slot);
void scene3d_particle_gpu_trail_attach(Scene3dParticleGpu *gpu, Scene3dParticleTrailState *trail);
bool scene3d_particle_gpu_trail_sample(Scene3dParticleGpu *gpu, unsigned slot,
                                       Scene3dParticleTrailPoint *points, unsigned *count);
void scene3d_particle_gpu_destroy(Scene3dParticleGpu *gpu);
void scene3d_particle_gpu_owner_lost(Scene3dParticleGpu **registry, bool retire);
bool scene3d_particle_gpu_upload(Scene3dParticleGpu *gpu, bool births_changed, Scene3dParticleGpuUpload *upload);
bool scene3d_particle_gpu_stage(Scene3dParticleGpu *gpu, const Scene3dParticleGpuControls &controls, double timestep);
void scene3d_particle_gpu_commit(Scene3dParticleGpu *gpu);
void scene3d_particle_gpu_discard(Scene3dParticleGpu *gpu);
bool scene3d_particle_gpu_checkpoint(Scene3dParticleGpu *gpu);
void scene3d_particle_gpu_checkpoint_commit(Scene3dParticleGpu *gpu);
bool scene3d_particle_gpu_sample(Scene3dParticleGpu *gpu, const unsigned *slots, unsigned count,
                                 Scene3dParticleSample *samples);
bool scene3d_particle_gpu_stats(Scene3dParticleGpu *gpu, Scene3dParticleGpuStats *stats);
const char *scene3d_particle_gpu_diagnostic(const Scene3dParticleGpu *gpu);
bool scene3d_present(DomElement *node, const char *key);

struct Scene3dParticles;
// scene seek validates dependencies before restoring the world; its final handoff shares that transaction.
bool scene3d_particles_replay_current(Scene3dParticles *state, Scene3dParticles *replay);
bool scene3d_particles_adopt_after_replay(Scene3dParticles *state, Scene3dParticles *replay);
struct Scene3dPhysics;
enum Scene3dParticleResponse { SCENE3D_PARTICLE_BOUNCE, SCENE3D_PARTICLE_SLIDE, SCENE3D_PARTICLE_KILL };
struct Scene3dParticleCollisionDesc {
    double radius, skin, restitution, friction, bounce_threshold;
    uint32_t membership, filter;
    unsigned response, impacts, iterations, event_capacity;
    bool enabled, sensors, ignore_owner, overlap_error, events;
};
struct Scene3dParticleMotion {
    double decay, travel, acceleration_travel, transport_travel, transport_gain;
};
struct Scene3dParticleCollisionWork {
    size_t queries, geometry, triangles;
};
struct Scene3dParticleCollisionFrame {
    Scene3dPhysics *physics;
    Scene3dMatrix world, inverse;
    uint64_t excluded_body;
};
struct Scene3dParticleContact {
    PhysicsHit hit;
    PhysicsVec center, velocity_before, velocity_after;
    double tick_fraction;
    bool killed;
};
using Scene3dParticleContactFn = bool (*)(void *, const Scene3dParticleContact &, PhysicsError *);
bool scene3d_particle_collision_read(DomElement *node, Scene3dParticleCollisionDesc *description);
bool scene3d_particle_analytic_read(DomElement *node, Scene3dParticleAnalytic *description);
Scene3dParticleMotion scene3d_particle_motion(double drag, double timestep);
bool scene3d_particle_integrate(PhysicsVec position, PhysicsVec velocity, PhysicsVec gravity,
                                const Scene3dParticleMotion &motion, PhysicsVec *next_position,
                                PhysicsVec *next_velocity, PhysicsVec transport = {});
bool scene3d_particle_collide(const Scene3dParticleCollisionDesc &description,
                              const Scene3dParticleCollisionFrame &frame, Scene3dParticleSample *sample,
                              Scene3dVec gravity, double drag, double timestep, Scene3dParticleCollisionWork *work,
                              Scene3dParticleContactFn contact, void *owner, bool *alive, PhysicsError *error,
                              Scene3dVec transport = {});

static constexpr unsigned SCENE3D_GEOMETRY_VERTEX_LIMIT = 262144, SCENE3D_GEOMETRY_BONE_LIMIT = 256;
// the caller's pool owns every stream; geometry decoding never acquires graphics resources.
struct Scene3dGeometrySource {
    unsigned vertex_count, index_count, color_components, morph_count, bone_index_count;
    float *positions, *normals, *uvs, *uvs1, *colors, *tangents, *sizes, *deformation[4];
    uint32_t* indices;
    bool vertex_alpha, source_uvs;
    Scene3dVec lo, hi;
};
bool scene3d_read_geometry(DomElement *node, Pool *pool, Scene3dGeometrySource *source, char *diagnostic,
                           size_t capacity, unsigned vertex_limit = SCENE3D_GEOMETRY_VERTEX_LIMIT,
                           unsigned index_limit = SCENE3D_GEOMETRY_VERTEX_LIMIT * 6);
DomElement *scene3d_find_source(DomElement *root, const char *id, bool *valid);
float scene3d_linear(float value);

static constexpr size_t SCENE3D_BAKE_TRIANGLE_LIMIT = 262144;
struct Scene3dNavigationSelection {
    const char* id;
    bool walkable;
    uint32_t groups;
    double cost;
};
using Scene3dNavigationTriangle = NavigationBakeTriangle;
struct Scene3dNavigationGeometry {
    lam::ArrayList<Scene3dNavigationTriangle> triangles;
    char source_hash[65] = {};
};
// worker geometry contains copied coordinates/metadata only; generation refs stay on the owner thread.
bool scene3d_collect_navigation_geometry(DomElement* root, const Scene3dNavigationSelection* selections, size_t count,
                                         Scene3dNavigationGeometry* geometry, lam::ArrayList<DomNodeRef>* references,
                                         char* diagnostic, size_t capacity);
bool scene3d_navigation_geometry_key(DomElement* root, const Scene3dNavigationSelection* selections, size_t count,
                                     uint64_t* key, char* diagnostic, size_t capacity);

// shared bounded source readers for projection and animation adapters; no authored data is retained.
const char* scene3d_text(DomElement* node, const char* key);
bool scene3d_tag(DomElement* node, const char* tag);
ItemReader scene3d_value(DomElement* node, const char* key);
// opaque counters accept uint64/int values or exact unsigned decimal strings, never lossy floats.
bool scene3d_counter(ItemReader value, uint64_t* result);
const char *scene3d_command_name(ItemReader value);
bool scene3d_counter_text(const char *text, size_t length, uint64_t *result);
bool scene3d_real(DomElement* node, const char* key, double fallback, double* result);
bool scene3d_unsigned(DomElement *node, const char *key, unsigned fallback, unsigned limit, unsigned *result);
bool scene3d_number(DomElement* node, const char* key, float fallback, float* result);
bool scene3d_flag(DomElement* node, const char* key, bool fallback, bool* result);
bool scene3d_numbers(DomElement* node, const char* key, Pool* pool, float** values, unsigned* count, unsigned limit);
bool scene3d_components(DomElement* node, const char* key, float* data, unsigned count, bool* present);
bool scene3d_vector(DomElement* node, const char* key, Scene3dVec fallback, Scene3dVec* result);
bool scene3d_color(DomElement* node, const char* key, const char* fallback, float color[4]);
enum Scene3dVisit { SCENE3D_VISIT_CHILDREN, SCENE3D_VISIT_SKIP, SCENE3D_VISIT_ERROR };
typedef Scene3dVisit (*Scene3dVisitor)(DomElement* node, void* context);
bool scene3d_visit(DomElement* root, Scene3dVisitor visitor, void* context);
bool scene3d_camera_targets(DomElement* node, const Scene3dMatrix& parent, const Scene3dMatrix& world,
                            Scene3dVec* target, Scene3dVec* up);
// fixed-tick consumers share renderer transforms; authored sampling omits animation/physics transforms.
// an override replaces a mapped node's world pose, before its descendants' local transforms.
bool scene3d_world_pose(DomElement* root, DomElement* node, Scene3dAnimationState* animation,
                        Scene3dPoseOverrideFn override_pose, void* owner, Scene3dMatrix* world, bool* visible,
                        bool authored = false);
bool scene3d_override_physics_pose(void* owner, DomElement* node, Scene3dMatrix* world);
// projection keys include node identity and omit nonvisual navigation assets.
bool scene3d_source_key(DomNode* node, uint64_t* hash, unsigned* count, unsigned depth = 0, bool projection = true);
// A shader material's numeric uniform `name` as authored in its `uniforms` map: a number, an array of
// numbers, or a "#rrggbb" color as three linear numbers (`color` set). False when absent, a texture
// reference, malformed or longer than `capacity`.
bool scene3d_uniform_numbers(DomElement* material, const char* name, float* values, unsigned capacity, unsigned* count,
                             bool* color);
