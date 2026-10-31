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
    float gravity[3], drag, seed[4], curve[SCENE3D_PARTICLE_CURVE_LIMIT][4];
    float rotation[SCENE3D_PARTICLE_CURVE_LIMIT][4];
    unsigned curve_count, rotation_count;
};
struct Scene3dParticleGpuUpload {
    Scene3dParticleGpuMetadata *metadata;
    Scene3dParticleGpuBirth *births;
};
struct Scene3dParticleGpuStats {
    NativeGlResource positions, births;
    uint64_t readbacks, passes, recoveries, gpu_bytes, cpu_bytes;
};
const char *scene3d_particle_gpu_fragment();
Scene3dParticleGpu *scene3d_particle_gpu_create(unsigned capacity, const Scene3dParticleGpuOps &ops,
                                                const Scene3dParticleSurfaces *surfaces);
void scene3d_particle_gpu_surface_changed(Scene3dParticleGpu *gpu, unsigned slot);
void scene3d_particle_gpu_destroy(Scene3dParticleGpu *gpu);
void scene3d_particle_gpu_owner_lost(Scene3dParticleGpu **registry, bool retire);
bool scene3d_particle_gpu_upload(Scene3dParticleGpu *gpu, bool births_changed, Scene3dParticleGpuUpload *upload);
bool scene3d_particle_gpu_stage(Scene3dParticleGpu *gpu, const Scene3dParticleGpuControls &controls);
void scene3d_particle_gpu_commit(Scene3dParticleGpu *gpu);
bool scene3d_particle_gpu_checkpoint(Scene3dParticleGpu *gpu);
void scene3d_particle_gpu_checkpoint_commit(Scene3dParticleGpu *gpu);
bool scene3d_particle_gpu_sample(Scene3dParticleGpu *gpu, const unsigned *slots, unsigned count,
                                 Scene3dParticleSample *samples);
bool scene3d_particle_gpu_stats(Scene3dParticleGpu *gpu, Scene3dParticleGpuStats *stats);

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
    double decay, travel, acceleration_travel;
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
Scene3dParticleMotion scene3d_particle_motion(double drag, double timestep);
bool scene3d_particle_integrate(PhysicsVec position, PhysicsVec velocity, PhysicsVec gravity,
                                const Scene3dParticleMotion &motion, PhysicsVec *next_position,
                                PhysicsVec *next_velocity);
bool scene3d_particle_collide(const Scene3dParticleCollisionDesc &description,
                              const Scene3dParticleCollisionFrame &frame, Scene3dParticleSample *sample,
                              Scene3dVec gravity, double drag, double timestep, Scene3dParticleCollisionWork *work,
                              Scene3dParticleContactFn contact, void *owner, bool *alive, PhysicsError *error);

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
ItemReader scene3d_value(DomElement* node, const char* key);
// opaque counters accept uint64/int values or exact unsigned decimal strings, never lossy floats.
bool scene3d_counter(ItemReader value, uint64_t* result);
bool scene3d_counter_text(const char *text, size_t length, uint64_t *result);
bool scene3d_real(DomElement* node, const char* key, double fallback, double* result);
bool scene3d_unsigned(DomElement *node, const char *key, unsigned fallback, unsigned limit, unsigned *result);
bool scene3d_number(DomElement* node, const char* key, float fallback, float* result);
bool scene3d_flag(DomElement* node, const char* key, bool fallback, bool* result);
bool scene3d_numbers(DomElement* node, const char* key, Pool* pool, float** values, unsigned* count, unsigned limit);
bool scene3d_components(DomElement* node, const char* key, float* data, unsigned count, bool* present);
bool scene3d_vector(DomElement* node, const char* key, Scene3dVec fallback, Scene3dVec* result);
bool scene3d_color(DomElement* node, const char* key, const char* fallback, float color[4]);
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
