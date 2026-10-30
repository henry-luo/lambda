#pragma once
#include "../../lib/arraylist.hpp"
#include "../../lib/navigation.h"
#include "scene3d_math.hpp"
#include <stddef.h>
#include <stdint.h>
struct DomElement;
struct DomNode;
struct Pool;
struct Scene3dAnimationState;
class ItemReader;
struct DomNodeRef;
typedef bool (*Scene3dPoseOverrideFn)(void* owner, DomElement* node, Scene3dMatrix* world);

static constexpr unsigned SCENE3D_GEOMETRY_VERTEX_LIMIT = 262144, SCENE3D_GEOMETRY_BONE_LIMIT = 256;
// the caller's pool owns every stream; geometry decoding never acquires graphics resources.
struct Scene3dGeometrySource {
    unsigned vertex_count, index_count, color_components, morph_count, bone_index_count;
    float *positions, *normals, *uvs, *uvs1, *colors, *tangents, *sizes, *deformation[4];
    uint32_t* indices;
    bool vertex_alpha, source_uvs;
    Scene3dVec lo, hi;
};
bool scene3d_read_geometry(DomElement* node, Pool* pool, Scene3dGeometrySource* source, char* diagnostic,
                           size_t capacity, unsigned vertex_limit = SCENE3D_GEOMETRY_VERTEX_LIMIT);
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
bool scene3d_real(DomElement* node, const char* key, double fallback, double* result);
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
