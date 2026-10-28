#ifndef LAMBDA_PHYSICS_H
#define LAMBDA_PHYSICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PhysicsWorld PhysicsWorld;
typedef struct PhysicsVec { double x, y, z; } PhysicsVec;
typedef struct PhysicsQuat { double x, y, z, w; } PhysicsQuat;
typedef struct PhysicsPose { PhysicsVec position; PhysicsQuat rotation; } PhysicsPose;
typedef enum PhysicsMotion { PHYSICS_FIXED, PHYSICS_DYNAMIC, PHYSICS_KINEMATIC_POSITION, PHYSICS_KINEMATIC_VELOCITY } PhysicsMotion;
typedef enum PhysicsShape { PHYSICS_SPHERE, PHYSICS_BOX, PHYSICS_CAPSULE, PHYSICS_CONVEX, PHYSICS_MESH, PHYSICS_HEIGHTFIELD } PhysicsShape;
#define PHYSICS_CONVEX_MAX_VERTICES 64
#define PHYSICS_WORLD_MAX_VERTICES 65536
#define PHYSICS_MESH_MAX_TRIANGLES 131072
#define PHYSICS_WORLD_MAX_TRIANGLES 262144
#define PHYSICS_MESH_QUERY_BUDGET 8192
typedef struct PhysicsBodyDesc {
    uint64_t id;
    PhysicsMotion motion;
    PhysicsPose pose;
    PhysicsVec velocity, angular_velocity;
    double linear_damping, angular_damping, gravity_scale;
} PhysicsBodyDesc;
typedef struct PhysicsColliderDesc {
    uint64_t id, body;
    PhysicsShape shape;
    PhysicsPose pose;
    PhysicsVec size;
    // capsules extend along local Y; height is the cylinder length, excluding the hemispheres.
    double radius, height, density, friction, restitution;
    uint32_t membership, filter;
    bool sensor;
    // convex point clouds are copied and cooked by creation/configuration.
    const PhysicsVec* vertices;
    size_t vertex_count;
    // environment surfaces own copied data; indices are zero-based CCW triples.
    const uint32_t* indices;
    size_t index_count;
    const double* heights;
    size_t rows, columns;
    double cell_width, cell_depth;
    bool double_sided;
} PhysicsColliderDesc;
typedef struct PhysicsDesc {
    PhysicsVec gravity;
    double timestep, length_scale;
    unsigned iterations;
    size_t contact_limit, event_limit;
    const PhysicsBodyDesc* bodies;
    size_t body_count;
    const PhysicsColliderDesc* colliders;
    size_t collider_count;
} PhysicsDesc;
typedef struct PhysicsError { const char* message; uint64_t id, tick; } PhysicsError;
typedef struct PhysicsBodyState {
    uint64_t id;
    PhysicsPose pose;
    PhysicsVec velocity, angular_velocity, centre_of_mass;
    double mass;
    bool sleeping;
} PhysicsBodyState;
typedef struct PhysicsEvent {
    uint64_t tick, collider_a, collider_b, body_a, body_b;
    bool started, sensor;
} PhysicsEvent;
typedef struct PhysicsState {
    uint64_t tick;
    double time;
    const PhysicsBodyState* bodies;
    size_t body_count;
    const PhysicsEvent* events;
    size_t event_count, candidate_count, contact_count;
} PhysicsState;
typedef enum PhysicsCommandKind {
    PHYSICS_IMPULSE, PHYSICS_FORCE, PHYSICS_TORQUE, PHYSICS_TELEPORT,
    PHYSICS_VELOCITY, PHYSICS_WAKE, PHYSICS_TARGET, PHYSICS_GRAVITY, PHYSICS_ANGULAR_VELOCITY
} PhysicsCommandKind;
typedef struct PhysicsCommand {
    PhysicsCommandKind kind;
    uint64_t body;
    PhysicsVec value, point;
    PhysicsPose pose;
    bool at_point, local;
} PhysicsCommand;
typedef enum PhysicsQueryKind { PHYSICS_RAY, PHYSICS_POINT, PHYSICS_OVERLAP, PHYSICS_DISTANCE } PhysicsQueryKind;
typedef struct PhysicsQuery {
    PhysicsQueryKind kind;
    PhysicsVec origin, direction, size;
    double distance;
    uint32_t membership, filter;
    bool include_sensors;
    uint64_t collider_a, collider_b;
} PhysicsQuery;
typedef struct PhysicsHit {
    bool found;
    uint64_t collider, body;
    double distance;
    PhysicsVec point, normal;
    PhysicsVec point_b;
    bool overlapping;
    // mesh diagnostics count tested triangles, including pruned-query misses.
    size_t triangle_tests;
} PhysicsHit;

// creation copies descriptors; all later calls require exclusive ownership of the world.
// result views belong to the world; callers copy anything retained across mutation.
bool physics_create(const PhysicsDesc* desc, PhysicsWorld** result, PhysicsError* error);
void physics_destroy(PhysicsWorld* world);
// invalid commands reject the entire batch; forces/torques last for the next tick only.
bool physics_apply(PhysicsWorld* world, const PhysicsCommand* commands, size_t count, PhysicsError* error);
// replaces body damping/gravity scale and every collider descriptor, in creation order, keeping
// each body's simulated pose and velocities; ids, motion types and collider owners must not
// change. Mass properties recompute; a rejected edit leaves the previous configuration.
bool physics_configure(PhysicsWorld* world, const PhysicsBodyDesc* bodies, size_t body_count,
                       const PhysicsColliderDesc* colliders, size_t collider_count, PhysicsError* error);
// events accumulate across this call's ticks. Numerical/quota faults disable further mutation.
bool physics_step(PhysicsWorld* world, size_t ticks, PhysicsState* result, PhysicsError* error);
// faulted worlds remain readable with the last completed body snapshot.
bool physics_read(PhysicsWorld* world, PhysicsState* result, PhysicsError* error);
// one nearest/first hit per query; PHYSICS_OVERLAP tests AABBs, not exact shape overlap.
bool physics_query(PhysicsWorld* world, const PhysicsQuery* queries, size_t count, PhysicsHit* results, PhysicsError* error);

#ifdef __cplusplus
}
#endif
#endif
