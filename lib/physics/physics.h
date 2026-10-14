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
typedef enum PhysicsShape { PHYSICS_SPHERE, PHYSICS_BOX } PhysicsShape;
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
    double radius, density, friction, restitution;
    uint32_t membership, filter;
    bool sensor;
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
    PHYSICS_VELOCITY, PHYSICS_WAKE, PHYSICS_TARGET, PHYSICS_GRAVITY
} PhysicsCommandKind;
typedef struct PhysicsCommand {
    PhysicsCommandKind kind;
    uint64_t body;
    PhysicsVec value, point;
    PhysicsPose pose;
    bool at_point, local;
} PhysicsCommand;
typedef enum PhysicsQueryKind { PHYSICS_RAY, PHYSICS_POINT, PHYSICS_OVERLAP } PhysicsQueryKind;
typedef struct PhysicsQuery {
    PhysicsQueryKind kind;
    PhysicsVec origin, direction, size;
    double distance;
    uint32_t membership, filter;
    bool include_sensors;
} PhysicsQuery;
typedef struct PhysicsHit {
    bool found;
    uint64_t collider, body;
    double distance;
    PhysicsVec point, normal;
} PhysicsHit;

// creation copies descriptors; all later calls require exclusive ownership of the world.
// result views belong to the world; callers copy anything retained across mutation.
bool physics_create(const PhysicsDesc* desc, PhysicsWorld** result, PhysicsError* error);
void physics_destroy(PhysicsWorld* world);
// invalid commands reject the entire batch; forces/torques last for the next tick only.
bool physics_apply(PhysicsWorld* world, const PhysicsCommand* commands, size_t count, PhysicsError* error);
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
