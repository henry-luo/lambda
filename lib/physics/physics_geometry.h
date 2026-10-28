#ifndef LAMBDA_PHYSICS_GEOMETRY_H
#define LAMBDA_PHYSICS_GEOMETRY_H
#include "physics_math.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PHYSICS_POLY_MAX_FACES (2 * PHYSICS_CONVEX_MAX_VERTICES - 4)
#define PHYSICS_POLY_MAX_EDGES (3 * PHYSICS_CONVEX_MAX_VERTICES - 6)
#define PHYSICS_MANIFOLD_MAX_POINTS (2 * PHYSICS_CONVEX_MAX_VERTICES)
typedef struct PhysicsHullFace {
    PhysicsVec normal;
    unsigned count, vertices[PHYSICS_CONVEX_MAX_VERTICES];
} PhysicsHullFace;
typedef struct PhysicsHullEdge {unsigned a,b;} PhysicsHullEdge;
typedef struct PhysicsHull {
    PhysicsVec vertices[PHYSICS_CONVEX_MAX_VERTICES], centre;
    PhysicsVec axes[PHYSICS_POLY_MAX_EDGES];
    PhysicsHullEdge edges[PHYSICS_POLY_MAX_EDGES];
    PhysicsHullFace* faces;
    unsigned vertex_count, face_count, edge_count, axis_count;
    double volume, inertia[9], scale;
} PhysicsHull;
typedef struct PhysicsMesh PhysicsMesh;
typedef struct PhysicsGeometry {
    const PhysicsColliderDesc* desc;
    const PhysicsHull* hull;
    PhysicsPose pose;
    const PhysicsMesh* mesh;
    // internal convex view of one surface triangle; it has no volume or mass.
    const PhysicsVec* triangle;
} PhysicsGeometry;
typedef struct PhysicsGeometryContact {PhysicsVec point,normal;double depth;} PhysicsGeometryContact;

// cooked hulls own their vertices, polygon faces and mass integrals.
bool physics_hull_create(const PhysicsVec* vertices,size_t count,PhysicsHull** out,const char** error);
void physics_hull_destroy(PhysicsHull* hull);
PhysicsVec physics_geometry_support(const PhysicsGeometry* geometry,PhysicsVec direction);
bool physics_hull_ray(const PhysicsHull* hull,PhysicsVec origin,PhysicsVec direction,double maximum,PhysicsHit* hit);
bool physics_hull_contains(const PhysicsHull* hull,PhysicsVec point);
bool physics_convex_contacts(const PhysicsGeometry* a,const PhysicsGeometry* b,
    PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error);
// nonconvergence is an error, never a successful separation result.
bool physics_geometry_distance(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsHit* hit,const char** error);

// shared clipping and segment witnesses also serve the analytic primitive paths.
unsigned pclip(const PhysicsVec* input,unsigned count,PhysicsVec* output,unsigned capacity,PhysicsVec axis,double plane);
void psegments_closest(PhysicsVec a,PhysicsVec ae,PhysicsVec b,PhysicsVec be,PhysicsVec* pa,PhysicsVec* pb);
PhysicsVec physics_triangle_closest(PhysicsVec a,PhysicsVec b,PhysicsVec c,PhysicsVec point);
bool physics_mesh_input_counts(const PhysicsColliderDesc* desc,size_t* vertices,size_t* triangles,const char** error);
bool physics_mesh_create(const PhysicsColliderDesc* desc,PhysicsMesh** out,const char** error);
void physics_mesh_destroy(PhysicsMesh* mesh);
void physics_mesh_bind(const PhysicsMesh* mesh,PhysicsColliderDesc* desc);
void physics_mesh_bounds(const PhysicsMesh* mesh,PhysicsVec* low,PhysicsVec* high);
bool physics_mesh_contacts(const PhysicsGeometry* surface,const PhysicsGeometry* convex,bool flip,
    PhysicsGeometryContact* contacts,size_t capacity,size_t* count,const char** error);
bool physics_mesh_ray(const PhysicsMesh* mesh,bool double_sided,PhysicsVec origin,PhysicsVec direction,
    double maximum,PhysicsHit* hit,const char** error);
bool physics_mesh_point(const PhysicsMesh* mesh,PhysicsVec point,PhysicsHit* hit,const char** error);
bool physics_mesh_distance(const PhysicsGeometry* a,const PhysicsGeometry* b,PhysicsHit* hit,const char** error);
#ifdef __cplusplus
}
#endif
#endif
